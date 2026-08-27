# DFPVU Nangate45 OpenROAD PPA Experiment Design

## Purpose

Create a reproducible OpenROAD-flow-scripts (ORFS) experiment for the
generated default DFPVU RTL and collect post-route PPA data using the bundled
Nangate45 technology platform.

## Scope and fixed inputs

- Design under test: `PvuTop`.
- RTL inputs: `vsrc/PvuTop.sv` and `src/main/resources/pvu/lzc.sv`.
- Static elaboration: the checked-in default RTL (`MAX_POSIT_WIDTH=32`,
  `MAX_VECTOR_SIZE=4`, `ES=2`, `FLOAT_MODE=3`).
- Implementation platform: ORFS `nangate45`.
- The PPA flow is separate from the Chisel elaboration and Verilator
  regression flow; it must not hand-edit `vsrc/PvuTop.sv`.

## Constraints

`PvuTop` is combinational in the current generated RTL: its `clock` and
`reset` ports have no sequential endpoints.  Timing is therefore measured as
input-to-output combinational delay rather than a register-to-register
frequency.

The initial experiment uses a 10 ns virtual clock (100 MHz) with 20% input
and output delay budgets.  Clock and reset are excluded from data input-delay
constraints.  The flow will expose the target period as a Make/ORFS override
so period sweeps can be run without changing RTL.

## Flow structure

Add a self-contained ORFS design directory for `nangate45/dfpvu` containing:

- `config.mk`: the design name, RTL list, SDC, utilization and deterministic
  synthesis settings.
- `constraint.sdc`: virtual-clock I/O timing constraints for the combinational
  top-level design.
- Bazel metadata if required by this checkout of ORFS.
- a small report-extraction utility that turns final ORFS reports into one
  machine-readable CSV/JSON summary and preserves source report paths.

The flow runs synthesis, floorplan, placement, routing, finishing and report
generation.  It uses the OpenROAD executable built from this checkout; no
external PDK download is required for Nangate45.

## Reported metrics

Each run records:

| Category | Metrics |
| --- | --- |
| Area | core/die area, standard-cell area, utilization, instance/cell count |
| Performance | target period, worst slack (WNS), total negative slack (TNS), worst input-to-output delay, inferred maximum frequency where meaningful |
| Power | ORFS/OpenSTA liberty-based total, internal, switching and leakage power, with the activity assumption and units recorded |
| Quality checks | design-rule errors, antenna violations, routing overflow, timing-violation count, flow status |
| Reproducibility | DFPVU and OpenROAD revisions, platform, command, timestamp and report locations |

## Power interpretation

The baseline has no workload-derived switching activity.  Any dynamic-power
number from this run is an early estimate based on the STA/Liberty default
activity assumptions and must be labeled accordingly.  A later, separate
experiment may convert a selected Verilator VCD waveform to SAIF and feed it
into the timing/power report path; its workload and window will be recorded
alongside the result.

## Validation and failure handling

- Before physical implementation, run the existing DFPVU Verilator regression
  selected by the repository configuration to establish the RTL baseline.
- Fail a PPA run if RTL elaboration/synthesis fails, final reports are missing,
  placement/routing has overflow, or report extraction cannot identify units.
- Preserve ORFS logs and reports on a failed run; summarize the failed stage
  instead of publishing partial PPA metrics as final data.
