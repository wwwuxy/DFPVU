#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_root="$(mktemp -d)"
trap 'rm -rf "$tmp_root"' EXIT

fake_repo="$tmp_root/repo"
flow_home="$tmp_root/openroad-flow"
bin_dir="$tmp_root/bin"
runner="$fake_repo/openroad/run_nangate45_ppa.sh"
python312="$bin_dir/python3.12"

mkdir -p "$fake_repo/openroad" \
  "$flow_home/flow/util" \
  "$flow_home/tools/install/OpenROAD/bin" \
  "$flow_home/tools/install/yosys/bin" \
  "$bin_dir"
cp "$repo_root/openroad/run_nangate45_ppa.sh" "$runner"
cp "$repo_root/openroad/summarize_ppa.py" "$fake_repo/openroad/summarize_ppa.py"

touch "$flow_home/tools/install/OpenROAD/bin/openroad" \
  "$flow_home/tools/install/yosys/bin/yosys"
chmod +x "$flow_home/tools/install/OpenROAD/bin/openroad" \
  "$flow_home/tools/install/yosys/bin/yosys"

printf '%s\n' \
  '.PHONY: all' \
  'all:' \
  $'\t@test "$(PYTHON_EXE)" = "$(EXPECTED_PYTHON)"' \
  > "$flow_home/flow/Makefile"

printf '%s\n' \
  '#!/usr/bin/env python3' \
  'import json' \
  'from pathlib import Path' \
  'import sys' \
  'output = sys.argv[sys.argv.index("--output") + 1]' \
  'logs = Path(sys.argv[sys.argv.index("--logs") + 1])' \
  'metrics = {' \
  '  "run__flow__platform": "nangate45",' \
  '  "finish__design__instance__area": 1.0,' \
  '  "finish__design__instance__count__stdcell": 1,' \
  '  "finish__timing__setup__ws": 0.0,' \
  '  "finish__timing__setup__tns": 0.0,' \
  '  "finish__power__total": 1.0,' \
  '  "finish__power__internal": 1.0,' \
  '  "finish__power__switching": 0.0,' \
  '  "finish__power__leakage": 0.0,' \
  '  "detailedroute__route__drc_errors": 0,' \
  '  "detailedroute__antenna__violating__nets": 0,' \
  '}' \
  'logs.mkdir(parents=True, exist_ok=True)' \
  '(logs / "5_1_grt.log").write_text("[INFO GRT-0096] Final congestion report:\nTotal 100 50 50.00% 0 / 0 / 0\n", encoding="utf-8")' \
  'with open(output, "w", encoding="utf-8") as stream:' \
  '    json.dump(metrics, stream)' \
  > "$flow_home/flow/util/genMetrics.py"

printf '%s\n' \
  '#!/usr/bin/env bash' \
  'if [[ "${1:-}" == "-c" ]]; then exit 0; fi' \
  'exec /bin/python3 "$@"' \
  > "$python312"
chmod +x "$python312"

printf '%s\n' \
  '#!/usr/bin/env bash' \
  'case "$*" in' \
  '  *openroad-flow/tools/OpenROAD*) printf "%s\\n" feedface ;;' \
  '  *openroad-flow*) printf "%s\\n" cafebabe ;;' \
  '  *) printf "%s\\n" deadbeef ;;' \
  'esac' \
  > "$bin_dir/git"
chmod +x "$bin_dir/git"

EXPECTED_PYTHON="$python312" \
PATH="$bin_dir:/bin:/usr/bin" \
OPENROAD_FLOW_HOME="$flow_home" \
PPA_CLOCK_PERIOD_NS=7.5 \
bash "$runner"

test -f "$fake_repo/openroad/results/ppa-summary.json"
python312_summary="$fake_repo/openroad/results/ppa-summary.json"
/bin/python3 - "$python312_summary" <<'PY'
import json
import sys

summary = json.load(open(sys.argv[1], encoding="utf-8"))
assert summary["run"]["dfpvu_revision"] == "deadbeef"
assert summary["run"]["openroad_revision"] == "feedface"
assert summary["run"]["orfs_revision"] == "cafebabe"
assert summary["run"]["target_period_ns"] == 7.5
assert summary["power"]["units"] == "W"
assert summary["quality"]["routing_overflow"] == 0
PY
