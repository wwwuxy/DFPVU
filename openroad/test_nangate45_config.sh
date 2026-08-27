#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

actual="$({
  cat <<'MAKEFILE'
include $(CONFIG_MK)

.PHONY: print-synthesis-config
print-synthesis-config:
	@printf '%s\n' '$(ABC_CLOCK_PERIOD_IN_PS)'
	@printf '%s\n' '$(SYNTH_HDL_FRONTEND)'
	@printf '%s\n' '$(VERILOG_FILES)'
MAKEFILE
} | PPA_CLOCK_PERIOD_NS=7.5 make --no-print-directory -s \
  CONFIG_MK="$repo_root/openroad/nangate45/config.mk" \
  -f - print-synthesis-config)"

expected="$(printf '7500\nslang\n%s' "$repo_root/vsrc/PvuTop.sv")"
if [[ "$actual" != "$expected" ]]; then
  printf 'Unexpected Nangate45 synthesis config:\n%s\n' "$actual" >&2
  exit 1
fi
