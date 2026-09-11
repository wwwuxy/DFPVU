#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "pvu_llm_mul_workload.h"

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
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
    module.input.values = {1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F};
    module.weight.shape = {2, 8};
    module.weight.values = {
        101.0F, 102.0F, 103.0F, 104.0F, 105.0F, 106.0F, 107.0F, 108.0F,
        109.0F, 110.0F, 111.0F, 112.0F, 113.0F, 114.0F, 115.0F, 116.0F,
    };
    return module;
}

}  // namespace

int main() {
    const auto module = make_module();
    const auto sampled = pvu::sample_llm_mul_vectors(module, 3);
    require(sampled.source_vectors == 4, "source multiply vector count");
    require(sampled.vectors.size() == 3, "requested multiply vector count");

    require(sampled.vectors[0].token == 0 && sampled.vectors[0].row == 0 &&
                    sampled.vectors[0].k == 0,
            "first vector coordinates");
    require(sampled.vectors[0].input[0] == 1.0F && sampled.vectors[0].weight[3] == 104.0F,
            "first vector operands");
    require(sampled.vectors[1].token == 0 && sampled.vectors[1].row == 0 &&
                    sampled.vectors[1].k == 4,
            "middle vector coordinates");
    require(sampled.vectors[1].input[0] == 5.0F && sampled.vectors[1].weight[3] == 108.0F,
            "middle vector operands");
    require(sampled.vectors[2].token == 0 && sampled.vectors[2].row == 1 &&
                    sampled.vectors[2].k == 4,
            "last vector coordinates");
    require(sampled.vectors[2].input[0] == 5.0F && sampled.vectors[2].weight[3] == 116.0F,
            "last vector operands");

    auto invalid_k = module;
    invalid_k.input.shape = {1, 6};
    invalid_k.input.values.resize(6);
    invalid_k.weight.shape = {2, 6};
    invalid_k.weight.values.resize(12);
    require_throws([&invalid_k] { (void)pvu::sample_llm_mul_vectors(invalid_k, 1); },
                   "LLM Multiply K dimension must be divisible by four");

    const auto clamped = pvu::sample_llm_mul_vectors(module, 9);
    require(clamped.source_vectors == 4 && clamped.vectors.size() == 4,
            "sample count must clamp to available vectors");

    const char* argv[] = {"mul_workload", "/tmp/trace", "4096"};
    const auto selection = pvu::parse_llm_mul_selection(3, const_cast<char**>(argv));
    require(selection.trace_root == "/tmp/trace" && selection.sample_count == 4096,
            "selection parser");
    require_throws([] { (void)pvu::parse_llm_mul_sample_count("0"); }, "sample count");
    require_throws([] { (void)pvu::parse_llm_mul_sample_count("12x"); }, "sample count");

    std::cout << "llm multiply workload helper tests passed\n";
    return 0;
}
