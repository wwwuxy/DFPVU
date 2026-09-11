#include "../config.h"

#if defined(CONFIG_LLM_P32_DOT_WORKLOAD) && CONFIG_LLM_P32_DOT_WORKLOAD

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
#include "pvu_llm_dot_workload.h"
#include "pvu_qwen3_metrics.h"
#include "pvu_qwen3_trace.h"

namespace {

constexpr uint8_t kDotOp = 5;

struct DotRequest {
  uint32_t tag = 0;
  std::string module_name;
  pvu::LlmDotVector vector;
  std::array<uint32_t, pvu::kLlmDotLanes> input{};
  std::array<uint32_t, pvu::kLlmDotLanes> weight{};
  uint32_t expected = 0;
  float fp32_reference = 0.0F;
};

struct DotMetrics {
  uint64_t cycles = 0;
  uint64_t requests = 0;
  uint64_t valid_mac_terms = 0;
  uint64_t exact_mismatches = 0;
  std::string first_mismatch;
};

struct DotRun {
  DotMetrics metrics;
  std::vector<uint32_t> results;
};

struct NamedDotVector {
  std::string module_name;
  pvu::LlmDotVector vector;
};

struct TraceDotVectors {
  uint64_t source_vectors = 0;
  std::vector<NamedDotVector> vectors;
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

  void drive(const DotRequest* request) {
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
    dut_.io_op = kDotOp;
    dut_.io_Isposit = 1;
    dut_.io_Outposit = 1;
    dut_.io_float_mode = 3;
    dut_.io_float_posit = 1;
    dut_.io_src_posit_width = 32;
    dut_.io_dst_posit_width = 32;
    dut_.io_vector_size = pvu::kLlmDotLanes;
    dut_.eval();
  }

