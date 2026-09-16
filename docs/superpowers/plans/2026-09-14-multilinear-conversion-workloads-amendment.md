# Multi-linear Conversion Workloads Plan Amendment

This amendment is part of
`2026-09-14-multilinear-conversion-workloads.md` and takes precedence where
the two documents differ.

## Task 3 corrections

The base MAC fixture invocation is:

```bash
"$runner" "$base_fixture" 1 3 2 >"$output"
```

Seven modules with replay tile `M=1,N=3,K=2` produce 21 selected output
elements. The assertions are:

```bash
rg -q '^  self_attn.o_proj: trace_shape=M1 N4 K4 replay_tile=M1 N3 K2$' "$output"
rg -q '^  mlp.down_proj: trace_shape=M1 N4 K4 replay_tile=M1 N3 K2$' "$output"
rg -q '^  overall: elements=21 exact mismatches=0$' "$output"
```

The K=8 subcase still selects one element per module. After adding `o_proj`,
its expected counts are seven elements, 14 requests, and 56 MAC terms.

## Task 4 correction

Extend the C++ helper with exact source-name assertions before the sampling
checks:

```cpp
require(std::string(pvu::llm_conversion_tensor_name(
            pvu::LlmConversionTensor::kInput)) == "input",
        "input conversion source name mismatch");
require(std::string(pvu::llm_conversion_tensor_name(
            pvu::LlmConversionTensor::kWeight)) == "weight",
        "weight conversion source name mismatch");
require(std::string(pvu::llm_conversion_tensor_name(
            pvu::LlmConversionTensor::kOutput)) == "output",
        "output conversion source name mismatch");
```

## Task 6 correction

The isolated test replaces only the copied repository's `.config` with the
following content after substituting the absolute copied-repository path for
`<copied-repo>`:

```text
CONFIG_LLM_P32_TO_P16_WORKLOAD=y
CONFIG_LLM_P32_PATH_MODE_CUSTOM=y
CONFIG_LLM_P32_MODEL_DIR="/tmp/not-used-when-fixture-exists"
CONFIG_LLM_P32_MAC_TRACE_DIR="<copied-repo>/test_src/qwen3-p32-fixture"
CONFIG_LLM_P32_CONVERSION_TENSOR_WEIGHT=y
CONFIG_LLM_P32_CONVERSION_TENSOR="weight"
CONFIG_LLM_P32_CONVERSION_SAMPLE_COUNT=5
CONFIG_LLM_P32_AUTO_EXPORT_TRACE=n
```

The Custom root contains the checked-in legacy trace, so `make run` reuses it
without local-model export. The test removes only its `mktemp -d` directory in
its exit trap and then verifies the caller's `.config` remains byte-for-byte
unchanged with `cmp -s`.
