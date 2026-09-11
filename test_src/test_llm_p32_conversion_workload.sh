#!/usr/bin/env bash
# Regression: replay the real LLM trace value distribution through both op=7
# conversion directions and retain exact SoftPosit/IEEE conformance.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
runner=${1:-"$repo_root/obj_dir/VPvuTop"}
fixture="$repo_root/test_src/qwen3-p32-fixture"
output=$(mktemp)
trap 'rm -f "$output"' EXIT

"$runner" "$fixture" >"$output"

rg -x -F 'LLM P32 conversion workload' "$output"
rg -x -F '  tensor_source: input' "$output"
rg -x -F '  source_elements: 24' "$output"
rg -x -F '  sampled_elements: 24' "$output"
rg -x -F '  fp32_to_p32_requests: 6' "$output"
rg -x '  fp32_to_p32_cycles: [1-9][0-9]*' "$output"
rg -x -F '  fp32_to_p32_exact_mismatches: 0' "$output"
rg -x -F '  p32_to_fp32_requests: 6' "$output"
rg -x '  p32_to_fp32_cycles: [1-9][0-9]*' "$output"
rg -x -F '  p32_to_fp32_exact_mismatches: 0' "$output"
rg -x -F '  round_trip_samples: 24' "$output"
rg -x -F '  conformance: PASS' "$output"

printf 'LLM P32 conversion fixture: 24 values, two exact op=7 directions\n'
