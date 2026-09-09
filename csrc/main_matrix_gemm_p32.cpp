#include "../config.h"

#ifdef CONFIG_MATRIX_GEMM_P32_BENCHMARK

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <verilated.h>

#include "VPvuTop.h"
#include "softposit.h"

namespace {

constexpr size_t kLanes = 4;
constexpr uint8_t kP32MacOp = 11;
constexpr uint64_t kNoProgressLimit = 64;

struct BenchmarkOptions {
  size_t m = 4;
  size_t n = 4;
  size_t k = 5;
  uint64_t seed = 1;
};

struct Matrix {
  size_t rows;
  size_t columns;
  std::vector<uint32_t> values;

  uint32_t at(size_t row, size_t column) const {
    return values.at(row * columns + column);
  }
};

struct WorkloadMetrics {
  uint64_t cycles = 0;
  uint64_t requests = 0;
  uint64_t valid_mac_terms = 0;
  uint64_t max_chain_latency = 0;
};

struct Request {
  std::array<uint32_t, kLanes> lhs{};
  std::array<uint32_t, kLanes> rhs{};
  uint32_t accumulator = 0;
  uint32_t tag = 0;
  uint8_t valid_lanes = 0;
};

std::string hex32(uint32_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
  return output.str();
}

size_t checked_product(size_t lhs, size_t rhs, const std::string& label) {
  if (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs) {
    throw std::runtime_error(label + " overflows host size_t");
  }
  return lhs * rhs;
}

bool has_decimal_digits(const std::string& value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return character >= '0' && character <= '9';
         });
}

size_t parse_positive_size(const char* text, const std::string& option) {
  const std::string value(text);
  if (!has_decimal_digits(value)) {
    throw std::runtime_error(option + " must be a positive integer");
  }
  size_t parsed_characters = 0;
  uint64_t parsed = 0;
  try {
    parsed = std::stoull(value, &parsed_characters, 10);
  } catch (const std::exception&) {
    throw std::runtime_error(option + " must be a positive integer");
  }
  if (parsed_characters != value.size() || parsed == 0 ||
      parsed > std::numeric_limits<size_t>::max()) {
    throw std::runtime_error(option + " must be a positive integer");
  }
  return static_cast<size_t>(parsed);
}

uint64_t parse_positive_seed(const char* text) {
  const std::string value(text);
  if (!has_decimal_digits(value)) {
    throw std::runtime_error("--seed must be a positive integer");
  }
  size_t parsed_characters = 0;
  uint64_t parsed = 0;
  try {
    parsed = std::stoull(value, &parsed_characters, 10);
  } catch (const std::exception&) {
    throw std::runtime_error("--seed must be a positive integer");
  }
  if (parsed_characters != value.size() || parsed == 0) {
    throw std::runtime_error("--seed must be a positive integer");
  }
  return parsed;
}

BenchmarkOptions parse_options(int argc, char** argv) {
  BenchmarkOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string option(argv[index]);
    if (option == "--help") {
      throw std::runtime_error(
          "usage: VPvuTop [--m M] [--n N] [--k K] [--seed SEED]");
    }
    if (option != "--m" && option != "--n" && option != "--k" &&
        option != "--seed") {
      throw std::runtime_error("unknown option: " + option);
    }
    if (++index == argc) {
      throw std::runtime_error("missing value for " + option);
    }
    if (option == "--m") {
      options.m = parse_positive_size(argv[index], option);
    } else if (option == "--n") {
      options.n = parse_positive_size(argv[index], option);
    } else if (option == "--k") {
      options.k = parse_positive_size(argv[index], option);
    } else {
      options.seed = parse_positive_seed(argv[index]);
    }
  }
  checked_product(options.m, options.k, "A matrix size");
  checked_product(options.k, options.n, "B matrix size");
  checked_product(options.m, options.n, "C matrix size");
  return options;
}

uint64_t next_random(uint64_t& state) {
  state ^= state >> 12;
  state ^= state << 25;
  state ^= state >> 27;
  return state * UINT64_C(2685821657736338717);
}

