#include "../config.h"

#if defined(CONFIG_PVU_PROTOCOL_REGRESSION) && CONFIG_PVU_PROTOCOL_REGRESSION

#include <verilated.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "VPvuTop.h"
#include "/root/SoftPosit/source/include/softposit.h"

namespace {

constexpr uint32_t kNaR = 0x80000000u;
constexpr uint32_t kOne = 0x40000000u;
constexpr uint32_t kNegOne = 0xc0000000u;
constexpr uint32_t kTwo = 0x48000000u;
constexpr uint32_t kNegTwo = 0xb8000000u;
constexpr uint32_t kMaxPos = 0x7fffffffu;
constexpr uint32_t kMinPos = 0x00000001u;
constexpr uint32_t kNegMaxPos = 0x80000001u;
constexpr size_t kLanes = 4;

struct PvuRequest {
  uint32_t tag = 0;
  uint8_t op = 0;
  bool is_posit = true;
  bool out_posit = true;
  bool float_to_posit = true;
  uint8_t float_mode = 3;
  uint8_t src_posit_width = 32;
  uint8_t dst_posit_width = 32;
  uint8_t vector_size = kLanes;
  std::array<uint32_t, kLanes> posit_i1{};
  std::array<uint32_t, kLanes> posit_i2{};
  std::array<uint64_t, kLanes> float_i{};
  std::array<uint64_t, kLanes> float_i2{};
};

struct PvuResponse {
  uint32_t tag = 0;
  uint8_t op = 0;
  std::array<uint32_t, kLanes> posit{};
  uint32_t posit_dot = 0;
  std::array<uint64_t, kLanes> floating{};
  uint64_t float_dot = 0;
  std::array<int32_t, kLanes> integer{};
};

enum class ResultKind { kPositVector, kPositDot, kFloatVector, kIntVector };

struct TestCase {
  std::string operation;
  std::string category;
  PvuRequest request;
  PvuResponse expected;
  ResultKind kind;
};

uint32_t bits(posit32_t value) { return value.v; }
posit32_t posit(uint32_t value) { return castP32(value); }

std::string hex32(uint32_t value) {
  std::ostringstream out;
  out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
  return out.str();
}

std::string vector_hex(const std::array<uint32_t, kLanes>& values) {
  std::ostringstream out;
  out << "{";
  for (size_t lane = 0; lane < values.size(); ++lane) {
    if (lane) out << ",";
    out << hex32(values[lane]);
  }
  out << "}";
  return out.str();
}

bool is_nar(uint32_t value) { return value == kNaR; }

uint32_t ref_binary(uint8_t op, uint32_t lhs, uint32_t rhs) {
  if (op == 1) return bits(p32_add(posit(lhs), posit(rhs)));
  if (op == 2) return bits(p32_sub(posit(lhs), posit(rhs)));
  if (op == 3) return bits(p32_mul(posit(lhs), posit(rhs)));
  if (op == 4) return bits(p32_div(posit(lhs), posit(rhs)));
  if (op == 8) {
    if (is_nar(lhs) || is_nar(rhs)) return kNaR;
    return static_cast<int32_t>(lhs) >= static_cast<int32_t>(rhs) ? lhs : rhs;
  }
  if (op == 9) {
    if (is_nar(lhs) || is_nar(rhs)) return kNaR;
    return static_cast<int32_t>(lhs) <= static_cast<int32_t>(rhs) ? lhs : rhs;
  }
  return 0;
}

uint32_t ref_dot(const std::array<uint32_t, kLanes>& lhs,
                 const std::array<uint32_t, kLanes>& rhs) {
  posit32_t accumulator = posit(0);
  for (size_t lane = 0; lane < kLanes; ++lane) {
    accumulator = p32_mulAdd(posit(lhs[lane]), posit(rhs[lane]), accumulator);
  }
  return bits(accumulator);
}

struct FpFormat { uint8_t exponent_bits; uint8_t fraction_bits; };
constexpr std::array<FpFormat, 5> kFpFormats{{{1, 2}, {4, 3}, {5, 10}, {8, 23}, {11, 52}}};

uint64_t width_mask(unsigned width) {
  return width == 64 ? std::numeric_limits<uint64_t>::max() : ((uint64_t{1} << width) - 1);
}

long double decode_fp(uint64_t raw, FpFormat format) {
  const unsigned width = 1 + format.exponent_bits + format.fraction_bits;
  raw &= width_mask(width);
  const uint64_t fraction_mask = width_mask(format.fraction_bits);
  const uint64_t fraction = raw & fraction_mask;
  const uint64_t exponent = (raw >> format.fraction_bits) & width_mask(format.exponent_bits);
  const bool sign = (raw >> (width - 1)) != 0;
  const uint64_t exponent_max = width_mask(format.exponent_bits);
  const int bias = (1 << (format.exponent_bits - 1)) - 1;
  long double value;
  if (exponent == exponent_max) {
    if (fraction != 0) return std::numeric_limits<long double>::quiet_NaN();
    return sign ? -std::numeric_limits<long double>::infinity() : std::numeric_limits<long double>::infinity();
  }
  if (exponent == 0) {
    value = std::ldexp(static_cast<long double>(fraction), 1 - bias - format.fraction_bits);
  } else {
    value = std::ldexp(1.0L + std::ldexp(static_cast<long double>(fraction), -format.fraction_bits),
                       static_cast<int>(exponent) - bias);
  }
  return sign ? -value : value;
}

uint64_t round_even(long double value) {
  const long double floor_value = std::floor(value);
  const long double fraction = value - floor_value;
  if (fraction > 0.5L) return static_cast<uint64_t>(floor_value + 1.0L);
  if (fraction < 0.5L) return static_cast<uint64_t>(floor_value);
  const uint64_t lower = static_cast<uint64_t>(floor_value);
  return (lower & 1u) == 0 ? lower : lower + 1;
}

uint64_t encode_fp(long double value, FpFormat format) {
  const unsigned width = 1 + format.exponent_bits + format.fraction_bits;
  const uint64_t sign = std::signbit(value) ? 1 : 0;
  const uint64_t exponent_max = width_mask(format.exponent_bits);
  const uint64_t fraction_mask = width_mask(format.fraction_bits);
  if (std::isnan(value)) return (sign << (width - 1)) | (exponent_max << format.fraction_bits) | 1;
  if (std::isinf(value)) return (sign << (width - 1)) | (exponent_max << format.fraction_bits);
  const long double magnitude = std::fabs(value);
  if (magnitude == 0.0L) return sign << (width - 1);

  const int bias = (1 << (format.exponent_bits - 1)) - 1;
  const int minimum_normal_exponent = 1 - bias;
  const int maximum_normal_exponent = static_cast<int>(exponent_max - 1) - bias;
  int exponent = static_cast<int>(std::floor(std::log2(magnitude)));
  uint64_t fraction = 0;
  uint64_t exponent_field = 0;
  if (exponent < minimum_normal_exponent) {
    fraction = round_even(std::ldexp(magnitude, format.fraction_bits - minimum_normal_exponent));
    if (fraction == 0) return sign << (width - 1);
    if (fraction > fraction_mask) { exponent_field = 1; fraction = 0; }
  } else {
    if (exponent > maximum_normal_exponent) {
      return (sign << (width - 1)) | (exponent_max << format.fraction_bits);
    }
    fraction = round_even(std::ldexp(std::ldexp(magnitude, -exponent) - 1.0L, format.fraction_bits));
    if (fraction > fraction_mask) {
      fraction = 0;
      ++exponent;
      if (exponent > maximum_normal_exponent) {
        return (sign << (width - 1)) | (exponent_max << format.fraction_bits);
      }
    }
    exponent_field = static_cast<uint64_t>(exponent + bias);
  }
  return (sign << (width - 1)) | (exponent_field << format.fraction_bits) | fraction;
}

uint32_t ref_float_to_posit(uint64_t raw, uint8_t mode) {
  const long double decoded = decode_fp(raw, kFpFormats.at(mode));
  if (!std::isfinite(decoded)) return kNaR;
  return bits(convertDoubleToP32(static_cast<double>(decoded)));
}

uint64_t ref_posit_to_float(uint32_t raw, uint8_t mode) {
  if (is_nar(raw)) return encode_fp(std::numeric_limits<long double>::quiet_NaN(), kFpFormats.at(mode));
  return encode_fp(static_cast<long double>(convertP32ToDouble(posit(raw))), kFpFormats.at(mode));
}

PvuResponse expected_for(const PvuRequest& request, ResultKind kind) {
  PvuResponse response{};
  response.tag = request.tag;
  response.op = request.op;
  switch (kind) {
    case ResultKind::kPositVector:
      for (size_t lane = 0; lane < kLanes; ++lane) {
        if (request.op == 6) response.posit[lane] = request.posit_i1[lane];
        else response.posit[lane] = ref_binary(request.op, request.posit_i1[lane], request.posit_i2[lane]);
      }
      break;
    case ResultKind::kPositDot:
      response.posit_dot = ref_dot(request.posit_i1, request.posit_i2);
      break;
    case ResultKind::kFloatVector:
      for (size_t lane = 0; lane < kLanes; ++lane) {
        if (request.float_to_posit) response.posit[lane] = ref_float_to_posit(request.float_i[lane], request.float_mode);
        else response.floating[lane] = ref_posit_to_float(request.posit_i1[lane], request.float_mode);
      }
      break;
    case ResultKind::kIntVector:
      for (size_t lane = 0; lane < kLanes; ++lane) response.integer[lane] = static_cast<int32_t>(p32_to_i32(posit(request.posit_i1[lane])));
      break;
  }
  return response;
}

class LegacyProtocolAdapter {
 public:
  explicit LegacyProtocolAdapter(VPvuTop* dut) : dut_(dut) {}

