#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "pvu_llm_to_int_workload.h"

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

template <typename Fn>
void require_throws(Fn&& fn, const std::string& expected_message) {
  try {
    fn();
  } catch (const std::exception& error) {
    require(std::string(error.what()).find(expected_message) != std::string::npos,
            "unexpected error message: " + std::string(error.what()));
    return;
  }
  require(false, "expected exception containing: " + expected_message);
}

pvu::LinearModuleTrace make_module() {
  pvu::LinearModuleTrace module;
  module.input.shape = {1, 8};
  module.input.values = {0.0F, 0.25F, 0.5F, 0.75F, 1.0F, -0.5F, -1.5F, 2.5F};
  module.weight.shape = {2, 8};
  module.weight.values = {
      -2.0F, -1.0F, -0.5F, -0.25F, 0.0F, 0.25F, 0.5F, 1.0F,
      2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F, 9.0F,
  };
  module.output.shape = {1, 4};
  module.output.values = {3.0F, -3.0F, 3.5F, -3.5F};
  return module;
}

}  // namespace

int main() {
  const auto module = make_module();
  const auto sampled = pvu::sample_llm_to_int_tensor(
      module, pvu::LlmToIntTensor::kInput, 3);
  require(sampled.source_elements == 8, "source element count");
  require(sampled.values.size() == 3, "requested sample count");
  require(sampled.source_indices[0] == 0 && sampled.source_indices[1] == 3 &&
              sampled.source_indices[2] == 7,
          "evenly spaced sample indices");
  require(sampled.values[0] == 0.0F && sampled.values[1] == 0.75F &&
              sampled.values[2] == 2.5F,
          "sampled input values");

  const auto output = pvu::sample_llm_to_int_tensor(
      module, pvu::LlmToIntTensor::kOutput, 9);
  require(output.source_elements == 4 && output.values.size() == 4,
          "sample count clamps to selected tensor");
  require(pvu::llm_to_int_tensor_name(pvu::LlmToIntTensor::kWeight) ==
              std::string("weight"),
          "weight tensor name");

  const char* argv[] = {"to_int_workload", "/tmp/trace", "weight", "1024"};
  const auto selection = pvu::parse_llm_to_int_selection(4, const_cast<char**>(argv));
  require(selection.trace_root == "/tmp/trace" &&
              selection.tensor == pvu::LlmToIntTensor::kWeight &&
              selection.sample_count == 1024,
          "selection parser");
  require_throws([] { (void)pvu::parse_llm_to_int_tensor("bias"); },
                 "input, weight, or output");
  require_throws([] { (void)pvu::parse_llm_to_int_sample_count("0"); },
                 "sample count");

  auto invalid = module;
  invalid.input.values.pop_back();
  require_throws(
      [&invalid] {
        (void)pvu::sample_llm_to_int_tensor(
            invalid, pvu::LlmToIntTensor::kInput, 1);
      },
      "LLM P32-to-Int tensor values do not match its shape");

  std::cout << "llm P32-to-Int workload helper tests passed\n";
  return 0;
}