Matrix make_matrix(size_t rows, size_t columns, uint64_t seed) {
  static constexpr std::array<uint32_t, 16> kRawP32Corpus{{
      0x40000000u, 0x38000000u, 0x48000000u, 0x44000000u,
      0x3c000000u, 0x34000000u, 0x30000000u, 0x50000000u,
      0xc0000000u, 0xc8000000u, 0xc4000000u, 0xbc000000u,
      0xb8000000u, 0xb4000000u, 0xd0000000u, 0xe0000000u,
  }};
  Matrix matrix{rows, columns, {}};
  matrix.values.reserve(checked_product(rows, columns, "matrix size"));
  uint64_t state = seed;
  for (size_t index = 0; index < rows * columns; ++index) {
    const uint64_t random = next_random(state);
    matrix.values.push_back(kRawP32Corpus.at((random >> 60) & 0xfu));
  }
  return matrix;
}

uint32_t p32_mul_add(uint32_t lhs, uint32_t rhs, uint32_t accumulator) {
  posit32_t lhs_posit{};
  posit32_t rhs_posit{};
  posit32_t accumulator_posit{};
  lhs_posit.v = lhs;
  rhs_posit.v = rhs;
  accumulator_posit.v = accumulator;
  return p32_mulAdd(lhs_posit, rhs_posit, accumulator_posit).v;
}

class Driver {
 public:
  explicit Driver(VPvuTop& dut) : dut_(dut) {}

  void reset() {
    dut_.io_in_valid = 0;
    dut_.io_out_ready = 0;
    dut_.reset = 1;
    tick();
    tick();
    dut_.reset = 0;
    dut_.eval();
    rising_edges_after_reset_ = 0;
  }

  uint32_t next_tag() { return next_tag_++; }

  void drive(const Request* request) {
    dut_.io_out_ready = 1;
    dut_.io_in_valid = request != nullptr;
    if (request == nullptr) {
      dut_.eval();
      return;
    }

    dut_.io_posit_i1_0 = request->lhs[0];
    dut_.io_posit_i1_1 = request->lhs[1];
    dut_.io_posit_i1_2 = request->lhs[2];
    dut_.io_posit_i1_3 = request->lhs[3];
    dut_.io_posit_i2_0 = request->rhs[0];
    dut_.io_posit_i2_1 = request->rhs[1];
    dut_.io_posit_i2_2 = request->rhs[2];
    dut_.io_posit_i2_3 = request->rhs[3];
    dut_.io_posit_i3 = request->accumulator;
    dut_.io_in_tag = request->tag;
    dut_.io_op = kP32MacOp;
    dut_.io_Isposit = 1;
    dut_.io_Outposit = 1;
    dut_.io_float_i_0 = 0;
    dut_.io_float_i_1 = 0;
    dut_.io_float_i_2 = 0;
    dut_.io_float_i_3 = 0;
    dut_.io_float_i2_0 = 0;
    dut_.io_float_i2_1 = 0;
    dut_.io_float_i2_2 = 0;
    dut_.io_float_i2_3 = 0;
    dut_.io_float_mode = 3;
    dut_.io_float_posit = 1;
    dut_.io_src_posit_width = 32;
    dut_.io_dst_posit_width = 32;
    dut_.io_vector_size = request->valid_lanes;
    dut_.eval();
  }

  bool in_ready() const { return dut_.io_in_ready != 0; }
  bool out_valid() const { return dut_.io_out_valid != 0; }
  uint32_t out_tag() const { return static_cast<uint32_t>(dut_.io_out_tag); }
  uint8_t out_op() const { return static_cast<uint8_t>(dut_.io_out_op); }
  uint32_t out_result() const {
    return static_cast<uint32_t>(dut_.io_posit_dot_o);
  }

  uint64_t cycles() const { return rising_edges_after_reset_; }

  void tick() {
    dut_.clock = 0;
    dut_.eval();
    dut_.clock = 1;
    dut_.eval();
    dut_.clock = 0;
    dut_.eval();
    if (dut_.reset == 0) {
      ++rising_edges_after_reset_;
    }
  }

 private:
  VPvuTop& dut_;
  uint32_t next_tag_ = 1;
  uint64_t rising_edges_after_reset_ = 0;
};

struct ElementState {
  size_t row = 0;
  size_t column = 0;
  size_t next_k = 0;
  uint32_t accumulator = 0;
  bool waiting_for_response = false;
  bool complete = false;
};

struct HeldRequest {
  size_t element_index = 0;
  Request request;
};

struct OutstandingRequest {
  size_t element_index = 0;
  uint8_t valid_lanes = 0;
  uint64_t accepted_cycle = 0;
};

