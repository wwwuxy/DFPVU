#ifndef PVU_LLM_TO_INT_WORKLOAD_H
#define PVU_LLM_TO_INT_WORKLOAD_H

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

#include "pvu_llm_conversion_workload.h"

namespace pvu {

enum class LlmToIntTensor { kInput, kWeight, kOutput };

using LlmToIntSamples = LlmConversionSamples;

inline LlmConversionTensor as_llm_conversion_tensor(LlmToIntTensor source) {
  switch (source) {
    case LlmToIntTensor::kInput:
      return LlmConversionTensor::kInput;
    case LlmToIntTensor::kWeight:
      return LlmConversionTensor::kWeight;
    case LlmToIntTensor::kOutput:
      return LlmConversionTensor::kOutput;
  }
  throw std::runtime_error("unknown LLM P32-to-Int tensor source");
}

inline const Tensor& select_llm_to_int_tensor(const LinearModuleTrace& module,
                                              LlmToIntTensor source) {
  return select_llm_conversion_tensor(module, as_llm_conversion_tensor(source));
}

inline const char* llm_to_int_tensor_name(LlmToIntTensor source) {
  switch (source) {
    case LlmToIntTensor::kInput:
      return "input";
    case LlmToIntTensor::kWeight:
      return "weight";
    case LlmToIntTensor::kOutput:
      return "output";
  }
  throw std::runtime_error("unknown LLM P32-to-Int tensor source");
}

inline LlmToIntSamples sample_llm_to_int_tensor(const LinearModuleTrace& module,
                                                 LlmToIntTensor source,
                                                 size_t requested_samples) {
  try {
    return sample_llm_conversion_tensor(
        select_llm_to_int_tensor(module, source), requested_samples);
  } catch (const std::runtime_error& error) {
    std::string message(error.what());
    constexpr char kConversionPrefix[] = "LLM conversion";
    if (message.compare(0, sizeof(kConversionPrefix) - 1,
                        kConversionPrefix) == 0) {
      message.replace(0, sizeof(kConversionPrefix) - 1, "LLM P32-to-Int");
    }
    throw std::runtime_error(message);
  }
}

struct LlmToIntSelection {
  std::string trace_root;
  LlmToIntTensor tensor = LlmToIntTensor::kInput;
  size_t sample_count = 256;
};

inline LlmToIntTensor parse_llm_to_int_tensor(const char* argument) {
  if (argument == nullptr) {
    throw std::runtime_error("LLM P32-to-Int tensor source must not be empty");
  }
  const std::string source(argument);
  if (source == "input") return LlmToIntTensor::kInput;
  if (source == "weight") return LlmToIntTensor::kWeight;
  if (source == "output") return LlmToIntTensor::kOutput;
  throw std::runtime_error(
      "LLM P32-to-Int tensor source must be input, weight, or output");
}

inline size_t parse_llm_to_int_sample_count(const char* argument) {
  if (argument == nullptr || *argument == '\0') {
    throw std::runtime_error(
        "LLM P32-to-Int sample count must be a positive integer");
  }
  const std::string value(argument);
  if (value.find_first_not_of("0123456789") != std::string::npos) {
    throw std::runtime_error(
        "LLM P32-to-Int sample count must be a positive integer");
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(argument, &end, 10);
  if (errno == ERANGE || end == nullptr || *end != '\0' || parsed == 0 ||
      parsed > std::numeric_limits<size_t>::max()) {
    throw std::runtime_error(
        "LLM P32-to-Int sample count is outside the supported range");
  }
  return static_cast<size_t>(parsed);
}

inline LlmToIntSelection parse_llm_to_int_selection(int argc, char** argv) {
  if (argc < 1 || argc > 4) {
    throw std::runtime_error(
        "usage: llm-p32-to-int-workload [trace-root [input|weight|output [samples]]]");
  }
  LlmToIntSelection selection;
  if (argc >= 2) {
    selection.trace_root = argv[1] == nullptr ? "" : argv[1];
  } else {
    const char* environment_root = std::getenv("LLM_P32_TRACE_DIR");
    selection.trace_root = environment_root == nullptr ? "" : environment_root;
  }
  if (selection.trace_root.empty()) {
    throw std::runtime_error(
        "LLM_P32_TRACE_DIR is required when trace-root is not an argument");
  }
  if (argc >= 3) selection.tensor = parse_llm_to_int_tensor(argv[2]);
  if (argc >= 4) selection.sample_count = parse_llm_to_int_sample_count(argv[3]);
  return selection;
}

}  // namespace pvu

#endif  // PVU_LLM_TO_INT_WORKLOAD_H
