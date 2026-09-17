# Ara 4-Lane OpenROAD Hardware-Cost Data at 45 ns

## Scope and provenance

This dataset implements upstream Ara revision `34bd3bc1` through the native `ara` accelerator module, wrapped only to expose its clock, reset, and accelerator interfaces to OpenROAD. The configuration uses `NR_LANES=4`, `VLEN=4096`, the Nangate45 platform, a 45.00 ns `ara_clk` constraint on the physical `clk_i` port, and 55% target core utilization. The Bender-generated manifest contains 382 SystemVerilog/Verilog sources and seven include directories. The evaluated boundary is the complete four-lane RVV accelerator, including its vector register file, load/store interface, dispatch/control logic, and vector functional units; it is not an arithmetic-unit-only boundary.

Yosys synthesis used the Slang frontend with `--unroll-limit=20000`. The standard SAT resource-sharing pass did not finish in a bounded run for this full design, so the reproducible configuration sets `SYNTH_ARGS=-noshare`. This is a synthesis-tool scalability fallback, not an RTL functional change. The synthesis check reports zero problems.

## Measured implementation cost

| Metric | Value | Stage and interpretation |
|---|---:|---|
| Mapped standard-cell instances | 1,996,778 | Technology-mapped synthesis |
| Mapped standard-cell area | 2,730,267.092 um² (2.730267 mm²) | Primary mapped logic-cost number |
| Sequential mapped-cell area | 933,198.224 um² (34.18%) | Synthesis report |
| DFF-class instances | 195,470 | 61,581 `DFFR_X1`, 177 `DFFS_X1`, and 133,712 `DFF_X1` |
| Core area | 4,962,090.350 um² (4.962090 mm²) | 55% target utilization floorplan |
| Die outline | 2.230030 mm × 2.230030 mm = 4.973030 mm² | Flow-created outline; not package or pad-ring area |
| Floorplan effective utilization | 55.0% | 1,996,778 instances before placement changes |
| Global-placement routability inflation | 713,009.79 um² (22.62%) | Screening placement diagnostic; this is artificial placement inflation, not mapped logic area |
| Global-placement final congestion | 1.4885 weighted | Screening placement diagnostic; target congestion was not reached |
| Screening CTS standard-cell instances | 2,212,392 | Includes implementation-added cells after CTS |
| Screening CTS standard-cell area | 2,879,063 um² (2.879063 mm²) | 148,795.908 um² (5.45%) above mapped area |
| Screening CTS utilization | 58.0212% | Fixed 4.962090 mm² core |
| Screening CTS clock networks / sinks | 42 / 215,976 | Ara has many gated/vector-register clock domains; the largest net has 62,038 sinks |
| Screening CTS clock construction | 33,757 leaf buffers; 58 long-wire buffers; 98 delay-balance buffers | Explicit CTS log counts; leaf, long-wire, and delay categories are reported separately |
| Screening CTS timing | WNS/TNS = 20.7537/0.00 ns; minimum period = 21.23 ns (47.11 MHz) | Pre-route screening only; no CTS timing repair was run |
| Screening CTS errors / warnings | 0 / 133,468 | OpenROAD metrics; many warnings are skipped one-sink clock-net messages |

The 2.730267 mm² synthesis area and 4.962090 mm² floorplan core are directly observed cost metrics under the same library, clock-period, and utilization policy. The CTS row is useful for showing the clocking and physical-cell overhead of the complete Ara boundary, but is deliberately not a routed or signoff implementation point.

## Physical-screening boundary

The default timing-driven global-placement attempt reached the all-net `repair_design` pass for 2,051,123 remaining nets without producing a recoverable checkpoint after more than 30 minutes. A separate screening configuration was therefore used to obtain bounded placement and CTS evidence. It retains the RTL, library, 45 ns constraint, floorplan, and CTS construction, but sets `GPL_TIMING_DRIVEN=0`, `SKIP_CTS_REPAIR_TIMING=1`, and `ENABLE_DPO=0`. The screening runner also reuses `3_3_place_gp.odb` as the resize input and skips only the stage-3 report aggregation; CTS final metrics remain enabled.

No Ara global-route, detailed-route, post-route timing, DRC, routed wire/via, or workload-activity power number is reported. The screening CTS result must not be described as route-complete, signoff-clean, DRC-clean, or directly comparable to DFPVU's post-GRT and limited detailed-route diagnostics.

## Permitted comparison language

At the same Nangate45 platform, 45 ns clock target, and 55% floorplan policy, the full four-lane Ara accelerator maps to 1,996,778 standard cells with 2.730267 mm² mapped area and uses a 4.962090 mm² core. Its screening CTS result contains 42 clock networks and 215,976 reported clock sinks, reflecting the clocking cost of the complete RVV accelerator boundary. These values may be placed beside the DFPVU `PvuTop` cost table as raw implementation evidence. They must not be converted into a DFPVU/Ara area, power, frequency, or energy ratio because `PvuTop` is an arithmetic-unit implementation whereas Ara includes the vector register file, memory interface, dispatch, and control subsystems.

## Evidence paths

- Harness and standard configuration: `openroad/ara_4lane/config.mk`
- Screening configuration and runner: `openroad/ara_4lane/config_screening.mk`, `openroad/run_ara_4lane_screening.sh`
- Synthesis evidence: `openroad/ara_4lane/work/reports/nangate45/ara_4lane/base/synth_stat.txt` and `synth_check.txt`
- Floorplan evidence: `openroad/ara_4lane/work/logs/nangate45/ara_4lane/base/2_1_floorplan.log` and `openroad/ara_4lane/work/reports/nangate45/ara_4lane/base/2_floorplan_final.rpt`
- Screening global-placement evidence: `openroad/ara_4lane/work/reports/nangate45/ara_4lane/base/3_global_place.rpt`
- Screening CTS evidence: `openroad/ara_4lane/work/reports/nangate45/ara_4lane/base/4_cts_final.rpt`, `openroad/ara_4lane/work/logs/nangate45/ara_4lane/base/4_1_cts.json`, and `4_1_cts.log`