Request make_request(Driver& driver, const Matrix& a, const Matrix& b,
                     const ElementState& element) {
  if (element.next_k >= a.columns) {
    throw std::runtime_error("attempted to schedule a completed GEMM element");
  }

  Request request{};
  request.accumulator = element.accumulator;
  request.tag = driver.next_tag();
  request.valid_lanes =
      static_cast<uint8_t>(std::min(kLanes, a.columns - element.next_k));
  for (size_t lane = 0; lane < request.valid_lanes; ++lane) {
    request.lhs[lane] = a.at(element.row, element.next_k + lane);
    request.rhs[lane] = b.at(element.next_k + lane, element.column);
  }
  return request;
}

std::vector<uint32_t> run_gemm(Driver& driver, const Matrix& a,
                               const Matrix& b, WorkloadMetrics& metrics) {
  if (a.columns != b.rows) {
    throw std::runtime_error("GEMM K dimensions do not match");
  }

  const size_t result_count = checked_product(a.rows, b.columns, "C matrix size");
  std::vector<ElementState> elements(result_count);
  for (size_t row = 0; row < a.rows; ++row) {
    for (size_t column = 0; column < b.columns; ++column) {
      elements[row * b.columns + column] = {row, column};
    }
  }

  std::vector<uint32_t> results(result_count);
  std::unordered_map<uint32_t, OutstandingRequest> outstanding_by_tag;
  HeldRequest held{};
  bool has_held_request = false;
  size_t next_candidate = 0;
  size_t completed = 0;
  uint64_t no_progress_cycles = 0;
  const uint64_t cycles_before = driver.cycles();

  while (completed != elements.size()) {
    if (!has_held_request) {
      for (size_t attempts = 0; attempts < elements.size(); ++attempts) {
        const size_t element_index = next_candidate++ % elements.size();
        ElementState& element = elements[element_index];
        if (!element.complete && !element.waiting_for_response) {
          held = {element_index, make_request(driver, a, b, element)};
          has_held_request = true;
          break;
        }
      }
    }

    driver.drive(has_held_request ? &held.request : nullptr);
    const bool accepted_request = has_held_request && driver.in_ready();
    const bool accepted_response = driver.out_valid();

    if (accepted_response) {
      if (driver.out_op() != kP32MacOp) {
        throw std::runtime_error("GEMM response op mismatch");
      }
      const auto response = outstanding_by_tag.find(driver.out_tag());
      if (response == outstanding_by_tag.end()) {
        throw std::runtime_error("GEMM response tag mismatch");
      }

      const OutstandingRequest outstanding = response->second;
      ElementState& element = elements.at(outstanding.element_index);
      if (!element.waiting_for_response) {
        throw std::runtime_error("GEMM response has no dependent element");
      }
      element.accumulator = driver.out_result();
      element.next_k += outstanding.valid_lanes;
      element.waiting_for_response = false;
      outstanding_by_tag.erase(response);

      const uint64_t latency = driver.cycles() - outstanding.accepted_cycle;
      metrics.max_chain_latency =
          std::max(metrics.max_chain_latency, latency);
      if (element.next_k == a.columns) {
        element.complete = true;
        results.at(element.row * b.columns + element.column) =
            element.accumulator;
        ++completed;
      }
    }

    if (accepted_request) {
      ElementState& element = elements.at(held.element_index);
      if (element.waiting_for_response || element.complete) {
        throw std::runtime_error("GEMM scheduler dependency violation");
      }
      element.waiting_for_response = true;
      const auto inserted = outstanding_by_tag.emplace(
          held.request.tag,
          OutstandingRequest{held.element_index, held.request.valid_lanes,
                             driver.cycles()});
      if (!inserted.second) {
        throw std::runtime_error("GEMM scheduler duplicate tag");
      }
      ++metrics.requests;
      metrics.valid_mac_terms += held.request.valid_lanes;
      has_held_request = false;
    }

    if (accepted_request || accepted_response) {
      no_progress_cycles = 0;
    } else if (++no_progress_cycles > kNoProgressLimit) {
      throw std::runtime_error("GEMM scheduler made no handshake progress");
    }
    driver.tick();
  }

  metrics.cycles += driver.cycles() - cycles_before;
  return results;
}

