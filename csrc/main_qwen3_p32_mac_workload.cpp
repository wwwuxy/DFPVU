#include "../config.h"

#ifdef CONFIG_QWEN3_P32_MAC_WORKLOAD

#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

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
                            size_t row) {
  if (module.input.shape.size() != 2 || module.weight.shape.size() != 2 ||
      module.input.shape[1] != module.weight.shape[1] ||
      module.input.shape[1] == 0 || module.input.shape[1] % kLanes != 0 ||
      token >= module.input.shape[0] || row >= module.weight.shape[0]) {
    throw std::runtime_error("invalid P32 MAC element selection");
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

  uint32_t run_p32_mac(const std::array<uint32_t, kLanes>& activations,
                       const std::array<uint32_t, kLanes>& weights,
                       uint32_t accumulator) {
    const uint32_t tag = next_tag_++;
    dut_.io_posit_i1_0 = activations[0];
    dut_.io_posit_i1_1 = activations[1];
    dut_.io_posit_i1_2 = activations[2];
    dut_.io_posit_i1_3 = activations[3];
    dut_.io_posit_i2_0 = weights[0];
    dut_.io_posit_i2_1 = weights[1];
    dut_.io_posit_i2_2 = weights[2];
    dut_.io_posit_i2_3 = weights[3];
    dut_.io_posit_i3 = accumulator;
    dut_.io_in_tag = tag;
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
    dut_.io_vector_size = kLanes;
    dut_.io_in_valid = 1;
    dut_.eval();

    size_t wait_cycles = 0;
    while (dut_.io_in_ready == 0) {
      if (++wait_cycles > 32) {
        throw std::runtime_error("P32 MAC request did not observe in_ready");
      }
      tick();
    }
    tick();
    dut_.io_in_valid = 0;
    dut_.eval();

    wait_cycles = 0;
    while (dut_.io_out_valid == 0) {
      if (++wait_cycles > 32) {
        throw std::runtime_error("P32 MAC request produced no out_valid");
      }
      tick();
    }
    if (static_cast<uint32_t>(dut_.io_out_tag) != tag ||
        static_cast<uint8_t>(dut_.io_out_op) != kP32MacOp) {
      throw std::runtime_error("P32 MAC response tag/op mismatch");
    }
    const uint32_t result = static_cast<uint32_t>(dut_.io_posit_dot_o);
    dut_.io_out_ready = 1;
    dut_.eval();
    tick();
    dut_.io_out_ready = 0;
    dut_.eval();
    return result;
  }

  uint64_t cycles() const { return rising_edges_after_reset_; }

 private:
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

  VPvuTop& dut_;
  uint32_t next_tag_ = 1;
  uint64_t rising_edges_after_reset_ = 0;
};

uint32_t run_p32_mac_element(Driver& driver,
                             const pvu::LinearModuleTrace& module,
                             size_t token, size_t row,
                             pvu::WorkloadMetrics& metrics) {
  require_element_bounds(module, token, row);
  const size_t k_width = module.input.shape[1];
  uint32_t accumulator = 0;
  const uint64_t cycles_before = driver.cycles();
  for (size_t k = 0; k < k_width; k += kLanes) {
    std::array<uint32_t, kLanes> activations{};
    std::array<uint32_t, kLanes> weights{};
    for (size_t lane = 0; lane < kLanes; ++lane) {
      activations[lane] =
          pvu::float_to_p32(module.input.values[token * k_width + k + lane]);
      weights[lane] =
          pvu::float_to_p32(module.weight.values[row * k_width + k + lane]);
    }
    accumulator = driver.run_p32_mac(activations, weights, accumulator);
    ++metrics.requests;
    metrics.active_lanes += kLanes;
  }
  metrics.cycles += driver.cycles() - cycles_before;
  return accumulator;
}

uint32_t ref_p32_mac_element(const pvu::LinearModuleTrace& module,
                             size_t token, size_t row) {
  require_element_bounds(module, token, row);
  const size_t k_width = module.input.shape[1];
  uint32_t accumulator = 0;
  for (size_t k = 0; k < k_width; k += kLanes) {
    for (size_t lane = 0; lane < kLanes; ++lane) {
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
                           size_t row) {
  require_element_bounds(module, token, row);
  const size_t k_width = module.input.shape[1];
  float accumulator = 0.0F;
  for (size_t k = 0; k < k_width; k += kLanes) {
    for (size_t lane = 0; lane < kLanes; ++lane) {
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
                             size_t token, size_t row) {
  require_element_bounds(module, token, row);
  return module.output.values[token * module.output.shape[1] + row];
}

void verify_element(Driver& driver, const std::string& module_name,
                    const pvu::LinearModuleTrace& module, size_t token,
                    size_t row, ModuleReport& report) {
  const uint32_t hardware =
      run_p32_mac_element(driver, module, token, row, report.workload);
  const uint32_t expected = ref_p32_mac_element(module, token, row);
  ++report.elements;
  if (hardware != expected) {
    ++report.exact_mismatches;
    if (report.first_mismatch.empty()) {
      report.first_mismatch =
          "P32 MAC mismatch: module=" + module_name +
          " token=" + std::to_string(token) +
          " row=" + std::to_string(row) +
          " expected=" + hex32(expected) + " actual=" + hex32(hardware);
    }
  }
  report.ordered_fp32.add(hardware, ref_fp32_mac_element(module, token, row));
  report.pytorch_output.add(hardware, pytorch_output_element(module, token, row));
}

void merge_workload(pvu::WorkloadMetrics& total,
                    const pvu::WorkloadMetrics& module) {
  total.cycles += module.cycles;
  total.requests += module.requests;
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
  const uint64_t mac_terms = metrics.requests * kLanes;
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

}  // namespace

int main(int argc, char** argv) {
  try {
    const pvu::Qwen3Selection selection =
        pvu::parse_qwen3_selection(argc, argv);
    const pvu::Qwen3Trace trace = pvu::load_qwen3_trace(selection.trace_root);
    if (trace.modules.size() != 6 ||
        trace.modules.at("q_proj").input.shape.size() != 2) {
      throw std::runtime_error("trace contract mismatch");
    }

    VPvuTop dut;
    Driver driver(dut);
    driver.reset();
    std::map<std::string, ModuleReport> reports;
    for (const auto& named_module : trace.modules) {
      ModuleReport report;
      const size_t token_count = selection.token_count == 0 ? trace.modules.at("q_proj").input.shape.at(0) : selection.token_count;
      for (size_t token = 0; token < token_count; ++token) {
        for (size_t row = 0; row < selection.row_count; ++row) {
          verify_element(driver, named_module.first, named_module.second, token,
                         row, report);
        }
      }
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

    std::cout << "Posit vs PyTorch output (Verilator workload data; observational)"
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

    if (total_exact_mismatches != 0) {
      throw std::runtime_error(first_mismatch);
    }
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "Qwen3 trace error: " << error.what() << std::endl;
    return EXIT_FAILURE;
  }
}

#endif  // CONFIG_QWEN3_P32_MAC_WORKLOAD
