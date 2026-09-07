#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "pvu_qwen3_metrics.h"
#include "pvu_qwen3_trace.h"

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
    if (expected.empty() ||
        std::string(error.what()).find(expected) != std::string::npos) {
      return;
    }
  }
  throw std::runtime_error(message);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      throw std::runtime_error("usage: test_pvu_qwen3_trace FIXTURE BAD_TRACE");
    }

    const pvu::Qwen3Trace trace = pvu::load_qwen3_trace(argv[1]);
    require(trace.model == "fixture", "fixture model identity mismatch");
    require(trace.modules.size() == 6, "fixture module count mismatch");
    require(trace.modules.at("q_proj").input.shape ==
                std::vector<size_t>({1, 4}),
            "fixture q_proj input shape mismatch");

    char arg0[] = "reader-test";
    char arg1[] = "test_src/qwen3-p32-fixture";
    char arg2[] = "1";
    char arg3[] = "4";
    char* selection_argv[] = {arg0, arg1, arg2, arg3};
    const pvu::Qwen3Selection selection =
        pvu::parse_qwen3_selection(4, selection_argv);
    require(selection.trace_root == arg1 && selection.token_count == 1 &&
                selection.row_count == 4,
            "explicit selection mismatch");

    if (setenv("QWEN3_TRACE_DIR", argv[1], 1) != 0) {
      throw std::runtime_error("failed to set QWEN3_TRACE_DIR");
    }
    char* environment_argv[] = {arg0};
    const pvu::Qwen3Selection environment_selection =
        pvu::parse_qwen3_selection(1, environment_argv);
    require(environment_selection.trace_root == argv[1],
            "QWEN3_TRACE_DIR selection mismatch");
    unsetenv("QWEN3_TRACE_DIR");

    char zero_count[] = "00";
    char* zero_argv[] = {arg0, arg1, zero_count};
    require_throws(
        [&] { (void)pvu::parse_qwen3_selection(3, zero_argv); }, "",
        "zero token count was not rejected");

    char excess_rows[] = "5";
    char* excess_argv[] = {arg0, arg1, arg2, excess_rows};
    require_throws(
        [&] { (void)pvu::parse_qwen3_selection(4, excess_argv); },
        "row-count exceeds", "out-of-range row count was not rejected");

    require_throws(
        [&] { (void)pvu::load_qwen3_trace(argv[2]); },
        "q_proj.input.f32: byte count",
        "truncated q_proj.input.f32 did not report a byte count error");

    require(pvu::p32_to_float(0x40000000u) == 1.0f,
            "SoftPosit P32 conversion mismatch");
    require(pvu::fp32_ulp_distance(-0.0F, 0.0F) == 0 &&
                pvu::fp32_ulp_distance(
                    -std::numeric_limits<float>::denorm_min(),
                    std::numeric_limits<float>::denorm_min()) == 2,
            "FP32 monotonic ULP ordering mismatch");
    pvu::ComparisonStats comparisons;
    comparisons.add(0x40000000u, 1.0f);
    comparisons.add(0u, 0.0f);
    comparisons.add(0x40000000u, 2.0f);
    comparisons.add(0x40000000u,
                    std::numeric_limits<float>::infinity());
    comparisons.add(0x80000000u, 4.0F);
    require(comparisons.ulp.zero == 2 && comparisons.finite_references == 4 &&
                comparisons.zero_references == 1 &&
                comparisons.non_finite_references == 1 &&
                comparisons.non_finite_results == 1 &&
                comparisons.max_absolute_error == 1.0 &&
                comparisons.mean_relative_error() == 0.25,
            "comparison metrics mismatch");

    pvu::WorkloadMetrics workload;
    workload.cycles = 7;
    workload.requests = 3;
    workload.active_lanes = 12;
    require(workload.cycles == 7 && workload.requests == 3 &&
                workload.active_lanes == 12,
            "workload metrics mismatch");

    std::cout << "qwen3 trace reader tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