std::vector<uint32_t> reference_gemm(const Matrix& a, const Matrix& b) {
  if (a.columns != b.rows) {
    throw std::runtime_error("GEMM K dimensions do not match");
  }
  std::vector<uint32_t> results(
      checked_product(a.rows, b.columns, "reference C matrix size"));
  for (size_t row = 0; row < a.rows; ++row) {
    for (size_t column = 0; column < b.columns; ++column) {
      uint32_t accumulator = 0;
      for (size_t k = 0; k < a.columns; ++k) {
        accumulator = p32_mul_add(a.at(row, k), b.at(k, column), accumulator);
      }
      results.at(row * b.columns + column) = accumulator;
    }
  }
  return results;
}

void print_report(const BenchmarkOptions& options,
                  const WorkloadMetrics& metrics, uint64_t exact_mismatches) {
  const double cycles = static_cast<double>(metrics.cycles);
  const double request_per_cycle =
      metrics.cycles == 0 ? 0.0 : static_cast<double>(metrics.requests) / cycles;
  const double mac_per_cycle = metrics.cycles == 0
                                   ? 0.0
                                   : static_cast<double>(metrics.valid_mac_terms) /
                                         cycles;
  const double lane_utilization =
      metrics.requests == 0
          ? 0.0
          : static_cast<double>(metrics.valid_mac_terms) /
                static_cast<double>(metrics.requests * kLanes);

  std::cout << "Matrix GEMM P32 benchmark: M=" << options.m
            << " N=" << options.n << " K=" << options.k
            << " seed=" << options.seed << std::endl;
  std::cout << std::fixed << std::setprecision(6)
            << "  requests=" << metrics.requests
            << " valid_mac_terms=" << metrics.valid_mac_terms
            << " cycles=" << metrics.cycles
            << " request_per_cycle=" << request_per_cycle
            << " mac_per_cycle=" << mac_per_cycle
            << " lane_utilization=" << lane_utilization
            << " max_chain_latency=" << metrics.max_chain_latency
            << " exact_mismatches=" << exact_mismatches << std::endl;
  std::cout
      << "M,N,K,seed,requests,valid_mac_terms,cycles,request_per_cycle,"
         "mac_per_cycle,lane_utilization,max_chain_latency,exact_mismatches"
      << std::endl;
  std::cout << options.m << ',' << options.n << ',' << options.k << ','
            << options.seed << ',' << metrics.requests << ','
            << metrics.valid_mac_terms << ',' << metrics.cycles << ','
            << request_per_cycle << ',' << mac_per_cycle << ','
            << lane_utilization << ',' << metrics.max_chain_latency << ','
            << exact_mismatches << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const BenchmarkOptions options = parse_options(argc, argv);
    const Matrix a = make_matrix(options.m, options.k, options.seed);
    const Matrix b = make_matrix(options.k, options.n,
                                 options.seed ^ UINT64_C(0x9e3779b97f4a7c15));

    VPvuTop dut;
    Driver driver(dut);
    driver.reset();

    WorkloadMetrics metrics;
    const std::vector<uint32_t> actual = run_gemm(driver, a, b, metrics);
    const std::vector<uint32_t> expected = reference_gemm(a, b);

    const uint64_t expected_requests = checked_product(
        checked_product(options.m, options.n, "GEMM output count"),
        (options.k + kLanes - 1) / kLanes, "GEMM request count");
    const uint64_t expected_mac_terms = checked_product(
        checked_product(options.m, options.n, "GEMM output count"), options.k,
        "GEMM MAC term count");
    if (metrics.requests != expected_requests ||
        metrics.valid_mac_terms != expected_mac_terms) {
      throw std::runtime_error("GEMM scheduler accounting mismatch");
    }

    uint64_t exact_mismatches = 0;
    std::string first_mismatch;
    for (size_t index = 0; index < actual.size(); ++index) {
      if (actual[index] != expected[index]) {
        ++exact_mismatches;
        if (first_mismatch.empty()) {
          const size_t row = index / options.n;
          const size_t column = index % options.n;
          first_mismatch = "GEMM mismatch at C[" + std::to_string(row) + ',' +
                           std::to_string(column) + "]: expected=" +
                           hex32(expected[index]) + " actual=" +
                           hex32(actual[index]);
        }
      }
    }

    print_report(options, metrics, exact_mismatches);
    if (exact_mismatches != 0) {
      throw std::runtime_error(first_mismatch);
    }
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "Matrix GEMM P32 benchmark error: " << error.what()
              << std::endl;
    return EXIT_FAILURE;
  }
}

#endif  // CONFIG_MATRIX_GEMM_P32_BENCHMARK
