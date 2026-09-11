#ifndef PVU_LLM_CONVERSION_WORKLOAD_H
#define PVU_LLM_CONVERSION_WORKLOAD_H

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "pvu_qwen3_trace.h"

namespace pvu {

enum class LlmConversionTensor { kInput, kWeight, kOutput };

struct LlmConversionSamples {
  size_t source_elements = 0;
  std::vector<size_t> source_indices;
  std::vector<float> values;
};

inline const Tensor& select_llm_conversion_tensor(
    const LinearModuleTrace& module, LlmConversionTensor source) {
  switch (source) {
    case LlmConversionTensor::kInput:
      return module.input;
    case LlmConversionTensor::kWeight:
      return module.weight;
    case LlmConversionTensor::kOutput:
      return module.output;
  }
  throw std::runtime_error("unknown LLM conversion tensor source");
}

inline const char* llm_conversion_tensor_name(LlmConversionTensor source) {
  switch (source) {
    case LlmConversionTensor::kInput:
      return "input";
    case LlmConversionTensor::kWeight:
      return "weight";
    case LlmConversionTensor::kOutput:
      return "output";
  }
  throw std::runtime_error("unknown LLM conversion tensor source");
}

inline LlmConversionSamples sample_llm_conversion_tensor(
    const Tensor& tensor, size_t requested_samples) {
  if (requested_samples == 0) {
    throw std::runtime_error("LLM conversion sample count must be positive");
  }
  if (tensor.shape.size() != 2) {
    throw std::runtime_error("LLM conversion tensor shape must be two-dimensional");
  }

  size_t source_elements = 1;
  for (const size_t dimension : tensor.shape) {
    if (dimension == 0 ||
        dimension > std::numeric_limits<size_t>::max() / source_elements) {
      throw std::runtime_error("LLM conversion tensor shape is invalid");
    }
    source_elements *= dimension;
  }
  if (tensor.values.size() != source_elements) {
    throw std::runtime_error(
        "LLM conversion tensor values do not match its shape");
  }

  const size_t sample_count = std::min(source_elements, requested_samples);
  LlmConversionSamples samples;
  samples.source_elements = source_elements;
  samples.source_indices.reserve(sample_count);
  samples.values.reserve(sample_count);
  for (size_t sample = 0; sample < sample_count; ++sample) {
    const size_t index = sample_count == 1
        ? 0
        : sample * (source_elements - 1) / (sample_count - 1);
    samples.source_indices.push_back(index);
    samples.values.push_back(tensor.values[index]);
  }
  return samples;
}

struct LlmConversionSelection {
  std::string trace_root;
  LlmConversionTensor tensor = LlmConversionTensor::kInput;
  size_t sample_count = 256;
};

inline LlmConversionTensor parse_llm_conversion_tensor(const char* argument) {
  if (argument == nullptr) {
    throw std::runtime_error("LLM conversion tensor source must not be empty");
  }
  const std::string source(argument);
  if (source == "input") return LlmConversionTensor::kInput;
  if (source == "weight") return LlmConversionTensor::kWeight;
  if (source == "output") return LlmConversionTensor::kOutput;
  throw std::runtime_error(
      "LLM conversion tensor source must be input, weight, or output");
}

inline size_t parse_llm_conversion_sample_count(const char* argument) {
  if (argument == nullptr || *argument == '\0') {
    throw std::runtime_error("LLM conversion sample count must be a positive integer");
  }
  const std::string value(argument);
  if (value.find_first_not_of("0123456789") != std::string::npos) {
    throw std::runtime_error("LLM conversion sample count must be a positive integer");
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(argument, &end, 10);
  if (errno == ERANGE || end == nullptr || *end != '\0' || parsed == 0 ||
      parsed > std::numeric_limits<size_t>::max()) {
    throw std::runtime_error("LLM conversion sample count is outside the supported range");
  }
  return static_cast<size_t>(parsed);
}

inline LlmConversionSelection parse_llm_conversion_selection(int argc,
                                                              char** argv) {
  if (argc < 1 || argc > 4) {
    throw std::runtime_error(
        "usage: llm-conversion-workload [trace-root [input|weight|output [samples]]]");
  }
  LlmConversionSelection selection;
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
  if (argc >= 3) selection.tensor = parse_llm_conversion_tensor(argv[2]);
  if (argc >= 4) selection.sample_count = parse_llm_conversion_sample_count(argv[3]);
  return selection;
}

}  // namespace pvu

#endif  // PVU_LLM_CONVERSION_WORKLOAD_H
