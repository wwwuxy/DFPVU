#include "../config.h"

#if defined(CONFIG_LLM_P32_MUL_WORKLOAD) && CONFIG_LLM_P32_MUL_WORKLOAD

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <verilated.h>

#include "VPvuTop.h"
#include "pvu_llm_mul_workload.h"
#include "pvu_qwen3_metrics.h"
#include "pvu_qwen3_trace.h"
#include "softposit.h"

namespace {

constexpr uint8_t kMultiplyOp = 3;

struct MulRequest {
  uint32_t tag = 0;
  std::string module_name;
  pvu::LlmMulVector vector;
  std::array<uint32_t, pvu::kLlmMulLanes> input{};
  std::array<uint32_t, pvu::kLlmMulLanes> weight{};
  std::array<uint32_t, pvu::kLlmMulLanes> expected{};
  std::array<float, pvu::kLlmMulLanes> fp32_reference{};
};

struct MulMetrics {
  uint64_t cycles = 0;
  uint64_t requests = 0;
  uint64_t valid_multiplications = 0;
  uint64_t exact_mismatches = 0;
  std::string first_mismatch;
};

struct MulRun {
  MulMetrics metrics;
};

struct NamedMulVector {
  std::string module_name;
  pvu::LlmMulVector vector;
};

struct TraceMulVectors {
  uint64_t source_vectors = 0;
  std::vector<NamedMulVector> vectors;
};

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
    cycles_after_reset_ = 0;
  }

  uint32_t next_tag() { return next_tag_++; }

  void drive(const MulRequest* request) {
    dut_.io_out_ready = 1;
    dut_.io_in_valid = request != nullptr;
    if (request == nullptr) {
      dut_.eval();
      return;
    }
    dut_.io_posit_i1_0 = request->input[0];
    dut_.io_posit_i1_1 = request->input[1];
    dut_.io_posit_i1_2 = request->input[2];
    dut_.io_posit_i1_3 = request->input[3];
    dut_.io_posit_i2_0 = request->weight[0];
    dut_.io_posit_i2_1 = request->weight[1];
    dut_.io_posit_i2_2 = request->weight[2];
    dut_.io_posit_i2_3 = request->weight[3];
    dut_.io_posit_i3 = 0;
    dut_.io_float_i_0 = 0;
    dut_.io_float_i_1 = 0;
    dut_.io_float_i_2 = 0;
    dut_.io_float_i_3 = 0;
    dut_.io_float_i2_0 = 0;
    dut_.io_float_i2_1 = 0;
    dut_.io_float_i2_2 = 0;
    dut_.io_float_i2_3 = 0;
    dut_.io_in_tag = request->tag;
    dut_.io_op = kMultiplyOp;
    dut_.io_Isposit = 1;
    dut_.io_Outposit = 1;
    dut_.io_float_mode = 3;
    dut_.io_float_posit = 1;
    dut_.io_src_posit_width = 32;
    dut_.io_dst_posit_width = 32;
    dut_.io_vector_size = pvu::kLlmMulLanes;
    dut_.eval();
  }

  bool in_ready() const { return dut_.io_in_ready != 0; }
  bool out_valid() const { return dut_.io_out_valid != 0; }
  uint32_t out_tag() const { return static_cast<uint32_t>(dut_.io_out_tag); }
  uint8_t out_op() const { return static_cast<uint8_t>(dut_.io_out_op); }
  std::array<uint32_t, pvu::kLlmMulLanes> output() const {
    return {static_cast<uint32_t>(dut_.io_posit_o_0),
            static_cast<uint32_t>(dut_.io_posit_o_1),
            static_cast<uint32_t>(dut_.io_posit_o_2),
            static_cast<uint32_t>(dut_.io_posit_o_3)};
  }

  void tick() {
    dut_.clock = 0;
    dut_.eval();
    dut_.clock = 1;
    dut_.eval();
    dut_.clock = 0;
    dut_.eval();
    if (dut_.reset == 0) ++cycles_after_reset_;
  }

  uint64_t cycles() const { return cycles_after_reset_; }

 private:
  VPvuTop& dut_;
  uint32_t next_tag_ = 1;
  uint64_t cycles_after_reset_ = 0;
};

