# Ara Four-VMFPU Compute-Unit Hardware-Cost Data at 45 ns

## Scope and provenance

This measurement isolates four real instances of Ara's lane-local `vmfpu` module from upstream Ara revision `34bd3bc1`. The synthesis top instantiates one `vmfpu` for each of four lanes with `NrLanes=4` and `VLEN=4096`. It retains the unit-local operation, operand, mask, result, reduction, and exception interfaces, together with the IEEE floating-point, integer-multiply, fixed-point, and queue/control logic implemented inside `vmfpu`. It excludes the vector register file, load/store unit, dispatcher, memory interface, and lane interconnect. The result therefore measures an Ara arithmetic-unit boundary rather than the complete Ara accelerator.

The flow uses OpenROAD-flow-scripts `nangate45`, a physical 45.00 ns `ara_clk` constraint on `clk_i`, and 55% target core utilization. The wrapper has 31 ports and 2,330 port bits; placement-stage port buffering is included only in physical-stage rows. Slang elaboration uses `--unroll-limit=20000`. The default Yosys resource-sharing pass did not produce a checkpoint during a 281 s bounded run, so the reproducible configuration uses `SYNTH_ARGS=-noshare`. This is a tool-scalability control and does not alter Ara RTL. The synthesis check reports zero problems.

## Measured implementation cost

| Metric | Value | Stage and interpretation |
|---|---:|---|
| Mapped standard-cell instances | 637,539 | Technology-mapped synthesis; primary cell-count result |
| Mapped standard-cell area | 748,853.308 um² (0.748853 mm²) | Technology-mapped synthesis; primary logic-cost result |
| Sequential mapped-cell area | 156,829.344 um² (20.94%) | Technology-mapped synthesis |
| DFF-class instances | 29,568 | 28,872 `DFFR_X1`, 104 `DFFS_X1`, and 592 `DFF_X1` |
| Core area | 1,360,488.920 um² (1.360489 mm²) | 55% target-utilization floorplan |
| Die outline | 1.168855 mm × 1.168855 mm = 1.366222 mm² | Flow-created outline; not package or pad-ring area |
| Floorplan effective utilization | 55.0% | Before placement-stage cell insertion |
| Global-placement design area | 751,944 um² (0.751944 mm²) | Includes placement-stage port buffering; not the mapped-logic number |
| Screening CTS design area | 762,037 um² (0.762037 mm²) | Includes placement and clock-tree cells; 1.7605% above mapped area |
| Screening CTS utilization | 56.0120% | Fixed 1.360489 mm² core |
| Screening CTS instances | 654,289 | Physical implementation after placement and CTS cell insertion |
| Screening CTS clock networks | 6 | TritonCTS-discovered clock nets |
| Main register-clock sinks | 29,416 before CTS; 33,107 after CTS | `clk_i_regs` clock network |
| Main register-clock buffers | 5,366 | Constructed for `clk_i_regs`; not a total physical-cell increase |
| Setup-only CTS diagnostic | WNS/TNS = 22.2029/0.00 ns; minimum period = 4.10 ns | Not a frequency result; see physical-screening boundary |
| Default-activity CTS power estimate | 0.381 W total; 3.51 mW clock | OpenSTA `report_power` without workload switching activity; not workload power |

The mapped 0.748853 mm² area is the appropriate logic-cost number. The 0.762037 mm² CTS row is a separate physical-overhead observation: it includes 1,363 input buffers, 700 output buffers, and clock-tree insertion after global placement. It must not be treated as an alternative synthesis area or as CTS-only overhead.

## Physical-screening boundary

The completed CTS point is intentionally a bounded screening implementation, not a route-complete PPA flow. It retains the RTL, Nangate45 library, 45 ns clock, floorplan, and CTS construction, while setting `GPL_TIMING_DRIVEN=0`, `GPL_ROUTABILITY_DRIVEN=0`, `SKIP_CTS_REPAIR_TIMING=1`, and `ENABLE_DPO=0`. These controls avoid unbounded physical optimization caused by the standalone wrapper's high port count. The global-placement checkpoint is reused as the resize input, and stage-3 report aggregation is skipped only for that replay path.

The CTS report contains 11,975 hold violations and 1,401 maximum-capacitance violations because timing repair was deliberately omitted. The reported 4.10 ns minimum period and setup WNS are consequently diagnostic values, not an achieved clock frequency. No global-route, detailed-route, post-route timing, DRC, routed wire/via, workload-activity power, energy, or signoff claim is available for this boundary. The 0.381 W power estimate uses default activity rather than a workload VCD/SAIF and cannot be compared with other power estimates.

## Relationship to DFPVU

This is a substantially closer Ara boundary to DFPVU `PvuTop` than the complete Ara accelerator: both exclude Ara's vector register file, load/store path, dispatcher, and interconnect. Under the same Nangate45, 45 ns, and 55% floorplan policy, DFPVU `PvuTop` maps to 0.541908 mm² and the four-`vmfpu` Ara boundary maps to 0.748853 mm². These are raw same-stage observations and may be shown in adjacent rows.

No DFPVU/Ara area ratio or superiority claim is defined. `PvuTop` implements DFPVU's Posit/IEEE vector datapath, whereas Ara `vmfpu` contains Ara's IEEE-oriented FPU/multiplier, unit-local queues and control, and half/single/double support. The instruction set, precision support, microarchitecture, and semantic coverage are not matched. A claim about a common arithmetic operation requires a separately constrained FP32 operation-level experiment or an IEEE-only DFPVU ablation under the same physical stage and activity assumptions.

## IPDPS-ready wording

*To separate Ara's arithmetic-unit cost from its full accelerator cost, four lane-local `vmfpu` instances were synthesized in the Nangate45 platform at a 45 ns target period and 55% core utilization. The boundary maps to 637,539 standard cells with 0.748853 mm² mapped area and 29,568 DFF-class cells; its 1.360489 mm² floorplan reaches 0.762037 mm² after placement and CTS cell insertion. This result excludes Ara's vector register file, load/store path, dispatcher, and interconnect. It provides a raw arithmetic-boundary reference for DFPVU `PvuTop`, but differences in supported formats, operation coverage, and unit-local control preclude an area or PPA superiority ratio.*

## Archived evidence

The reproducible harness, runner, and generated OpenROAD reports are retained in the experiment worktree `codex/ara-openroad-ppa`. Per the requested main-branch scope, main records the paper-facing result rather than generated ODB/log artifacts.
