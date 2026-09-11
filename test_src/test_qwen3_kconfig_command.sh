#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
make -C "$repo_root" -n menuconfig | rg "^menuconfig$"
actual=$(make -C "$repo_root" -n run "CONFIG_LLM_P32_MAC_WORKLOAD=y" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_MAC_TRACE_DIR=/tmp/llm;echo injected" "CONFIG_MATRIX_GEMM_P32_M=2" "CONFIG_MATRIX_GEMM_P32_N=3" "CONFIG_MATRIX_GEMM_P32_K=5" | rg "^./obj_dir/VPvuTop")
expected="./obj_dir/VPvuTop  \"/tmp/llm;echo injected\" \"2\" \"3\" \"5\""

if [[ "$actual" != "$expected" ]]; then
  printf "expected shell-quoted Kconfig trace arguments:\n  %s\nactual:\n  %s\n" "$expected" "$actual" >&2
  exit 1
fi

scratch_dir=$(mktemp -d)
trap 'rm -rf "$scratch_dir"' EXIT
missing_trace="$scratch_dir/trace"
missing_model="$scratch_dir/model-is-not-installed"
if output=$(make -C "$repo_root" --no-print-directory llm_p32_mac_trace_config "CONFIG_LLM_P32_MAC_WORKLOAD=y" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_MAC_TRACE_DIR=$missing_trace" "CONFIG_LLM_P32_MODEL_DIR=$missing_model" 2>&1); then
  printf "expected a missing local model directory to fail, but command succeeded\n" >&2
  exit 1
fi
if ! printf '%s\n' "$output" | rg -F "Local LLM model directory is missing: $missing_model" >/dev/null; then
  printf "expected a clear missing-model diagnostic, got:\n%s\n" "$output" >&2
  exit 1
fi

printf "LLM Kconfig trace arguments are shell-quoted\n"

conversion_actual=$(make -C "$repo_root" -n run "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=y" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_MAC_TRACE_DIR=/tmp/llm-conversion" "CONFIG_LLM_P32_CONVERSION_TENSOR=weight" "CONFIG_LLM_P32_CONVERSION_SAMPLE_COUNT=256" | rg "^./obj_dir/VPvuTop")
conversion_expected="./obj_dir/VPvuTop  \"/tmp/llm-conversion\" \"weight\" \"256\""

if [[ "$conversion_actual" != "$conversion_expected" ]]; then
  printf "expected conversion Kconfig arguments:\n  %s\nactual:\n  %s\n" "$conversion_expected" "$conversion_actual" >&2
  exit 1
fi

printf "LLM conversion Kconfig arguments select source and sample count\n"

dot_actual=$(make -C "$repo_root" -n run "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=y" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_MAC_TRACE_DIR=/tmp/llm-dot" "CONFIG_LLM_P32_DOT_SAMPLE_COUNT=256" | rg "^./obj_dir/VPvuTop")
dot_expected="./obj_dir/VPvuTop  \"/tmp/llm-dot\" \"256\""
if [[ "$dot_actual" != "$dot_expected" ]]; then
  printf "expected Dot Kconfig arguments:\n  %s\nactual:\n  %s\n" "$dot_expected" "$dot_actual" >&2
  exit 1
fi
printf "LLM Dot Kconfig arguments select sample count\n"

mul_actual=$(make -C "$repo_root" -n run "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=y" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_MAC_TRACE_DIR=/tmp/llm-mul" "CONFIG_LLM_P32_MUL_SAMPLE_COUNT=256" | rg "^./obj_dir/VPvuTop")
mul_expected="./obj_dir/VPvuTop  \"/tmp/llm-mul\" \"256\""
if [[ "$mul_actual" != "$mul_expected" ]]; then
  printf "expected Multiply Kconfig arguments:\n  %s\nactual:\n  %s\n" "$mul_expected" "$mul_actual" >&2
  exit 1
fi
printf "LLM Multiply Kconfig arguments select sample count\n"

to_int_actual=$(make -C "$repo_root" -n run "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=y" "CONFIG_LLM_P32_MAC_TRACE_DIR=/tmp/llm-to-int" "CONFIG_LLM_P32_TO_INT_TENSOR=output" "CONFIG_LLM_P32_TO_INT_SAMPLE_COUNT=256" | rg "^./obj_dir/VPvuTop")
to_int_expected="./obj_dir/VPvuTop  \"/tmp/llm-to-int\" \"output\" \"256\""
if [[ "$to_int_actual" != "$to_int_expected" ]]; then
  printf "expected P32-to-Int Kconfig arguments:\n  %s\nactual:\n  %s\n" "$to_int_expected" "$to_int_actual" >&2
  exit 1
fi
printf "LLM P32-to-Int Kconfig arguments select source and sample count\n"

stale_trace="$scratch_dir/stale-profile-trace"
mkdir -p "$stale_trace"
printf '{"format_version":2,"profile":"qwen3-0.6b"}\n' >"$stale_trace/metadata.json"
if output=$(make -C "$repo_root" --no-print-directory llm_p32_trace_config "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=y" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_PROFILE_QWEN3_0_6B=n" "CONFIG_LLM_P32_PROFILE_GEMMA3_1B=y" "CONFIG_LLM_P32_PROFILE_PHI4_MINI=n" "CONFIG_LLM_P32_MAC_TRACE_DIR=$stale_trace" 2>&1); then
  printf "expected a stale LLM trace profile to fail, but command succeeded\n" >&2
  exit 1
fi
if ! printf '%s\n' "$output" | rg -F 'LLM trace profile does not match selected Kconfig profile' >/dev/null; then
  printf "expected a clear stale-profile diagnostic, got:\n%s\n" "$output" >&2
  exit 1
fi

printf "LLM trace profile is validated against the selected Kconfig model\n"