uint32_t expected_p32_mul(uint32_t lhs_raw, uint32_t rhs_raw) {
  posit32_t lhs{};
  posit32_t rhs{};
  lhs.v = lhs_raw;
  rhs.v = rhs_raw;
  return p32_mul(lhs, rhs).v;
}

std::vector<MulRequest> make_mul_requests(
    Driver& driver, const std::vector<NamedMulVector>& vectors) {
  std::vector<MulRequest> requests;
  requests.reserve(vectors.size());
  for (const NamedMulVector& named_vector : vectors) {
    MulRequest request;
    request.tag = driver.next_tag();
    request.module_name = named_vector.module_name;
    request.vector = named_vector.vector;
    for (size_t lane = 0; lane < pvu::kLlmMulLanes; ++lane) {
      request.input[lane] = pvu::float_to_p32(request.vector.input[lane]);
      request.weight[lane] = pvu::float_to_p32(request.vector.weight[lane]);
      request.expected[lane] =
          expected_p32_mul(request.input[lane], request.weight[lane]);
      request.fp32_reference[lane] =
          request.vector.input[lane] * request.vector.weight[lane];
    }
    requests.push_back(std::move(request));
  }
  return requests;
}

MulRun run_mul(Driver& driver, const std::vector<MulRequest>& requests,
               pvu::ComparisonStats& fp32_comparison) {
  if (requests.empty()) {
    throw std::runtime_error("LLM Multiply trace has no selected vectors");
  }
  MulRun run;
  std::unordered_map<uint32_t, size_t> outstanding;
  size_t next_request = 0;
  size_t completed = 0;
  size_t idle_cycles = 0;
  const uint64_t cycles_before = driver.cycles();
  const MulRequest* held = nullptr;

  while (completed != requests.size()) {
    if (held == nullptr && next_request != requests.size()) {
      held = &requests[next_request];
    }
    driver.drive(held);
    const bool accepted_request = held != nullptr && driver.in_ready();
    const bool accepted_response = driver.out_valid();

    if (accepted_response) {
      if (driver.out_op() != kMultiplyOp) {
        throw std::runtime_error("LLM Multiply response operation mismatch");
      }
      const auto response = outstanding.find(driver.out_tag());
      if (response == outstanding.end()) {
        throw std::runtime_error("LLM Multiply response tag mismatch");
      }
      const size_t request_index = response->second;
      const MulRequest& request = requests[request_index];
      const std::array<uint32_t, pvu::kLlmMulLanes> actual = driver.output();
      for (size_t lane = 0; lane < pvu::kLlmMulLanes; ++lane) {
        if (actual[lane] != request.expected[lane]) {
          ++run.metrics.exact_mismatches;
          if (run.metrics.first_mismatch.empty()) {
            run.metrics.first_mismatch =
                "LLM Multiply mismatch: module=" + request.module_name +
                " token=" + std::to_string(request.vector.token) +
                " row=" + std::to_string(request.vector.row) +
                " k=" + std::to_string(request.vector.k) +
                " lane=" + std::to_string(lane) +
                " expected=" + std::to_string(request.expected[lane]) +
                " actual=" + std::to_string(actual[lane]);
          }
        }
        fp32_comparison.add(actual[lane], request.fp32_reference[lane]);
      }
      outstanding.erase(response);
      ++completed;
    }

    if (accepted_request) {
      const auto inserted = outstanding.emplace(held->tag, next_request);
      if (!inserted.second) {
        throw std::runtime_error("LLM Multiply duplicate request tag");
      }
      ++run.metrics.requests;
      run.metrics.valid_multiplications += pvu::kLlmMulLanes;
      ++next_request;
      held = nullptr;
    }

    if (accepted_request || accepted_response) {
      idle_cycles = 0;
    } else if (++idle_cycles > 32) {
      throw std::runtime_error("LLM Multiply made no handshake progress");
    }
    driver.tick();
  }
  run.metrics.cycles = driver.cycles() - cycles_before;
  return run;
}

