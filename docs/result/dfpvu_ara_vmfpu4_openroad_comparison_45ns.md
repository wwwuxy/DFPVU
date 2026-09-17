# DFPVU and Ara VMFPU4: OpenROAD Hardware-Cost Comparison at 45 ns

## Purpose and claim boundary

This record is the paper-facing OpenROAD comparison between DFPVU `PvuTop` and four real instances of Ara's lane-local `vmfpu`. It replaces the earlier complete-Ara comparison, whose vector register file, load/store path, dispatcher, and interconnect made it unsuitable as an arithmetic-boundary reference. The new Ara wrapper excludes those subsystems and retains four VMFPU instances with their local operand/result queues, control, integer-multiply, fixed-point, and IEEE floating-point logic.

The comparison supports three different statement classes. First, it supports raw same-condition mapped-cost observations. Second, it supports capability claims about DFPVU's native Posit/IEEE data path. Third, separately collected RTL cycle data support cycle-efficiency and shape-robustness claims. It does not support a normalized area, frequency, power, energy, or area-efficiency comparison. DFPVU and Ara VMFPU4 still differ in instruction semantics, supported formats, precision coverage, and internal control.

## Common implementation conditions

| Item | DFPVU `PvuTop` | Ara VMFPU4 | Comparison use |
|---|---|---|---|
| Technology platform | OpenROAD-flow-scripts Nangate45 | OpenROAD-flow-scripts Nangate45 | Matched |
| Target clock constraint | 45.00 ns on physical `clock` | 45.00 ns on physical `clk_i` | Matched |
| Target core utilization | 55% | 55% | Matched |
| Arithmetic boundary | DFPVU Posit/IEEE vector datapath | Four Ara lane-local `vmfpu` instances | Closer than full Ara, but not operation-equivalent |
| Excluded Ara subsystems | Not applicable | Vector register file, load/store unit, dispatcher, memory interface, and interconnect | Removes the previous system-scope mismatch |
| Arithmetic/format coverage | Posit and IEEE conversion/data paths | IEEE-oriented FPU/multiplier with half/single/double, fixed-point, and unit-local control | Not matched |
| Synthesis control | Clock-bound DFPVU flow | Slang `--unroll-limit=20000`, Yosys `-noshare` scalability control | Tool settings differ where required for completion |

The first three rows align library, clock constraint, and floorplan policy. They do not make the arithmetic operations identical. Ara VMFPU4 retains a wider IEEE-oriented functional set and local queue/control logic, while DFPVU retains native Posit/IEEE support. Therefore, the mapped-cost rows below are direct observations of the implemented boundaries, not a claim that one design realizes the same operation set more efficiently.

## Mapped logic and floorplan observations

| Metric | DFPVU `PvuTop` | Ara VMFPU4 | Paper interpretation |
|---|---:|---:|---|
| Mapped standard-cell instances | 468,466 | 637,539 | DFPVU has 169,073 fewer cells in these implemented boundaries; raw observation only |
| Mapped standard-cell area | 541,907.702 um² (0.541908 mm²) | 748,853.308 um² (0.748853 mm²) | DFPVU is 206,945.606 um² smaller at mapped stage; not a normalized area advantage |
| Sequential mapped-cell area | Not reported as a comparable category | 156,829.344 um² (20.94%) | No cross-design conclusion |
| DFF-class count | 8,857 `DFF_X1` | 29,568 DFF-class cells | Cell-class definitions differ; report values separately |
| Core area | 983,637.144 um² (0.983637 mm²) | 1,360,488.920 um² (1.360489 mm²) | Fixed by the 55% floorplan policy; DFPVU core is 376,851.776 um² smaller |
| Flow-created die area | 3.957036 mm² | 1.366222 mm² | Not comparable: die-outline margins differ and neither value is package area |
| Initial/effective floorplan utilization | 55.1% | 55.0% | Aligned target utilization |

The mapped-cell and mapped-area rows provide the strongest current implementation-cost evidence. Under the stated conditions, the DFPVU boundary maps to fewer standard cells and less standard-cell area despite exposing dual-format arithmetic. That observation is useful when motivating implementation compactness, but the paper must not convert it into an area ratio or a general area-efficiency claim because the two boundaries retain different functional coverage.

## Physical-stage metrics and why they are not PPA comparisons

| Metric | DFPVU evidence | Ara VMFPU4 evidence | Allowed use |
|---|---|---|---|
| Post-placement/CTS area | Post-GRT design area: 525,718 um² at 53% | Screening CTS area: 762,037 um² at 56.0120% | Do not compare: stages and inserted-cell policies differ |
| Clock construction | 1 root, 8,857 original sinks, 1,568 inserted buffers | 6 clock networks; main register net: 29,416 initial and 33,107 final sinks, 5,366 buffers | Describe clocking structure only |
| Timing | Post-GRT WNS/TNS = -0.02/-0.12 ns; minimum period 45.02 ns | Setup-only CTS WNS/TNS = 22.2029/0.00 ns; reported minimum period 4.10 ns | No frequency claim |
| Closure quality | Near 45 ns closure, but not timing-clean | 11,975 hold and 1,401 capacitance violations because CTS repair was skipped | Neither point is signoff quality |
| Routing | GRT completed; one detailed-route repair iteration leaves 43,770 DRC violations | No global-route or detailed-route result | No routability or manufacturability comparison |
| Power | Post-GRT default-activity estimate: 19.8 W total, 1.09 mW clock | CTS default-activity estimate: 0.381 W total, 3.51 mW clock | No power or energy comparison |

