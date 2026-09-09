#!/usr/bin/env bash
# Regression: serializing every P32 MAC response must not return unnoticed.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
runner=${1:-"$repo_root/obj_dir/VPvuTop"}
output=$(mktemp)
trap 'rm -f "$output"' EXIT

"$runner" "$repo_root/test_src/qwen3-p32-fixture" >"$output"

rg -q '^  overall: elements=24 exact mismatches=0$' "$output"
overall=$(rg '^  overall: requests=.*requests/cycle=' "$output")
requests_per_cycle=$(sed -E 's/.*requests\/cycle=([0-9.]+).*/\1/' <<<"$overall")

# The former response-serialized driver reported 0.142857 requests/cycle on
# this fixture.  Four independent elements per module should hide the MAC
# response latency by a clear margin while retaining exact raw-P32 results.
awk -v actual="$requests_per_cycle" 'BEGIN {
  if (actual < 0.350000) {
    printf("fixture scheduler throughput too low: %f requests/cycle\\n", actual) > "/dev/stderr";
    exit 1;
  }
}'

printf 'fixture P32 MAC scheduler: exact mismatches=0 requests/cycle=%s\n' \
  "$requests_per_cycle"
