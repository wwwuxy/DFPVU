#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
ara_root=/root/ara
app_name=dfpvu_workload_compare
app_dir="$ara_root/apps/$app_name"
output=$(mktemp)
trap 'rm -f "$output"' EXIT

python3 "$repo_root/tools/generate_ara_workload_data.py" \
  --trace "$repo_root/test_src/qwen3-p32-fixture" \
  --add-trace "$repo_root/test_src/llm-p32-add-fixture" \
  --output "$app_dir/generated_data.h"

# generated_data.h is intentionally replaced above but is not declared by ARA's
# generic Makefile rule, so force recompilation to avoid a stale ELF.
make -B -C "$ara_root/apps" --no-print-directory "bin/$app_name.spike"
"$ara_root/install/riscv-isa-sim/bin/spike" \
  --isa=rv64gcv_zfh_zvfh_zvl4096b \
  "$ara_root/apps/bin/$app_name.spike" >"$output"

rg -F 'ARA_WORKLOAD_CSV,legacy-qwen3-v1,mac,rvv-fp32-direct' "$output"
rg -F 'ARA_WORKLOAD_CSV,legacy-qwen3-v1,dot,rvv-fp32-direct' "$output"
rg -F 'ARA_WORKLOAD_CSV,legacy-qwen3-v1,multiply,rvv-fp32-direct' "$output"
rg -F 'ARA_WORKLOAD_CSV,legacy-qwen3-v1,to_int,rvv-fp32-direct' "$output"
rg -F 'ARA_WORKLOAD_CSV,legacy-qwen3-v1,add,rvv-fp32-direct' "$output"
rg -F 'ARA_WORKLOAD_CSV,legacy-qwen3-v1,p16_proxy,rvv-fp32-to-fp16-proxy' "$output"
rg -F ',PASS' "$output"

printf 'ARA workload fixture integration passed\n'
