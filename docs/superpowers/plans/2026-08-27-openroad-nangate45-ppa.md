# DFPVU Nangate45 OpenROAD PPA Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create and run a reproducible Nangate45 ORFS physical-design flow for the default DFPVU `PvuTop` RTL, then publish post-route PPA data with clear power assumptions.

**Architecture:** Keep all DFPVU-owned inputs, wrappers and outputs under `DFPVU/openroad/`; invoke the external ORFS checkout through its stable Make interface using an absolute `DESIGN_CONFIG`.  ORFS produces its native logs, reports and artifacts, then a small DFPVU Python utility validates and reduces ORFS `genMetrics.py` JSON into a human-readable Markdown and machine-readable JSON summary.

**Tech Stack:** SystemVerilog; Tcl SDC; GNU Make; OpenROAD-flow-scripts; OpenROAD/Yosys; Python 3 standard library; Nangate Open Cell Library.

**Spec:** `docs/superpowers/specs/2026-08-27-openroad-nangate45-ppa-design.md`

## Global Constraints

- The design under test is generated default `PvuTop`; its RTL inputs are `vsrc/PvuTop.sv` and `src/main/resources/pvu/lzc.sv`.
- Do not hand-edit generated `vsrc/PvuTop.sv`.
- Use the ORFS `nangate45` platform and maintain the PPA flow separately from Chisel elaboration and Verilator regression.
- Treat `PvuTop` as a combinational design: use virtual-clock I/O timing, not register-to-register timing.
- Default to a 10 ns virtual-clock period with 20% input and output delay budgets; exclude `clock` and `reset` from data-path input constraints.
- Label baseline dynamic power as a Liberty/default-activity estimate; it is not workload-derived power.
- Preserve native ORFS logs/reports for every pass or failure; do not publish incomplete metrics as final PPA data.

---

## Planned File Structure

| File | Responsibility |
| --- | --- |
| `openroad/nangate45/config.mk` | External ORFS design declaration, RTL list, technology selection and repeatable physical-design defaults. |
| `openroad/nangate45/constraint.sdc` | Virtual-clock input/output timing constraints for the combinational top. |
| `openroad/run_nangate45_ppa.sh` | Validates tool paths, runs ORFS, invokes ORFS metrics collection and records DFPVU revision. |
| `openroad/summarize_ppa.py` | Validates the final ORFS metrics and emits portable `ppa-summary.json` and `ppa-summary.md`. |
| `openroad/test_summarize_ppa.py` | Standard-library unit tests for success and incomplete-flow rejection. |
| `openroad/results/` | Ignored run outputs: ORFS work tree, raw `metrics.json`, final summaries and logs. |

### Task 1: Establish the external ORFS design and timing contract

**Files:**

- Create: `openroad/nangate45/config.mk`
- Create: `openroad/nangate45/constraint.sdc`

**Interfaces:**

- Consumes: `OPENROAD_FLOW_HOME` (external ORFS checkout), `DFPVU_ROOT` inferred from the config directory, `PPA_CLOCK_PERIOD_NS` supplied by the runner.
- Produces: a config accepted by `make -C "$OPENROAD_FLOW_HOME/flow" DESIGN_CONFIG=<absolute-config> all` and a constrained virtual clock named `dfpvu_vclk`.