  void reset() {
    dut_->reset = 1;
    tick();
    tick();
    dut_->reset = 0;
    pending_ = false;
  }

  bool in_ready() const { return !pending_; }
  bool out_valid() const { return pending_; }

  void send(const PvuRequest& request) {
    // The generated baseline has no channel ports yet. This adapter represents
    // an in_valid && in_ready transfer and makes the combinational result a
    // one-entry response that is consumed only by recv()'s ready handshake.
    drive(request);
    tick();
    pending_response_ = sample(request.tag, request.op);
    pending_ = true;
  }

  PvuResponse recv(bool out_ready) {
    if (!pending_ || !out_ready) throw std::runtime_error("response handshake requested without valid/ready");
    pending_ = false;
    return pending_response_;
  }

 private:
  void tick() {
    dut_->clock = 0;
    dut_->eval();
    dut_->clock = 1;
    dut_->eval();
    dut_->clock = 0;
    dut_->eval();
  }

  void drive(const PvuRequest& request) {
    dut_->io_posit_i1_0 = request.posit_i1[0]; dut_->io_posit_i1_1 = request.posit_i1[1];
    dut_->io_posit_i1_2 = request.posit_i1[2]; dut_->io_posit_i1_3 = request.posit_i1[3];
    dut_->io_posit_i2_0 = request.posit_i2[0]; dut_->io_posit_i2_1 = request.posit_i2[1];
    dut_->io_posit_i2_2 = request.posit_i2[2]; dut_->io_posit_i2_3 = request.posit_i2[3];
    dut_->io_float_i_0 = request.float_i[0]; dut_->io_float_i_1 = request.float_i[1];
    dut_->io_float_i_2 = request.float_i[2]; dut_->io_float_i_3 = request.float_i[3];
    dut_->io_float_i2_0 = request.float_i2[0]; dut_->io_float_i2_1 = request.float_i2[1];
    dut_->io_float_i2_2 = request.float_i2[2]; dut_->io_float_i2_3 = request.float_i2[3];
    dut_->io_op = request.op;
    dut_->io_Isposit = request.is_posit;
    dut_->io_Outposit = request.out_posit;
    dut_->io_float_mode = request.float_mode;
    dut_->io_float_posit = request.float_to_posit;
    dut_->io_src_posit_width = request.src_posit_width;
    dut_->io_dst_posit_width = request.dst_posit_width;
    dut_->io_vector_size = request.vector_size;
  }

