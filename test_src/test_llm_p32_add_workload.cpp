#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "pvu_llm_add_workload.h"

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

pvu::AddOperationTrace make_operation() {
  pvu::AddOperationTrace operation;
  operation.lhs.shape = {2, 3};
  operation.lhs.values = {1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
  operation.rhs.shape = {2, 3};
  operation.rhs.values = {10.0F, 20.0F, 30.0F, 40.0F, 50.0F, 60.0F};
  operation.output.shape = {2, 3};
  operation.output.values = {11.0F, 22.0F, 33.0F, 44.0F, 55.0F, 66.0F};
  return operation;
}

}  // namespace

int main() {
  const auto operation = make_operation();
  const auto sampled = pvu::sample_llm_add_elements(operation, 5);
  require(sampled.source_elements == 6 && sampled.elements.size() == 5,
          "Add samples must clamp and retain the requested count");
  require(sampled.source_indices.size() == 5 && sampled.source_indices[0] == 0 &&
              sampled.source_indices[1] == 1 && sampled.source_indices[2] == 2 &&
              sampled.source_indices[3] == 3 && sampled.source_indices[4] == 5,
          "Add samples must be evenly spaced and include both endpoints");
  require(sampled.elements.front().lhs == 1.0F &&
              sampled.elements.front().rhs == 10.0F &&
              sampled.elements.back().lhs == 6.0F &&
              sampled.elements.back().rhs == 60.0F,
          "Add samples must preserve paired operand values");

  const auto clamped = pvu::sample_llm_add_elements(operation, 9);
  require(clamped.source_elements == 6 && clamped.elements.size() == 6,
          "Add sample count must clamp to source elements");

  const char* argv[] = {"add_workload", "/tmp/trace", "1024"};
  const auto selection =
      pvu::parse_llm_add_selection(3, const_cast<char**>(argv));
  require(selection.trace_root == "/tmp/trace" && selection.sample_count == 1024,
          "Add selection parser");

  require_throws([] { (void)pvu::parse_llm_add_sample_count("0"); },
                 "sample count");
  auto invalid = operation;
  invalid.rhs.shape = {3, 2};
  require_throws(
      [&invalid] { (void)pvu::sample_llm_add_elements(invalid, 1); },
      "shapes must match");
  invalid = operation;
  invalid.output.values.pop_back();
  require_throws(
      [&invalid] { (void)pvu::sample_llm_add_elements(invalid, 1); },
      "values do not match its shape");

  std::cout << "llm P32 Add workload helper tests passed\n";
  return 0;
}