- [ ] **Step 1: Write the config and constraints**

  Create `config.mk` with an inferred project root and an explicit source list:

  ```make
  export DESIGN_NAME = PvuTop
  export DESIGN_NICKNAME = dfpvu
  export PLATFORM = nangate45

  DFPVU_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
  export VERILOG_FILES = $(DFPVU_ROOT)/vsrc/PvuTop.sv \
                         $(DFPVU_ROOT)/src/main/resources/pvu/lzc.sv
  export SDC_FILE = $(DFPVU_ROOT)/openroad/nangate45/constraint.sdc

  export CORE_UTILIZATION ?= 55
  export PLACE_DENSITY_LB_ADDON = 0.20
  export TNS_END_PERCENT = 100
  export SYNTH_REPEATABLE_BUILD ?= 1
  export ABC_AREA = 1
  export ADDER_MAP_FILE :=
  ```

  Create `constraint.sdc` with these exact constraints:

  ```tcl
  current_design PvuTop

  if {![info exists ::env(PPA_CLOCK_PERIOD_NS)]} {
    set ::env(PPA_CLOCK_PERIOD_NS) 10.0
  }
  set clk_period $::env(PPA_CLOCK_PERIOD_NS)
  set io_delay [expr {$clk_period * 0.20}]

  create_clock -name dfpvu_vclk -period $clk_period
  set data_inputs [remove_from_collection [all_inputs] [get_ports {clock reset}]]
  set_input_delay $io_delay -clock dfpvu_vclk $data_inputs
  set_output_delay $io_delay -clock dfpvu_vclk [all_outputs]
  set_false_path -from [get_ports {clock reset}]
  ```

- [ ] **Step 2: Perform static interface checks**

  Run the following and require zero exit status:

  ```bash
  rg -n '^module PvuTop' vsrc/PvuTop.sv
  rg -n '^module LZC|^package cf_math_pkg' src/main/resources/pvu/lzc.sv
  rg -n 'PvuTop\.sv|lzc\.sv|PLATFORM = nangate45' openroad/nangate45/config.mk
  rg -n 'create_clock -name dfpvu_vclk|remove_from_collection.*clock reset' openroad/nangate45/constraint.sdc
  ```

- [ ] **Step 3: Dry-run the ORFS flow invocation**

  Run:

  ```bash
  make -C /root/openroad/flow -n \
    DESIGN_CONFIG=/root/DFPVU/openroad/nangate45/config.mk \
    WORK_HOME=/root/DFPVU/openroad/results/work \
    PPA_CLOCK_PERIOD_NS=10.0 all
  ```

  Expected: Make expands a `PvuTop` Nangate45 flow and references both absolute RTL inputs.  It must not report a missing `PLATFORM`, `DESIGN_NAME`, SDC or source file.

- [ ] **Step 4: Commit the timing contract**

  ```bash
  git add openroad/nangate45/config.mk openroad/nangate45/constraint.sdc
  git commit -m "feat: add Nangate45 PPA flow constraints"
  ```

### Task 2: Implement validated PPA-summary generation

**Files:**

- Create: `openroad/summarize_ppa.py`
- Create: `openroad/test_summarize_ppa.py`

**Interfaces:**

- Consumes: `--metrics <ORFS genMetrics JSON>`, `--output-json <path>`, `--output-md <path>`, `--dfpvu-revision <SHA>`.
- Produces: JSON fields `status`, `run`, `area`, `timing`, `power`, `quality`, and Markdown containing the same values and the default-activity power caveat.

- [ ] **Step 1: Write a failing unit test for successful extraction**

  In `test_summarize_ppa.py`, create a temporary metrics JSON with the following mandatory representative fields and assert `main()` returns `0`, writes both outputs, and marks power as estimated:

  ```python
  metrics = {
      "run__flow__platform": "nangate45",
      "run__flow__openroad_commit": "abc123",
      "finish__design__instance__area": 1234.5,
      "finish__design__instance__count__stdcell": 321,
      "finish__timing__setup__ws": 1.25,
      "finish__timing__setup__tns": 0.0,
      "finish__power__total": 0.0042,
      "finish__power__internal": 0.0020,
      "finish__power__switching": 0.0015,
      "finish__power__leakage": 0.0007,
      "detailedroute__route__drc_errors": 0,
      "detailedroute__antenna__violating__nets": 0,
  }
  ```

- [ ] **Step 2: Run the test to verify it fails**

  Run:

  ```bash
  python3 -m unittest openroad/test_summarize_ppa.py -v
  ```

  Expected: FAIL because `openroad.summarize_ppa` does not exist.

