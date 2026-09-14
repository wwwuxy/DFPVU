#!/usr/bin/env bash
# Regression: replay actual binary Add trace artifacts through op=1, including
# a final partial vector, with raw SoftPosit results checked by the runner.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
runner=${1:-"$repo_root/obj_dir/VPvuTop"}
fixture="$repo_root/test_src/llm-p32-add-fixture"
output=$(mktemp)
partial_output=$(mktemp)
trap 'rm -f "$output" "$partial_output"' EXIT

"$runner" "$fixture" >"$output"

rg -x -F 'LLM P32 Add workload' "$output"
rg -x -F '  trace_profile: gemma3-1b' "$output"
rg -x -F '  add_operations: 1' "$output"
rg -x -F '  requested_samples_per_operation: 256' "$output"
rg -x -F '  source_elements: 6' "$output"
rg -x -F '  sampled_elements: 6' "$output"
rg -x -F '  requests: 2' "$output"
rg -x -F '  elements: 6' "$output"
rg -x '  cycles: [1-9][0-9]*' "$output"
rg -x -F '  exact_mismatches: 0' "$output"
rg -x -F '  fp32_reference_samples: 6' "$output"
rg -x -F '  conformance: PASS' "$output"

"$runner" "$fixture" 5 >"$partial_output"

rg -x -F 'LLM P32 Add workload' "$partial_output"
rg -x -F '  requested_samples_per_operation: 5' "$partial_output"
rg -x -F '  sampled_elements: 5' "$partial_output"
rg -x -F '  requests: 2' "$partial_output"
rg -x -F '  elements: 5' "$partial_output"
rg -x -F '  exact_mismatches: 0' "$partial_output"
rg -x -F '  fp32_reference_samples: 5' "$partial_output"
rg -x -F '  conformance: PASS' "$partial_output"

printf 'LLM P32 Add fixture: full and partial requests conform exactly\n'
