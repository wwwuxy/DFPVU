#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

#include "pvu_llm_dot_workload.h"

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
  module.input = {{2, 8}, {1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F,
                            9.0F, 10.0F, 11.0F, 12.0F, 13.0F, 14.0F, 15.0F,
                            16.0F}};
  module.weight = {{2, 8}, {101.0F, 102.0F, 103.0F, 104.0F, 105.0F, 106.0F,
                             107.0F, 108.0F, 109.0F, 110.0F, 111.0F, 112.0F,
                             113.0F, 114.0F, 115.0F, 116.0F}};
  return module;
}

}  // namespace

int main() {
  try {
    const pvu::LinearModuleTrace module = fixture_module();

    const pvu::LlmDotSamples samples = pvu::sample_llm_dot_vectors(module, 4);
    require(samples.source_vectors == 8 && samples.vectors.size() == 4,
            "Dot samples must cover the requested coordinate count");
    require(samples.vectors[0].token == 0 && samples.vectors[0].row == 0 &&
                samples.vectors[0].k == 0 &&
                samples.vectors[0].input[0] == 1.0F &&
                samples.vectors[0].weight[3] == 104.0F,
            "first Dot coordinate or vector values are wrong");
    require(samples.vectors[1].token == 0 && samples.vectors[1].row == 1 &&
                samples.vectors[1].k == 0 &&
                samples.vectors[1].input[0] == 1.0F &&
                samples.vectors[1].weight[3] == 112.0F,
            "second Dot coordinate must select the second output row");
    require(samples.vectors[2].token == 1 && samples.vectors[2].row == 0 &&
                samples.vectors[2].k == 0 &&
                samples.vectors[2].input[0] == 9.0F &&
                samples.vectors[2].weight[3] == 104.0F,
            "third Dot coordinate must select the second token");
    require(samples.vectors[3].token == 1 && samples.vectors[3].row == 1 &&
                samples.vectors[3].k == 4 &&
                samples.vectors[3].input[0] == 13.0F &&
                samples.vectors[3].weight[3] == 116.0F,
            "last Dot sample must cover the final coordinate");

    const pvu::LlmDotSamples clamped =
        pvu::sample_llm_dot_vectors(module, 16);
    require(clamped.vectors.size() == 8 && clamped.vectors[7].token == 1 &&
                clamped.vectors[7].row == 1 && clamped.vectors[7].k == 4,
            "oversized Dot sample count must retain every vector once");

    require_throws(
        [&] { (void)pvu::sample_llm_dot_vectors(module, 0); }, "sample count",
        "zero requested Dot samples were accepted");
    pvu::LinearModuleTrace mismatched_k = module;
    mismatched_k.weight.shape[1] = 4;
    mismatched_k.weight.values.resize(8);
    require_throws(
        [&] { (void)pvu::sample_llm_dot_vectors(mismatched_k, 1); }, "K",
        "mismatched Dot K dimensions were accepted");
    pvu::LinearModuleTrace partial_k = module;
    partial_k.input.shape[1] = 6;
    partial_k.weight.shape[1] = 6;
    partial_k.input.values.resize(12);
    partial_k.weight.values.resize(12);
    require_throws(
        [&] { (void)pvu::sample_llm_dot_vectors(partial_k, 1); }, "divisible",
        "non-four-lane Dot K dimension was accepted");

    char arg0[] = "dot-workload";
    char arg1[] = "test_src/qwen3-p32-fixture";
    char arg2[] = "4096";
    char* selection_argv[] = {arg0, arg1, arg2};
    const pvu::LlmDotSelection selection =
        pvu::parse_llm_dot_selection(3, selection_argv);
    require(selection.trace_root == arg1 && selection.sample_count == 4096,
            "explicit Dot workload selection mismatch");

    char invalid_count[] = "four";
    char* invalid_argv[] = {arg0, arg1, invalid_count};
    require_throws(
        [&] { (void)pvu::parse_llm_dot_selection(3, invalid_argv); },
        "positive integer", "invalid Dot sample count was accepted");

    std::cout << "llm dot workload helper tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