Ara VMFPU4 uses a bounded screening configuration: timing-driven placement, routability-driven placement, CTS timing repair, and DPO are disabled. Its CTS checkpoint is therefore useful for reporting physical cell overhead and clock-network scale, but it does not establish timing closure. DFPVU reaches later physical stages, but its limited detailed-route run is also not DRC-clean. The post-layout timing and power rows must be presented as per-design diagnostics rather than comparative PPA evidence.

## Evidence-supported DFPVU advantages

| Advantage class | Evidence | Claim that is supported | Claim that remains unsupported |
|---|---|---|---|
| Dual-format functionality | DFPVU implements FP32-to-P32, P32-to-FP32, and P32-to-P16 paths; Ara VMFPU4 is IEEE-oriented | DFPVU provides native Posit/IEEE interoperability absent from the unmodified Ara vector FP path | Posit has better model accuracy than FP32 |
| Raw mapped implementation cost | 468,466 cells and 0.541908 mm² for DFPVU; 637,539 cells and 0.748853 mm² for Ara VMFPU4 | The measured DFPVU boundary is smaller in mapped cells and area under the same library/clock/utilization policy | A normalized area or area-efficiency advantage |
| Passing direct RTL workloads | Multiply: 3.64×; integer conversion: 2.64×–2.67×; add: 3.63× DFPVU/Ara useful-work per cycle | DFPVU has higher measured cycle efficiency for these passing, corresponding operation classes | Frequency-normalized throughput or end-to-end speedup |
| Matrix-shape throughput | Fifteen synthetic MNK cases pass their respective references; ratios span 1.66×–88.42× with 6.87× descriptive geometric mean | DFPVU has higher measured MAC/cycle on every tested shape and maintains 3.997072 MAC/cycle for both GEMV orientations | Bit-identical Posit-versus-FP32 instruction latency or statistical population inference |
| Small-shape control overhead | `TAIL_K1`–`TAIL_K4`: DFPVU 12 cycles versus Ara 181–272 cycles; `TAIL_K5`: 19 versus 316 cycles | The evaluated DFPVU request/pipeline organization has lower measured kernel-cycle overhead for these presets | Attribution of the entire gain to a single hardware block |

The direct-workload advantages above exclude trace-derived DFPVU MAC cases with 13, 10, and 3 exact mismatches for Qwen, Gemma, and Phi, and exclude the Gemma dot-product row with one mismatch. Narrow-format conversion is treated as a functionality proxy rather than a direct speedup because it compares P32-to-P16 against FP32-to-FP16. The matrix results use identical shapes and useful MAC counts and pass their respective Posit32 and FP32 references; format semantics remain different.

## IPDPS-ready interpretation

At the same Nangate45 platform, 45 ns clock constraint, and 55% core-utilization target, the DFPVU `PvuTop` boundary maps to 468,466 standard cells and 0.541908 mm², whereas the four-VMFPU Ara boundary maps to 637,539 standard cells and 0.748853 mm². The absolute mapped-area difference is 0.206946 mm². This observation is reported as an implementation-boundary cost result rather than a normalized area advantage because DFPVU provides Posit/IEEE support while Ara VMFPU4 retains a different IEEE-oriented operation set and local control structure.

Separate four-lane RTL experiments show that DFPVU sustains higher useful throughput for all fifteen measured matrix shapes, with a descriptive 6.87× geometric-mean MAC/cycle ratio. Its strongest shape-robustness result is the identical 3.997072 MAC/cycle obtained for both row- and column-oriented GEMV, while the evaluated Ara kernel falls from 2.402346 to 0.045203 MAC/cycle when the vectorized output dimension becomes one. For passing direct workloads, DFPVU yields 2.64×–3.64× higher useful work per cycle for integer conversion, multiply, and add. These are cycle-efficiency results, not frequency-, energy-, or area-normalized claims.

## Required wording limits

- Use “smaller mapped implementation boundary under the stated conditions,” not “PPA-efficient” or “area-efficient.”
- Use “higher measured RTL cycle efficiency” for the passing direct workloads and synthetic matrix shapes, not “faster hardware” without the cycle-level qualifier.
- State that Posit/IEEE interoperability is a functional advantage; do not infer an accuracy advantage without end-to-end model evaluation.
- Do not use Ara VMFPU4 screening CTS timing, power, or physical area to form a ratio against DFPVU post-GRT or detailed-route diagnostics.
- Do not characterize either design as route-complete, DRC-clean, timing-clean, signoff-clean, or energy-measured.

## Evidence sources

- [`dfpvu_openroad_ppa_45ns.md`](dfpvu_openroad_ppa_45ns.md)
- [`ara_vmfpu4_openroad_compute_cost_45ns.md`](ara_vmfpu4_openroad_compute_cost_45ns.md)
- [`dfpvu_vs_ara_ipdps_en.md`](dfpvu_vs_ara_ipdps_en.md)
- [`matrix_gemm_p32_mnk_results.md`](matrix_gemm_p32_mnk_results.md)
- [`ara_4lane_workload_results.md`](ara_4lane_workload_results.md)
