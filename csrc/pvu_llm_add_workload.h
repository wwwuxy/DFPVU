#ifndef PVU_LLM_ADD_WORKLOAD_H
#define PVU_LLM_ADD_WORKLOAD_H

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "pvu_qwen3_trace.h"

namespace pvu {

struct LlmAddElement {
  float lhs = 0.0F;
  float rhs = 0.0F;
  float output = 0.0F;
};

struct LlmAddSamples {
  size_t source_elements = 0;
  std::vector<size_t> source_indices;
  std::vector<LlmAddElement> elements;
};

inline size_t llm_add_tensor_elements(const Tensor& tensor,
                                      const char* tensor_name) {
  if (tensor.shape.size() != 2) {
    throw std::runtime_error(std::string("LLM Add ") + tensor_name +
                             " tensor shape must be two-dimensional");
  }

  size_t elements = 1;
  for (const size_t dimension : tensor.shape) {
    if (dimension == 0 ||
        dimension > std::numeric_limits<size_t>::max() / elements) {
      throw std::runtime_error(std::string("LLM Add ") + tensor_name +
                               " tensor shape is invalid");
    }
    elements *= dimension;
  }
  if (tensor.values.size() != elements) {
    throw std::runtime_error(std::string("LLM Add ") + tensor_name +
                             " tensor values do not match its shape");
  }
  for (const float value : tensor.values) {
    if (!std::isfinite(value)) {
      throw std::runtime_error(std::string("LLM Add ") + tensor_name +
                               " tensor values must be finite");
    }
  }
  return elements;
}

inline LlmAddSamples sample_llm_add_elements(const AddOperationTrace& operation,
                                             size_t requested_samples) {
  if (requested_samples == 0) {
    throw std::runtime_error("LLM Add sample count must be positive");
  }

  const size_t lhs_elements = llm_add_tensor_elements(operation.lhs, "lhs");
  const size_t rhs_elements = llm_add_tensor_elements(operation.rhs, "rhs");
  const size_t output_elements =
      llm_add_tensor_elements(operation.output, "output");
  if (operation.lhs.shape != operation.rhs.shape ||
      operation.lhs.shape != operation.output.shape) {
    throw std::runtime_error("LLM Add operand and output shapes must match");
  }
  if (lhs_elements != rhs_elements || lhs_elements != output_elements) {
    throw std::runtime_error(
        "LLM Add operand and output element counts must match");
  }

  const size_t sample_count = std::min(lhs_elements, requested_samples);
  LlmAddSamples samples;
  samples.source_elements = lhs_elements;
  samples.source_indices.reserve(sample_count);
  samples.elements.reserve(sample_count);
  for (size_t sample = 0; sample < sample_count; ++sample) {
    const size_t index = sample_count == 1
        ? 0
        : sample * (lhs_elements - 1) / (sample_count - 1);
    samples.source_indices.push_back(index);
    samples.elements.push_back(
        {operation.lhs.values[index], operation.rhs.values[index],
         operation.output.values[index]});
  }
  return samples;
}

struct LlmAddSelection {
  std::string trace_root;
  size_t sample_count = 256;
};

inline size_t parse_llm_add_sample_count(const char* argument) {
  if (argument == nullptr || *argument == '\0') {
    throw std::runtime_error("LLM Add sample count must be a positive integer");
  }
  const std::string value(argument);
  if (value.find_first_not_of("0123456789") != std::string::npos) {
    throw std::runtime_error("LLM Add sample count must be a positive integer");
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(argument, &end, 10);
  if (errno == ERANGE || end == nullptr || *end != '\0' || parsed == 0 ||
      parsed > std::numeric_limits<size_t>::max()) {
    throw std::runtime_error(
        "LLM Add sample count is outside the supported range");
  }
  return static_cast<size_t>(parsed);
}

inline LlmAddSelection parse_llm_add_selection(int argc, char** argv) {
  if (argc < 1 || argc > 3) {
    throw std::runtime_error("usage: llm-add-workload [trace-root [samples]]");
  }
  LlmAddSelection selection;
  if (argc >= 2) {
    selection.trace_root = argv[1] == nullptr ? "" : argv[1];
  } else {
    const char* environment_root = std::getenv("LLM_P32_ADD_TRACE_DIR");
    selection.trace_root = environment_root == nullptr ? "" : environment_root;
  }
  if (selection.trace_root.empty()) {
    throw std::runtime_error(
        "LLM_P32_ADD_TRACE_DIR is required when trace-root is not an argument");
  }
  if (argc >= 3) selection.sample_count = parse_llm_add_sample_count(argv[2]);
  return selection;
}

}  // namespace pvu

#endif  // PVU_LLM_ADD_WORKLOAD_H
