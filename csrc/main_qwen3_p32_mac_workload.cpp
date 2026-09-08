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

  uint64_t cycles() const { return cycles_; }

 private:
  void tick() {
    dut_.clock = 0;
    dut_.eval();
    dut_.clock = 1;
    dut_.eval();
    dut_.clock = 0;
    dut_.eval();
    ++cycles_;
  }

  VPvuTop& dut_;
  uint32_t next_tag_ = 1;
  uint64_t cycles_ = 0;
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

void verify_element(Driver& driver, const std::string& module_name,
                    const pvu::LinearModuleTrace& module, size_t token,
                    size_t row, pvu::WorkloadMetrics& metrics,
                    uint64_t& exact_mismatches) {
  const uint32_t hardware =
      run_p32_mac_element(driver, module, token, row, metrics);
  const uint32_t expected = ref_p32_mac_element(module, token, row);
  if (hardware != expected) {
    ++exact_mismatches;
    throw std::runtime_error("P32 MAC mismatch: module=" + module_name +
                             " token=" + std::to_string(token) +
                             " row=" + std::to_string(row) +
                             " expected=" + hex32(expected) +
                             " actual=" + hex32(hardware));
  }
  (void)ref_fp32_mac_element(module, token, row);
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
    pvu::WorkloadMetrics metrics;
    uint64_t total_exact_mismatches = 0;

    const uint32_t hardware = run_p32_mac_element(
        driver, trace.modules.at("q_proj"), 0, 0, metrics);
    const uint32_t expected =
        ref_p32_mac_element(trace.modules.at("q_proj"), 0, 0);
    if (hardware != expected) {
      throw std::runtime_error("P32 MAC mismatch: module=q_proj token=0 row=0 "
                               "expected=" + hex32(expected) +
                               " actual=" + hex32(hardware));
    }

    for (const auto& named_module : trace.modules) {
      uint64_t module_exact_mismatches = 0;
      for (size_t token = 0; token < selection.token_count; ++token) {
        for (size_t row = 0; row < selection.row_count; ++row) {
          if (named_module.first == "q_proj" && token == 0 && row == 0) {
            continue;
          }
          verify_element(driver, named_module.first, named_module.second, token,
                         row, metrics, module_exact_mismatches);
        }
      }
      total_exact_mismatches += module_exact_mismatches;
      std::cout << named_module.first
                << ": exact mismatches=" << module_exact_mismatches << '\n';
    }
    std::cout << "total: exact mismatches=" << total_exact_mismatches << '\n';
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "Qwen3 trace error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}

#endif  // CONFIG_QWEN3_P32_MAC_WORKLOAD
