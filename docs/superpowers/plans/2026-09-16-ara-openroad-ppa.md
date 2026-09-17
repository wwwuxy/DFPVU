# Ara 4-Lane OpenROAD PPA Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generate reproducible, clock-constrained OpenROAD/Nangate45 area, timing, routing, and vectorless-power results for the upstream Ara vector accelerator at `NrLanes=4` and `VLEN=4096`, then report them beside the existing DFPVU physical-cost baseline without claiming an unfair like-for-like ratio.

**Architecture:** A small, versioned OpenROAD harness under `openroad/ara_4lane/` will obtain Ara's ordered RTL/dependency file list from its pinned Bender manifest, bind the top-level configuration explicitly, and use the same Nangate45 platform, 45 ns target clock, and 55% target core utilization as the DFPVU baseline. A transparent elaboration wrapper is permitted only if the native `ara` top cannot bind its typed CVA6 interfaces directly; its module instance and all parameter/type bindings must be documented. The result document will retain raw report paths and label differences in design scope.

**Tech Stack:** Ara/Bender/SystemVerilog, OpenROAD-flow-scripts, Yosys/Slang, Nangate45, Tcl, POSIX shell, Python unittest.

**Spec:** `docs/superpowers/specs/2026-09-16-ara-openroad-ppa-design.md`

## Global Constraints

- Preserve all existing and untracked workspace files; do not reset, clean, or commit the dirty worktree.
- Use the already selected physical-comparison contract: Nangate45, 45 ns clock period, and 55% target core utilization.
- Synthesize Ara in the `NrLanes=4`, `VLEN=4096` configuration using Bender's dependency ordering and the target tags used by Ara's hardware Makefile.
- Clock only a real propagated design clock port (`clk_i` for Ara); never use a virtual clock.
- Retain Ara's native typed `ara` top whenever it elaborates. If a wrapper is required, make it a one-instance pass-through with no functional logic and document its port/type/parameter bindings.
- Run no workload simulation or switching-activity estimation. Label any power number as OpenROAD vectorless estimate, not workload power.
- Capture raw report file paths, exact tool command, and all quality caveats. Never report a PPA comparison percentage unless top-level scope, physical contract, and quality gates permit it.
- Route only as far as available resources permit. A failed or incomplete route is a reported result, not a reason to invent a number.

---

## Task 1: Establish a reproducible Ara source manifest and configuration binding

**Files:**
- Create: `openroad/ara_4lane/generate_filelist.sh`
- Create: `openroad/ara_4lane/README.md`
- Create: `openroad/ara_4lane/ara_4lane.flist` (generated artifact)
- Create: `openroad/test_ara_4lane_filelist.sh`
- Inspect: `/root/ara/Bender.yml`, `/root/ara/hardware/Makefile`, `/root/ara/hardware/src/ara.sv`, `/root/ara/hardware/src/ara_system.sv`

- [ ] **Step 1: Write a failing manifest test.**

  Create `openroad/test_ara_4lane_filelist.sh` with checks that the generated file list is non-empty, contains `ara_pkg.sv` before `ara.sv`, has no duplicate source paths, and records the exact four-lane/4096-bit Bender defines. Run it before creating the generator and capture the expected missing-file failure.

- [ ] **Step 2: Implement Bender-driven source-list generation.**

  In `generate_filelist.sh`, run from `/root/ara/hardware`:

  ```bash
  ./bender script flist-plus \
    -t rtl \
    -t cv64a6_imafdcv_sv39 \
    -t tech_cells_generic_include_tc_sram \
    -t tech_cells_generic_include_tc_clk \
    -t exclude_first_pass_decoder \
    --define NR_LANES=4 \
    --define VLEN=4096 \
    --define ARIANE_ACCELERATOR_PORT=1 \
    --define COMMON_CELLS_ASSERTS_OFF
  ```

  Normalize the emitted result only as needed to make absolute source and include paths consumable by OpenROAD's selected SystemVerilog front end. Keep a comment header stating the Bender command and Ara revision. Do not hand-maintain dependency ordering.

- [ ] **Step 3: Inspect and record top-level parameter/type binding.**

  Identify how `ara_system` instantiates `ara`, including `NrLanes`, `VLEN`, CVA6 configuration, and typed interface parameters. Record the selected native-top binding approach in `README.md`. If direct `ara` elaboration with the Bender list cannot resolve those types, document the minimally transparent wrapper required for the exact binding.