- [ ] **Step 3: Implement the minimal extractor and failure validation**

  Implement `summarize_ppa.py` with `argparse`, `json`, `pathlib` and no third-party packages.  Require these fields: platform, final area, standard-cell count, setup WNS/TNS, total/internal/switching/leakage power, detailed-route DRC count, and antenna-net count.  If a mandatory field is absent or has the ORFS sentinel values `ERR`/`N/A`, print the missing field names to stderr and return `2` without writing a success summary.

  Use the following output skeleton:

  ```python
  summary = {
      "status": "complete",
      "run": {
          "platform": metrics["run__flow__platform"],
          "openroad_commit": metrics.get("run__flow__openroad_commit", "unknown"),
          "dfpvu_revision": args.dfpvu_revision,
          "power_activity": "ORFS/OpenSTA default activity; not workload-derived",
      },
      "area": {"instance_area_um2": metrics["finish__design__instance__area"],
               "stdcell_count": metrics["finish__design__instance__count__stdcell"]},
      "timing": {"setup_wns_ns": metrics["finish__timing__setup__ws"],
                 "setup_tns_ns": metrics["finish__timing__setup__tns"]},
      "power": {"total": metrics["finish__power__total"],
                "internal": metrics["finish__power__internal"],
                "switching": metrics["finish__power__switching"],
                "leakage": metrics["finish__power__leakage"]},
      "quality": {"drc_errors": metrics["detailedroute__route__drc_errors"],
                  "antenna_violating_nets": metrics["detailedroute__antenna__violating__nets"]},
  }
  ```

- [ ] **Step 4: Extend the test for incomplete flows and run all tests**

  Add a test deleting `finish__power__total`; assert return code `2` and that no output JSON is created.  Run:

  ```bash
  python3 -m unittest openroad/test_summarize_ppa.py -v
  ```

  Expected: both tests PASS.

- [ ] **Step 5: Commit the report utility**

  ```bash
  git add openroad/summarize_ppa.py openroad/test_summarize_ppa.py
  git commit -m "feat: summarize OpenROAD PPA metrics"
  ```

### Task 3: Add a reproducible runner and verify its preflight checks

**Files:**

- Create: `openroad/run_nangate45_ppa.sh`
- Modify: `.gitignore` (add only `openroad/results/` if it is not already ignored)

**Interfaces:**

- Consumes: optional `OPENROAD_FLOW_HOME` (defaults to `/root/openroad`), optional `PPA_CLOCK_PERIOD_NS` (defaults to `10.0`), optional `NUM_CORES` (defaults to host core count).
- Produces: raw outputs below `openroad/results/work/`, `openroad/results/metrics.json`, `openroad/results/ppa-summary.json`, and `openroad/results/ppa-summary.md`.

- [ ] **Step 1: Write a failing preflight test**

  Run the absent-tool case with a temporary empty directory:

  ```bash
  OPENROAD_FLOW_HOME=/tmp/no-openroad-flow bash openroad/run_nangate45_ppa.sh
  ```

  Expected: non-zero exit and a message naming the missing `flow/Makefile`; no directory under `openroad/results/` is created.

- [ ] **Step 2: Implement the runner**

  Create a Bash script with `set -euo pipefail`.  It must validate `flow/Makefile`, `tools/install/OpenROAD/bin/openroad`, and `tools/install/yosys/bin/yosys` before creating output paths.  It must run these commands in order:

  ```bash
  make -C "$flow_home/flow" \
    DESIGN_CONFIG="$dfpvu_root/openroad/nangate45/config.mk" \
    WORK_HOME="$result_root/work" \
    PPA_CLOCK_PERIOD_NS="$period" \
    NUM_CORES="$cores" all

  OPENROAD_EXE="$flow_home/tools/install/OpenROAD/bin/openroad" \
  PLATFORM_DIR="$flow_home/flow/platforms/nangate45" \
  python3 "$flow_home/flow/util/genMetrics.py" -x \
    --platform nangate45 --design dfpvu --flowVariant base \
    --logs "$result_root/work/logs/nangate45/dfpvu/base" \
    --reports "$result_root/work/reports/nangate45/dfpvu/base" \
    --results "$result_root/work/results/nangate45/dfpvu/base" \
    --output "$result_root/metrics.json"

  python3 "$dfpvu_root/openroad/summarize_ppa.py" \
    --metrics "$result_root/metrics.json" \
    --output-json "$result_root/ppa-summary.json" \
    --output-md "$result_root/ppa-summary.md" \
    --dfpvu-revision "$(git -C "$dfpvu_root" rev-parse HEAD)"
  ```

  Add `openroad/results/` to `.gitignore` so physical-design artifacts are never staged.

