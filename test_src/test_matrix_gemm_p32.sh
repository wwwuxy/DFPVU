#!/usr/bin/env bash
set -euo pipefail

test_output=$(mktemp)
trap 'rm -f "$test_output"' EXIT

matrix_m=$(awk -F= '$1 == "CONFIG_MATRIX_GEMM_P32_M" { print $2 }' .config)
matrix_n=$(awk -F= '$1 == "CONFIG_MATRIX_GEMM_P32_N" { print $2 }' .config)
matrix_k=$(awk -F= '$1 == "CONFIG_MATRIX_GEMM_P32_K" { print $2 }' .config)
selected_presets=$(awk -F= '$1 ~ /^CONFIG_MATRIX_GEMM_P32_PRESET_/ && $2 == "y" { count += 1 } END { print count + 0 }' .config)
test "$selected_presets" -eq 1
test -n "$matrix_m"
test -n "$matrix_n"
test -n "$matrix_k"
matrix_outputs=$((matrix_m * matrix_n))
vector_groups=$(((matrix_k + 3) / 4))
expected_requests=$((matrix_outputs * vector_groups))
expected_valid_mac_terms=$((matrix_outputs * matrix_k))
expected_lane_utilization=$(awk -v k="$matrix_k" -v groups="$vector_groups" 'BEGIN { printf "%.6f", k / (4 * groups) }')

make run >"$test_output"

rg -x -F "Matrix GEMM P32 benchmark" "$test_output"
rg -x -F "  M: $matrix_m" "$test_output"
rg -x -F "  N: $matrix_n" "$test_output"
rg -x -F "  K: $matrix_k" "$test_output"
rg -x -F "  seed: 1" "$test_output"
rg -x -F "  requests: $expected_requests" "$test_output"
rg -x -F "  valid_mac_terms: $expected_valid_mac_terms" "$test_output"
rg -x '  cycles: [1-9][0-9]*' "$test_output"
actual_cycles=$(awk -F': ' '$1 == "  cycles" { print $2 }' "$test_output")
test -n "$actual_cycles"
expected_request_per_cycle=$(awk -v requests="$expected_requests" -v cycles="$actual_cycles" 'BEGIN { printf "%.6f", requests / cycles }')
expected_mac_per_cycle=$(awk -v terms="$expected_valid_mac_terms" -v cycles="$actual_cycles" 'BEGIN { printf "%.6f", terms / cycles }')
rg -x -F "  request_per_cycle: $expected_request_per_cycle" "$test_output"
rg -x -F "  mac_per_cycle: $expected_mac_per_cycle" "$test_output"
rg -x -F "  lane_utilization: $expected_lane_utilization" "$test_output"
rg -x '  max_chain_latency: [1-9][0-9]*' "$test_output"
actual_max_chain_latency=$(awk -F': ' '$1 == "  max_chain_latency" { print $2 }' "$test_output")
test -n "$actual_max_chain_latency"
rg -x -F "  exact_mismatches: 0" "$test_output"
rg -F "M,N,K,seed,requests,valid_mac_terms,cycles,request_per_cycle,mac_per_cycle,lane_utilization,max_chain_latency,exact_mismatches" "$test_output"
expected_csv="$matrix_m,$matrix_n,$matrix_k,1,$expected_requests,$expected_valid_mac_terms,$actual_cycles,$expected_request_per_cycle,$expected_mac_per_cycle,$expected_lane_utilization,$actual_max_chain_latency,0"
rg -x -F "$expected_csv" "$test_output"
