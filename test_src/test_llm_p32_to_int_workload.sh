#!/usr/bin/env bash
# Regression: replay real LLM activation values through op=10 and retain
# exact SoftPosit P32-to-Int32 conformance under tagged pipelined traffic.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
runner=${1:-"$repo_root/obj_dir/VPvuTop"}
fixture="$repo_root/test_src/qwen3-p32-fixture"
output=$(mktemp)
partial_output=$(mktemp)
trap 'rm -f "$output" "$partial_output"' EXIT

"$runner" "$fixture" >"$output"

rg -x -F 'LLM P32-to-Int workload' "$output"
rg -x -F '  tensor_source: input' "$output"
rg -x -F '  requested_samples_per_module: 256' "$output"
rg -x -F '  source_elements: 24' "$output"
rg -x -F '  sampled_elements: 24' "$output"
rg -x -F '  requests: 6' "$output"
rg -x -F '  elements: 24' "$output"
rg -x '  cycles: [1-9][0-9]*' "$output"
rg -x -F '  exact_mismatches: 0' "$output"
rg -x -F '  fp32_reference_samples: 24' "$output"
rg -x '  saturation_results: [0-9]+' "$output"
rg -x -F '  conformance: PASS' "$output"


"$runner" "$fixture" input 3 >"$partial_output"

rg -x -F 'LLM P32-to-Int workload' "$partial_output"
rg -x -F '  tensor_source: input' "$partial_output"
rg -x -F '  requested_samples_per_module: 3' "$partial_output"
rg -x -F '  sampled_elements: 18' "$partial_output"
rg -x -F '  requests: 5' "$partial_output"
rg -x -F '  elements: 18' "$partial_output"
rg -x -F '  exact_mismatches: 0' "$partial_output"
rg -x -F '  fp32_reference_samples: 18' "$partial_output"
rg -x '  saturation_results: [0-9]+' "$partial_output"
rg -x -F '  conformance: PASS' "$partial_output"

printf 'LLM P32-to-Int fixture: full and partial requests conform exactly\n'