- [ ] **Step 3: Run preflight tests**

  Run the absent-tool command from Step 1, then run:

  ```bash
  bash -n openroad/run_nangate45_ppa.sh
  git check-ignore -v openroad/results/placeholder
  ```

  Expected: the first command fails safely; syntax validation passes; the ignored-path check reports the new ignore rule.

- [ ] **Step 4: Commit the runner**

  ```bash
  git add .gitignore openroad/run_nangate45_ppa.sh
  git commit -m "feat: add reproducible Nangate45 PPA runner"
  ```

### Task 4: Build OpenROAD, execute the baseline and publish the measured data

**Files:**

- Create: `openroad/results/metrics.json` (ignored)
- Create: `openroad/results/ppa-summary.json` (ignored)
- Create: `openroad/results/ppa-summary.md` (ignored)
- Create: `openroad/results/work/` (ignored ORFS artifacts)

**Interfaces:**

- Consumes: Tasks 1–3, the DFPVU RTL and a successfully built ORFS OpenROAD/Yosys installation.
- Produces: a complete, inspectable Nangate45 post-route PPA result or a failed-stage report with preserved logs.

- [ ] **Step 1: Establish the functional RTL baseline**

  Run the repository-supported regression:

  ```bash
  make config.h
  make run
  ```

  Expected: the selected Verilator C++ regression completes without a mismatch.  Record the chosen `.config` in the final PPA handoff.

- [ ] **Step 2: Build missing ORFS executables without destructive cleanup**

  If either `tools/install/OpenROAD/bin/openroad` or `tools/install/yosys/bin/yosys` is absent, run:

  ```bash
  ./build_openroad.sh --local --threads 16
  ```

  Do not pass `--clean` or `--clean-force`.  On completion, verify:

  ```bash
  tools/install/OpenROAD/bin/openroad -version
  tools/install/yosys/bin/yosys -V
  ```

- [ ] **Step 3: Run the 10 ns baseline**

  Run:

  ```bash
  PPA_CLOCK_PERIOD_NS=10.0 NUM_CORES=16 bash openroad/run_nangate45_ppa.sh
  ```

  Expected: ORFS `all` reaches `finish`, and the runner writes raw metrics plus both PPA summaries.

- [ ] **Step 4: Validate completed output before reporting values**

  Run:

  ```bash
  python3 -m json.tool openroad/results/ppa-summary.json >/dev/null
  sed -n '1,220p' openroad/results/ppa-summary.md
  test -f openroad/results/work/reports/nangate45/dfpvu/base/6_finish.rpt
  ```

  Expected: valid JSON, a complete Markdown summary, and the final ORFS timing/power report.  Confirm that DRC and antenna counts are zero before calling the result a successful physical implementation.

- [ ] **Step 5: Report the baseline without committing generated artifacts**

  Present area, standard-cell count, WNS/TNS, power components, DRC/antenna status, target period and the exact default-activity caveat.  Link the ignored local summary and raw ORFS report paths; leave `openroad/results/` untracked.

## Plan Self-Review

- **Spec coverage:** Task 1 implements the Nangate45 source set and virtual I/O timing; Task 2 implements reproducible metric reduction and invalid-run rejection; Task 3 preserves a portable runner and output isolation; Task 4 establishes functional validity, builds the tools, runs physical implementation and validates the requested PPA/quality data.
- **Placeholder scan:** No `TBD`, `TODO`, deferred implementation, undefined interface or generic test instruction remains.  Every new executable interface and command is specified above.
- **Consistency:** `PPA_CLOCK_PERIOD_NS`, `OPENROAD_FLOW_HOME`, `openroad/results/`, the `dfpvu` design nickname and `base` variant are used consistently in every task and command.
