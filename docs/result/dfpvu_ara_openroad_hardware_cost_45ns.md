# DFPVU and Ara: OpenROAD Hardware-Cost Evidence at 45 ns

Table 1 juxtaposes raw implementation observations, not a normalized PPA comparison. Both rows use the OpenROAD Nangate45 platform, a 45.00 ns target clock, and 55% target core utilization. Their RTL boundaries differ materially: DFPVU is the `PvuTop` arithmetic-unit implementation, whereas Ara is a complete four-lane (`NR_LANES=4`, `VLEN=4096`) RVV accelerator. No DFPVU/Ara ratio is defined.

| Metric | DFPVU `PvuTop` | Ara full four-lane accelerator | Evidence boundary |
|---|---:|---:|---|
| RTL scope | Arithmetic unit | Vector register file, memory interface, dispatch/control, and vector functional units | Different implementation scope |
| Mapped standard-cell instances | 468,466 | 1,996,778 | Technology-mapped synthesis |
| Mapped standard-cell area | 0.541908 mm² | 2.730267 mm² | Primary logic-cost number; raw values only |
| Mapped DFF-class instances | 8,857 `DFF_X1` | 195,470 DFF-class cells | Synthesis cell statistics |
| Core area | 0.983637 mm² | 4.962090 mm² | 55% target utilization floorplan |
| Flow-created die area | 3.957036 mm² | 4.973030 mm² | Not package or pad-ring area |
| Floorplan utilization | 55.1% | 55.0% | Before placement-stage changes |
| CTS physical status | Standard CTS: 1 root, 8,857 original sinks, 1,568 inserted buffers | Screening CTS: 42 clock networks, 215,976 reported sinks, 33,757 leaf buffers, 58 long-wire buffers, and 98 delay buffers | Ara CTS is screening-only |
| CTS area / utilization | Not used as the primary DFPVU area number | 2.879063 mm² / 58.0212% | Ara value includes implementation-added cells |
| CTS timing | WNS/TNS = 0.00/0.00 ns; min period 44.99 ns | WNS/TNS = 20.7537/0.00 ns; min period 21.23 ns | Not a frequency comparison: Ara skips placement/CTS timing repair and is pre-route |
| Route evidence | GRT near closure; one detailed-route repair iteration retains 43,770 DRC | Not produced | Ara has no route-complete point |
| Power evidence | Post-GRT vectorless estimate only | Not reported | No common activity or route stage |

The appropriate hardware-cost conclusion is limited. The full Ara accelerator maps to 2.730267 mm² of Nangate45 standard cells and requires a 4.962090 mm² core under the stated 45 ns/55% policy. Its multi-domain clocking structure reaches 42 clock networks and 215,976 reported sinks in the CTS screening run. DFPVU `PvuTop` maps to 0.541908 mm² in a 0.983637 mm² core, but this smaller boundary excludes the Ara subsystems listed above. The data consequently demonstrate implementation scope and cost composition; they do not demonstrate an area, frequency, power, energy, or area-efficiency advantage.

Ara's synthesis/floorplan data are standard-flow results. Its CTS is a bounded screening flow: timing-driven global placement, CTS timing repair, and DPO were disabled; global placement was reused as the resize input; and only stage-3 metric aggregation was skipped. The Ara result is not routed, signoff-clean, DRC-clean, or comparable to DFPVU post-GRT/detailed-route values.

Data provenance: [`dfpvu_openroad_ppa_45ns.md`](dfpvu_openroad_ppa_45ns.md) and [`ara_openroad_ppa_45ns.md`](ara_openroad_ppa_45ns.md).
