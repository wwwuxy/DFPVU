#!/usr/bin/env bash
# Integration regression: make run must route the checked-in Custom trace to
# the P32-to-P16 workload without changing the caller's configuration.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
temporary_root=$(mktemp -d)
original_config="$temporary_root/original.config"
copied_repo="$temporary_root/repo"
output="$temporary_root/output"
trap 'rm -rf "$temporary_root"' EXIT

cp "$repo_root/.config" "$original_config"
cp -a "$repo_root/." "$copied_repo"

cat >"$copied_repo/.config" <<CONFIG
CONFIG_LLM_P32_TO_P16_WORKLOAD=y
CONFIG_LLM_P32_PATH_MODE_CUSTOM=y
CONFIG_LLM_P32_MODEL_DIR="/tmp/not-used-when-fixture-exists"
CONFIG_LLM_P32_MAC_TRACE_DIR="$copied_repo/test_src/qwen3-p32-fixture"
CONFIG_LLM_P32_CONVERSION_TENSOR_WEIGHT=y
CONFIG_LLM_P32_CONVERSION_TENSOR="weight"
CONFIG_LLM_P32_CONVERSION_SAMPLE_COUNT=5
CONFIG_LLM_P32_AUTO_EXPORT_TRACE=n
CONFIG

make -C "$copied_repo" --no-print-directory run >"$output"

rg -x -F 'LLM P32-to-P16 workload' "$output"
rg -x -F '  tensor_source: weight' "$output"
rg -x -F '  requested_samples_per_module: 5' "$output"
rg -x -F '  exact_mismatches: 0' "$output"
rg -x -F '  conformance: PASS' "$output"
cmp -s "$original_config" "$repo_root/.config"

printf 'isolated make run: P32-to-P16 Custom fixture conformance passed\n'
