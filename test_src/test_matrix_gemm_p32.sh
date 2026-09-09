#!/usr/bin/env bash
set -euo pipefail

test_output=$(mktemp)
invalid_output=$(mktemp)
trap 'rm -f "$test_output" "$invalid_output"' EXIT

make run MATRIX_GEMM_P32_ARGS="--m 2 --n 3 --k 5 --seed 7" >"$test_output"

rg -F "Matrix GEMM P32 benchmark: M=2 N=3 K=5 seed=7" "$test_output"
rg -F "requests=12 valid_mac_terms=30" "$test_output"
rg -F "lane_utilization=0.625000" "$test_output"
rg -F "exact_mismatches=0" "$test_output"
rg -F "M,N,K,seed,requests,valid_mac_terms,cycles,request_per_cycle,mac_per_cycle,lane_utilization,max_chain_latency,exact_mismatches" "$test_output"
rg '^2,3,5,7,12,30,[0-9][0-9]*,0[.][0-9][0-9]*,1[.][0-9][0-9]*,0[.]625000,[0-9][0-9]*,0$' "$test_output"

expect_failure() {
  local arguments=$1
  local expected_error=$2
  if make run MATRIX_GEMM_P32_ARGS="$arguments" >"$invalid_output" 2>&1; then
    printf 'invalid arguments unexpectedly succeeded: %s\n' "$arguments" >&2
    exit 1
  fi
  rg -F "$expected_error" "$invalid_output"
}

expect_failure "--m 0" "Matrix GEMM P32 benchmark error: --m must be a positive integer"
expect_failure "--m -1" "Matrix GEMM P32 benchmark error: --m must be a positive integer"
expect_failure "--seed -1" "Matrix GEMM P32 benchmark error: --seed must be a positive integer"
expect_failure "--m" "Matrix GEMM P32 benchmark error: missing value for --m"
expect_failure "--unknown 1" "Matrix GEMM P32 benchmark error: unknown option: --unknown"
