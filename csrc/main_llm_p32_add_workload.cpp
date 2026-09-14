#include "../config.h"

#if defined(CONFIG_LLM_P32_ADD_WORKLOAD) && CONFIG_LLM_P32_ADD_WORKLOAD

#include <algorithm>
#include <array>
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
#include "pvu_llm_add_workload.h"
#include "pvu_qwen3_metrics.h"
#include "pvu_qwen3_trace.h"
#include "softposit.h"

namespace {

constexpr size_t kLanes = 4;
constexpr uint8_t kAddOp = 1;

struct NamedAddElement {
  std::string operation_name;
  size_t source_index = 0;
  pvu::LlmAddElement element;
};

struct AddRequest {
  uint32_t tag = 0;
  uint8_t valid_lanes = 0;
  std::array<NamedAddElement, kLanes> elements{};
  std::array<uint32_t, kLanes> lhs{};
  std::array<uint32_t, kLanes> rhs{};
  std::array<uint32_t, kLanes> expected{};
};

struct AddMetrics {
  uint64_t cycles = 0;
  uint64_t requests = 0;
  uint64_t elements = 0;
  uint64_t exact_mismatches = 0;
  std::string first_mismatch;
};

struct TraceAddElements {
  uint64_t source_elements = 0;
  std::vector<NamedAddElement> elements;
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

