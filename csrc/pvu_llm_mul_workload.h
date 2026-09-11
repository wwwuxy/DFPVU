#ifndef PVU_LLM_MUL_WORKLOAD_H
#define PVU_LLM_MUL_WORKLOAD_H

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

#include "pvu_llm_dot_workload.h"

namespace pvu {

constexpr size_t kLlmMulLanes = kLlmDotLanes;

using LlmMulVector = LlmDotVector;
using LlmMulSamples = LlmDotSamples;

// A multiply workload reuses the input/weight vector pairs selected for Dot.
// It differs only in that each of the four products is observed independently.
inline LlmMulSamples sample_llm_mul_vectors(const LinearModuleTrace& module,
                                            size_t requested_samples) {
  try {
    return sample_llm_dot_vectors(module, requested_samples);
  } catch (const std::runtime_error& error) {
    std::string message(error.what());
    constexpr char kDotPrefix[] = "LLM Dot ";
    if (message.compare(0, sizeof(kDotPrefix) - 1, kDotPrefix) == 0) {
      message.replace(4, 3, "Multiply");
    }
    throw std::runtime_error(message);
  }
}

struct LlmMulSelection {
  std::string trace_root;
  size_t sample_count = 256;
};

inline size_t parse_llm_mul_sample_count(const char* argument) {
  if (argument == nullptr || *argument == '\0') {
    throw std::runtime_error("LLM Multiply sample count must be a positive integer");
  }
  const std::string value(argument);
  if (value.find_first_not_of("0123456789") != std::string::npos) {
    throw std::runtime_error("LLM Multiply sample count must be a positive integer");
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(argument, &end, 10);
  if (errno == ERANGE || end == nullptr || *end != '\0' || parsed == 0 ||
      parsed > std::numeric_limits<size_t>::max()) {
    throw std::runtime_error(
        "LLM Multiply sample count is outside the supported range");
  }
  return static_cast<size_t>(parsed);
}

inline LlmMulSelection parse_llm_mul_selection(int argc, char** argv) {
  if (argc < 1 || argc > 3) {
    throw std::runtime_error(
        "usage: llm-multiply-workload [trace-root [samples]]");
  }
  LlmMulSelection selection;
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
  if (argc >= 3) selection.sample_count = parse_llm_mul_sample_count(argv[2]);
  return selection;
}

}  // namespace pvu

#endif  // PVU_LLM_MUL_WORKLOAD_H