TraceMulVectors collect_trace_vectors(const pvu::LlmTrace& trace,
                                      size_t requested_samples) {
  TraceMulVectors collected;
  for (const auto& named_module : trace.modules) {
    const pvu::LlmMulSamples samples =
        pvu::sample_llm_mul_vectors(named_module.second, requested_samples);
    if (samples.source_vectors >
        std::numeric_limits<uint64_t>::max() - collected.source_vectors) {
      throw std::runtime_error("LLM Multiply source vector count overflow");
    }
    collected.source_vectors += samples.source_vectors;
    for (const pvu::LlmMulVector& vector : samples.vectors) {
      collected.vectors.push_back({named_module.first, vector});
    }
  }
  if (collected.vectors.empty()) {
    throw std::runtime_error("LLM Multiply trace has no selected vectors");
  }
  return collected;
}

double per_cycle(uint64_t count, uint64_t cycles) {
  return cycles == 0 ? 0.0
                     : static_cast<double>(count) / static_cast<double>(cycles);
}

void print_comparison(const pvu::ComparisonStats& stats) {
  std::cout << "  fp32_reference_samples: " << stats.samples << '\n';
  std::cout << "  p32_vs_fp32_finite_samples: " << stats.finite_samples << '\n';
  std::cout << "  p32_vs_fp32_zero_references: " << stats.zero_references << '\n';
  std::cout << "  p32_vs_fp32_special_samples: " << stats.special_samples << '\n';
  std::cout << "  p32_vs_fp32_ulp_zero: " << stats.ulp.zero << '\n';
  std::cout << "  p32_vs_fp32_ulp_one: " << stats.ulp.one << '\n';
  std::cout << "  p32_vs_fp32_ulp_two_to_four: " << stats.ulp.two_to_four << '\n';
  std::cout << "  p32_vs_fp32_ulp_five_or_more: " << stats.ulp.five_or_more << '\n';
  std::cout << std::scientific << std::setprecision(9)
            << "  p32_vs_fp32_max_relative_error: "
            << stats.max_relative_error << '\n'
            << "  p32_vs_fp32_mean_relative_error: "
            << stats.mean_relative_error() << '\n'
            << "  p32_vs_fp32_max_absolute_error: "
            << stats.max_absolute_error << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const pvu::LlmMulSelection selection =
        pvu::parse_llm_mul_selection(argc, argv);
    const pvu::LlmTrace trace = pvu::load_llm_trace(selection.trace_root);
    const TraceMulVectors vectors =
        collect_trace_vectors(trace, selection.sample_count);

    VPvuTop dut;
    Driver driver(dut);
    driver.reset();
    pvu::ComparisonStats fp32_comparison;
    const MulRun run = run_mul(
        driver, make_mul_requests(driver, vectors.vectors), fp32_comparison);

    const bool passed = run.metrics.exact_mismatches == 0;
    std::cout << "LLM P32 Multiply workload\n";
    std::cout << "  trace_model: " << trace.model << '\n';
    std::cout << "  trace_profile: " << trace.profile << '\n';
    std::cout << "  layer_index: " << trace.layer_index << '\n';
    std::cout << "  module_count: " << trace.modules.size() << '\n';
    std::cout << "  requested_samples_per_module: "
              << selection.sample_count << '\n';
    std::cout << "  source_mul_vectors: " << vectors.source_vectors << '\n';
    std::cout << "  sampled_mul_vectors: " << vectors.vectors.size() << '\n';
    std::cout << "  requests: " << run.metrics.requests << '\n';
    std::cout << "  valid_multiplications: "
              << run.metrics.valid_multiplications << '\n';
    std::cout << "  cycles: " << run.metrics.cycles << '\n';
    std::cout << std::fixed << std::setprecision(6)
              << "  request_per_cycle: "
              << per_cycle(run.metrics.requests, run.metrics.cycles) << '\n'
              << "  vector_per_cycle: "
              << per_cycle(run.metrics.requests, run.metrics.cycles) << '\n'
              << "  element_per_cycle: "
              << per_cycle(run.metrics.valid_multiplications, run.metrics.cycles)
              << '\n'
              << "  lane_utilization: "
              << (run.metrics.valid_multiplications == 0 ? 0.0 : 1.0) << '\n';
    std::cout << "  exact_mismatches: " << run.metrics.exact_mismatches << '\n';
    print_comparison(fp32_comparison);
    std::cout << "  conformance: " << (passed ? "PASS" : "FAIL") << '\n';
    if (!passed) throw std::runtime_error(run.metrics.first_mismatch);
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "LLM Multiply workload error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}

#endif  // CONFIG_LLM_P32_MUL_WORKLOAD