  bool in_ready() const { return dut_.io_in_ready != 0; }
  bool out_valid() const { return dut_.io_out_valid != 0; }
  uint32_t out_tag() const { return static_cast<uint32_t>(dut_.io_out_tag); }
  uint8_t out_op() const { return static_cast<uint8_t>(dut_.io_out_op); }
  uint32_t out_result() const {
    return static_cast<uint32_t>(dut_.io_posit_dot_o);
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

uint32_t expected_p32_dot(const DotRequest& request) {
  uint32_t accumulator = 0;
  for (size_t lane = 0; lane < pvu::kLlmDotLanes; ++lane) {
    accumulator = pvu::p32_mul_add(request.input[lane], request.weight[lane],
                                   accumulator);
  }
  return accumulator;
}

float expected_fp32_dot(const pvu::LlmDotVector& vector) {
  float accumulator = 0.0F;
  for (size_t lane = 0; lane < pvu::kLlmDotLanes; ++lane) {
    accumulator = std::fma(vector.input[lane], vector.weight[lane], accumulator);
  }
  return accumulator;
}

std::vector<DotRequest> make_dot_requests(
    Driver& driver, const std::vector<NamedDotVector>& vectors) {
  std::vector<DotRequest> requests;
  requests.reserve(vectors.size());
  for (const NamedDotVector& named_vector : vectors) {
    DotRequest request;
    request.tag = driver.next_tag();
    request.module_name = named_vector.module_name;
    request.vector = named_vector.vector;
    for (size_t lane = 0; lane < pvu::kLlmDotLanes; ++lane) {
      request.input[lane] = pvu::float_to_p32(request.vector.input[lane]);
      request.weight[lane] = pvu::float_to_p32(request.vector.weight[lane]);
    }
    request.expected = expected_p32_dot(request);
    request.fp32_reference = expected_fp32_dot(request.vector);
    requests.push_back(std::move(request));
  }
  return requests;
}

DotRun run_dot(Driver& driver, const std::vector<DotRequest>& requests,
               pvu::ComparisonStats& fp32_comparison) {
  if (requests.empty()) {
    throw std::runtime_error("LLM Dot trace has no selected vectors");
  }
  DotRun run;
  run.results.resize(requests.size());
  std::unordered_map<uint32_t, size_t> outstanding;
  size_t next_request = 0;
  size_t completed = 0;
  size_t idle_cycles = 0;
  const uint64_t cycles_before = driver.cycles();
  const DotRequest* held = nullptr;

  while (completed != requests.size()) {
    if (held == nullptr && next_request != requests.size()) {
      held = &requests[next_request];
    }
    driver.drive(held);
    const bool accepted_request = held != nullptr && driver.in_ready();
    const bool accepted_response = driver.out_valid();

    if (accepted_response) {
      if (driver.out_op() != kDotOp) {
        throw std::runtime_error("LLM Dot response operation mismatch");
      }
      const auto response = outstanding.find(driver.out_tag());
      if (response == outstanding.end()) {
        throw std::runtime_error("LLM Dot response tag mismatch");
      }
      const size_t request_index = response->second;
      const DotRequest& request = requests[request_index];
      const uint32_t actual = driver.out_result();
      run.results[request_index] = actual;
      if (actual != request.expected) {
        ++run.metrics.exact_mismatches;
        if (run.metrics.first_mismatch.empty()) {
          run.metrics.first_mismatch =
              "LLM Dot mismatch: module=" + request.module_name +
              " token=" + std::to_string(request.vector.token) +
              " row=" + std::to_string(request.vector.row) +
              " k=" + std::to_string(request.vector.k) +
              " expected=" + std::to_string(request.expected) +
              " actual=" + std::to_string(actual);
        }
      }
      fp32_comparison.add(actual, request.fp32_reference);
      outstanding.erase(response);
      ++completed;
    }

    if (accepted_request) {
      const auto inserted = outstanding.emplace(held->tag, next_request);
      if (!inserted.second) {
        throw std::runtime_error("LLM Dot duplicate request tag");
      }
      ++run.metrics.requests;
      run.metrics.valid_mac_terms += pvu::kLlmDotLanes;
      ++next_request;
      held = nullptr;
    }

    if (accepted_request || accepted_response) {
      idle_cycles = 0;
    } else if (++idle_cycles > 32) {
      throw std::runtime_error("LLM Dot made no handshake progress");
    }
    driver.tick();
  }
  run.metrics.cycles = driver.cycles() - cycles_before;
  return run;
}

TraceDotVectors collect_trace_vectors(const pvu::LlmTrace& trace,
                                      size_t requested_samples) {
  TraceDotVectors collected;
  for (const auto& named_module : trace.modules) {
    const pvu::LlmDotSamples samples =
        pvu::sample_llm_dot_vectors(named_module.second, requested_samples);
    if (samples.source_vectors >
        std::numeric_limits<uint64_t>::max() - collected.source_vectors) {
      throw std::runtime_error("LLM Dot source vector count overflow");
    }
    collected.source_vectors += samples.source_vectors;
    for (const pvu::LlmDotVector& vector : samples.vectors) {
      collected.vectors.push_back({named_module.first, vector});
    }
  }
  if (collected.vectors.empty()) {
    throw std::runtime_error("LLM Dot trace has no selected vectors");
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
    const pvu::LlmDotSelection selection =
        pvu::parse_llm_dot_selection(argc, argv);
    const pvu::LlmTrace trace = pvu::load_llm_trace(selection.trace_root);
    const TraceDotVectors vectors =
        collect_trace_vectors(trace, selection.sample_count);

    VPvuTop dut;
    Driver driver(dut);
    driver.reset();
    pvu::ComparisonStats fp32_comparison;
    const DotRun run = run_dot(
        driver, make_dot_requests(driver, vectors.vectors), fp32_comparison);

    const bool passed = run.metrics.exact_mismatches == 0;
    std::cout << "LLM P32 Dot workload\n";
    std::cout << "  trace_model: " << trace.model << '\n';
    std::cout << "  trace_profile: " << trace.profile << '\n';
    std::cout << "  layer_index: " << trace.layer_index << '\n';
    std::cout << "  module_count: " << trace.modules.size() << '\n';
    std::cout << "  requested_samples_per_module: "
              << selection.sample_count << '\n';
    std::cout << "  source_dot_vectors: " << vectors.source_vectors << '\n';
    std::cout << "  sampled_dot_vectors: " << vectors.vectors.size() << '\n';
    std::cout << "  requests: " << run.metrics.requests << '\n';
    std::cout << "  valid_mac_terms: " << run.metrics.valid_mac_terms << '\n';
    std::cout << "  cycles: " << run.metrics.cycles << '\n';
    std::cout << std::fixed << std::setprecision(6)
              << "  request_per_cycle: "
              << per_cycle(run.metrics.requests, run.metrics.cycles) << '\n'
              << "  dot_per_cycle: "
              << per_cycle(run.metrics.requests, run.metrics.cycles) << '\n'
              << "  mac_terms_per_cycle: "
              << per_cycle(run.metrics.valid_mac_terms, run.metrics.cycles)
              << '\n'
              << "  lane_utilization: "
              << (run.metrics.valid_mac_terms == 0 ? 0.0 : 1.0) << '\n';
    std::cout << "  exact_mismatches: " << run.metrics.exact_mismatches << '\n';
    print_comparison(fp32_comparison);
    std::cout << "  conformance: " << (passed ? "PASS" : "FAIL") << '\n';
    if (!passed) throw std::runtime_error(run.metrics.first_mismatch);
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "LLM Dot workload error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}

#endif  // CONFIG_LLM_P32_DOT_WORKLOAD
