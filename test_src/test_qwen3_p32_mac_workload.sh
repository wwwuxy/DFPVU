#!/usr/bin/env bash
# Regression: response serialization and accumulator chaining must not regress.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
runner=${1:-"$repo_root/obj_dir/VPvuTop"}
base_fixture="$repo_root/test_src/qwen3-p32-fixture"
k8_metadata="$repo_root/test_src/qwen3-p32-k8-fixture/metadata.json"
output=$(mktemp)
k8_fixture=$(mktemp -d)
k8_output=$(mktemp)
trap 'rm -f "$output" "$k8_output"; rm -rf "$k8_fixture"' EXIT

"$runner" "$base_fixture" >"$output"

rg -q '^  overall: elements=24 exact mismatches=0$' "$output"
rg -q '^Workload summary$' "$output"
rg -q '^Precision conclusion$' "$output"
overall=$(rg '^  overall: requests=.*requests/cycle=' "$output")
requests_per_cycle=$(sed -E 's/.*requests\/cycle=([0-9.]+).*/\1/' <<<"$overall")

# The former response-serialized driver reported 0.142857 requests/cycle on
# this fixture. Four independent elements per module should hide MAC response
# latency by a clear margin while retaining exact raw-P32 results.
awk -v actual="$requests_per_cycle" 'BEGIN {
  if (actual < 0.350000) {
    printf("fixture scheduler throughput too low: %f requests/cycle\\n", actual) > "/dev/stderr";
    exit 1;
  }
}'

# The checked-in K=8 metadata is paired with temporary, byte-for-byte P32
# operands made by repeating the finite first K=4 group. Every module has one
# selected element and exactly two op=11 groups. Exact SoftPosit conformance
# therefore fails if group two does not consume group one's raw response.
cp "$k8_metadata" "$k8_fixture/metadata.json"
for module in q_proj k_proj v_proj gate_proj up_proj down_proj; do
  dd if="$base_fixture/$module.input.f32" of="$k8_fixture/$module.input.f32" bs=16 count=1 status=none
  dd if="$base_fixture/$module.input.f32" of="$k8_fixture/$module.input.f32" bs=16 seek=1 count=1 conv=notrunc status=none
  dd if="$base_fixture/$module.weight.f32" of="$k8_fixture/$module.weight.f32" bs=16 count=1 status=none
  dd if="$base_fixture/$module.weight.f32" of="$k8_fixture/$module.weight.f32" bs=16 seek=1 count=1 conv=notrunc status=none
  dd if="$base_fixture/$module.output.f32" of="$k8_fixture/$module.output.f32" bs=4 count=1 status=none
done

if ! "$runner" "$k8_fixture" 1 1 >"$k8_output"; then
  cat "$k8_output" >&2
  exit 1
fi
rg -q '^  overall: elements=6 exact mismatches=0$' "$k8_output"
rg -q '^  overall: requests=12 mac terms=48 cycles=.*requests/cycle=' "$k8_output"

printf 'fixture P32 MAC scheduler: exact mismatches=0 requests/cycle=%s; K=8 recurrence exact=0 requests=12\n' "$requests_per_cycle"
