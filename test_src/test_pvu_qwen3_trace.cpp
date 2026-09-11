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

std::filesystem::path mutated_trace(
    const std::filesystem::path& fixture, const std::filesystem::path& cases,
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

void make_v2_metadata(std::string& metadata, const std::string& profile,
                      const std::string& semantics = "linear-no-bias") {
  replace_once(metadata, "  \"format_version\": 1,\n",
               "  \"format_version\": 2,\n"
               "  \"profile\": \"" + profile + "\",\n"
               "  \"output_semantics\": \"" + semantics + "\",\n");
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      throw std::runtime_error("usage: test_pvu_qwen3_trace FIXTURE BAD_TRACE");
    }

    const pvu::LlmTrace trace = pvu::load_llm_trace(argv[1]);
    require(trace.format_version == 1 && trace.model == "fixture",
            "legacy fixture identity mismatch");
    require(trace.profile == "legacy-qwen3-v1" && trace.modules.size() == 6,
            "legacy fixture compatibility mismatch");
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
    require_throws([&] { (void)pvu::load_llm_trace(missing_key.string()); },
                   "missing modules[0].dtype", "missing dtype was not rejected");

    const auto wrong_dtype = mutated_trace(argv[1], cases, "wrong-dtype",
        [](std::string& metadata) {
          replace_once(metadata, "\"float32-le\"", "\"float32-be\"");
        });
    require_throws([&] { (void)pvu::load_llm_trace(wrong_dtype.string()); },
                   "dtype must be float32-le", "wrong dtype was not rejected");

    const auto bias = mutated_trace(argv[1], cases, "bias",
        [](std::string& metadata) {
          replace_once(metadata, "\"has_bias\": false", "\"has_bias\": true");
        });
    require_throws([&] { (void)pvu::load_llm_trace(bias.string()); },
                   "unsupported bias", "v1 bias was not rejected");

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
    require_throws([&] { (void)pvu::load_llm_trace(nonfinite.string()); },
                   "non-finite value", "non-finite operand was not rejected");

    const auto bad_k = mutated_trace(argv[1], cases, "bad-k",
        [](std::string& metadata) {
          replace_once(metadata, "\"input_shape\": [1, 4]",
                       "\"input_shape\": [1, 3]");
          replace_once(metadata, "\"weight_shape\": [4, 4]",
                       "\"weight_shape\": [4, 3]");
        });
    require_throws([&] { (void)pvu::load_llm_trace(bad_k.string()); },
                   "divisible by four", "nondivisible K was not rejected");

    const auto v2 = mutated_trace(argv[1], cases, "v2",
        [](std::string& metadata) {
          make_v2_metadata(metadata, "gemma3-1b");
          replace_once(metadata, "\"model\": \"fixture\"",
                       "\"model\": \"/root/models/gemma-3-1b-it\"");
          replace_once(metadata, "\"has_bias\": false", "\"has_bias\": true");
        });
    const pvu::LlmTrace v2_trace = pvu::load_llm_trace(v2.string());
    require(v2_trace.format_version == 2 && v2_trace.profile == "gemma3-1b" &&
                v2_trace.output_semantics == "linear-no-bias" &&
                v2_trace.modules.size() == 6,
            "v2 generic trace was not accepted");

    const auto missing_profile = mutated_trace(argv[1], cases, "missing-profile",
        [](std::string& metadata) {
          replace_once(metadata, "\"format_version\": 1", "\"format_version\": 2");
        });
    require_throws([&] { (void)pvu::load_llm_trace(missing_profile.string()); },
                   "missing root.profile", "v2 trace without profile was accepted");

    const auto wrong_semantics = mutated_trace(argv[1], cases, "wrong-semantics",
        [](std::string& metadata) {
          make_v2_metadata(metadata, "phi4-mini", "module-output");
        });
    require_throws([&] { (void)pvu::load_llm_trace(wrong_semantics.string()); },
                   "output_semantics", "nonlinear v2 output semantics was accepted");

    char arg0[] = "reader-test";
    char arg1[] = "test_src/qwen3-p32-fixture";
    char arg2[] = "1";
    char arg3[] = "4";
    char arg4[] = "3";
    char* selection_argv[] = {arg0, arg1, arg2, arg3, arg4};
    const pvu::LlmSelection selection = pvu::parse_llm_selection(5, selection_argv);
    require(selection.trace_root == arg1 && selection.m == 1 &&
                selection.n == 4 && selection.k == 3,
            "explicit MNK selection mismatch");

    if (setenv("LLM_P32_TRACE_DIR", argv[1], 1) != 0) {
      throw std::runtime_error("failed to set LLM_P32_TRACE_DIR");
    }
    char* environment_argv[] = {arg0};
    const pvu::LlmSelection environment_selection =
        pvu::parse_llm_selection(1, environment_argv);
    require(environment_selection.trace_root == argv[1],
            "LLM_P32_TRACE_DIR selection mismatch");
    unsetenv("LLM_P32_TRACE_DIR");

    require_throws(
        [&] { (void)pvu::parse_llm_selection(1, environment_argv); },
        "LLM_P32_TRACE_DIR", "missing trace selection fell back to fixture");

    char zero_count[] = "0";
    char* zero_argv[] = {arg0, arg1, zero_count};
    const pvu::LlmSelection zero_selection =
        pvu::parse_llm_selection(3, zero_argv);
    require(zero_selection.m == 0,
            "zero token count did not select automatic module sizing");

    char excess_rows[] = "5";
    char* excess_argv[] = {arg0, arg1, arg2, excess_rows};
    require_throws(
        [&] { (void)pvu::parse_llm_selection(4, excess_argv); },
        "N exceeds", "out-of-range N was not rejected");

    require_throws(
        [&] { (void)pvu::load_llm_trace(argv[2]); },
        "q_proj.input.f32:",
        "invalid q_proj input artifact was not rejected");

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
    std::cout << "llm trace reader tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
