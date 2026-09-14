#!/usr/bin/env bash
# Regression: replay real LLM trace distributions through op=6 P32-to-P16
# conversion and retain exact raw SoftPosit conformance.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
runner=${1:-"$repo_root/obj_dir/VPvuTop"}
fixture="$repo_root/test_src/qwen3-p32-fixture"
output=$(mktemp)
trap 'rm -f "$output"' EXIT

check_conversion() {
  local source=$1 source_elements=$2 sampled_elements=$3 requests=$4
  "$runner" "$fixture" "$source" 5 >"$output"
  rg -x -F 'LLM P32-to-P16 workload' "$output"
  rg -x -F "  tensor_source: $source" "$output"
  rg -x -F '  requested_samples_per_module: 5' "$output"
  rg -x -F "  source_elements: $source_elements" "$output"
  rg -x -F "  sampled_elements: $sampled_elements" "$output"
  rg -x -F "  requests: $requests" "$output"
  rg -x -F "  elements: $sampled_elements" "$output"
  rg -x '  cycles: [1-9][0-9]*' "$output"
  rg -x -F '  exact_mismatches: 0' "$output"
  rg -x -F "  reconstructed_p16_samples: $sampled_elements" "$output"
  rg -x -F '  conformance: PASS' "$output"
}

check_conversion input 28 28 7
check_conversion weight 112 35 9
check_conversion output 28 28 7

printf 'LLM P32-to-P16 fixture: all three trace sources conform exactly\n'
