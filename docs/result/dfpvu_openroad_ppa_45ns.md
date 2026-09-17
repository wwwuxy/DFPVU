# DFPVU Hardware-Cost Data from a Clock-Bound OpenROAD Flow

## Implementation scope and provenance

This dataset implements `PvuTop` with the OpenROAD-flow-scripts `nangate45` platform, a 45.00 ns target period, and 55% target core utilization. The SDC binds `dfpvu_vclk` to the physical top-level `clock` port; it is not a virtual clock. The data quantify a standard-cell implementation-cost baseline, not a fabricated chip.

Synthesis through global-routing values come from `openroad/clock_bound_45ns_u55_repro/work/`. Detailed-routing values come from the retained completed run in `openroad/clock_bound_45ns/work/`, which used the same `PvuTop`, RTL file list, Nangate45 library, 45.00 ns clock period, 55% core utilization, and `DETAILED_ROUTE_END_ITERATION=1`. This split is explicit: the reproduction run was interrupted during detailed routing after its reports through global routing had been regenerated.

## Paper-table data

| Metric | Value | Implementation stage / interpretation |
|---|---:|---|
| Standard-cell instances | 468,466 | Technology-mapped synthesis |
| Standard-cell area | 541,907.702 um² (0.541908 mm²) | Technology-mapped synthesis; primary logic-cost number |
| DFF_X1 instances | 8,857 | Technology-mapped synthesis |
| Core area | 983,637.144 um² (0.983637 mm²) | 55% target utilization; excludes package, pads, and IO ESD |
| Die outline | 1.989230 mm × 1.989230 mm = 3.957036 mm² | Flow-created die; not a pad-ring or package area |
| Initial effective core utilization | 55.1% | Floorplan |
| Post-GRT design area / utilization | 525,718 um² / 53% | Global-routing log; not interchangeable with mapped area |
| Clock constraint | 45.00 ns (22.22 MHz target) | `create_clock ... [get_ports clock]` |
| CTS clock roots / original sinks | 1 / 8,857 | CTS recognizes the real `clock` net |
| CTS inserted clock buffers | 1,568 | Clock-tree construction |
| CTS timing | WNS = 0.00 ns, TNS = 0.00 ns; min period = 44.99 ns | CTS stage only |
| Post-GRT timing | WNS = -0.02 ns, TNS = -0.12 ns; min period = 45.02 ns (22.21 MHz) | Near-closure; not timing clean |
| Global-routing resource use | 40.49%; overflow = 0 / 0 / 0 | Global-routing report |
| Detailed-routing DRC, iteration 0 | 108,619 | Before the one allowed repair iteration |
| Detailed-routing DRC, iteration 1 | 43,770 | `DETAILED_ROUTE_END_ITERATION=1`; not DRC clean |
| Detailed-routing wire length, iteration 1 | 6,382,115 um (6.382115 m) | Diagnostic routed-interconnect value |
| Detailed-routing vias, iteration 1 | 2,977,052 | Diagnostic routed-interconnect value |
| Post-GRT total power | 19.8 W | OpenSTA vectorless/default-activity estimate; not workload power |
| Post-GRT clock power | 1.09 mW | Same vectorless/default-activity assumption |

The first detailed-routing repair iteration reduces the DRC count by 59.7% (108,619 to 43,770), but the remaining count is still large. Thus the area, cell-count, clock-tree, and post-GRT near-closure data may be reported as implementation-cost evidence; the design must not be called signoff-clean, DRC-clean, manufacturable, or energy-measured.

## IPDPS-ready wording

**Suggested table caption.** *OpenROAD implementation cost of DFPVU `PvuTop` in the Nangate45 platform at a 45 ns target period. The clock is constrained to the physical top-level clock port. Post-GRT timing is near closure; the reported detailed-routing DRC and interconnect metrics are from a flow limited to one repair iteration and are not signoff results.*

**Suggested results text.** *Under a clock-bound Nangate45 implementation, DFPVU maps to 468,466 standard cells (0.5419 mm²) and 8,857 flip-flops. At a 55% target core utilization, the flow uses a 0.9836 mm² core and constructs a clock tree with 1,568 inserted buffers for 8,857 original clock sinks. The 45 ns target is near timing closure after global routing (WNS/TNS = -0.02/-0.12 ns). Detailed routing limited to one repair iteration reduces the DRC count from 108,619 to 43,770; consequently, these results quantify current implementation cost and optimization headroom rather than a signoff-clean layout.*

## Ara VMFPU4 comparison boundary

The maintained Ara reference isolates four lane-local `vmfpu` instances and excludes the vector register file, load/store path, dispatcher, and interconnect. Under the same Nangate45, 45 ns, and 55% floorplan policy, it maps to 637,539 cells and 0.748853 mm². This is a raw same-stage arithmetic-boundary observation beside DFPVU `PvuTop`, not a normalized PPA comparison.

No area, power, energy, frequency, or efficiency ratio is defined. DFPVU implements a Posit/IEEE vector datapath, while Ara `vmfpu` is an IEEE-oriented FPU/multiplier with its own queues, control, and half/single/double support. The Ara CTS point also remains a screening checkpoint with timing/routability-driven placement, CTS repair, and DPO disabled. See `ara_vmfpu4_openroad_compute_cost_45ns.md` for the scope, measured values, and physical-stage limitations.

## Evidence paths

- Reproduced synthesis/CTS/GRT reports: `openroad/clock_bound_45ns_u55_repro/work/reports/nangate45/dfpvu/base/`
- Reproduced implementation logs: `openroad/clock_bound_45ns_u55_repro/work/logs/nangate45/dfpvu/base/`
- Completed detailed-routing log: `openroad/clock_bound_45ns/work/logs/nangate45/dfpvu/base/5_2_route.log`
- Clock constraint: `openroad/nangate45/constraint.sdc`