- [ ] **Step 4: Run the manifest test.**

  ```bash
  bash openroad/ara_4lane/generate_filelist.sh
  bash openroad/test_ara_4lane_filelist.sh
  ```

  Expected: the file list is deterministic, has the expected ordering, and represents the specified Ara configuration.

## Task 2: Add a clock-constrained OpenROAD configuration with tests first

**Files:**
- Create: `openroad/ara_4lane/config.mk`
- Create: `openroad/ara_4lane/constraint.sdc`
- Create: `openroad/ara_4lane/ara_openroad_top.sv` (only if Task 1 proves a wrapper is required)
- Create: `openroad/test_ara_4lane_config.sh`
- Create: `openroad/test_ara_4lane_constraint_sdc.tcl`
- Reference: `openroad/nangate45/config.mk`, `openroad/nangate45/constraint.sdc`, `/root/openroad/flow/designs/nangate45/*/config.mk`

- [ ] **Step 1: Write failing configuration and SDC tests.**

  The shell test must require `DESIGN_NAME`, `PLATFORM = nangate45`, a 55% core-utilization setting, an RTL source-list mechanism, and a 45 ns clock-period definition. The Tcl test must read `constraint.sdc` after creating an in-memory `clk_i` port and fail unless `get_clocks` returns a clock attached to that real port. Run both tests before adding the config/SDC and capture the expected failures.

- [ ] **Step 2: Implement `config.mk`.**

  Base it on the established DFPVU Nangate45 configuration, changing only what Ara requires. Set:

  ```make
  export PLATFORM = nangate45
  export CLOCK_PERIOD = 45.0
  export CORE_UTILIZATION = 55
  ```

  Use the generated Bender list without reordering it. Select `SYNTH_HDL_FRONTEND = slang` if Ara packages/interfaces require it, and route generated include directories to the corresponding OpenROAD-flow-scripts variable. Preserve the direct `ara` top if feasible; otherwise use the documented transparent wrapper as `DESIGN_NAME`.

- [ ] **Step 3: Implement real-port SDC.**

  In `constraint.sdc`, use `create_clock -name ara_clk -period $clk_period [get_ports clk_i]`. Add input/output delay and uncertainty only when copied from the DFPVU baseline policy; do not introduce an unmatched timing assumption.

- [ ] **Step 4: Re-run the configuration tests.**

  ```bash
  bash openroad/test_ara_4lane_config.sh
  tclsh openroad/test_ara_4lane_constraint_sdc.tcl
  ```

  Expected: both pass, and the Tcl assertion proves that the physical clock constrains `clk_i`.

## Task 3: Prove elaboration and synthesis before physical implementation

**Files:**
- Create: `openroad/run_ara_4lane_ppa.sh`
- Modify: `openroad/ara_4lane/README.md`
- Output directory: `openroad/ara_4lane/work/`

- [ ] **Step 1: Add a runner with explicit stage selection.**

  `run_ara_4lane_ppa.sh` must accept one of `synth`, `floorplan`, `cts`, `grt`, or `route`, call `/root/openroad/flow/Makefile` with `DESIGN_CONFIG=.../config.mk`, and record the full command/output location. It must reject any unsupported stage rather than silently falling through.

- [ ] **Step 2: Run a clean synthesis feasibility check.**

  ```bash
  bash openroad/run_ara_4lane_ppa.sh synth
  ```

  Expected: the selected Ara top elaborates and yields synthesis area/cell reports. If compilation fails, preserve the first actionable diagnostic, trace it to file-list/type/parameter binding, apply the smallest fix from Tasks 1–2, and repeat. Do not change microarchitecture to silence a front-end error.

- [ ] **Step 3: Inspect synthesis artifacts.**

  Confirm design name, cell area, cell count, sequential-cell count, and available clock port in the generated reports/logs. Add raw paths and any wrapper disclosure to `README.md`.

## Task 4: Run the matched physical flow and capture quality status

**Files:**
- Modify: `openroad/ara_4lane/config.mk` (only if a documented flow setting is necessary)
- Modify: `openroad/ara_4lane/README.md`
- Output directory: `openroad/ara_4lane/work/`