  void drive(const AddRequest* request) {
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
    dut_.io_op = kAddOp;
    dut_.io_Isposit = 1;
    dut_.io_Outposit = 1;
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
  std::array<uint32_t, kLanes> output() const {
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

uint32_t expected_p32_add(uint32_t lhs_raw, uint32_t rhs_raw) {
  posit32_t lhs{};
  posit32_t rhs{};
  lhs.v = lhs_raw;
  rhs.v = rhs_raw;
  return p32_add(lhs, rhs).v;
}

TraceAddElements collect_trace_elements(const pvu::LlmTrace& trace,
                                        size_t requested_samples) {
  if (trace.add_operations.empty()) {
    throw std::runtime_error("LLM Add trace has no add_operations");
  }

  TraceAddElements collected;
  for (const auto& named_operation : trace.add_operations) {
    const pvu::LlmAddSamples samples =
        pvu::sample_llm_add_elements(named_operation.second, requested_samples);
    if (samples.source_elements >
        std::numeric_limits<uint64_t>::max() - collected.source_elements) {
      throw std::runtime_error("LLM Add source element count overflow");
    }
    collected.source_elements += samples.source_elements;
    for (size_t index = 0; index < samples.elements.size(); ++index) {
      collected.elements.push_back(
          {named_operation.first, samples.source_indices[index],
           samples.elements[index]});
    }
  }
  if (collected.elements.empty()) {
    throw std::runtime_error("LLM Add trace has no selected elements");
  }
  return collected;
}

std::vector<AddRequest> make_add_requests(
    Driver& driver, const std::vector<NamedAddElement>& elements) {
  std::vector<AddRequest> requests;
  requests.reserve((elements.size() + kLanes - 1) / kLanes);
  for (size_t offset = 0; offset < elements.size(); offset += kLanes) {
    AddRequest request;
    request.tag = driver.next_tag();
    request.valid_lanes =
        static_cast<uint8_t>(std::min(kLanes, elements.size() - offset));
    for (size_t lane = 0; lane < request.valid_lanes; ++lane) {
      request.elements[lane] = elements[offset + lane];
      request.lhs[lane] =
          pvu::float_to_p32(request.elements[lane].element.lhs);
      request.rhs[lane] =
          pvu::float_to_p32(request.elements[lane].element.rhs);
      request.expected[lane] = expected_p32_add(
          request.lhs[lane], request.rhs[lane]);
    }
    requests.push_back(std::move(request));
  }
  return requests;
}

AddMetrics run_add(Driver& driver, const std::vector<AddRequest>& requests,
                   pvu::ComparisonStats& fp32_comparison) {
  if (requests.empty()) {
    throw std::runtime_error("LLM Add trace has no requests");
  }

  AddMetrics metrics;
  std::unordered_map<uint32_t, size_t> outstanding;
  size_t next_request = 0;
  size_t completed = 0;
  size_t idle_cycles = 0;
  const uint64_t cycles_before = driver.cycles();
  const AddRequest* held = nullptr;

  while (completed != requests.size()) {
    if (held == nullptr && next_request != requests.size()) {
      held = &requests[next_request];
    }
    driver.drive(held);
    const bool accepted_request = held != nullptr && driver.in_ready();
    const bool accepted_response = driver.out_valid();

    if (accepted_response) {
      if (driver.out_op() != kAddOp) {
        throw std::runtime_error("LLM Add response operation mismatch");
      }
      const auto response = outstanding.find(driver.out_tag());
      if (response == outstanding.end()) {
        throw std::runtime_error("LLM Add response tag mismatch");
      }
      const AddRequest& request = requests[response->second];
      const std::array<uint32_t, kLanes> actual = driver.output();
      for (size_t lane = 0; lane < request.valid_lanes; ++lane) {
        if (actual[lane] != request.expected[lane]) {
          ++metrics.exact_mismatches;
          if (metrics.first_mismatch.empty()) {
            const NamedAddElement& element = request.elements[lane];
            metrics.first_mismatch =
                "LLM Add mismatch: operation=" + element.operation_name +
                " source_index=" + std::to_string(element.source_index) +
                " lane=" + std::to_string(lane) +
                " expected=" + std::to_string(request.expected[lane]) +
                " actual=" + std::to_string(actual[lane]);
          }
        }
        fp32_comparison.add(actual[lane],
                            request.elements[lane].element.output);
      }
      outstanding.erase(response);
      ++completed;
    }

    if (accepted_request) {
      const auto inserted = outstanding.emplace(held->tag, next_request);
      if (!inserted.second) {
        throw std::runtime_error("LLM Add duplicate request tag");
      }
      ++metrics.requests;
      metrics.elements += held->valid_lanes;
      ++next_request;
      held = nullptr;
    }

    if (accepted_request || accepted_response) {
      idle_cycles = 0;
    } else if (++idle_cycles > 32) {
      throw std::runtime_error("LLM Add made no handshake progress");
    }
    driver.tick();
  }
  metrics.cycles = driver.cycles() - cycles_before;
  return metrics;
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
    const pvu::LlmAddSelection selection =
        pvu::parse_llm_add_selection(argc, argv);
    const pvu::LlmTrace trace = pvu::load_llm_trace(selection.trace_root);
    const TraceAddElements elements =
        collect_trace_elements(trace, selection.sample_count);

    VPvuTop dut;
    Driver driver(dut);
    driver.reset();
    pvu::ComparisonStats fp32_comparison;
    const AddMetrics metrics = run_add(
        driver, make_add_requests(driver, elements.elements), fp32_comparison);

    const bool passed = metrics.exact_mismatches == 0;
    std::cout << "LLM P32 Add workload\n";
    std::cout << "  trace_model: " << trace.model << '\n';
    std::cout << "  trace_profile: " << trace.profile << '\n';
    std::cout << "  layer_index: " << trace.layer_index << '\n';
    std::cout << "  add_operations: " << trace.add_operations.size() << '\n';
    std::cout << "  requested_samples_per_operation: "
              << selection.sample_count << '\n';
    std::cout << "  source_elements: " << elements.source_elements << '\n';
    std::cout << "  sampled_elements: " << elements.elements.size() << '\n';
    std::cout << "  requests: " << metrics.requests << '\n';
    std::cout << "  elements: " << metrics.elements << '\n';
    std::cout << "  cycles: " << metrics.cycles << '\n';
    std::cout << std::fixed << std::setprecision(6)
              << "  request_per_cycle: "
              << per_cycle(metrics.requests, metrics.cycles) << '\n'
              << "  element_per_cycle: "
              << per_cycle(metrics.elements, metrics.cycles) << '\n';
    std::cout << "  exact_mismatches: " << metrics.exact_mismatches << '\n';
    print_comparison(fp32_comparison);
    std::cout << "  conformance: " << (passed ? "PASS" : "FAIL") << '\n';
    if (!passed) throw std::runtime_error(metrics.first_mismatch);
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "LLM P32 Add workload error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}

#endif  // CONFIG_LLM_P32_ADD_WORKLOAD
