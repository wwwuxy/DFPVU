#include "../config.h"

#if defined(CONFIG_LLM_P32_CONVERSION_WORKLOAD) && \
    CONFIG_LLM_P32_CONVERSION_WORKLOAD

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
#include "pvu_float_reference.h"
#include "pvu_llm_conversion_workload.h"
#include "pvu_precision_report.h"
#include "pvu_qwen3_metrics.h"
#include "pvu_qwen3_trace.h"

namespace {

constexpr size_t kLanes = 4;
constexpr uint8_t kConversionOp = 7;

uint32_t fp32_bits(float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "float must be IEEE binary32");
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float fp32_value(uint64_t raw) {
  static_assert(std::numeric_limits<float>::is_iec559 &&
                    std::numeric_limits<float>::digits == 24,
                "float must be IEEE binary32");
  const uint32_t bits = static_cast<uint32_t>(raw);
  float value = 0.0F;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

struct ConversionRequest {
  uint32_t tag = 0;
  size_t sample_offset = 0;
  uint8_t valid_lanes = 0;
  bool float_to_p32 = true;
  std::array<uint64_t, kLanes> floating{};
  std::array<uint32_t, kLanes> posit{};
  std::array<uint64_t, kLanes> expected{};
};

struct ConversionMetrics {
  uint64_t cycles = 0;
  uint64_t requests = 0;
  uint64_t elements = 0;
  uint64_t exact_mismatches = 0;
  std::string first_mismatch;
};

struct ConversionRun {
  ConversionMetrics metrics;
  std::vector<std::array<uint64_t, kLanes>> results;
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

  void drive(const ConversionRequest* request) {
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
    dut_.io_float_i_0 = request->floating[0];
    dut_.io_float_i_1 = request->floating[1];
    dut_.io_float_i_2 = request->floating[2];
    dut_.io_float_i_3 = request->floating[3];
    dut_.io_float_i2_0 = 0;
    dut_.io_float_i2_1 = 0;
    dut_.io_float_i2_2 = 0;
    dut_.io_float_i2_3 = 0;
    dut_.io_in_tag = request->tag;
    dut_.io_op = kConversionOp;
    dut_.io_Isposit = request->float_to_p32 ? 0 : 1;
    dut_.io_Outposit = request->float_to_p32 ? 1 : 0;
    dut_.io_float_mode = 3;
    dut_.io_float_posit = request->float_to_p32 ? 1 : 0;
    dut_.io_src_posit_width = 32;
    dut_.io_dst_posit_width = 32;
    dut_.io_vector_size = request->valid_lanes;
    dut_.eval();
  }

  bool in_ready() const { return dut_.io_in_ready != 0; }
  bool out_valid() const { return dut_.io_out_valid != 0; }
  uint32_t out_tag() const { return static_cast<uint32_t>(dut_.io_out_tag); }
  uint8_t out_op() const { return static_cast<uint8_t>(dut_.io_out_op); }

  std::array<uint64_t, kLanes> output(bool float_to_p32) const {
    if (float_to_p32) {
      return {static_cast<uint32_t>(dut_.io_posit_o_0),
              static_cast<uint32_t>(dut_.io_posit_o_1),
              static_cast<uint32_t>(dut_.io_posit_o_2),
              static_cast<uint32_t>(dut_.io_posit_o_3)};
    }
    return {static_cast<uint64_t>(dut_.io_float_o_0),
            static_cast<uint64_t>(dut_.io_float_o_1),
            static_cast<uint64_t>(dut_.io_float_o_2),
            static_cast<uint64_t>(dut_.io_float_o_3)};
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

std::vector<ConversionRequest> make_fp32_to_p32_requests(
    Driver& driver, const std::vector<float>& samples) {
  std::vector<ConversionRequest> requests;
  requests.reserve((samples.size() + kLanes - 1) / kLanes);
  for (size_t offset = 0; offset < samples.size(); offset += kLanes) {
    ConversionRequest request;
    request.tag = driver.next_tag();
    request.sample_offset = offset;
    request.valid_lanes = static_cast<uint8_t>(
        std::min(kLanes, samples.size() - offset));
    request.float_to_p32 = true;
    for (size_t lane = 0; lane < request.valid_lanes; ++lane) {
      request.floating[lane] = fp32_bits(samples[offset + lane]);
      request.expected[lane] = pvu::float_to_p32(samples[offset + lane]);
    }
    requests.push_back(request);
  }
  return requests;
}

std::vector<ConversionRequest> make_p32_to_fp32_requests(
    Driver& driver, const std::vector<uint32_t>& samples) {
  std::vector<ConversionRequest> requests;
  requests.reserve((samples.size() + kLanes - 1) / kLanes);
  for (size_t offset = 0; offset < samples.size(); offset += kLanes) {
    ConversionRequest request;
    request.tag = driver.next_tag();
    request.sample_offset = offset;
    request.valid_lanes = static_cast<uint8_t>(
        std::min(kLanes, samples.size() - offset));
    request.float_to_p32 = false;
    for (size_t lane = 0; lane < request.valid_lanes; ++lane) {
      request.posit[lane] = samples[offset + lane];
      request.expected[lane] = pvu::ref_posit_to_float(
          samples[offset + lane], 3);
    }
    requests.push_back(request);
  }
  return requests;
}

ConversionRun run_conversion(Driver& driver,
                             const std::vector<ConversionRequest>& requests,
                             const std::string& stage) {
  ConversionRun run;
  run.results.resize(requests.size());
  std::unordered_map<uint32_t, size_t> outstanding;
  size_t next_request = 0;
  size_t completed = 0;
  size_t idle_cycles = 0;
  const uint64_t cycles_before = driver.cycles();
  const ConversionRequest* held = nullptr;

  while (completed != requests.size()) {
    if (held == nullptr && next_request != requests.size()) {
      held = &requests[next_request];
    }
    driver.drive(held);
    const bool accepted_request = held != nullptr && driver.in_ready();
    const bool accepted_response = driver.out_valid();

    if (accepted_response) {
      if (driver.out_op() != kConversionOp) {
        throw std::runtime_error(stage + " response operation mismatch");
      }
      const auto iterator = outstanding.find(driver.out_tag());
      if (iterator == outstanding.end()) {
        throw std::runtime_error(stage + " response tag mismatch");
      }
      const size_t request_index = iterator->second;
      const ConversionRequest& request = requests[request_index];
      const std::array<uint64_t, kLanes> actual =
          driver.output(request.float_to_p32);
      run.results[request_index] = actual;
      for (size_t lane = 0; lane < request.valid_lanes; ++lane) {
        if (actual[lane] != request.expected[lane]) {
          ++run.metrics.exact_mismatches;
          if (run.metrics.first_mismatch.empty()) {
            run.metrics.first_mismatch =
                stage + " mismatch at sample " +
                std::to_string(request.sample_offset + lane);
          }
        }
      }
      outstanding.erase(iterator);
      ++completed;
    }

    if (accepted_request) {
      const auto inserted = outstanding.emplace(held->tag, next_request);
      if (!inserted.second) {
        throw std::runtime_error(stage + " duplicate request tag");
      }
      ++run.metrics.requests;
      run.metrics.elements += held->valid_lanes;
      ++next_request;
      held = nullptr;
    }

    if (accepted_request || accepted_response) {
      idle_cycles = 0;
    } else if (++idle_cycles > 32) {
      throw std::runtime_error(stage + " made no handshake progress");
    }
    driver.tick();
  }
  run.metrics.cycles = driver.cycles() - cycles_before;
  return run;
}

std::vector<uint32_t> flatten_p32_results(const ConversionRun& run,
                                          size_t sample_count) {
  std::vector<uint32_t> results(sample_count);
  size_t offset = 0;
  for (const auto& group : run.results) {
    for (size_t lane = 0; lane < kLanes && offset < sample_count;
         ++lane, ++offset) {
      results[offset] = static_cast<uint32_t>(group[lane]);
    }
  }
  return results;
}

std::vector<uint64_t> flatten_fp32_results(const ConversionRun& run,
                                           size_t sample_count) {
  std::vector<uint64_t> results(sample_count);
  size_t offset = 0;
  for (const auto& group : run.results) {
    for (size_t lane = 0; lane < kLanes && offset < sample_count;
         ++lane, ++offset) {
      results[offset] = group[lane];
    }
  }
  return results;
}

struct TraceSamples {
  uint64_t source_elements = 0;
  std::vector<float> values;
};

TraceSamples collect_trace_samples(const pvu::LlmTrace& trace,
                                   pvu::LlmConversionTensor source,
                                   size_t requested_samples) {
  TraceSamples collected;
  for (const auto& named_module : trace.modules) {
    const pvu::LlmConversionSamples samples = pvu::sample_llm_conversion_tensor(
        pvu::select_llm_conversion_tensor(named_module.second, source),
        requested_samples);
    if (samples.source_elements >
        std::numeric_limits<uint64_t>::max() - collected.source_elements) {
      throw std::runtime_error("LLM conversion source element count overflow");
    }
    collected.source_elements += samples.source_elements;
    collected.values.insert(collected.values.end(), samples.values.begin(),
                            samples.values.end());
  }
  if (collected.values.empty()) {
    throw std::runtime_error("LLM conversion trace has no selected samples");
  }
  return collected;
}

void add_round_trip_sample(pvu::PrecisionLossStats& stats, float source,
                           uint64_t converted_raw) {
  const float converted = fp32_value(converted_raw);
  if (!std::isfinite(source) || !std::isfinite(converted)) {
    stats.add_special();
  } else if (source == 0.0F) {
    stats.add_zero();
  } else {
    stats.add_finite(static_cast<long double>(source),
                     static_cast<long double>(converted));
  }
}

double per_cycle(uint64_t count, uint64_t cycles) {
  return cycles == 0 ? 0.0
                     : static_cast<double>(count) / static_cast<double>(cycles);
}

void print_stage(const std::string& name, const ConversionMetrics& metrics) {
  std::cout << "  " << name << "_requests: " << metrics.requests << '\n';
  std::cout << "  " << name << "_cycles: " << metrics.cycles << '\n';
  std::cout << std::fixed << std::setprecision(6)
            << "  " << name << "_request_per_cycle: "
            << per_cycle(metrics.requests, metrics.cycles) << '\n'
            << "  " << name << "_elements_per_cycle: "
            << per_cycle(metrics.elements, metrics.cycles) << '\n';
  std::cout << "  " << name << "_exact_mismatches: "
            << metrics.exact_mismatches << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const pvu::LlmConversionSelection selection =
        pvu::parse_llm_conversion_selection(argc, argv);
    const pvu::LlmTrace trace = pvu::load_llm_trace(selection.trace_root);
    const TraceSamples samples = collect_trace_samples(
        trace, selection.tensor, selection.sample_count);

    VPvuTop dut;
    Driver driver(dut);
    driver.reset();
    const ConversionRun fp32_to_p32 = run_conversion(
        driver, make_fp32_to_p32_requests(driver, samples.values),
        "fp32_to_p32");
    const std::vector<uint32_t> p32_values =
        flatten_p32_results(fp32_to_p32, samples.values.size());
    const ConversionRun p32_to_fp32 = run_conversion(
        driver, make_p32_to_fp32_requests(driver, p32_values),
        "p32_to_fp32");
    const std::vector<uint64_t> round_trip_values =
        flatten_fp32_results(p32_to_fp32, samples.values.size());

    pvu::PrecisionLossStats round_trip_loss;
    for (size_t index = 0; index < samples.values.size(); ++index) {
      add_round_trip_sample(round_trip_loss, samples.values[index],
                            round_trip_values[index]);
    }

    std::cout << "LLM P32 conversion workload\n";
    std::cout << "  trace_model: " << trace.model << '\n';
    std::cout << "  trace_profile: " << trace.profile << '\n';
    std::cout << "  layer_index: " << trace.layer_index << '\n';
    std::cout << "  tensor_source: "
              << pvu::llm_conversion_tensor_name(selection.tensor) << '\n';
    std::cout << "  requested_samples_per_module: "
              << selection.sample_count << '\n';
    std::cout << "  source_elements: " << samples.source_elements << '\n';
    std::cout << "  sampled_elements: " << samples.values.size() << '\n';
    print_stage("fp32_to_p32", fp32_to_p32.metrics);
    print_stage("p32_to_fp32", p32_to_fp32.metrics);
    std::cout << "  round_trip_samples: " << samples.values.size() << '\n';
    std::cout << "  round_trip_finite_nonzero: "
              << round_trip_loss.finite_nonzero() << '\n';
    std::cout << "  round_trip_zero_inputs: "
              << round_trip_loss.zero_inputs() << '\n';
    std::cout << "  round_trip_special_inputs: "
              << round_trip_loss.special_inputs() << '\n';
    std::cout << std::scientific << std::setprecision(9)
              << "  round_trip_max_relative_error: "
              << static_cast<double>(round_trip_loss.max_relative_error()) << '\n'
              << "  round_trip_mean_relative_error: "
              << static_cast<double>(round_trip_loss.mean_relative_error()) << '\n'
              << "  round_trip_max_absolute_error: "
              << static_cast<double>(round_trip_loss.max_absolute_error()) << '\n';

    const bool passed = fp32_to_p32.metrics.exact_mismatches == 0 &&
                        p32_to_fp32.metrics.exact_mismatches == 0;
    std::cout << "  conformance: " << (passed ? "PASS" : "FAIL") << '\n';
    if (!passed) {
      const std::string& mismatch =
          fp32_to_p32.metrics.first_mismatch.empty()
              ? p32_to_fp32.metrics.first_mismatch
              : fp32_to_p32.metrics.first_mismatch;
      throw std::runtime_error(mismatch);
    }
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "LLM conversion workload error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}

#endif  // CONFIG_LLM_P32_CONVERSION_WORKLOAD