- [ ] **Step 1: Execute floorplan and CTS.**

  ```bash
  bash openroad/run_ara_4lane_ppa.sh floorplan
  bash openroad/run_ara_4lane_ppa.sh cts
  ```

  Capture placed core/die area, CTS sink count, buffer count, WNS/TNS, and minimum period. If CTS cannot meet 45 ns, retain the negative slack and report it rather than relaxing the target clock.

- [ ] **Step 2: Execute global routing.**

  ```bash
  bash openroad/run_ara_4lane_ppa.sh grt
  ```

  Capture routing resource usage, overflow, post-GRT WNS/TNS/minimum period, and vectorless power if the OpenROAD flow emits it.

- [ ] **Step 3: Attempt bounded detailed routing.**

  Set only the previously disclosed bounded routing setting (for example `DETAILED_ROUTE_END_ITERATION = 1`) and run:

  ```bash
  bash openroad/run_ara_4lane_ppa.sh route
  ```

  Record DRC count by iteration, wire length, via count, runtime/resource failure, and whether the final route is DRC clean. If the host exhausts memory or time, stop the stage safely and report global-route metrics plus the exact limitation.

- [ ] **Step 4: Preserve exact provenance.**

  In `README.md`, list the Ara commit/hash (if available), Bender command, OpenROAD path/version output, platform, target utilization, target clock, top/wrapper identity, and relevant report paths.

## Task 5: Extract evidence-backed results and write the paper-ready comparison

**Files:**
- Create: `docs/result/ara_openroad_ppa_45ns.md`
- Create: `docs/result/ara_openroad_ppa_45ns_zh.md`
- Modify: `docs/result/dfpvu_openroad_ppa_45ns.md`
- Modify: `docs/result/dfpvu_openroad_ppa_45ns_zh.md`
- Modify: `plan/experiment-protocol.md`
- Modify: `tables/table-schema.md`

- [ ] **Step 1: Add parser tests before writing extraction logic.**

  Extend the existing PPA summary test suite (or add a focused Ara test) with small report fixtures that assert extraction of mapped standard-cell area, cell count, sequential-cell count, core/die area, timing, congestion/overflow, DRC, and vectorless power. Include a fixture without detailed-route output; it must emit `N/A`, never a zero.

- [ ] **Step 2: Extract directly from raw reports.**

  Write `ara_openroad_ppa_45ns.md` with a source-artifact table and a result table. Every value must identify its flow stage/report. Use `N/A` for unavailable metrics and distinguish target core utilization from final realized core/die utilization.

- [ ] **Step 3: Write the Chinese paper-facing version.**

  `ara_openroad_ppa_45ns_zh.md` must provide concise IPDPS-ready wording: methodology, raw values, quality gates, and limitation statement. It must say “向量无关功耗估计” for vectorless power and avoid workload-energy claims.

- [ ] **Step 4: Add a conservative DFPVU/Ara side-by-side table.**

  Append a matched-contract table to the DFPVU English and Chinese result documents. It may compare raw physical metrics, but must prominently state that DFPVU `PvuTop` and upstream Ara's selected accelerator/top-wrapper have different integration boundaries; therefore no area/power improvement percentage is claimed unless the top-level scope is demonstrated equivalent.

- [ ] **Step 5: Update experiment/schema provenance.**

  Add Ara's source revision, top identity, parameterization, physical contract, and raw report locations to the protocol/schema. Preserve the DFPVU baseline values already present.

## Task 6: Verify all claims and hand off auditable data

**Files:**
- Review: all files created or modified by Tasks 1–5

- [ ] **Step 1: Run automated checks.**

  ```bash
  bash openroad/test_ara_4lane_filelist.sh
  bash openroad/test_ara_4lane_config.sh
  tclsh openroad/test_ara_4lane_constraint_sdc.tcl
  python3 -m unittest openroad.test_summarize_ppa
  git diff --check
  ```

  Expected: all tests pass and the diff has no whitespace errors.

- [ ] **Step 2: Manually cross-check reported numbers.**

  Re-open every cited report and ensure each table value, units conversion, stage label, clock period, utilization setting, and quality note matches the raw evidence. Check that no `TODO`, placeholder, virtual-clock, or unqualified “signoff” claim remains.

- [ ] **Step 3: Final delivery.**

  Hand off links to the English and Chinese Ara reports, the side-by-side comparison, and raw OpenROAD output directory. State whether detailed routing completed and enumerate any reasons that absolute results are not a fair normalized DFPVU-vs-Ara efficiency claim.
