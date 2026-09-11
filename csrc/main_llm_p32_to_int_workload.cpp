#include "../config.h"

#if defined(CONFIG_LLM_P32_TO_INT_WORKLOAD) && CONFIG_LLM_P32_TO_INT_WORKLOAD

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
#include "pvu_llm_to_int_workload.h"
#include "pvu_qwen3_metrics.h"
#include "pvu_qwen3_trace.h"

namespace {

constexpr size_t kLanes = 4;
constexpr uint8_t kPositToIntOp = 10;

struct NamedSample {
  std::string module_name;
  size_t source_index = 0;
  float value = 0.0F;
};

struct Request {
  uint32_t tag = 0;
  uint8_t valid_lanes = 0;
  std::array<NamedSample, kLanes> samples{};
  std::array<uint32_t, kLanes> posit{};
  std::array<int32_t, kLanes> expected{};
};

struct Metrics {
  uint64_t cycles = 0;
  uint64_t requests = 0;
  uint64_t elements = 0;
  uint64_t exact_mismatches = 0;
  uint64_t saturation_results = 0;
  std::string first_mismatch;
};

struct IntegerizationStats {
  uint64_t samples = 0;
  uint64_t finite_samples = 0;
  uint64_t zero_references = 0;
  uint64_t special_references = 0;
  uint64_t relative_error_samples = 0;
  long double absolute_error_sum = 0.0L;
  long double relative_error_sum = 0.0L;
  long double max_absolute_error = 0.0L;
  long double max_relative_error = 0.0L;

  void add(int32_t integer_result, float source) {
    ++samples;
    if (!std::isfinite(source)) {
      ++special_references;
      return;
    }
    ++finite_samples;
    if (source == 0.0F) ++zero_references;

    const long double error = std::fabs(
        static_cast<long double>(integer_result) - static_cast<long double>(source));
    absolute_error_sum += error;
    if (error > max_absolute_error) max_absolute_error = error;
    if (source == 0.0F) return;

    const long double relative_error =
        error / std::fabs(static_cast<long double>(source));
    ++relative_error_samples;
    relative_error_sum += relative_error;
    if (relative_error > max_relative_error) max_relative_error = relative_error;
  }

  long double mean_absolute_error() const {
    return finite_samples == 0
               ? 0.0L
               : absolute_error_sum / static_cast<long double>(finite_samples);
  }

  long double mean_relative_error() const {
    return relative_error_samples == 0
               ? 0.0L
               : relative_error_sum /
                     static_cast<long double>(relative_error_samples);
  }
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