  PvuResponse sample(uint32_t tag, uint8_t op) const {
    PvuResponse response{};
    response.tag = tag;
    response.op = op;
    response.posit = {static_cast<uint32_t>(dut_->io_posit_o_0), static_cast<uint32_t>(dut_->io_posit_o_1),
                      static_cast<uint32_t>(dut_->io_posit_o_2), static_cast<uint32_t>(dut_->io_posit_o_3)};
    response.posit_dot = static_cast<uint32_t>(dut_->io_posit_dot_o);
    response.floating = {static_cast<uint64_t>(dut_->io_float_o_0), static_cast<uint64_t>(dut_->io_float_o_1),
                         static_cast<uint64_t>(dut_->io_float_o_2), static_cast<uint64_t>(dut_->io_float_o_3)};
    response.float_dot = static_cast<uint64_t>(dut_->io_float_dot_o);
    response.integer = {static_cast<int32_t>(dut_->io_int_o_0), static_cast<int32_t>(dut_->io_int_o_1),
                        static_cast<int32_t>(dut_->io_int_o_2), static_cast<int32_t>(dut_->io_int_o_3)};
    return response;
  }

  VPvuTop* dut_;
  bool pending_ = false;
  PvuResponse pending_response_{};
};

std::array<uint32_t, kLanes> repeat(uint32_t value) { return {value, value, value, value}; }

std::vector<uint32_t> load_words(const char* path, size_t limit) {
  std::ifstream file(path, std::ios::binary);
  std::vector<uint32_t> words;
  uint32_t word;
  while (words.size() < limit && file.read(reinterpret_cast<char*>(&word), sizeof(word))) {
    if (word != 0x00000fa0u) words.push_back(word);
  }
  return words;
}

PvuRequest base_request(uint32_t tag, uint8_t op) {
  PvuRequest request{};
  request.tag = tag;
  request.op = op;
  return request;
}

void add_case(std::vector<TestCase>& tests, const std::string& operation, const std::string& category,
              PvuRequest request, ResultKind kind) {
  tests.push_back({operation, category, request, expected_for(request, kind), kind});
}

std::vector<TestCase> build_tests() {
  std::vector<TestCase> tests;
  uint32_t tag = 1;
  const std::array<std::pair<std::string, std::pair<std::array<uint32_t, kLanes>, std::array<uint32_t, kLanes>>>, 4> binary_boundaries{{
      {"zero", {repeat(0), repeat(kOne)}},
      {"NaR", {repeat(kNaR), repeat(kOne)}},
      {"extrema", {{kMaxPos, kMinPos, kNegMaxPos, kMinPos}, {kOne, kTwo, kNegOne, kNegTwo}}},
      {"mixed-sign", {{kOne, kNegOne, kTwo, kNegTwo}, repeat(kOne)}}}};

  for (uint8_t op : {uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{4}, uint8_t{8}, uint8_t{9}}) {
    for (const auto& boundary : binary_boundaries) {
      PvuRequest request = base_request(tag++, op);
      request.posit_i1 = boundary.second.first;
      request.posit_i2 = boundary.second.second;
      add_case(tests, "op" + std::to_string(op), boundary.first, request, ResultKind::kPositVector);
    }
  }

  const std::array<std::pair<std::string, std::array<uint32_t, kLanes>>, 4> unary_boundaries{{
      {"zero", repeat(0)}, {"NaR", repeat(kNaR)}, {"extrema", {kMaxPos, kMinPos, kNegMaxPos, kMinPos}},
      {"mixed-sign", {kOne, kNegOne, kTwo, kNegTwo}}}};
  for (uint8_t op : {uint8_t{6}, uint8_t{10}}) {
    for (const auto& boundary : unary_boundaries) {
      PvuRequest request = base_request(tag++, op);
      request.posit_i1 = boundary.second;
      add_case(tests, "op" + std::to_string(op), boundary.first, request,
               op == 10 ? ResultKind::kIntVector : ResultKind::kPositVector);
    }
  }

  using PositPair = std::pair<std::array<uint32_t, kLanes>, std::array<uint32_t, kLanes>>;
  const std::array<std::pair<std::string, PositPair>, 5> dot_boundaries{{
      std::make_pair(std::string("cancellation"), PositPair{{kOne, kNegOne, kTwo, kNegTwo}, repeat(kOne)}),
      std::make_pair(std::string("zero"), PositPair{repeat(0), repeat(kOne)}),
      std::make_pair(std::string("NaR"), PositPair{repeat(kNaR), repeat(kOne)}),
      std::make_pair(std::string("extrema"), PositPair{{kMaxPos, kMinPos, kNegMaxPos, kMinPos}, {kMinPos, kMaxPos, kMinPos, kNegMaxPos}}),
      std::make_pair(std::string("mixed-sign"), PositPair{{kOne, kNegOne, kTwo, kNegTwo}, {kTwo, kNegTwo, kNegOne, kOne}})}};
  for (const auto& boundary : dot_boundaries) {
    PvuRequest request = base_request(tag++, 5);
    request.posit_i1 = boundary.second.first;
    request.posit_i2 = boundary.second.second;
    add_case(tests, "op5", boundary.first, request, ResultKind::kPositDot);
  }

  const std::vector<uint32_t> activations = load_words("test_src/posit_activations.bin", 24);
  const std::vector<uint32_t> weights = load_words("test_src/posit_weights.bin", 24);
  const std::vector<uint32_t> posit_data = load_words("test_src/posit32_data.bin", 24);
  for (size_t sample = 0; sample + 3 < activations.size() && sample + 3 < weights.size(); sample += 4) {
    for (uint8_t op : {uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{4}, uint8_t{5}, uint8_t{6}, uint8_t{8}, uint8_t{9}, uint8_t{10}}) {
      PvuRequest request = base_request(tag++, op);
      for (size_t lane = 0; lane < kLanes; ++lane) {
        request.posit_i1[lane] = activations[sample + lane];
        request.posit_i2[lane] = weights[sample + lane];
      }
      add_case(tests, "op" + std::to_string(op), "seeded-binary-" + std::to_string(sample / 4), request,
               op == 5 ? ResultKind::kPositDot : (op == 10 ? ResultKind::kIntVector : ResultKind::kPositVector));
    }
  }

  for (uint8_t mode = 0; mode < kFpFormats.size(); ++mode) {
    const FpFormat format = kFpFormats[mode];
    const unsigned width = 1 + format.exponent_bits + format.fraction_bits;
    const uint64_t sign_bit = uint64_t{1} << (width - 1);
    const uint64_t exponent_max = width_mask(format.exponent_bits);
    const uint64_t max_finite = ((exponent_max - 1) << format.fraction_bits) | width_mask(format.fraction_bits);
    const std::array<std::pair<std::string, uint64_t>, 5> fp_boundaries{{
        {"zero", 0}, {"NaR-NaN", (exponent_max << format.fraction_bits) | 1}, {"extrema", max_finite},
        {"mixed-sign", sign_bit | (uint64_t{1} << format.fraction_bits)}, {"infinity", exponent_max << format.fraction_bits}}};
    for (const auto& boundary : fp_boundaries) {
      PvuRequest request = base_request(tag++, 7);
      request.is_posit = false;
      request.out_posit = true;
      request.float_to_posit = true;
      request.float_mode = mode;
      request.float_i = {boundary.second, boundary.second ^ sign_bit, boundary.second, boundary.second ^ sign_bit};
      add_case(tests, "op7-fp" + std::to_string(mode) + "-to-posit", boundary.first, request, ResultKind::kFloatVector);

      request = base_request(tag++, 7);
      request.is_posit = true;
      request.out_posit = false;
      request.float_to_posit = false;
      request.float_mode = mode;
      if (boundary.first == "zero") request.posit_i1 = repeat(0);
      else if (boundary.first == "NaR-NaN") request.posit_i1 = repeat(kNaR);
      else if (boundary.first == "extrema") request.posit_i1 = {kMaxPos, kMinPos, kNegMaxPos, kMinPos};
      else request.posit_i1 = {kOne, kNegOne, kTwo, kNegTwo};
      add_case(tests, "op7-posit-to-fp" + std::to_string(mode), boundary.first, request, ResultKind::kFloatVector);
    }
    for (size_t sample = 0; sample + 3 < posit_data.size(); sample += 4) {
      PvuRequest request = base_request(tag++, 7);
      request.is_posit = true;
      request.out_posit = false;
      request.float_to_posit = false;
      request.float_mode = mode;
      for (size_t lane = 0; lane < kLanes; ++lane) request.posit_i1[lane] = posit_data[sample + lane];
      add_case(tests, "op7-posit-to-fp" + std::to_string(mode), "seeded-binary-" + std::to_string(sample / 4), request, ResultKind::kFloatVector);
    }
  }
  // Fixed seed makes this supplemental SoftPosit corpus reproducible.
  std::mt19937 seeded(0x5eed1234u);
  for (size_t sample = 0; sample < 8; ++sample) {
    std::array<uint32_t, kLanes> lhs{};
    std::array<uint32_t, kLanes> rhs{};
    for (size_t lane = 0; lane < kLanes; ++lane) { lhs[lane] = seeded(); rhs[lane] = seeded(); }
    for (uint8_t op : {uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{4}, uint8_t{5}, uint8_t{6}, uint8_t{8}, uint8_t{9}, uint8_t{10}}) {
      PvuRequest request = base_request(tag++, op);
      request.posit_i1 = lhs;
      request.posit_i2 = rhs;
      add_case(tests, "op" + std::to_string(op), "seed-0x5eed1234-" + std::to_string(sample), request,
               op == 5 ? ResultKind::kPositDot : (op == 10 ? ResultKind::kIntVector : ResultKind::kPositVector));
    }
    for (uint8_t mode = 0; mode < kFpFormats.size(); ++mode) {
      PvuRequest request = base_request(tag++, 7);
      request.is_posit = false; request.out_posit = true; request.float_to_posit = true; request.float_mode = mode;
      const unsigned width = 1 + kFpFormats[mode].exponent_bits + kFpFormats[mode].fraction_bits;
      for (size_t lane = 0; lane < kLanes; ++lane) request.float_i[lane] = seeded() & width_mask(width);
      add_case(tests, "op7-fp" + std::to_string(mode) + "-to-posit", "seed-0x5eed1234-" + std::to_string(sample), request, ResultKind::kFloatVector);
      request = base_request(tag++, 7);
      request.out_posit = false; request.float_to_posit = false; request.float_mode = mode; request.posit_i1 = lhs;
      add_case(tests, "op7-posit-to-fp" + std::to_string(mode), "seed-0x5eed1234-" + std::to_string(sample), request, ResultKind::kFloatVector);
    }
  }
  return tests;
}

bool matches(const TestCase& test, const PvuResponse& actual, std::string& reason) {
  if (actual.tag != test.expected.tag || actual.op != test.expected.op) {
    reason = "tag/op expected tag=" + std::to_string(test.expected.tag) + " op=" + std::to_string(test.expected.op) +
             " got tag=" + std::to_string(actual.tag) + " op=" + std::to_string(actual.op);
    return false;
  }
  for (size_t lane = 0; lane < kLanes; ++lane) {
    if (test.kind == ResultKind::kPositVector || (test.kind == ResultKind::kFloatVector && test.request.float_to_posit)) {
      if (actual.posit[lane] != test.expected.posit[lane]) { reason = "posit lane " + std::to_string(lane) + " expected=" + hex32(test.expected.posit[lane]) + " got=" + hex32(actual.posit[lane]); return false; }
    } else if (test.kind == ResultKind::kFloatVector) {
      const unsigned width = 1 + kFpFormats[test.request.float_mode].exponent_bits + kFpFormats[test.request.float_mode].fraction_bits;
      if ((actual.floating[lane] & width_mask(width)) != test.expected.floating[lane]) { reason = "float lane " + std::to_string(lane) + " mismatch"; return false; }
    } else if (test.kind == ResultKind::kIntVector && actual.integer[lane] != test.expected.integer[lane]) {
      reason = "int lane " + std::to_string(lane) + " expected=" + std::to_string(test.expected.integer[lane]) + " got=" + std::to_string(actual.integer[lane]); return false;
    }
  }
  if (test.kind == ResultKind::kPositDot && actual.posit_dot != test.expected.posit_dot) {
    reason = "dot expected=" + hex32(test.expected.posit_dot) + " got=" + hex32(actual.posit_dot);
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  VPvuTop dut;
  LegacyProtocolAdapter adapter(&dut);
  adapter.reset();
  const std::vector<TestCase> tests = build_tests();
  std::map<std::string, std::pair<size_t, size_t>> summary;
  size_t mismatches = 0;

  for (const TestCase& test : tests) {
    if (!adapter.in_ready()) throw std::runtime_error("input valid/ready handshake blocked by pending response");
    adapter.send(test.request);
    if (!adapter.out_valid()) throw std::runtime_error("accepted request produced no response valid");
    const PvuResponse actual = adapter.recv(true);
    ++summary[test.operation].first;
    std::string reason;
    if (!matches(test, actual, reason)) {
      ++summary[test.operation].second;
      ++mismatches;
      std::cerr << "MISMATCH " << test.operation << " " << test.category << " tag=" << test.request.tag << ": " << reason << "\n";
      if (test.operation == "op5" && test.category == "cancellation") {
        std::cerr << "  directed cancellation lhs=" << vector_hex(test.request.posit_i1)
                  << " rhs=" << vector_hex(test.request.posit_i2)
                  << " exact-SoftPosit=" << hex32(test.expected.posit_dot) << "\n";
      }
    }
  }

  std::cout << "Exact SoftPosit PVU protocol regression (legacy always-ready adapter)\n";
  for (const auto& [operation, counts] : summary) {
    std::cout << operation << ": samples=" << counts.first << " mismatches=" << counts.second << "\n";
  }
  std::cout << "total samples=" << tests.size() << " total mismatches=" << mismatches << "\n";
  dut.final();
  return mismatches == 0 ? 0 : 1;
}

#endif  // CONFIG_PVU_PROTOCOL_REGRESSION
