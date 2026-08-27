#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dfpvu_root="$(cd "$script_dir/.." && pwd)"
flow_home="${OPENROAD_FLOW_HOME:-/root/openroad}"
period="${PPA_CLOCK_PERIOD_NS:-10.0}"
cores="${NUM_CORES:-$(nproc)}"
result_root="$dfpvu_root/openroad/results"

require_file() {
  local path="$1"
  if [[ ! -f "$path" ]]; then
    printf 'Missing required file: %s\n' "$path" >&2
    exit 2
  fi
}

require_executable() {
  local path="$1"
  if [[ ! -x "$path" ]]; then
    printf 'Missing required executable: %s\n' "$path" >&2
    exit 2
  fi
}

select_python_executable() {
  local candidate
  local candidate_path
  local -a candidates

  if [[ -n "${PYTHON_EXE:-}" ]]; then
    candidates=("$PYTHON_EXE")
  else
    candidates=(python3.13 python3.12 python3.11 python3.10 python3)
  fi

  for candidate in "${candidates[@]}"; do
    candidate_path="$(command -v "$candidate" 2>/dev/null || true)"
    if [[ -x "$candidate_path" ]] &&
      "$candidate_path" -c 'import sys; raise SystemExit(sys.version_info < (3, 10))'
    then
      printf '%s\n' "$candidate_path"
      return 0
    fi
  done

  printf 'Python 3.10 or newer is required by this OpenROAD flow.\n' >&2
  return 2
}

require_file "$flow_home/flow/Makefile"
require_executable "$flow_home/tools/install/OpenROAD/bin/openroad"
require_executable "$flow_home/tools/install/yosys/bin/yosys"
python_exe="$(select_python_executable)"

mkdir -p "$result_root"

make -C "$flow_home/flow" \
  DESIGN_CONFIG="$dfpvu_root/openroad/nangate45/config.mk" \
  WORK_HOME="$result_root/work" \
  PPA_CLOCK_PERIOD_NS="$period" \
  PYTHON_EXE="$python_exe" \
  NUM_CORES="$cores" all

OPENROAD_EXE="$flow_home/tools/install/OpenROAD/bin/openroad" \
PLATFORM_DIR="$flow_home/flow/platforms/nangate45" \
"$python_exe" "$flow_home/flow/util/genMetrics.py" -x \
  --platform nangate45 --design dfpvu --flowVariant base \
  --logs "$result_root/work/logs/nangate45/dfpvu/base" \
  --reports "$result_root/work/reports/nangate45/dfpvu/base" \
  --results "$result_root/work/results/nangate45/dfpvu/base" \
  --output "$result_root/metrics.json"

"$python_exe" "$dfpvu_root/openroad/summarize_ppa.py" \
  --metrics "$result_root/metrics.json" \
  --output-json "$result_root/ppa-summary.json" \
  --output-md "$result_root/ppa-summary.md" \
  --dfpvu-revision "$(git -C "$dfpvu_root" rev-parse HEAD)"