  void drive(const Request* request) {
    dut_.io_out_ready = 1;
    dut_.io_in_valid = request != nullptr;
    if (request == nullptr) {
      dut_.eval();
      return;
    }

    dut_.io_posit_i1_0 = request->posit[0];
    dut_.io_posit_i1_1 = request->posit[1];
    dut_.io_posit_i1_2 = request->posit[2];
    dut_.io_posit_i1_3 = request->posit[3];
    dut_.io_posit_i2_0 = 0;
    dut_.io_posit_i2_1 = 0;
    dut_.io_posit_i2_2 = 0;
    dut_.io_posit_i2_3 = 0;
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
    dut_.io_op = kPositToIntOp;
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
  std::array<int32_t, kLanes> output() const {
    return {static_cast<int32_t>(dut_.io_int_o_0),
            static_cast<int32_t>(dut_.io_int_o_1),
            static_cast<int32_t>(dut_.io_int_o_2),
            static_cast<int32_t>(dut_.io_int_o_3)};
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

int32_t expected_posit_to_int(uint32_t raw) {
  posit32_t posit{};
  posit.v = raw;
  return static_cast<int32_t>(p32_to_i32(posit));
}

std::vector<Request> make_requests(Driver& driver,
                                   const std::vector<NamedSample>& samples) {
  std::vector<Request> requests;
  requests.reserve((samples.size() + kLanes - 1) / kLanes);
  for (size_t offset = 0; offset < samples.size(); offset += kLanes) {
    Request request;
    request.tag = driver.next_tag();
    request.valid_lanes =
        static_cast<uint8_t>(std::min(kLanes, samples.size() - offset));
    for (size_t lane = 0; lane < request.valid_lanes; ++lane) {
      request.samples[lane] = samples[offset + lane];
      request.posit[lane] = pvu::float_to_p32(request.samples[lane].value);
      request.expected[lane] = expected_posit_to_int(request.posit[lane]);
    }
    requests.push_back(std::move(request));
  }
  return requests;
}

Metrics run(Driver& driver, const std::vector<Request>& requests,
            IntegerizationStats& integerization) {
  if (requests.empty()) {
    throw std::runtime_error("LLM P32-to-Int trace has no selected samples");
  }

  Metrics metrics;
  std::unordered_map<uint32_t, size_t> outstanding;
  size_t next_request = 0;
  size_t completed = 0;
  size_t idle_cycles = 0;
  const uint64_t cycles_before = driver.cycles();
  const Request* held = nullptr;

  while (completed != requests.size()) {
    if (held == nullptr && next_request != requests.size()) {
      held = &requests[next_request];
    }
    driver.drive(held);
    const bool accepted_request = held != nullptr && driver.in_ready();
    const bool accepted_response = driver.out_valid();

    if (accepted_response) {
      if (driver.out_op() != kPositToIntOp) {
        throw std::runtime_error("LLM P32-to-Int response operation mismatch");
      }
      const auto response = outstanding.find(driver.out_tag());
      if (response == outstanding.end()) {
        throw std::runtime_error("LLM P32-to-Int response tag mismatch");
      }
      const Request& request = requests[response->second];
      const std::array<int32_t, kLanes> actual = driver.output();
      for (size_t lane = 0; lane < request.valid_lanes; ++lane) {
        if (actual[lane] != request.expected[lane]) {
          ++metrics.exact_mismatches;
          if (metrics.first_mismatch.empty()) {
            const NamedSample& sample = request.samples[lane];
            metrics.first_mismatch =
                "LLM P32-to-Int mismatch: module=" + sample.module_name +
                " source_index=" + std::to_string(sample.source_index) +
                " posit_raw=" + std::to_string(request.posit[lane]) +
                " expected=" + std::to_string(request.expected[lane]) +
                " actual=" + std::to_string(actual[lane]);
          }
        }
        if (actual[lane] == std::numeric_limits<int32_t>::min() ||
            actual[lane] == std::numeric_limits<int32_t>::max()) {
          ++metrics.saturation_results;
        }
        integerization.add(actual[lane], request.samples[lane].value);
      }
      outstanding.erase(response);
      ++completed;
    }

    if (accepted_request) {
      const auto inserted = outstanding.emplace(held->tag, next_request);
      if (!inserted.second) {
        throw std::runtime_error("LLM P32-to-Int duplicate request tag");
      }
      ++metrics.requests;
      metrics.elements += held->valid_lanes;
      ++next_request;
      held = nullptr;
    }

    if (accepted_request || accepted_response) {
      idle_cycles = 0;
    } else if (++idle_cycles > 32) {
      throw std::runtime_error("LLM P32-to-Int made no handshake progress");
    }
    driver.tick();
  }
  metrics.cycles = driver.cycles() - cycles_before;
  return metrics;
}

struct TraceSamples {
  uint64_t source_elements = 0;
  std::vector<NamedSample> values;
};

TraceSamples collect_trace_samples(const pvu::LlmTrace& trace,
                                   pvu::LlmToIntTensor source,
                                   size_t requested_samples) {
  TraceSamples collected;
  for (const auto& named_module : trace.modules) {
    const pvu::LlmToIntSamples samples = pvu::sample_llm_to_int_tensor(
        named_module.second, source, requested_samples);
    if (samples.source_elements >
        std::numeric_limits<uint64_t>::max() - collected.source_elements) {
      throw std::runtime_error("LLM P32-to-Int source element count overflow");
    }
    collected.source_elements += samples.source_elements;
    for (size_t index = 0; index < samples.values.size(); ++index) {
      collected.values.push_back(
          {named_module.first, samples.source_indices[index], samples.values[index]});
    }
  }
  if (collected.values.empty()) {
    throw std::runtime_error("LLM P32-to-Int trace has no selected samples");
  }
  return collected;
}

double per_cycle(uint64_t count, uint64_t cycles) {
  return cycles == 0 ? 0.0
                     : static_cast<double>(count) / static_cast<double>(cycles);
}

void print_integerization(const IntegerizationStats& stats) {
  std::cout << "  fp32_reference_samples: " << stats.samples << '\n';
  std::cout << "  integerization_finite_samples: " << stats.finite_samples << '\n';
  std::cout << "  integerization_zero_references: "
            << stats.zero_references << '\n';
  std::cout << "  integerization_special_references: "
            << stats.special_references << '\n';
  std::cout << std::scientific << std::setprecision(9)
            << "  integerization_max_absolute_error: "
            << static_cast<double>(stats.max_absolute_error) << '\n'
            << "  integerization_mean_absolute_error: "
            << static_cast<double>(stats.mean_absolute_error()) << '\n'
            << "  integerization_max_relative_error: "
            << static_cast<double>(stats.max_relative_error) << '\n'
            << "  integerization_mean_relative_error: "
            << static_cast<double>(stats.mean_relative_error()) << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const pvu::LlmToIntSelection selection =
        pvu::parse_llm_to_int_selection(argc, argv);
    const pvu::LlmTrace trace = pvu::load_llm_trace(selection.trace_root);
    const TraceSamples samples = collect_trace_samples(
        trace, selection.tensor, selection.sample_count);

    VPvuTop dut;
    Driver driver(dut);
    driver.reset();
    IntegerizationStats integerization;
    const Metrics metrics = run(
        driver, make_requests(driver, samples.values), integerization);

    std::cout << "LLM P32-to-Int workload\n";
    std::cout << "  trace_model: " << trace.model << '\n';
    std::cout << "  trace_profile: " << trace.profile << '\n';
    std::cout << "  layer_index: " << trace.layer_index << '\n';
    std::cout << "  tensor_source: "
              << pvu::llm_to_int_tensor_name(selection.tensor) << '\n';
    std::cout << "  requested_samples_per_module: "
              << selection.sample_count << '\n';
    std::cout << "  source_elements: " << samples.source_elements << '\n';
    std::cout << "  sampled_elements: " << samples.values.size() << '\n';
    std::cout << "  requests: " << metrics.requests << '\n';
    std::cout << "  elements: " << metrics.elements << '\n';
    std::cout << "  cycles: " << metrics.cycles << '\n';
    std::cout << std::fixed << std::setprecision(6)
              << "  request_per_cycle: "
              << per_cycle(metrics.requests, metrics.cycles) << '\n'
              << "  element_per_cycle: "
              << per_cycle(metrics.elements, metrics.cycles) << '\n';
    std::cout << "  exact_mismatches: " << metrics.exact_mismatches << '\n';
    std::cout << "  saturation_results: " << metrics.saturation_results << '\n';
    print_integerization(integerization);

    const bool passed = metrics.exact_mismatches == 0;
    std::cout << "  conformance: " << (passed ? "PASS" : "FAIL") << '\n';
    if (!passed) {
      std::cerr << metrics.first_mismatch << '\n';
      return 1;
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "LLM P32-to-Int workload error: " << error.what() << '\n';
    return 1;
  }
}

#endif  // CONFIG_LLM_P32_TO_INT_WORKLOAD
