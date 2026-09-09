#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
make -C "$repo_root" -n menuconfig | rg "^menuconfig$"
actual=$(make -C "$repo_root" -n run "CONFIG_QWEN3_P32_MAC_WORKLOAD=y" "CONFIG_QWEN3_P32_MAC_TRACE_DIR=/tmp/qwen;echo injected" | rg "^./obj_dir/VPvuTop")
expected="./obj_dir/VPvuTop \"/tmp/qwen;echo injected\""

if [[ "$actual" != "$expected" ]]; then
  printf "expected shell-quoted Kconfig trace argument:\n  %s\nactual:\n  %s\n" "$expected" "$actual" >&2
  exit 1
fi

printf "Qwen3 Kconfig trace argument is shell-quoted\n"
