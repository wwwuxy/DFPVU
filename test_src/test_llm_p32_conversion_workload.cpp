#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pvu_llm_conversion_workload.h"

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void require_throws(const std::function<void()>& operation,
                    const std::string& expected,
                    const std::string& message) {
  try {
    operation();
  } catch (const std::runtime_error& error) {
    if (std::string(error.what()).find(expected) != std::string::npos) return;
  }
  throw std::runtime_error(message);
}

pvu::LinearModuleTrace fixture_module() {
  pvu::LinearModuleTrace module;
  module.input = {{2, 4}, {1.0F, 2.0F, 3.0F, 4.0F,
                            5.0F, 6.0F, 7.0F, 8.0F}};
  module.weight = {{2, 4}, {10.0F, 11.0F, 12.0F, 13.0F,
                             14.0F, 15.0F, 16.0F, 17.0F}};
  module.output = {{2, 2}, {100.0F, 101.0F, 102.0F, 103.0F}};
  return module;
}

}  // namespace

int main() {
  try {
    const pvu::LinearModuleTrace module = fixture_module();

    require(&pvu::select_llm_conversion_tensor(
                module, pvu::LlmConversionTensor::kInput) == &module.input,
            "input conversion source selection mismatch");
    require(&pvu::select_llm_conversion_tensor(
                module, pvu::LlmConversionTensor::kWeight) == &module.weight,
            "weight conversion source selection mismatch");
    require(&pvu::select_llm_conversion_tensor(
                module, pvu::LlmConversionTensor::kOutput) == &module.output,
            "output conversion source selection mismatch");

    const pvu::LlmConversionSamples samples =
        pvu::sample_llm_conversion_tensor(module.weight, 4);
    require(samples.source_elements == 8 &&
                samples.source_indices == std::vector<size_t>({0, 2, 4, 7}) &&
                samples.values == std::vector<float>({10.0F, 12.0F, 14.0F, 17.0F}),
            "conversion samples must cover the tensor from first to last");

    const pvu::Tensor short_tensor{{1, 3}, {21.0F, 22.0F, 23.0F}};
    const pvu::LlmConversionSamples clamped =
        pvu::sample_llm_conversion_tensor(short_tensor, 5);
    require(clamped.source_indices == std::vector<size_t>({0, 1, 2}) &&
                clamped.values == std::vector<float>({21.0F, 22.0F, 23.0F}),
            "sample count larger than a tensor must retain every element once");

    require_throws(
        [&] { (void)pvu::sample_llm_conversion_tensor(module.input, 0); },
        "sample count", "zero requested conversion samples were accepted");
    const pvu::Tensor malformed{{2, 2}, {1.0F, 2.0F, 3.0F}};
    require_throws(
        [&] { (void)pvu::sample_llm_conversion_tensor(malformed, 2); },
        "shape", "malformed conversion tensor was accepted");

    char arg0[] = "conversion-workload";
    char arg1[] = "test_src/qwen3-p32-fixture";
    char arg2[] = "output";
    char arg3[] = "4096";
    char* selection_argv[] = {arg0, arg1, arg2, arg3};
    const pvu::LlmConversionSelection selection =
        pvu::parse_llm_conversion_selection(4, selection_argv);
    require(selection.trace_root == arg1 &&
                selection.tensor == pvu::LlmConversionTensor::kOutput &&
                selection.sample_count == 4096,
            "explicit conversion workload selection mismatch");

    char invalid_source[] = "residual";
    char* invalid_argv[] = {arg0, arg1, invalid_source};
    require_throws(
        [&] { (void)pvu::parse_llm_conversion_selection(3, invalid_argv); },
        "tensor source", "unknown conversion tensor source was accepted");

    std::cout << "llm conversion workload helper tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
