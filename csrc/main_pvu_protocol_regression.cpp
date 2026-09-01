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
constexpr uint32_t kHalf = 0x38000000u;
constexpr uint32_t kNegHalf = 0xc8000000u;
constexpr uint32_t kQuarter = 0x30000000u;
constexpr uint32_t kFour = 0x50000000u;
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

std::string hex64(uint64_t value) {
  std::ostringstream out;
  out << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
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
                 const std::array<uint32_t, kLanes>& rhs, uint8_t vector_size) {
  posit32_t accumulator = posit(0);
  const size_t active_lanes = vector_size == 0 ? kLanes :
    (static_cast<size_t>(vector_size) < kLanes ? static_cast<size_t>(vector_size) : kLanes);
  for (size_t lane = 0; lane < active_lanes; ++lane) {
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

uint32_t ref_posit_convert(uint32_t raw, uint8_t destination_width) {
  return destination_width == 32 ? raw : p32_to_pX2(posit(raw), destination_width).v;
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
        if (request.op == 6) response.posit[lane] = ref_posit_convert(request.posit_i1[lane], request.dst_posit_width);
        else response.posit[lane] = ref_binary(request.op, request.posit_i1[lane], request.posit_i2[lane]);
      }
      break;
    case ResultKind::kPositDot:
      response.posit_dot = ref_dot(request.posit_i1, request.posit_i2, request.vector_size);
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

class ProtocolDriver {
 public:
  explicit ProtocolDriver(VPvuTop* dut) : dut_(dut) {}

  void reset() {
    dut_->io_in_valid = 0;
    dut_->io_out_ready = 0;
    dut_->reset = 1;
    tick();
    tick();
    dut_->reset = 0;
    dut_->eval();
  }

  bool in_ready() const { return dut_->io_in_ready != 0; }
  bool out_valid() const { return dut_->io_out_valid != 0; }

  void send(const PvuRequest& request) {
    drive(request);
    dut_->io_in_valid = 1;
    dut_->eval();
    size_t cycles = 0;
    while (!in_ready()) {
      if (++cycles > 32) throw std::runtime_error("request did not observe in_ready");
      tick();
    }
    tick();
    dut_->io_in_valid = 0;
    dut_->eval();
  }

  void present(const PvuRequest& request) {
    drive(request);
    dut_->io_in_valid = 1;
    dut_->eval();
  }

  void withdraw_request() {
    dut_->io_in_valid = 0;
    dut_->eval();
  }

  void set_out_ready(bool ready) {
    dut_->io_out_ready = ready ? 1 : 0;
    dut_->eval();
  }

  void advance() { tick(); }

  PvuResponse response() const { return sample(); }

  PvuResponse recv() {
    wait_for_response();
    set_out_ready(true);
    const PvuResponse completed = sample();
    tick();
    set_out_ready(false);
    return completed;
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
    dut_->io_in_tag = request.tag;
    dut_->io_op = request.op;
    dut_->io_Isposit = request.is_posit;
    dut_->io_Outposit = request.out_posit;
    dut_->io_float_mode = request.float_mode;
    dut_->io_float_posit = request.float_to_posit;
    dut_->io_src_posit_width = request.src_posit_width;
    dut_->io_dst_posit_width = request.dst_posit_width;
    dut_->io_vector_size = request.vector_size;
  }

  void wait_for_response() {
    size_t cycles = 0;
    while (!out_valid()) {
      if (++cycles > 32) throw std::runtime_error("accepted request produced no out_valid");
      tick();
    }
  }

  PvuResponse sample() const {
    PvuResponse response{};
    response.tag = static_cast<uint32_t>(dut_->io_out_tag);
    response.op = static_cast<uint8_t>(dut_->io_out_op);
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

 public:
  static bool same_response(const PvuResponse& lhs, const PvuResponse& rhs) {
    return lhs.tag == rhs.tag && lhs.op == rhs.op && lhs.posit == rhs.posit &&
      lhs.posit_dot == rhs.posit_dot && lhs.floating == rhs.floating &&
      lhs.float_dot == rhs.float_dot && lhs.integer == rhs.integer;
  }

  VPvuTop* dut_;
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

void add_int_case(std::vector<TestCase>& tests, const std::string& category, PvuRequest request,
                  std::array<int32_t, kLanes> expected_integer) {
  PvuResponse expected{};
  expected.tag = request.tag;
  expected.op = request.op;
  expected.integer = expected_integer;
  tests.push_back({"op10", category, request, expected, ResultKind::kIntVector});
}

void add_posit_case(std::vector<TestCase>& tests, const std::string& operation,
                    const std::string& category, PvuRequest request,
                    std::array<uint32_t, kLanes> expected_posit) {
  PvuResponse expected{};
  expected.tag = request.tag;
  expected.op = request.op;
  expected.posit = expected_posit;
  tests.push_back({operation, category, request, expected, ResultKind::kPositVector});
}

void add_posit_dot_case(std::vector<TestCase>& tests, const std::string& category,
                        PvuRequest request, uint32_t expected_dot) {
  PvuResponse expected{};
  expected.tag = request.tag;
  expected.op = request.op;
  expected.posit_dot = expected_dot;
  tests.push_back({"op5", category, request, expected, ResultKind::kPositDot});
}

void add_float_case(std::vector<TestCase>& tests, uint8_t mode, const std::string& category,
                    PvuRequest request, std::array<uint64_t, kLanes> expected_float) {
  PvuResponse expected{};
  expected.tag = request.tag;
  expected.op = request.op;
  expected.floating = expected_float;
  tests.push_back({"op7-posit-to-fp" + std::to_string(mode), category, request, expected,
                   ResultKind::kFloatVector});
}

void add_arithmetic_float_case(std::vector<TestCase>& tests, const std::string& category,
                               PvuRequest request,
                               std::array<uint64_t, kLanes> expected_float) {
  PvuResponse expected{};
  expected.tag = request.tag;
  expected.op = request.op;
  expected.floating = expected_float;
  tests.push_back({"op" + std::to_string(request.op) + "-float", category, request, expected,
                   ResultKind::kFloatVector});
}

void add_float_to_posit_case(std::vector<TestCase>& tests, uint8_t mode,
                             const std::string& category, PvuRequest request,
                             std::array<uint32_t, kLanes> expected_posit) {
  PvuResponse expected{};
  expected.tag = request.tag;
  expected.op = request.op;
  expected.posit = expected_posit;
  tests.push_back({"op7-fp" + std::to_string(mode) + "-to-posit", category, request, expected,
                   ResultKind::kFloatVector});
}

void set_p32_to_p16(PvuRequest& request) {
  request.src_posit_width = 32;
  request.dst_posit_width = 16;
}

std::vector<TestCase> build_tests() {
  std::vector<TestCase> tests;
  uint32_t tag = 1;
  const std::array<std::pair<std::string, std::pair<std::array<uint32_t, kLanes>, std::array<uint32_t, kLanes>>>, 5> binary_boundaries{{
      {"zero", {repeat(0), repeat(kOne)}},
      {"NaR", {repeat(kNaR), repeat(kOne)}},
      {"NaR-rhs", {repeat(kOne), repeat(kNaR)}},
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

  for (uint8_t op : {uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{4}}) {
    const uint32_t finite_lhs = op == 2 ? kTwo : kOne;
    for (size_t lane = 0; lane < kLanes; ++lane) {
      PvuRequest lhs_nar = base_request(tag++, op);
      lhs_nar.posit_i1 = repeat(finite_lhs);
      lhs_nar.posit_i2 = repeat(kOne);
      lhs_nar.posit_i1[lane] = kNaR;
      add_case(tests, "op" + std::to_string(op), "NaR-lhs-lane-" + std::to_string(lane),
               lhs_nar, ResultKind::kPositVector);

      PvuRequest rhs_nar = base_request(tag++, op);
      rhs_nar.posit_i1 = repeat(finite_lhs);
      rhs_nar.posit_i2 = repeat(kOne);
      rhs_nar.posit_i2[lane] = kNaR;
      add_case(tests, "op" + std::to_string(op), "NaR-rhs-lane-" + std::to_string(lane),
               rhs_nar, ResultKind::kPositVector);
    }
  }

  {
    PvuRequest request = base_request(tag++, 1);
    request.posit_i1 = {kOne, kNegOne, kTwo, kNegTwo};
    request.posit_i2 = {kNegOne, kOne, kNegTwo, kTwo};
    add_posit_case(tests, "op1", "exact-cancellation", request, repeat(0));
  }
  {
    PvuRequest request = base_request(tag++, 1);
    request.posit_i1 = {kOne, kOne, kNegOne, kNegOne};
    request.posit_i2 = {kMinPos, 0x00900000u, 0xffffffffu, 0xff700000u};
    add_posit_case(tests, "op1", "large-exponent-gap-sticky", request,
                   {kOne, 0x40000001u, kNegOne, 0xbfffffffu});
  }
  {
    PvuRequest request = base_request(tag++, 1);
    request.posit_i1 = {0x44000000u, 0x44000001u, 0xbc000000u, 0xbbffffffu};
    request.posit_i2 = {0x00800000u, 0x00800000u, 0xff800000u, 0xff800000u};
    add_posit_case(tests, "op1", "rne-even-ties", request,
                   {0x44000000u, 0x44000002u, 0xbc000000u, 0xbbfffffeu});
  }
  {
    PvuRequest request = base_request(tag++, 1);
    request.posit_i1 = {0x47ffffffu, 0xb8000001u, kMaxPos, kNegMaxPos};
    request.posit_i2 = {0x00800000u, 0xff800000u, kMaxPos, kNegMaxPos};
    add_posit_case(tests, "op1", "rounding-carry-and-saturation", request,
                   {kTwo, kNegTwo, kMaxPos, kNegMaxPos});
  }
  {
    PvuRequest request = base_request(tag++, 1);
    request.posit_i1 = {0, kNegOne, kNaR, 0};
    request.posit_i2 = {kOne, 0, 0, kNaR};
    add_posit_case(tests, "op1", "zero-and-nar", request,
                   {kOne, kNegOne, kNaR, kNaR});
  }

  {
    PvuRequest request = base_request(tag++, 2);
    request.posit_i1 = {kOne, kNegOne, kMaxPos, kMinPos};
    request.posit_i2 = request.posit_i1;
    add_posit_case(tests, "op2", "exact-cancellation", request, repeat(0));
  }
  {
    PvuRequest request = base_request(tag++, 2);
    request.posit_i1 = {kOne, kOne, kNegOne, kNegOne};
    request.posit_i2 = {kMinPos, 0x00900000u, 0xffffffffu, 0xff700000u};
    add_posit_case(tests, "op2", "large-exponent-gap-sticky", request,
                   {kOne, 0x3ffffffeu, kNegOne, 0xc0000002u});
  }
  {
    PvuRequest request = base_request(tag++, 2);
    request.posit_i1 = {0x44000000u, 0x44000001u, 0xbc000000u, 0xbbffffffu};
    request.posit_i2 = {0x00800000u, 0x00800000u, 0xff800000u, 0xff800000u};
    add_posit_case(tests, "op2", "rne-even-ties", request,
                   {0x44000000u, 0x44000000u, 0xbc000000u, 0xbc000000u});
  }
  {
    PvuRequest request = base_request(tag++, 2);
    request.posit_i1 = {0x47ffffffu, 0xb8000001u, kMaxPos, kNegMaxPos};
    request.posit_i2 = {0xff800000u, 0x00800000u, kNegMaxPos, kMaxPos};
    add_posit_case(tests, "op2", "rounding-carry-and-saturation", request,
                   {kTwo, kNegTwo, kMaxPos, kNegMaxPos});
  }
  {
    PvuRequest request = base_request(tag++, 2);
    request.posit_i1 = {0, kNegOne, kNaR, 0};
    request.posit_i2 = {kOne, 0, 0, kNaR};
    add_posit_case(tests, "op2", "zero-and-nar", request,
                   {kNegOne, kNegOne, kNaR, kNaR});
  }

  {
    PvuRequest request = base_request(tag++, 1);
    request.is_posit = false;
    request.out_posit = false;
    request.float_i = {0x3f800000u, 0x40000000u, 0xbf800000u, 0xc0000000u};
    request.float_i2 = {0x3f800000u, 0x3f800000u, 0xbf800000u, 0xbf800000u};
    add_arithmetic_float_case(tests, "float-input-output-route", request,
                              {0x40000000u, 0x40400000u, 0xc0000000u, 0xc0400000u});
  }
  {
    PvuRequest request = base_request(tag++, 2);
    request.is_posit = false;
    request.out_posit = false;
    request.float_i = {0x40000000u, 0x40800000u, 0xbf800000u, 0xc0000000u};
    request.float_i2 = {0x3f800000u, 0x40000000u, 0x3f800000u, 0xbf800000u};
    add_arithmetic_float_case(tests, "float-input-output-route", request,
                              {0x3f800000u, 0x40000000u, 0xc0000000u, 0xbf800000u});
  }
  {
    PvuRequest request = base_request(tag++, 1);
    set_p32_to_p16(request);
    request.posit_i1 = {0x40000001u, 0xbfffffffu, kOne, 0};
    request.posit_i2 = {0x00800000u, 0xff800000u, kOne, kOne};
    add_posit_case(tests, "op1-p16", "p32-to-p16-route", request,
                   {kOne, 0xbfff0000u, kTwo, kOne});
  }
  {
    PvuRequest request = base_request(tag++, 2);
    set_p32_to_p16(request);
    request.posit_i1 = {kOne, kNegOne, kTwo, kNegTwo};
    request.posit_i2 = {0x00900000u, 0xff700000u, kOne, kNegOne};
    add_posit_case(tests, "op2-p16", "p32-to-p16-route", request,
                   {0x3fff0000u, kNegOne, kOne, kNegOne});
  }
  {
    PvuRequest request = base_request(tag++, 1);
    request.src_posit_width = 16;
    request.dst_posit_width = 32;
    request.posit_i1 = {0x06b40000u, kOne, kNegOne, kTwo};
    request.posit_i2 = {0x22440000u, kOne, kNegOne, kNegOne};
    add_posit_case(tests, "op1-width-route", "src16-to-dst32", request,
                   {0x22476800u, kTwo, kNegTwo, kOne});
  }
  {
    PvuRequest request = base_request(tag++, 2);
    request.src_posit_width = 16;
    request.dst_posit_width = 32;
    request.posit_i1 = {0x0b290000u, kTwo, kNegOne, kNegTwo};
    request.posit_i2 = {0x626e0000u, kOne, kOne, kNegOne};
    add_posit_case(tests, "op2-width-route", "src16-to-dst32", request,
                   {0x9d920ca4u, kOne, kNegTwo, kNegOne});
  }
  {
    PvuRequest request = base_request(tag++, 1);
    request.vector_size = 2;
    request.posit_i1 = {kOne, kNegOne, kNaR, 0x12345678u};
    request.posit_i2 = {kOne, kNegOne, kNaR, 0x87654321u};
    add_posit_case(tests, "op1", "inactive-lanes-zero", request,
                   {kTwo, kNegTwo, 0, 0});
  }
  {
    PvuRequest request = base_request(tag++, 2);
    request.vector_size = 2;
    request.posit_i1 = {kTwo, kNegTwo, kNaR, 0x12345678u};
    request.posit_i2 = {kOne, kNegOne, kNaR, 0x87654321u};
    add_posit_case(tests, "op2", "inactive-lanes-zero", request,
                   {kOne, kNegOne, 0, 0});
  }

  {
    PvuRequest request = base_request(tag++, 3);
    request.posit_i1 = {kHalf, kTwo, kNegHalf, kMaxPos};
    request.posit_i2 = {kHalf, kQuarter, kHalf, kOne};
    add_posit_case(tests, "op3", "signed-scale-and-maxpos", request,
                   {kQuarter, kHalf, 0xd0000000u, kMaxPos});
  }
  {
    PvuRequest request = base_request(tag++, 3);
    request.posit_i1 = repeat(0);
    request.posit_i2 = {kMinPos, kMaxPos, 0xffffffffu, kNegMaxPos};
    add_posit_case(tests, "op3", "zero-times-signed-extrema", request, repeat(0));
  }
  {
    PvuRequest request = base_request(tag++, 3);
    request.posit_i1 = {kMinPos, kMaxPos, 0xffffffffu, kNegMaxPos};
    request.posit_i2 = repeat(0);
    add_posit_case(tests, "op3", "signed-extrema-times-zero", request, repeat(0));
  }
  {
    PvuRequest request = base_request(tag++, 3);
    request.posit_i1 = repeat(kNaR);
    request.posit_i2 = {kMinPos, kMaxPos, 0xffffffffu, kNegMaxPos};
    add_posit_case(tests, "op3", "nar-times-signed-extrema", request, repeat(kNaR));
  }
  {
    PvuRequest request = base_request(tag++, 3);
    request.posit_i1 = {kMinPos, kMaxPos, 0xffffffffu, kNegMaxPos};
    request.posit_i2 = repeat(kNaR);
    add_posit_case(tests, "op3", "signed-extrema-times-nar", request, repeat(kNaR));
  }
  {
    PvuRequest request = base_request(tag++, 3);
    request.posit_i1 = repeat(0x464cdf6fu);
    request.posit_i2 = repeat(0x43ab5f8fu);
    add_posit_case(tests, "op3", "normalize-product-lsb-sticky", request,
                   repeat(0x4a6e0499u));
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.posit_i1 = {0, 0, kOne, kNaR};
    request.posit_i2 = {0, kOne, 0, kOne};
    add_posit_case(tests, "op4", "zero-nar-and-divide-by-zero", request,
                   {kNaR, 0, kNaR, kNaR});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.posit_i1 = {kOne, kOne, kOne, kNegOne};
    request.posit_i2 = {kTwo, kHalf, kNegTwo, kTwo};
    add_posit_case(tests, "op4", "exact-reciprocals-and-signs", request,
                   {kHalf, kTwo, kNegHalf, kNegHalf});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.src_posit_width = 0;
    request.dst_posit_width = 0;
    request.posit_i1 = {kMaxPos, kMinPos, kMinPos, kMaxPos};
    request.posit_i2 = {kFour, kQuarter, kFour, kMinPos};
    add_posit_case(tests, "op4", "rne-even-ties-and-saturation", request,
                   {0x7ffffffeu, 0x00000002u, kMinPos, kMaxPos});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.is_posit = false;
    request.out_posit = false;
    request.float_to_posit = false;
    request.float_mode = 3;
    request.posit_i1 = {kNaR, kNegOne, 0, kMaxPos};
    request.posit_i2 = {0, kNaR, kNegOne, 0};
    request.float_i = {0x41000000u, 0xc1000000u, 0x3fc00000u, 0x3f800000u};
    request.float_i2 = {0x40000000u, 0x40000000u, 0xbf000000u, 0x40000000u};
    add_arithmetic_float_case(tests, "float-input-finite-and-signs", request,
                              {0x40800000u, 0xc0800000u, 0xc0400000u, 0x3f000000u});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.is_posit = false;
    request.out_posit = false;
    request.float_to_posit = false;
    request.float_mode = 3;
    request.posit_i1 = {kOne, 0, kNaR, kNegOne};
    request.posit_i2 = {kNaR, kOne, 0, kMaxPos};
    request.float_i = {0x00000000u, 0xbf800000u, 0x7f800000u, 0xbf800000u};
    request.float_i2 = {0x00000000u, 0x00000000u, 0x7f800000u, 0x7f800000u};
    add_arithmetic_float_case(tests, "float-input-special-values", request,
                              {0x7f800001u, 0xff800000u, 0x7f800001u, 0x80000000u});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.is_posit = false;
    request.out_posit = true;
    request.float_to_posit = true;
    request.float_mode = 3;
    request.posit_i1 = {kNaR, kNegOne, 0, kMaxPos};
    request.posit_i2 = {0, kNaR, kNegOne, 0};
    request.float_i = {0x41000000u, 0xc1000000u, 0x3fc00000u, 0x3f800000u};
    request.float_i2 = {0x40000000u, 0x40000000u, 0xbf000000u, 0x40000000u};
    add_posit_case(tests, "op4-float-to-posit", "float-input-default-posit-output", request,
                   {kFour, 0xb0000000u, 0xb4000000u, kHalf});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.out_posit = false;
    request.float_to_posit = false;
    request.float_mode = 3;
    request.posit_i1 = {kOne, kNegOne, kMaxPos, kMinPos};
    request.posit_i2 = {kTwo, kTwo, kMinPos, kMaxPos};
    add_arithmetic_float_case(tests, "posit-input-float-output-scale-range", request,
                              {0x3f000000u, 0xbf000000u, 0x7f800000u, 0x00000000u});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.out_posit = false;
    request.float_to_posit = false;
    request.float_mode = 3;
    request.posit_i1 = {kNaR, kOne, 0, kOne};
    request.posit_i2 = {kOne, 0, 0, kNaR};
    add_arithmetic_float_case(tests, "posit-input-invalid-canonical-positive-nan", request,
                              {0x7f800001u, 0x7f800001u, 0x7f800001u, 0x7f800001u});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.out_posit = false;
    request.float_to_posit = false;
    request.float_mode = 3;
    request.posit_i1 = {kMinPos, kMinPos, kOne, kNegOne};
    request.posit_i2 = {0x70000000u, 0x6c000000u, 0x4c000000u, 0x4c000000u};
    add_arithmetic_float_case(tests, "posit-input-float-subnormal-and-rne", request,
                              {0x00200000u, 0x00400000u, 0x3eaaaaabu, 0xbeaaaaabu});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.is_posit = false;
    request.out_posit = false;
    request.float_to_posit = false;
    request.float_mode = 3;
    request.posit_i1 = {kNaR, kMinPos, 0, kMaxPos};
    request.posit_i2 = {0, kNaR, kOne, 0};
    request.float_i = {0x00000001u, 0x3f800000u, 0x00400000u, 0x00800000u};
    request.float_i2 = {0x3f800000u, 0x00000001u, 0x40000000u, 0x40000000u};
    add_arithmetic_float_case(tests, "float-input-subnormal-operands", request,
                              {0x00000001u, 0x7f800000u, 0x00200000u, 0x00400000u});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.is_posit = false;
    request.out_posit = false;
    request.float_to_posit = false;
    request.float_mode = 3;
    request.float_i = {0x7f7fffffu, 0xff7fffffu, 0x00000001u, 0x80000001u};
    request.float_i2 = {0x00000001u, 0x00000001u, 0x7f7fffffu, 0x7f7fffffu};
    add_arithmetic_float_case(tests, "float-input-tenth-scale-bit", request,
                              {0x7f800000u, 0xff800000u, 0x00000000u, 0x80000000u});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.is_posit = false;
    request.out_posit = false;
    request.float_to_posit = false;
    request.float_mode = 3;
    request.float_i = {0x00000005u, 0x00000003u, 0x00800000u, 0x00ffffffu};
    request.float_i2 = {0x40000000u, 0x40000000u, 0x40400000u, 0x40000000u};
    add_arithmetic_float_case(tests, "float-input-subnormal-grs-rne", request,
                              {0x00000002u, 0x00000002u, 0x002aaaabu, 0x00800000u});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.dst_posit_width = 16;
    request.posit_i1 = {kOne, kNegOne, kOne, kOne};
    request.posit_i2 = {0x4c000000u, 0x4c000000u, kTwo, 0};
    add_posit_case(tests, "op4-p16", "nondefault-posit-output", request,
                   {0x32ab0000u, 0xcd550000u, kHalf, kNaR});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.src_posit_width = 16;
    request.dst_posit_width = 32;
    request.posit_i1 = {kOne, kNegOne, kOne, kOne};
    request.posit_i2 = {0x4c000000u, 0x4c000000u, kTwo, 0};
    add_posit_case(tests, "op4-width-route", "nondefault-source-must-not-select-raw-p32",
                   request, {0x32aaaaaau, 0xcd555556u, kHalf, kNaR});
  }

  {
    PvuRequest request = base_request(tag++, 4);
    request.src_posit_width = 16;
    request.dst_posit_width = 32;
    request.posit_i1 = {kMaxPos, kNaR, kOne, kNegOne};
    request.posit_i2 = {kOne, kOne, kNegTwo, kTwo};
    add_posit_case(tests, "op4-width-route", "src16-decoded-maxpos-nar-and-sign", request,
                   {kOne, kNaR, kNegHalf, kNegHalf});
  }

  for (uint8_t op : {uint8_t{8}, uint8_t{9}}) {
    PvuRequest request = base_request(tag++, op);
    request.posit_i1 = {0x12345678u, 0xedcba988u, kMaxPos, kNegMaxPos};
    request.posit_i2 = request.posit_i1;
    add_case(tests, "op" + std::to_string(op), "raw-equality", request, ResultKind::kPositVector);
  }

  const std::array<std::pair<std::string, std::array<uint32_t, kLanes>>, 4> unary_boundaries{{
      {"zero", repeat(0)}, {"NaR", repeat(kNaR)}, {"extrema", {kMaxPos, kMinPos, kNegMaxPos, kMinPos}},
      {"mixed-sign", {kOne, kNegOne, kTwo, kNegTwo}}}};
  for (uint8_t op : {uint8_t{6}, uint8_t{10}}) {
    for (const auto& boundary : unary_boundaries) {
      PvuRequest request = base_request(tag++, op);
      if (op == 6) set_p32_to_p16(request);
      request.posit_i1 = boundary.second;
      add_case(tests, "op" + std::to_string(op), boundary.first, request,
               op == 10 ? ResultKind::kIntVector : ResultKind::kPositVector);
    }
  }

  {
    PvuRequest request = base_request(tag++, 10);
    request.posit_i1 = {0x38000000u, 0xc8000000u, 0x44000000u, 0xbc000000u};
    add_int_case(tests, "rne-even-ties", request, {0, 0, 2, -2});
  }

  {
    PvuRequest request = base_request(tag++, 10);
    request.posit_i1 = {0x4e000000u, 0xb2000000u, 0x51000000u, 0xaf000000u};
    add_int_case(tests, "general-rne-even-ties", request, {4, -4, 4, -4});
  }

  {
    PvuRequest request = base_request(tag++, 10);
    request.posit_i1 = {kNaR, kMaxPos, kNegMaxPos, kMinPos};
    add_int_case(tests, "nar-and-finite-extrema", request,
                 {0, std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min(), 0});
  }

  {
    PvuRequest request = base_request(tag++, 10);
    request.posit_i1 = {0, kMinPos, 0xffffffffu, kNaR};
    add_int_case(tests, "zero-and-signed-minpos", request, {0, 0, 0, 0});
  }

  {
    PvuRequest request = base_request(tag++, 10);
    request.vector_size = 2;
    request.posit_i1 = {kOne, kNegOne, kMaxPos, kNegMaxPos};
    add_int_case(tests, "inactive-lanes-zero", request, {1, -1, 0, 0});
  }

  {
    PvuRequest request = base_request(tag++, 10);
    request.posit_i1 = {0x7fafffffu, 0x7fb00000u, 0x80500001u, 0x80500000u};
    add_int_case(tests, "saturation-edges", request,
                 {2147482624, std::numeric_limits<int32_t>::max(), -2147482624,
                  std::numeric_limits<int32_t>::min()});
  }

  std::mt19937 op10_seeded(0x10c032u);
  for (size_t sample = 0; sample < 256; ++sample) {
    PvuRequest request = base_request(tag++, 10);
    for (size_t lane = 0; lane < kLanes; ++lane) request.posit_i1[lane] = op10_seeded();
    add_case(tests, "op10", "seed-0x10c032-" + std::to_string(sample), request,
             ResultKind::kIntVector);
  }

  {
    PvuRequest request = base_request(tag++, 6);
    request.src_posit_width = 32;
    request.dst_posit_width = 16;
    request.posit_i1 = {0x40008000u, 0x40018000u, 0xbfff8000u, 0xbffe8000u};
    add_posit_case(tests, "op6", "p32-to-p16-rne-ties", request,
                   {0x40000000u, 0x40020000u, 0xc0000000u, 0xbffe0000u});
  }

  {
    PvuRequest request = base_request(tag++, 6);
    request.src_posit_width = 32;
    request.dst_posit_width = 16;
    request.posit_i1 = {0, kNaR, 0x7fff8000u, 0x80008000u};
    add_posit_case(tests, "op6", "p32-to-p16-special-saturation", request,
                   {0, kNaR, 0x7fff0000u, 0x80010000u});
  }

  {
    PvuRequest request = base_request(tag++, 6);
    request.src_posit_width = 32;
    request.dst_posit_width = 16;
    request.posit_i1 = {0x47ff8000u, 0xb8008000u, 0x00000001u, 0xffffffffu};
    add_posit_case(tests, "op6", "p32-to-p16-round-carry-and-minpos", request,
                   {0x48000000u, 0xb8000000u, 0x00010000u, 0xffff0000u});
  }

  {
    PvuRequest request = base_request(tag++, 6);
    request.src_posit_width = 32;
    request.dst_posit_width = 16;
    // 0x40018001 has guard/sticky bits that round upward when encoded as P16.
    request.posit_i1 = {0x40018001u, 0xbffe7fffu, kNaR, kMaxPos};
    add_case(tests, "op6", "p32-to-p16-rne", request, ResultKind::kPositVector);
  }

  using PositPair = std::pair<std::array<uint32_t, kLanes>, std::array<uint32_t, kLanes>>;
  constexpr uint32_t kThreeHalves = 0x44000000u;
  constexpr uint32_t kNegThreeHalves = 0xbc000000u;
  const std::array<std::pair<std::string, PositPair>, 7> dot_boundaries{{
      std::make_pair(std::string("cancellation"), PositPair{{kOne, kNegOne, kTwo, kNegTwo}, repeat(kOne)}),
      std::make_pair(std::string("zero"), PositPair{repeat(0), repeat(kOne)}),
      std::make_pair(std::string("NaR"), PositPair{repeat(kNaR), repeat(kOne)}),
      std::make_pair(std::string("extrema"), PositPair{{kMaxPos, kMinPos, kNegMaxPos, kMinPos}, {kMinPos, kMaxPos, kMinPos, kNegMaxPos}}),
      std::make_pair(std::string("mixed-sign"), PositPair{{kOne, kNegOne, kTwo, kNegTwo}, {kTwo, kNegTwo, kNegOne, kOne}}),
      std::make_pair(std::string("positive-near-full-scale"), PositPair{repeat(kThreeHalves), repeat(kThreeHalves)}),
      std::make_pair(std::string("negative-near-full-scale"), PositPair{repeat(kThreeHalves), repeat(kNegThreeHalves)})}};
  for (const auto& boundary : dot_boundaries) {
    PvuRequest request = base_request(tag++, 5);
    request.posit_i1 = boundary.second.first;
    request.posit_i2 = boundary.second.second;
    add_case(tests, "op5", boundary.first, request, ResultKind::kPositDot);
  }

  {
    PvuRequest request = base_request(tag++, 5);
    request.posit_i1 = {0x48000000u, 0x79194000u, 0x50000000u, 0x00000000u};
    request.posit_i2 = request.posit_i1;
    add_posit_dot_case(tests, "sequential-rounded-accumulation", request,
                       0x7f469fb2u);
  }
  {
    PvuRequest request = base_request(tag++, 5);
    request.posit_i1 = {0x464cdf6fu, 0u, 0u, 0u};
    request.posit_i2 = {0x43ab5f8fu, kOne, kOne, kOne};
    add_posit_dot_case(tests, "fused-normalize-product-lsb-sticky", request,
                       0x4a6e0499u);
  }

  {
    const std::array<uint32_t, kLanes> lhs{{0xde3d9385u, 0xef1fc26du, 0x9cd48b28u, 0x1f35fc38u}};
    const std::array<uint32_t, kLanes> rhs{{0x369800e1u, 0xdbd84b1cu, 0xa25d4f24u, 0x6b7dbfbcu}};
    for (size_t prefix = 1; prefix <= kLanes; ++prefix) {
      PvuRequest request = base_request(tag++, 5);
      request.vector_size = static_cast<uint8_t>(prefix);
      request.posit_i1 = lhs;
      request.posit_i2 = rhs;
      add_case(tests, "op5", "fused-prefix-" + std::to_string(prefix), request,
               ResultKind::kPositDot);
    }
  }

  {
    PvuRequest request = base_request(tag++, 5);
    request.vector_size = 1;
    request.posit_i1 = {kOne, kNaR, kNaR, kNaR};
    request.posit_i2 = {kTwo, kNaR, kNaR, kNaR};
    add_case(tests, "op5", "inactive-lane-nar-is-ignored", request, ResultKind::kPositDot);
  }

  for (size_t lane = 0; lane < kLanes; ++lane) {
    PvuRequest lhs_nar = base_request(tag++, 5);
    lhs_nar.posit_i1 = repeat(kOne);
    lhs_nar.posit_i2 = repeat(kOne);
    lhs_nar.posit_i1[lane] = kNaR;
    add_case(tests, "op5", "NaR-lhs-lane-" + std::to_string(lane), lhs_nar, ResultKind::kPositDot);

    PvuRequest rhs_nar = base_request(tag++, 5);
    rhs_nar.posit_i1 = repeat(kOne);
    rhs_nar.posit_i2 = repeat(kOne);
    rhs_nar.posit_i2[lane] = kNaR;
    add_case(tests, "op5", "NaR-rhs-lane-" + std::to_string(lane), rhs_nar, ResultKind::kPositDot);
  }

  const std::vector<uint32_t> activations = load_words("test_src/posit_activations.bin", 24);
  const std::vector<uint32_t> weights = load_words("test_src/posit_weights.bin", 24);
  const std::vector<uint32_t> posit_data = load_words("test_src/posit32_data.bin", 24);
  for (size_t sample = 0; sample + 3 < activations.size() && sample + 3 < weights.size(); sample += 4) {
    for (uint8_t op : {uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{4}, uint8_t{5}, uint8_t{6}, uint8_t{8}, uint8_t{9}, uint8_t{10}}) {
      PvuRequest request = base_request(tag++, op);
      if (op == 6) set_p32_to_p16(request);
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

  struct DirectedPositToFloatCase {
    uint8_t mode;
    const char* category;
    std::array<uint32_t, kLanes> input;
    std::array<uint64_t, kLanes> expected;
  };
  const std::array<DirectedPositToFloatCase, 18> posit_to_float_directed{{
      {0, "special-and-signed-underflow", {kNaR, 0, kMinPos, 0xffffffffu},
       {0x5, 0x0, 0x0, 0x8}},
      {0, "subnormal-range", {0x38000000u, 0xc8000000u, 0x44000000u, 0xbc000000u},
       {0x1, 0x9, 0x3, 0xb}},
      {0, "subnormal-to-infinity-transition", {0x46000000u, 0xba000000u, kMaxPos, kNegMaxPos},
       {0x4, 0xc, 0x4, 0xc}},
      {0, "rne-even-and-overflow-carry", {0x3c000000u, 0x42000000u, 0x46000000u, 0xba000000u},
       {0x2, 0x2, 0x4, 0xc}},

      {1, "special-and-signed-underflow", {kNaR, 0, kMinPos, 0xffffffffu},
       {0x79, 0x00, 0x00, 0x80}},
      {1, "subnormal-normal-transition", {0x0e000000u, 0xf2000000u, 0x17000000u, 0x17800000u},
       {0x01, 0x81, 0x07, 0x08}},
      {1, "max-finite-and-overflow", {0x6f800000u, 0x90800000u, 0x6fc00000u, 0x90400000u},
       {0x77, 0xf7, 0x78, 0xf8}},
      {1, "rne-even-and-significand-carry", {0x40800000u, 0x41800000u, 0x47800000u, 0xb8800000u},
       {0x38, 0x3a, 0x40, 0xc0}},

      {2, "special-and-signed-underflow", {kNaR, 0, kMinPos, 0xffffffffu},
       {0x7c01, 0x0000, 0x0000, 0x8000}},
      {2, "subnormal-normal-transition", {0x01000000u, 0xff000000u, 0x05ff8000u, 0x05ffc000u},
       {0x0001, 0x8001, 0x03ff, 0x0400}},
      {2, "max-finite-and-overflow", {0x7bffc000u, 0x84004000u, 0x7bffe000u, 0x84002000u},
       {0x7bff, 0xfbff, 0x7c00, 0xfc00}},
      {2, "rne-even-and-significand-carry", {0x40010000u, 0x40030000u, 0x47ff0000u, 0xb8010000u},
       {0x3c00, 0x3c02, 0x4000, 0xc000}},

      {3, "special-and-reachable-normal-minimum", {kNaR, 0, kMinPos, 0xffffffffu},
       {0x7f800001, 0x00000000, 0x03800000, 0x83800000}},
      {3, "reachable-normal-maximum", {kMaxPos, kNegMaxPos, kOne, kNegOne},
       {0x7b800000, 0xfb800000, 0x3f800000, 0xbf800000}},
      {3, "rne-even-and-significand-carry", {0x40000008u, 0x40000018u, 0x47fffff8u, 0xb8000008u},
       {0x3f800000, 0x3f800002, 0x40000000, 0xc0000000}},

      {4, "special-and-reachable-normal-minimum", {kNaR, 0, kMinPos, 0xffffffffu},
       {0x7ff0000000000001ULL, 0x0000000000000000ULL, 0x3870000000000000ULL,
        0xb870000000000000ULL}},
      {4, "reachable-normal-maximum", {kMaxPos, kNegMaxPos, kOne, kNegOne},
       {0x4770000000000000ULL, 0xc770000000000000ULL, 0x3ff0000000000000ULL,
        0xbff0000000000000ULL}},
      {4, "exact-fixed-point-alignment", {0x40000001u, 0x40000003u, 0x47ffffffu, kTwo},
       {0x3ff0000002000000ULL, 0x3ff0000006000000ULL, 0x3ffffffffe000000ULL,
        0x4000000000000000ULL}},
  }};
  for (const DirectedPositToFloatCase& directed : posit_to_float_directed) {
    PvuRequest request = base_request(tag++, 7);
    request.is_posit = true;
    request.out_posit = false;
    request.float_to_posit = false;
    request.float_mode = directed.mode;
    request.posit_i1 = directed.input;
    add_float_case(tests, directed.mode, directed.category, request, directed.expected);
  }

  struct DirectedFloatToPositCase {
    uint8_t mode;
    const char* category;
    std::array<uint64_t, kLanes> input;
    std::array<uint32_t, kLanes> expected;
  };
  const std::array<DirectedFloatToPositCase, 12> float_to_posit_directed{{
      {0, "signed-zero-and-subnormal", {0x0, 0x8, 0x1, 0x9},
       {0x00000000u, 0x00000000u, 0x38000000u, 0xc8000000u}},
      {0, "finite-high-and-special", {0x3, 0xb, 0x4, 0x7},
       {0x44000000u, 0xbc000000u, kNaR, kNaR}},

      {1, "signed-zero-and-subnormal", {0x00, 0x80, 0x01, 0x81},
       {0x00000000u, 0x00000000u, 0x0e000000u, 0xf2000000u}},
      {1, "finite-high-and-special", {0x77, 0xf7, 0x78, 0x79},
       {0x6f800000u, 0x90800000u, kNaR, kNaR}},

      {2, "signed-zero-and-subnormal", {0x0000, 0x8000, 0x0001, 0x8001},
       {0x00000000u, 0x00000000u, 0x01000000u, 0xff000000u}},
      {2, "finite-high-and-special", {0x7bff, 0xfbff, 0x7c00, 0x7c01},
       {0x7bffc000u, 0x84004000u, kNaR, kNaR}},

      {3, "signed-zero-and-subnormal-saturation",
       {0x00000000, 0x80000000, 0x00000001, 0x80000001},
       {0x00000000u, 0x00000000u, kMinPos, 0xffffffffu}},
      {3, "finite-high-saturation-and-special",
       {0x7f7fffff, 0xff7fffff, 0x7f800000, 0x7fc00001},
       {kMaxPos, kNegMaxPos, kNaR, kNaR}},
      {3, "rne-even-ties-and-carry", {0x49800001, 0x49800003, 0x4b7fffff, 0xcb7fffff},
       {0x7e000000u, 0x7e000002u, 0x7f000000u, 0x81000000u}},

      {4, "signed-zero-and-subnormal-saturation",
       {0x0000000000000000ULL, 0x8000000000000000ULL, 0x0000000000000001ULL,
        0x8000000000000001ULL},
       {0x00000000u, 0x00000000u, kMinPos, 0xffffffffu}},
      {4, "finite-high-saturation-and-special",
       {0x7fefffffffffffffULL, 0xffefffffffffffffULL, 0x7ff0000000000000ULL,
        0x7ff8000000000001ULL},
       {kMaxPos, kNegMaxPos, kNaR, kNaR}},
      {4, "single-rne-and-carry",
       {0x413000005fffffffULL, 0xc13000005fffffffULL, 0x416fffffffffffffULL,
        0xc16fffffffffffffULL},
       {0x7e000001u, 0x81ffffffu, 0x7f000000u, 0x81000000u}},
  }};
  for (const DirectedFloatToPositCase& directed : float_to_posit_directed) {
    PvuRequest request = base_request(tag++, 7);
    request.is_posit = false;
    request.out_posit = true;
    request.float_to_posit = true;
    request.float_mode = directed.mode;
    request.float_i = directed.input;
    add_float_to_posit_case(tests, directed.mode, directed.category, request, directed.expected);
  }

  // Fixed seed makes this supplemental SoftPosit corpus reproducible.
  std::mt19937 seeded(0x5eed1234u);
  for (size_t sample = 0; sample < 8; ++sample) {
    std::array<uint32_t, kLanes> lhs{};
    std::array<uint32_t, kLanes> rhs{};
    for (size_t lane = 0; lane < kLanes; ++lane) { lhs[lane] = seeded(); rhs[lane] = seeded(); }
    for (uint8_t op : {uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{4}, uint8_t{5}, uint8_t{6}, uint8_t{8}, uint8_t{9}, uint8_t{10}}) {
      PvuRequest request = base_request(tag++, op);
      if (op == 6) set_p32_to_p16(request);
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
  std::mt19937 mul_seeded(0x4d553332u);
  for (size_t sample = 0; sample < 512; ++sample) {
    PvuRequest request = base_request(tag++, 3);
    for (size_t lane = 0; lane < kLanes; ++lane) {
      request.posit_i1[lane] = mul_seeded();
      request.posit_i2[lane] = mul_seeded();
    }
    add_case(tests, "op3", "exact-random-0x4d553332-" + std::to_string(sample),
             request, ResultKind::kPositVector);
  }

  std::mt19937 dot_seeded(0xd07f32u);
  for (size_t sample = 0; sample < 512; ++sample) {
    PvuRequest request = base_request(tag++, 5);
    for (size_t lane = 0; lane < kLanes; ++lane) {
      request.posit_i1[lane] = dot_seeded();
      request.posit_i2[lane] = dot_seeded();
    }
    add_case(tests, "op5", "fused-random-0xd07f32-" + std::to_string(sample),
             request, ResultKind::kPositDot);
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
      const uint64_t actual_float = actual.floating[lane] & width_mask(width);
      if (actual_float != test.expected.floating[lane]) {
        reason = "float lane " + std::to_string(lane) + " expected=" + hex64(test.expected.floating[lane]) +
                 " got=" + hex64(actual_float);
        return false;
      }
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

void require_match(const TestCase& test, const PvuResponse& actual, const char* phase) {
  std::string reason;
  if (!matches(test, actual, reason)) {
    throw std::runtime_error(std::string(phase) + ": " + reason);
  }
}


void run_source_structural_guards() {
  std::ifstream source_file("src/main/scala/pvu/PvuTop.scala");
  if (!source_file) {
    throw std::runtime_error("could not open PvuTop.scala for structural guard");
  }

  const std::string source((std::istreambuf_iterator<char>(source_file)),
                           std::istreambuf_iterator<char>());
  const std::string encode_marker = "val encodedNext = Wire";
  const std::string division_marker = "val executedDivision = Wire";
  const size_t encode_begin = source.find(encode_marker);
  const size_t encode_end = source.find(division_marker);
  if (encode_begin == std::string::npos || encode_end == std::string::npos ||
      encode_end <= encode_begin) {
    throw std::runtime_error("could not locate PvuTop encode stage for structural guard");
  }

  const std::string encode_stage = source.substr(encode_begin, encode_end - encode_begin);
  const std::array<std::string, 3> forbidden{
      "new PositDecode", "new PositAddSub", "new ExactP32Mul"};
  for (const std::string& token : forbidden) {
    if (encode_stage.find(token) != std::string::npos) {
      throw std::runtime_error("raw P32 encode stage still recomputes through " + token);
    }
  }

  const std::array<std::string, 5> required{
      "rawScale", "rawMagnitude", "rawAligned", "rawLowerSticky", "rawBypassValue"};
  for (const std::string& token : required) {
    if (source.find(token) == std::string::npos) {
      throw std::runtime_error("raw P32 pipeline payload is missing " + token);
    }
  }

  const auto count_instances = [](const std::string& text, const std::string& token) {
    size_t count = 0;
    size_t offset = 0;
    while ((offset = text.find(token, offset)) != std::string::npos) {
      ++count;
      offset += token.size();
    }
    return count;
  };
  const size_t source_dividers = count_instances(source, "new Div(");
  if (source_dividers != 1) {
    throw std::runtime_error("PvuTop.scala must elaborate exactly one divider, found " +
                             std::to_string(source_dividers));
  }

  std::ifstream rtl_file("vsrc/PvuTop.sv");
  if (!rtl_file) {
    throw std::runtime_error("could not open generated PvuTop.sv for divider guard");
  }
  const std::string rtl((std::istreambuf_iterator<char>(rtl_file)),
                        std::istreambuf_iterator<char>());
  const size_t rtl_dividers = count_instances(rtl, "\n  Div ");
  if (rtl_dividers != 1) {
    throw std::runtime_error("generated PvuTop.sv must instantiate exactly one divider, found " +
                             std::to_string(rtl_dividers));
  }
}

void run_registered_boundary_latency(ProtocolDriver& adapter, const TestCase& test) {
  if (test.request.op == 4) {
    throw std::runtime_error("registered boundary latency test requires a non-division request");
  }

  adapter.reset();
  adapter.set_out_ready(true);
  adapter.present(test.request);
  if (!adapter.in_ready()) throw std::runtime_error("latency test request was not ready");
  adapter.advance();
  adapter.withdraw_request();

  size_t latency = 0;
  while (!adapter.out_valid()) {
    if (++latency > 32) throw std::runtime_error("latency test request did not complete");
    adapter.advance();
  }
  if (latency != 5) {
    throw std::runtime_error("non-division request crossed " + std::to_string(latency) +
                             " registered boundaries, expected decode/core/reduce/normalize/encode = 5");
  }
  require_match(test, adapter.response(), "registered boundary latency response");
  adapter.advance();
  adapter.set_out_ready(false);
}

void run_adjacent_non_division_pipeline(ProtocolDriver& adapter,
                                        const TestCase& a,
                                        const TestCase& b) {
  if (a.request.op == 4 || b.request.op == 4) {
    throw std::runtime_error("adjacent non-division test received a division request");
  }

  adapter.reset();
  adapter.set_out_ready(true);
  adapter.present(a.request);
  if (!adapter.in_ready()) throw std::runtime_error("non-division request A was not ready");
  adapter.advance();

  // The accepted request must cross registered execution boundaries.  A
  // response here would be the Task 3 accept-edge combinational bypass.
  if (adapter.out_valid()) {
    throw std::runtime_error("non-division response bypassed the registered pipeline");
  }

  adapter.present(b.request);
  if (!adapter.in_ready()) {
    throw std::runtime_error("adjacent non-division request B was not accepted at one-per-cycle throughput");
  }
  adapter.advance();
  adapter.withdraw_request();

  const std::array<const TestCase*, 2> expected{{&a, &b}};
  size_t completed = 0;
  size_t idle_cycles = 0;
  bool previous_cycle_completed = false;
  while (completed < expected.size()) {
    const bool completing = adapter.out_valid();
    if (completing) {
      require_match(*expected.at(completed), adapter.response(),
                    "adjacent non-division response order");
      if (completed == 1 && !previous_cycle_completed) {
        throw std::runtime_error("adjacent non-division responses lost one-per-cycle throughput");
      }
      ++completed;
    } else if (++idle_cycles > 32) {
      throw std::runtime_error("adjacent non-division requests did not drain");
    }
    previous_cycle_completed = completing;
    adapter.advance();
  }
  adapter.set_out_ready(false);
}

void run_division_busy_backpressure(ProtocolDriver& adapter,
                                    const TestCase& a,
                                    const TestCase& b,
                                    const TestCase& non_division) {
  if (a.request.op != 4 || b.request.op != 4 || non_division.request.op == 4) {
    throw std::runtime_error("division concurrency test requires div A, div B, and non-div C");
  }

  adapter.reset();
  adapter.set_out_ready(true);
  adapter.present(a.request);
  if (!adapter.in_ready()) throw std::runtime_error("division request A was not ready");
  adapter.advance();

  // Prove that a second division cannot overwrite the occupied lane.
  adapter.present(b.request);
  if (adapter.in_ready()) {
    throw std::runtime_error("second division observed in_ready while the division lane was busy");
  }
  adapter.advance();
  adapter.withdraw_request();

  // A non-division request must still use the independent fixed-latency lane.
  adapter.present(non_division.request);
  if (!adapter.in_ready()) {
    throw std::runtime_error("non-division request C was blocked by an occupied division lane");
  }
  adapter.advance();
  adapter.withdraw_request();

  // Hold B again.  It may handshake only after the older A and C transactions
  // have completed in order and the division lane/order guard has capacity.
  adapter.present(b.request);
  const std::array<const TestCase*, 2> older{{&a, &non_division}};
  size_t completed = 0;
  bool b_accepted = false;
  size_t cycles = 0;
  while (!b_accepted) {
    const bool completing = adapter.out_valid();
    const bool accepting_b = adapter.in_ready();
    if (completing) {
      if (completed >= older.size()) {
        throw std::runtime_error("unexpected response before division B acceptance");
      }
      require_match(*older.at(completed), adapter.response(),
                    "division/non-division ordered response");
      ++completed;
    }
    adapter.advance();
    b_accepted = accepting_b;
    if (++cycles > 64) throw std::runtime_error("division B never regained safe lane capacity");
  }
  adapter.withdraw_request();
  // B may reserve the idle divider before C drains, but the barrier must keep
  // every older response ahead of B at the output.
  while (completed != older.size()) {
    if (adapter.out_valid()) {
      require_match(*older.at(completed), adapter.response(),
                    "division/non-division ordered response after B admission");
      ++completed;
    }
    adapter.advance();
  }

  size_t wait_cycles = 0;
  while (!adapter.out_valid()) {
    if (++wait_cycles > 32) throw std::runtime_error("division request B did not complete");
    adapter.advance();
  }
  require_match(b, adapter.response(), "division response B");
  adapter.advance();
  adapter.set_out_ready(false);
}

void run_non_division_then_division(ProtocolDriver& adapter,
                                    const TestCase& non_division,
                                    const TestCase& division) {
  if (non_division.request.op == 4 || division.request.op != 4) {
    throw std::runtime_error("reverse division concurrency test requires non-div A and div B");
  }

  adapter.reset();
  adapter.set_out_ready(true);
  adapter.present(non_division.request);
  if (!adapter.in_ready()) throw std::runtime_error("non-division A was not ready");
  adapter.advance();
  adapter.withdraw_request();

  // B must be admitted while A occupies the fixed lane because the division
  // lane is still idle.  The response order must nevertheless remain A then B.
  adapter.present(division.request);
  if (!adapter.in_ready()) {
    throw std::runtime_error("idle division lane was blocked by fixed-lane traffic");
  }
  adapter.advance();
  adapter.withdraw_request();

  const std::array<const TestCase*, 2> expected{{&non_division, &division}};
  size_t completed = 0;
  size_t cycles = 0;
  while (completed < expected.size()) {
    if (adapter.out_valid()) {
      require_match(*expected.at(completed), adapter.response(),
                    "non-division/division ordered response");
      ++completed;
    }
    adapter.advance();
    if (++cycles > 64) throw std::runtime_error("non-division/division requests did not drain");
  }
  adapter.set_out_ready(false);
}


void run_stalled_fixed_lane_then_division(ProtocolDriver& adapter,
                                          const TestCase& a,
                                          const TestCase& b,
                                          const TestCase& division) {
  if (a.request.op == 4 || b.request.op == 4 || division.request.op != 4) {
    throw std::runtime_error("stalled reverse concurrency test requires fixed A/B and division C");
  }

  adapter.reset();
  adapter.set_out_ready(false);
  adapter.present(a.request);
  if (!adapter.in_ready()) throw std::runtime_error("fixed request A was not ready");
  adapter.advance();

  adapter.present(b.request);
  if (!adapter.in_ready()) throw std::runtime_error("fixed request B was not ready");
  adapter.advance();
  adapter.withdraw_request();

  size_t fill_cycles = 0;
  while (!adapter.out_valid()) {
    if (++fill_cycles > 16) throw std::runtime_error("fixed request A never reached stalled output");
    adapter.advance();
  }
  const PvuResponse held_a = adapter.response();

  // Keep A stalled long enough for B to occupy the fixed encode stage.  The
  // divider is still idle, so it must be able to capture C independently.
  for (size_t cycle = 0; cycle < 2; ++cycle) {
    if (!adapter.out_valid() || !ProtocolDriver::same_response(held_a, adapter.response())) {
      throw std::runtime_error("fixed response A was not stable during output stall");
    }
    adapter.advance();
  }

  adapter.present(division.request);
  if (!adapter.in_ready()) {
    throw std::runtime_error("idle divider was blocked by stalled fixed-lane output");
  }
  adapter.advance();
  adapter.withdraw_request();

  const std::array<const TestCase*, 3> expected{{&a, &b, &division}};
  size_t completed = 0;
  adapter.set_out_ready(true);
  size_t cycles = 0;
  while (completed < expected.size()) {
    if (adapter.out_valid()) {
      require_match(*expected.at(completed), adapter.response(),
                    "stalled fixed-lane/division ordered response");
      ++completed;
    }
    adapter.advance();
    if (++cycles > 96) throw std::runtime_error("stalled fixed-lane/division requests did not drain");
  }
  adapter.set_out_ready(false);
}

bool run_backpressure_pop_push(ProtocolDriver& adapter, const TestCase& a, const TestCase& b) {
  std::array<const TestCase*, 2> scoreboard{};
  size_t head = 0;
  size_t tail = 0;
  auto enqueue = [&](const TestCase& test) { scoreboard.at(tail++) = &test; };
  auto expect_front = [&](const char* phase) {
    if (head == tail) throw std::runtime_error(std::string("empty scoreboard at ") + phase);
    if (!adapter.out_valid()) throw std::runtime_error(std::string("out_valid dropped at ") + phase);
    std::string reason;
    if (!matches(*scoreboard.at(head), adapter.response(), reason)) {
      throw std::runtime_error(std::string("scoreboard mismatch at ") + phase + ": " + reason);
    }
  };

  adapter.set_out_ready(false);
  adapter.present(a.request);
  if (!adapter.in_ready()) throw std::runtime_error("A was not accepted into an empty response buffer");
  enqueue(a);
  adapter.advance();
  adapter.withdraw_request();
  size_t a_wait_cycles = 0;
  while (!adapter.out_valid()) {
    if (++a_wait_cycles > 32) throw std::runtime_error("A did not reach the response buffer");
    adapter.advance();
  }
  expect_front("A response creation");
  const PvuResponse held_a = adapter.response();

  // Present B with a distinct tag while A is stalled.  The execution pipeline
  // still has capacity, so B may be accepted before A leaves the output slot.
  // Once accepted, withdraw it exactly as a real ready/valid producer would.
  adapter.present(b.request);
  if (!adapter.in_ready()) {
    throw std::runtime_error("B was not accepted into an empty execution pipeline");
  }
  enqueue(b);
  adapter.advance();
  adapter.withdraw_request();

  // Let B fill the fixed-latency lane while A remains backpressured.  The
  // visible response must stay bit-for-bit stable throughout.
  for (size_t cycle = 0; cycle < 6; ++cycle) {
    if (!adapter.out_valid()) throw std::runtime_error("out_valid was not continuous during backpressure");
    if (!ProtocolDriver::same_response(held_a, adapter.response())) {
      throw std::runtime_error("A payload/tag/op changed while out_valid && !out_ready");
    }
    adapter.advance();
  }

  adapter.set_out_ready(true);
  if (!adapter.out_valid()) throw std::runtime_error("A disappeared before its response handshake");
  expect_front("A pop edge");
  ++head;
  adapter.advance();
  adapter.set_out_ready(false);
  size_t b_wait_cycles = 0;
  while (!adapter.out_valid()) {
    if (++b_wait_cycles > 32) throw std::runtime_error("B did not reach the response buffer");
    adapter.advance();
  }
  expect_front("B response creation");

  adapter.set_out_ready(true);
  expect_front("B completion");
  ++head;
  adapter.advance();
  adapter.set_out_ready(false);
  if (head != tail || adapter.out_valid()) {
    throw std::runtime_error("scoreboard did not drain in A-to-B order");
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  VPvuTop dut;
  ProtocolDriver adapter(&dut);
  adapter.reset();
  const std::vector<TestCase> tests = build_tests();
  std::map<std::string, std::pair<size_t, size_t>> summary;
  size_t mismatches = 0;
  run_source_structural_guards();
  std::cout << "raw P32 staged arithmetic structural guard: PASS\n";
  run_registered_boundary_latency(adapter, tests.at(0));
  std::cout << "registered decode/core/reduce/normalize/encode latency: PASS\n";
  run_adjacent_non_division_pipeline(adapter, tests.at(0), tests.at(10));
  std::cout << "adjacent non-division pipeline: PASS\n";
  run_division_busy_backpressure(adapter, tests.at(16), tests.at(17), tests.at(10));
  std::cout << "division busy with non-division concurrency: PASS\n";
  run_non_division_then_division(adapter, tests.at(10), tests.at(16));
  std::cout << "non-division then division concurrency: PASS\n";
  run_stalled_fixed_lane_then_division(adapter, tests.at(0), tests.at(10), tests.at(16));
  std::cout << "stalled fixed-lane then division concurrency: PASS\n";
  adapter.reset();
  run_backpressure_pop_push(adapter, tests.at(0), tests.at(1));

  for (const TestCase& test : tests) {
    if (!adapter.in_ready()) throw std::runtime_error("input valid/ready handshake blocked by pending response");
    adapter.send(test.request);
    const PvuResponse actual = adapter.recv();
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

  std::cout << "Exact SoftPosit PVU ready/valid protocol regression\n";
  for (const auto& [operation, counts] : summary) {
    std::cout << operation << ": samples=" << counts.first << " mismatches=" << counts.second << "\n";
  }
  std::cout << "total samples=" << tests.size() << " total mismatches=" << mismatches << "\n";
  dut.final();
  return mismatches == 0 ? 0 : 1;
}

#endif  // CONFIG_PVU_PROTOCOL_REGRESSION
