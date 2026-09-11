#!/usr/bin/env bash
# Regression: replay four-lane projection input-times-weight vectors through
# op=3 and retain exact raw-Posit32 conformance for every independent lane.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
runner=${1:-"$repo_root/obj_dir/VPvuTop"}
fixture="$repo_root/test_src/qwen3-p32-fixture"
output=$(mktemp)
trap 'rm -f "$output"' EXIT

"$runner" "$fixture" >"$output"

rg -x -F 'LLM P32 Multiply workload' "$output"
rg -x -F '  requested_samples_per_module: 256' "$output"
rg -x -F '  source_mul_vectors: 24' "$output"
rg -x -F '  sampled_mul_vectors: 24' "$output"
rg -x -F '  requests: 24' "$output"
rg -x -F '  valid_multiplications: 96' "$output"
rg -x '  cycles: [1-9][0-9]*' "$output"
rg -x -F '  exact_mismatches: 0' "$output"
rg -x -F '  conformance: PASS' "$output"

printf 'LLM P32 Multiply fixture: 24 exact four-lane projection vectors\n'
