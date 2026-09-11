#ifndef PVU_LLM_DOT_WORKLOAD_H
#define PVU_LLM_DOT_WORKLOAD_H

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "pvu_qwen3_trace.h"

namespace pvu {

constexpr size_t kLlmDotLanes = 4;

struct LlmDotVector {
  size_t token = 0;
  size_t row = 0;
  size_t k = 0;
  std::array<float, kLlmDotLanes> input{};
  std::array<float, kLlmDotLanes> weight{};
};

struct LlmDotSamples {
  size_t source_vectors = 0;
  std::vector<LlmDotVector> vectors;
};

inline size_t llm_dot_tensor_elements(const Tensor& tensor,
                                      const char* tensor_name) {
  if (tensor.shape.size() != 2) {
    throw std::runtime_error(std::string("LLM Dot ") + tensor_name +
                             " tensor shape must be two-dimensional");
  }
  size_t elements = 1;
  for (const size_t dimension : tensor.shape) {
    if (dimension == 0 ||
        dimension > std::numeric_limits<size_t>::max() / elements) {
      throw std::runtime_error(std::string("LLM Dot ") + tensor_name +
                               " tensor shape is invalid");
    }
    elements *= dimension;
  }
  if (tensor.values.size() != elements) {
    throw std::runtime_error(std::string("LLM Dot ") + tensor_name +
                             " tensor values do not match its shape");
  }
  return elements;
}

inline size_t checked_llm_dot_product(size_t lhs, size_t rhs) {
  if (lhs == 0 || rhs == 0 ||
      rhs > std::numeric_limits<size_t>::max() / lhs) {
    throw std::runtime_error("LLM Dot source vector count overflow");
  }
  return lhs * rhs;
}

inline LlmDotSamples sample_llm_dot_vectors(const LinearModuleTrace& module,
                                             size_t requested_samples) {
  if (requested_samples == 0) {
    throw std::runtime_error("LLM Dot sample count must be positive");
  }
  (void)llm_dot_tensor_elements(module.input, "input");
  (void)llm_dot_tensor_elements(module.weight, "weight");

  const size_t token_count = module.input.shape[0];
  const size_t input_k = module.input.shape[1];
  const size_t row_count = module.weight.shape[0];
  const size_t weight_k = module.weight.shape[1];
  if (input_k != weight_k) {
    throw std::runtime_error("LLM Dot input and weight K dimensions differ");
  }
  if (input_k % kLlmDotLanes != 0) {
    throw std::runtime_error("LLM Dot K dimension must be divisible by four");
  }

  const size_t groups_per_vector = input_k / kLlmDotLanes;
  const size_t source_vectors = checked_llm_dot_product(
      checked_llm_dot_product(token_count, row_count), groups_per_vector);
  const size_t sample_count = std::min(source_vectors, requested_samples);

  LlmDotSamples samples;
  samples.source_vectors = source_vectors;
  samples.vectors.reserve(sample_count);
  for (size_t sample = 0; sample < sample_count; ++sample) {
    const size_t flat = sample_count == 1
        ? 0
        : sample * (source_vectors - 1) / (sample_count - 1);
    const size_t group = flat % groups_per_vector;
    const size_t row_and_token = flat / groups_per_vector;
    const size_t row = row_and_token % row_count;
    const size_t token = row_and_token / row_count;
    const size_t k = group * kLlmDotLanes;

    LlmDotVector vector;
    vector.token = token;
    vector.row = row;
    vector.k = k;
    for (size_t lane = 0; lane < kLlmDotLanes; ++lane) {
      vector.input[lane] = module.input.values[token * input_k + k + lane];
      vector.weight[lane] = module.weight.values[row * input_k + k + lane];
    }
    samples.vectors.push_back(vector);
  }
  return samples;
}

struct LlmDotSelection {
  std::string trace_root;
  size_t sample_count = 256;
};

inline size_t parse_llm_dot_sample_count(const char* argument) {
  if (argument == nullptr || *argument == '\0') {
    throw std::runtime_error("LLM Dot sample count must be a positive integer");
  }
  const std::string value(argument);
  if (value.find_first_not_of("0123456789") != std::string::npos) {
    throw std::runtime_error("LLM Dot sample count must be a positive integer");
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(argument, &end, 10);
  if (errno == ERANGE || end == nullptr || *end != '\0' || parsed == 0 ||
      parsed > std::numeric_limits<size_t>::max()) {
    throw std::runtime_error("LLM Dot sample count is outside the supported range");
  }
  return static_cast<size_t>(parsed);
}

inline LlmDotSelection parse_llm_dot_selection(int argc, char** argv) {
  if (argc < 1 || argc > 3) {
    throw std::runtime_error(
        "usage: llm-dot-workload [trace-root [samples]]");
  }
  LlmDotSelection selection;
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
  if (argc >= 3) selection.sample_count = parse_llm_dot_sample_count(argv[2]);
  return selection;
}

}  // namespace pvu

#endif  // PVU_LLM_DOT_WORKLOAD_H
