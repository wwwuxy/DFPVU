#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_root="$(mktemp -d)"
trap 'rm -rf "$tmp_root"' EXIT

fake_repo="$tmp_root/repo"
flow_home="$tmp_root/openroad-flow"
runner="$fake_repo/openroad/run_nangate45_ppa.sh"

mkdir -p "$fake_repo/openroad" \
  "$flow_home/flow" \
  "$flow_home/tools/install/OpenROAD/bin" \
  "$flow_home/tools/install/yosys/bin"
cp "$repo_root/openroad/run_nangate45_ppa.sh" "$runner"
touch "$flow_home/flow/Makefile" \
  "$flow_home/tools/install/OpenROAD/bin/openroad" \
  "$flow_home/tools/install/yosys/bin/yosys"

assert_rejected() {
  local tool_path="$1"
  local output
  local status

  if output=$(OPENROAD_FLOW_HOME="$flow_home" bash "$runner" 2>&1); then
    printf 'Expected preflight failure for %s\n' "$tool_path" >&2
    return 1
  else
    status=$?
  fi

  test "$status" -eq 2
  [[ "$output" == *"Missing required executable: $tool_path"* ]]
  test ! -e "$fake_repo/openroad/results"
}

assert_rejected "$flow_home/tools/install/OpenROAD/bin/openroad"
chmod +x "$flow_home/tools/install/OpenROAD/bin/openroad"
assert_rejected "$flow_home/tools/install/yosys/bin/yosys"
