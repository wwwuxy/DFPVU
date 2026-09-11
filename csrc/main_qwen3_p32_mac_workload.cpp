#include "../config.h"

#ifdef CONFIG_LLM_P32_MAC_WORKLOAD

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <verilated.h>

#include "VPvuTop.h"
#include "pvu_qwen3_metrics.h"
#include "pvu_qwen3_trace.h"

namespace {

constexpr size_t kLanes = 4;
constexpr uint8_t kP32MacOp = 11;

std::string hex32(uint32_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
  return output.str();
}

void require_element_bounds(const pvu::LinearModuleTrace& module, size_t token,
                            size_t row, size_t selected_k) {
  if (module.input.shape.size() != 2 || module.weight.shape.size() != 2 ||
      module.input.shape[1] != module.weight.shape[1] ||
      module.input.shape[1] == 0 || selected_k == 0 ||
      selected_k > module.input.shape[1] || token >= module.input.shape[0] ||
      row >= module.weight.shape[0]) {
    throw std::runtime_error("invalid P32 MAC tile selection");
  }
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

  struct P32MacRequest {
    std::array<uint32_t, kLanes> activations;
    std::array<uint32_t, kLanes> weights;
    uint32_t accumulator;
    uint32_t tag;
    uint8_t valid_lanes;
  };

  uint32_t next_tag() { return next_tag_++; }

  void drive(const P32MacRequest* request) {
    dut_.io_out_ready = 1;
    dut_.io_in_valid = request != nullptr;
    if (request == nullptr) {
      dut_.eval();
      return;
    }
    dut_.io_posit_i1_0 = request->activations[0];
    dut_.io_posit_i1_1 = request->activations[1];
    dut_.io_posit_i1_2 = request->activations[2];
    dut_.io_posit_i1_3 = request->activations[3];
    dut_.io_posit_i2_0 = request->weights[0];
    dut_.io_posit_i2_1 = request->weights[1];
    dut_.io_posit_i2_2 = request->weights[2];
    dut_.io_posit_i2_3 = request->weights[3];
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

  void tick() { tick_clock(); }

  uint64_t cycles() const { return rising_edges_after_reset_; }

 private:
  void tick_clock() {
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

  VPvuTop& dut_;
  uint32_t next_tag_ = 1;
  uint64_t rising_edges_after_reset_ = 0;
};

struct ElementCoordinate {
  size_t token;
  size_t row;
};

struct ElementState {
  ElementCoordinate coordinate;
  size_t next_k = 0;
  uint32_t accumulator = 0;
  bool waiting_for_response = false;
  bool complete = false;
};

struct HeldRequest {
  size_t element_index;
  Driver::P32MacRequest request;
};

Driver::P32MacRequest make_p32_mac_request(
    Driver& driver, const pvu::LinearModuleTrace& module,
    const ElementState& element, size_t selected_k) {
  const size_t k_width = module.input.shape[1];
  Driver::P32MacRequest request{};
  request.accumulator = element.accumulator;
  request.tag = driver.next_tag();
  request.valid_lanes = static_cast<uint8_t>(
      std::min(kLanes, selected_k - element.next_k));
  for (size_t lane = 0; lane < request.valid_lanes; ++lane) {
    request.activations[lane] = pvu::float_to_p32(
        module.input.values[element.coordinate.token * k_width +
                            element.next_k + lane]);
    request.weights[lane] = pvu::float_to_p32(
        module.weight.values[element.coordinate.row * k_width +
                             element.next_k + lane]);
  }
  return request;
}

std::vector<uint32_t> run_p32_mac_elements(
    Driver& driver, const pvu::LinearModuleTrace& module,
    const std::vector<ElementCoordinate>& coordinates, size_t selected_k,
    pvu::WorkloadMetrics& metrics) {
  const size_t k_width = module.input.shape[1];
  std::vector<ElementState> elements;
  elements.reserve(coordinates.size());
  for (const ElementCoordinate coordinate : coordinates) {
    require_element_bounds(module, coordinate.token, coordinate.row, selected_k);
    elements.push_back({coordinate});
  }

  std::vector<uint32_t> results(coordinates.size());
  std::unordered_map<uint32_t, std::pair<size_t, uint8_t>> outstanding_by_tag;
  HeldRequest held{};
  bool has_held_request = false;
  size_t completed = 0;
  size_t next_candidate = 0;
  size_t idle_cycles = 0;
  const uint64_t cycles_before = driver.cycles();

  while (completed != elements.size()) {
    if (!has_held_request) {
      for (size_t attempts = 0; attempts < elements.size(); ++attempts) {
        const size_t element_index = next_candidate++ % elements.size();
        ElementState& element = elements[element_index];
        if (!element.complete && !element.waiting_for_response) {
          held = {element_index,
                  make_p32_mac_request(driver, module, element, selected_k)};
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
        throw std::runtime_error("P32 MAC response tag/op mismatch");
      }
      const auto response = outstanding_by_tag.find(driver.out_tag());
      if (response == outstanding_by_tag.end()) {
        throw std::runtime_error("P32 MAC response tag/op mismatch");
      }
      const std::pair<size_t, uint8_t> outstanding = response->second;
      ElementState& element = elements[outstanding.first];
      if (!element.waiting_for_response) {
        throw std::runtime_error("P32 MAC response tag/op mismatch");
      }
      element.accumulator = driver.out_result();
      element.next_k += outstanding.second;
      element.waiting_for_response = false;
      const size_t element_index = outstanding.first;
      outstanding_by_tag.erase(response);
      if (element.next_k == selected_k) {
        element.complete = true;
        results[element_index] = element.accumulator;
        ++completed;
      }
    }

    if (accepted_request) {
      ElementState& element = elements[held.element_index];
      if (element.waiting_for_response || element.complete) {
        throw std::runtime_error("P32 MAC scheduler dependency violation");
      }
      element.waiting_for_response = true;
      const auto inserted =
          outstanding_by_tag.emplace(
          held.request.tag, std::make_pair(held.element_index,
                                           held.request.valid_lanes));
      if (!inserted.second) {
        throw std::runtime_error("P32 MAC scheduler duplicate tag");
      }
      has_held_request = false;
      ++metrics.requests;
      metrics.valid_mac_terms += held.request.valid_lanes;
      metrics.active_lanes += held.request.valid_lanes;
    }

    if (accepted_request || accepted_response) {
      idle_cycles = 0;
    } else if (++idle_cycles > 32) {
      throw std::runtime_error("P32 MAC scheduler made no handshake progress");
    }
    driver.tick();
  }
  metrics.cycles += driver.cycles() - cycles_before;
  return results;
}

uint32_t ref_p32_mac_element(const pvu::LinearModuleTrace& module,
                             size_t token, size_t row, size_t selected_k) {
  require_element_bounds(module, token, row, selected_k);
  const size_t k_width = module.input.shape[1];
  uint32_t accumulator = 0;
  for (size_t k = 0; k < selected_k; k += kLanes) {
    for (size_t lane = 0; lane < std::min(kLanes, selected_k - k); ++lane) {
      const uint32_t activation =
          pvu::float_to_p32(module.input.values[token * k_width + k + lane]);
      const uint32_t weight =
          pvu::float_to_p32(module.weight.values[row * k_width + k + lane]);
      accumulator = pvu::p32_mul_add(activation, weight, accumulator);
    }
  }
  return accumulator;
}

float ref_fp32_mac_element(const pvu::LinearModuleTrace& module, size_t token,
                           size_t row, size_t selected_k) {
  require_element_bounds(module, token, row, selected_k);
  const size_t k_width = module.input.shape[1];
  float accumulator = 0.0F;
  for (size_t k = 0; k < selected_k; k += kLanes) {
    for (size_t lane = 0; lane < std::min(kLanes, selected_k - k); ++lane) {
      accumulator = std::fma(module.input.values[token * k_width + k + lane],
                             module.weight.values[row * k_width + k + lane],
                             accumulator);
    }
  }
  return accumulator;
}

struct ModuleReport {
  pvu::WorkloadMetrics workload;
  uint64_t elements = 0;
  uint64_t exact_mismatches = 0;
  pvu::ComparisonStats ordered_fp32;
  pvu::ComparisonStats pytorch_output;
  std::string first_mismatch;
};

float pytorch_output_element(const pvu::LinearModuleTrace& module,
                             size_t token, size_t row, size_t selected_k) {
  require_element_bounds(module, token, row, selected_k);
  return module.output.values[token * module.output.shape[1] + row];
}

void verify_elements(Driver& driver, const std::string& module_name,
                     const pvu::LinearModuleTrace& module,
                     const std::vector<ElementCoordinate>& coordinates,
                     size_t selected_k, ModuleReport& report) {
  const std::vector<uint32_t> hardware =
      run_p32_mac_elements(driver, module, coordinates, selected_k,
                           report.workload);
  for (size_t index = 0; index < coordinates.size(); ++index) {
    const ElementCoordinate coordinate = coordinates[index];
    const uint32_t expected =
        ref_p32_mac_element(module, coordinate.token, coordinate.row, selected_k);
    ++report.elements;
    if (hardware[index] != expected) {
      ++report.exact_mismatches;
      if (report.first_mismatch.empty()) {
        report.first_mismatch =
            "P32 MAC mismatch: module=" + module_name +
            " token=" + std::to_string(coordinate.token) +
            " row=" + std::to_string(coordinate.row) +
            " expected=" + hex32(expected) +
            " actual=" + hex32(hardware[index]);
      }
    }
    report.ordered_fp32.add(
        hardware[index],
        ref_fp32_mac_element(module, coordinate.token, coordinate.row, selected_k));
    if (selected_k == module.input.shape[1]) {
      report.pytorch_output.add(
          hardware[index], pytorch_output_element(module, coordinate.token,
                                                   coordinate.row, selected_k));
    }
  }
}

void merge_workload(pvu::WorkloadMetrics& total,
                    const pvu::WorkloadMetrics& module) {
  total.cycles += module.cycles;
  total.requests += module.requests;
  total.valid_mac_terms += module.valid_mac_terms;
  total.active_lanes += module.active_lanes;
}

void print_comparison_stats(const std::string& name,
                            const pvu::ComparisonStats& stats) {
  std::cout << "  " << name << ": samples=" << stats.samples
            << " finite=" << stats.finite_samples
            << " zero=" << stats.zero_references
            << " special=" << stats.special_samples
            << " result-special=" << stats.non_finite_results << std::endl;
  std::cout << "    ULP distribution: 0=" << stats.ulp.zero
            << " 1=" << stats.ulp.one
            << " 2-4=" << stats.ulp.two_to_four
            << " >=5=" << stats.ulp.five_or_more << std::endl;
  std::cout << std::fixed << std::setprecision(9)
            << "    max relative error=" << stats.max_relative_error
            << " mean relative error=" << stats.mean_relative_error()
            << " max absolute error=" << stats.max_absolute_error << std::endl;
}

void print_cycle_report(const std::string& name,
                        const pvu::WorkloadMetrics& metrics) {
  const uint64_t mac_terms = metrics.valid_mac_terms;
  const double requests_per_cycle =
      metrics.cycles == 0
          ? 0.0
          : static_cast<double>(metrics.requests) /
                static_cast<double>(metrics.cycles);
  const double mac_terms_per_cycle =
      metrics.cycles == 0
          ? 0.0
          : static_cast<double>(mac_terms) / static_cast<double>(metrics.cycles);
  const double lane_utilization =
      mac_terms == 0
          ? 0.0
          : static_cast<double>(metrics.active_lanes) /
                static_cast<double>(mac_terms);
  std::cout << std::fixed << std::setprecision(6) << "  " << name
            << ": requests=" << metrics.requests << " mac terms=" << mac_terms
            << " cycles=" << metrics.cycles
            << " requests/cycle=" << requests_per_cycle
            << " mac terms/cycle=" << mac_terms_per_cycle
            << " lane utilization=" << lane_utilization << std::endl;
}

void print_workload_summary(const pvu::LlmTrace& trace,
                            uint64_t selected_elements,
                            const pvu::WorkloadMetrics& metrics) {
  const uint64_t mac_terms = metrics.valid_mac_terms;
  const double requests_per_cycle =
      metrics.cycles == 0
          ? 0.0
          : static_cast<double>(metrics.requests) /
                static_cast<double>(metrics.cycles);
  const double mac_terms_per_cycle =
      metrics.cycles == 0
          ? 0.0
          : static_cast<double>(mac_terms) / static_cast<double>(metrics.cycles);
  const double lane_utilization =
      mac_terms == 0
          ? 0.0
          : static_cast<double>(metrics.active_lanes) /
                static_cast<double>(mac_terms);
  std::cout << "Workload summary" << std::endl;
  std::cout << std::fixed << std::setprecision(6)
            << "  trace model=" << trace.model << " profile=" << trace.profile
            << " selected elements=" << selected_elements
            << " MAC requests=" << metrics.requests
            << " MAC terms=" << mac_terms << " cycles=" << metrics.cycles
            << " requests/cycle=" << requests_per_cycle
            << " MAC terms/cycle=" << mac_terms_per_cycle
            << " lane utilization=" << lane_utilization << std::endl;
}

void print_precision_conclusion(const pvu::ComparisonStats& ordered_fp32,
                                uint64_t exact_mismatches) {
  std::cout << "Precision conclusion" << std::endl;
  std::cout << "  Ordered FP32 is the numerical reference; Posit<32,2> error "
               "is relative to it."
            << std::endl;
  std::cout << std::fixed << std::setprecision(9)
            << "  P32-vs-FP32: ULP bins 0=" << ordered_fp32.ulp.zero
            << " 1=" << ordered_fp32.ulp.one
            << " 2-4=" << ordered_fp32.ulp.two_to_four
            << " >=5=" << ordered_fp32.ulp.five_or_more
            << "; max relative error=" << ordered_fp32.max_relative_error
            << "; mean relative error=" << ordered_fp32.mean_relative_error()
            << std::endl;
  if (exact_mismatches == 0) {
    std::cout << "  Hardware-vs-SoftPosit conformance PASS; P32 hardware "
                 "accuracy conclusion is valid."
              << std::endl;
  } else {
    std::cout << "  Hardware-vs-SoftPosit conformance FAILED; no P32 hardware "
                 "accuracy conclusion is valid."
              << std::endl;
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const pvu::LlmSelection selection = pvu::parse_llm_selection(argc, argv);
    const pvu::LlmTrace trace = pvu::load_llm_trace(selection.trace_root);

    VPvuTop dut;
    Driver driver(dut);
    driver.reset();
    std::map<std::string, ModuleReport> reports;
    for (const auto& named_module : trace.modules) {
      const pvu::LinearModuleTrace& module = named_module.second;
      ModuleReport report;
      const size_t m = selection.m == 0 ? module.input.shape.at(0) : selection.m;
      const size_t n = selection.n == 0
                           ? std::min<size_t>(64, module.weight.shape.at(0))
                           : selection.n;
      const size_t k = selection.k == 0 ? module.input.shape.at(1) : selection.k;
      std::vector<ElementCoordinate> coordinates;
      coordinates.reserve(m * n);
      for (size_t token = 0; token < m; ++token) {
        for (size_t index = 0; index < n; ++index) {
          const size_t row =
              n == 1 ? 0 : index * (module.weight.shape.at(0) - 1) / (n - 1);
          coordinates.push_back({token, row});
        }
      }
      verify_elements(driver, named_module.first, module, coordinates, k,
                      report);
      reports.emplace(named_module.first, std::move(report));
    }

    pvu::WorkloadMetrics total_workload;
    pvu::ComparisonStats total_ordered_fp32;
    pvu::ComparisonStats total_pytorch_output;
    uint64_t total_elements = 0;
    uint64_t total_exact_mismatches = 0;
    std::string first_mismatch;
    for (const auto& named_report : reports) {
      const ModuleReport& report = named_report.second;
      total_elements += report.elements;
      total_exact_mismatches += report.exact_mismatches;
      merge_workload(total_workload, report.workload);
      total_ordered_fp32.merge(report.ordered_fp32);
      total_pytorch_output.merge(report.pytorch_output);
      if (first_mismatch.empty() && !report.first_mismatch.empty()) {
        first_mismatch = report.first_mismatch;
      }
    }

    std::cout << "LLM P32 MNK tile" << std::endl;
    std::cout << "  M: " << selection.m << std::endl;
    std::cout << "  N: " << selection.n << std::endl;
    std::cout << "  K: " << selection.k << std::endl;
    std::cout << "Hardware vs SoftPosit (Verilator workload data)" << std::endl;
    for (const auto& named_report : reports) {
      const ModuleReport& report = named_report.second;
      std::cout << "  " << named_report.first << ": elements="
                << report.elements << " exact mismatches="
                << report.exact_mismatches << std::endl;
    }
    std::cout << "  overall: elements=" << total_elements
              << " exact mismatches=" << total_exact_mismatches << std::endl;

    std::cout << "Posit vs ordered FP32 (Verilator workload data; observational)"
              << std::endl;
    for (const auto& named_report : reports) {
      print_comparison_stats(named_report.first,
                             named_report.second.ordered_fp32);
    }
    print_comparison_stats("overall", total_ordered_fp32);

    std::cout << "Posit vs exported pre-bias FP32 (full-K tiles only; observational)"
              << std::endl;
    for (const auto& named_report : reports) {
      print_comparison_stats(named_report.first,
                             named_report.second.pytorch_output);
    }
    print_comparison_stats("overall", total_pytorch_output);

    std::cout << "Cycle report (Verilator workload data; not frequency/system TOPS)"
              << std::endl;
    for (const auto& named_report : reports) {
      print_cycle_report(named_report.first, named_report.second.workload);
    }
    print_cycle_report("overall", total_workload);

    print_workload_summary(trace, total_elements, total_workload);
    print_precision_conclusion(total_ordered_fp32, total_exact_mismatches);

    if (total_exact_mismatches != 0) {
      throw std::runtime_error(first_mismatch);
    }
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "LLM trace error: " << error.what() << std::endl;
    return EXIT_FAILURE;
  }
}

#endif  // CONFIG_LLM_P32_MAC_WORKLOAD
