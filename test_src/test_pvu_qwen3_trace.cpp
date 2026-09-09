#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot read " + path.string());
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void write_text(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path);
  if (!output || !(output << text)) {
    throw std::runtime_error("cannot write " + path.string());
  }
}

void replace_once(std::string& text, const std::string& from,
                  const std::string& to) {
  const size_t position = text.find(from);
  if (position == std::string::npos) {
    throw std::runtime_error("test mutation source not found: " + from);
  }
  text.replace(position, from.size(), to);
}

std::filesystem::path mutated_trace(const std::filesystem::path& fixture,
                                    const std::filesystem::path& cases,
                                    const std::string& name,
                                    const std::function<void(std::string&)>& edit) {
  const std::filesystem::path root = cases / name;
  std::filesystem::copy(fixture, root,
                        std::filesystem::copy_options::recursive);
  std::string metadata = read_text(root / "metadata.json");
  edit(metadata);
  write_text(root / "metadata.json", metadata);
  return root;
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

    const std::filesystem::path cases =
        std::filesystem::path(argv[2]).string() + "-contract-cases";
    std::filesystem::remove_all(cases);
    std::filesystem::create_directories(cases);

    const auto missing_key = mutated_trace(argv[1], cases, "missing-key",
        [](std::string& metadata) {
          replace_once(metadata, "      \"dtype\": \"float32-le\",\n", "");
        });
    require_throws([&] { (void)pvu::load_qwen3_trace(missing_key.string()); },
                   "missing modules[0].dtype", "missing dtype was not rejected");

    const auto wrong_dtype = mutated_trace(argv[1], cases, "wrong-dtype",
        [](std::string& metadata) {
          replace_once(metadata, "\"float32-le\"", "\"float32-be\"");
        });
    require_throws([&] { (void)pvu::load_qwen3_trace(wrong_dtype.string()); },
                   "dtype must be float32-le", "wrong dtype was not rejected");

    const auto bias = mutated_trace(argv[1], cases, "bias",
        [](std::string& metadata) {
          replace_once(metadata, "\"has_bias\": false", "\"has_bias\": true");
        });
    require_throws([&] { (void)pvu::load_qwen3_trace(bias.string()); },
                   "unsupported bias", "bias-bearing module was not rejected");

    const auto nonfinite = mutated_trace(argv[1], cases, "nonfinite",
        [](std::string&) {});
    {
      const uint32_t infinity_bits = UINT32_C(0x7f800000);
      std::ofstream output(nonfinite / "q_proj.input.f32",
                           std::ios::binary | std::ios::trunc);
      output.write(reinterpret_cast<const char*>(&infinity_bits),
                   sizeof(infinity_bits));
      const std::array<uint32_t, 3> zeros = {0, 0, 0};
      output.write(reinterpret_cast<const char*>(zeros.data()),
                   sizeof(zeros));
    }
    require_throws([&] { (void)pvu::load_qwen3_trace(nonfinite.string()); },
                   "non-finite value", "non-finite operand was not rejected");

    const auto wrong_tokens = mutated_trace(argv[1], cases, "wrong-tokens",
        [](std::string& metadata) {
          replace_once(metadata, "\"model\": \"fixture\"",
                       "\"model\": \"/root/models/Qwen3-0.6B\"");
        });
    require_throws([&] { (void)pvu::load_qwen3_trace(wrong_tokens.string()); },
                   "requires exactly 16 input tokens",
                   "nonfixture one-token trace was not rejected");

    const auto bad_k = mutated_trace(argv[1], cases, "bad-k",
        [](std::string& metadata) {
          replace_once(metadata, "\"input_shape\": [1, 4]",
                       "\"input_shape\": [1, 3]");
          replace_once(metadata, "\"weight_shape\": [4, 4]",
                       "\"weight_shape\": [4, 3]");
        });
    require_throws([&] { (void)pvu::load_qwen3_trace(bad_k.string()); },
                   "divisible by four", "nondivisible K was not rejected");

    const auto wrong_model = mutated_trace(argv[1], cases, "wrong-model",
        [](std::string& metadata) {
          replace_once(metadata, "\"model\": \"fixture\"",
                       "\"model\": \"/root/models/Qwen3-1.7B\"");
        });
    require_throws([&] { (void)pvu::load_qwen3_trace(wrong_model.string()); },
                   "Qwen3-0.6B", "wrong production model was not rejected");

    const auto wrong_dimensions = mutated_trace(argv[1], cases, "wrong-dimensions",
        [](std::string& metadata) {
          replace_once(metadata, "\"model\": \"fixture\"",
                       "\"model\": \"/root/models/Qwen3-0.6B\"");
          replace_once(metadata,
                       "\"input_token_ids\": [\n    0\n  ]",
                       "\"input_token_ids\": [0, 1, 2, 3, 4, 5, 6, 7, "
                       "8, 9, 10, 11, 12, 13, 14, 15]");
        });
    require_throws(
        [&] { (void)pvu::load_qwen3_trace(wrong_dimensions.string()); },
        "Qwen3-0.6B tensor dimensions",
        "wrong production dimensions were not rejected by the model contract");

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

    require_throws(
        [&] { (void)pvu::parse_qwen3_selection(1, environment_argv); },
        "QWEN3_TRACE_DIR", "missing trace selection fell back to fixture");

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

    pvu::ComparisonStats nonfinite_zero;
    nonfinite_zero.add(0x80000000u, 0.0F);
    require(nonfinite_zero.zero_references == 1 &&
                nonfinite_zero.non_finite_results == 1,
            "zero reference and non-finite result were not classified independently");

    pvu::ComparisonStats both_nonfinite;
    both_nonfinite.add(0x80000000u,
                       std::numeric_limits<float>::infinity());
    require(both_nonfinite.non_finite_references == 1 &&
                both_nonfinite.non_finite_results == 1,
            "non-finite reference and result were not classified independently");

    pvu::WorkloadMetrics workload;
    workload.cycles = 7;
    workload.requests = 3;
    workload.active_lanes = 12;
    require(workload.cycles == 7 && workload.requests == 3 &&
                workload.active_lanes == 12,
            "workload metrics mismatch");

    std::filesystem::remove_all(cases);

    std::cout << "qwen3 trace reader tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
