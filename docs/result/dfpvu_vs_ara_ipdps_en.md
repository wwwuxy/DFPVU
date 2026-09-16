# Performance Comparison with Four-Lane Ara

## Experimental Scope and Comparison Boundary

We compare DFPVU against Ara using four arithmetic lanes and kernel-level cycle counts obtained from RTL simulation. The comparison covers trace-derived arithmetic workloads from Qwen3-0.6B, Gemma 3 1B, and Phi-4-mini, together with the same fifteen matrix shapes selected by the DFPVU Kconfig presets. Useful throughput is defined as the number of valid elements or MAC terms divided by the measured kernel cycles. Simulator wall-clock time is excluded because it reflects host simulation speed rather than hardware performance.

The two designs do not implement identical arithmetic semantics. DFPVU executes Posit32 operations and provides explicit Posit/IEEE conversion paths, whereas the evaluated Ara configuration executes IEEE 754 FP32 vector operations. Equal lane count is a structural matching parameter rather than proof of equal datapath area or resources. The reported ratios therefore quantify cycle efficiency for the corresponding arithmetic implementations under matched useful-work counts; they are not bit-identical instruction-latency measurements. They also do not imply frequency-, area-, power-, or energy-normalized superiority. Those dimensions require the planned PPA evaluation under a common technology and timing constraint.

## Trace-Derived Arithmetic Throughput

Table 1 summarizes the direct workloads. The three profiles exercise different sample counts but produce tightly clustered throughput within each implementation, so ranges are reported when the values are not identical. The ratio is computed as DFPVU throughput divided by Ara throughput. Since the useful-work count is matched within each profile, this is also the ratio of Ara cycles to DFPVU cycles.

| Workload | DFPVU throughput | Ara throughput | DFPVU/Ara ratio | Interpretation |
|---|---:|---:|---:|---|
| 16-term MAC | 3.976699 MAC/cycle | 0.231445–0.231543 MAC/cycle | 17.17×–17.18× | Performance result; DFPVU exact-conformance failures are discussed below |
| Four-term dot product | 3.976699–3.986652 MAC/cycle | 0.230448–0.230890 MAC/cycle | 17.26×–17.27× | Qwen and Phi pass exact conformance; Gemma has one mismatch |
| Element-wise multiply | 3.976699–3.986652 element/cycle | 1.093432–1.095689 element/cycle | 3.64× | All DFPVU and Ara cases pass their respective references |
| Conversion to integer | 3.908397–3.947137 element/cycle | 1.464949–1.497076 element/cycle | 2.64×–2.67× | All cases pass their respective references |
| Element-wise add | 3.992801–3.994149 element/cycle | 1.099892–1.100188 element/cycle | 3.63× | All cases pass their respective references |
| Narrow-format conversion | 3.908397–3.947137 element/cycle | 0.059524–0.059578 element/cycle | 65.66×–66.25× | Capability proxy only: P32-to-P16 versus FP32-to-FP16 |

DFPVU sustains approximately one four-lane request per cycle in the multiply, add, conversion, dot-product, and MAC tests. This behavior yields nearly four useful operations per cycle when all lanes carry valid work. Ara reaches about 1.10 elements/cycle for add and multiply and about 1.46–1.50 elements/cycle for FP32-to-integer conversion, giving DFPVU measured cycle-efficiency gains of 2.64×–3.64× for the directly corresponding operation classes. The larger 17.2× ratio for dot product and MAC reflects both the datapath and the evaluated kernel/instruction schedule; it should not be attributed to a single hardware block without an instruction-level stall breakdown.

The narrow-format row demonstrates a capability difference more than a fair arithmetic speedup. DFPVU performs native P32-to-P16 conversion at 3.91–3.95 elements/cycle, while the Ara workload is an IEEE FP32-to-FP16 proxy and does not provide Posit encoding. The measured 65.66×–66.25× ratio shows the cost of realizing the proxy in the evaluated Ara software/hardware path, but it must not be presented as a comparison between semantically identical conversion instructions. More fundamentally, DFPVU supports FP32-to-P32, P32-to-FP32, and P32-to-P16 operations at 3.91–3.95 elements/cycle, functionality that is absent from the unmodified Ara FP vector unit.

## Matrix Throughput and Shape Robustness

Table 2 uses identical MNK shapes and useful MAC counts. DFPVU results are taken from the synthetic Posit32 matrix baseline, for which all fifteen cases match SoftPosit exactly. Ara uses FP32 operands equal to 1.0 and also passes every output check. This dataset is the strongest current evidence for performance because both sides complete the same matrix shapes without correctness failures, although their arithmetic formats remain different.

| Kconfig preset | M×N×K | DFPVU MAC/cycle | Ara MAC/cycle | DFPVU/Ara ratio |
|---|---:|---:|---:|---:|
| `TAIL_K1` | 2×3×1 | 0.500000 | 0.028571 | 17.50× |
| `TAIL_K2` | 2×3×2 | 1.000000 | 0.066298 | 15.08× |
| `TAIL_K3` | 2×3×3 | 1.500000 | 0.079295 | 18.92× |
| `TAIL_K4` | 2×3×4 | 2.000000 | 0.088235 | 22.67× |
| `TAIL_K5` | 2×3×5 | 1.578947 | 0.094936 | 16.63× |
| `SQUARE_16` | 16×16×16 | 3.976699 | 0.592849 | 6.71× |
| `SQUARE_32_K63` | 32×32×63 | 3.936059 | 1.091887 | 3.60× |
| `SQUARE_32_K64` | 32×32×64 | 3.998536 | 1.085482 | 3.68× |
| `SQUARE_32_K65` | 32×32×65 | 3.822212 | 1.091773 | 3.50× |
| `GEMV_ROW` | 1×128×256 | 3.997072 | 2.402346 | 1.66× |
| `GEMV_COLUMN` | 128×1×256 | 3.997072 | 0.045203 | 88.42× |
| `WIDE` | 16×128×64 | 3.999268 | 2.379709 | 1.68× |
| `TALL` | 128×16×64 | 3.999268 | 0.628151 | 6.37× |
| `CUBE_64` | 64×64×64 | 3.999634 | 1.705700 | 2.34× |
| `CUBE_128` | 128×128×128 | 3.999954 | 2.397712 | 1.67× |

DFPVU provides higher useful-MAC throughput for every tested shape. The per-shape ratio spans 1.66×–88.42×, with an unweighted geometric mean of 6.87× across the fifteen presets. The geometric mean is a descriptive summary over one observation per shape, not a statistical estimate. On large, fully occupied matrices, DFPVU reaches 3.997–4.000 MAC/cycle, corresponding to 99.9% or more of the four-lane useful-work ceiling. Its cycle count follows the number of four-lane requests plus a fixed six-cycle overhead, which indicates that request dispatch and pipeline fill/drain costs are almost completely amortized.

Shape sensitivity distinguishes the two implementations more clearly than peak throughput. DFPVU requires 8,198 cycles for both `GEMV_ROW` and `GEMV_COLUMN` and sustains 3.997072 MAC/cycle in each case. Ara reaches 2.402346 MAC/cycle for `GEMV_ROW` but only 0.045203 MAC/cycle for `GEMV_COLUMN`, because the evaluated RVV kernel vectorizes across the N dimension and exposes little vector parallelism when N equals one. DFPVU is consequently 1.66× faster in cycle efficiency for the row-oriented case and 88.42× for the column-oriented case. The equal DFPVU throughput for these transposed aspect ratios supports a stronger claim than peak performance alone: its request organization converts K-dimension work into stable lane utilization without depending on a wide output dimension.

The tail tests expose control overhead at the opposite end of the workload scale. DFPVU completes `TAIL_K1` through `TAIL_K4` in 12 cycles and `TAIL_K5` in 19 cycles, whereas Ara requires 181–316 cycles. The resulting 15.08×–22.67× ratios indicate that DFPVU's compact request interface and fixed pipeline overhead remain effective even when only six matrix outputs are produced. For K values that are not multiples of four, DFPVU throughput changes in accordance with the final partially occupied request; this makes the utilization loss explicit and predictable rather than shape-dependent on M or N.

## Correctness Status and Claim Boundary

The performance evidence must be separated from exact arithmetic conformance. All synthetic DFPVU matrix cases and all Ara cases pass their respective references. DFPVU's trace-derived element-wise multiply, integer conversion, narrow conversion, and add workloads also pass SoftPosit conformance. However, the three trace-derived DFPVU MAC cases contain 13, 10, and 3 exact mismatches for Qwen, Gemma, and Phi, respectively, and the Gemma dot-product case contains one mismatch. These failures prevent a claim that all trace-derived DFPVU arithmetic is bit-exact. They do not invalidate the measured cycle counts, but the root cause must be resolved before the corresponding MAC results can serve as positive correctness evidence.

The current tests also do not establish a Posit accuracy advantage over IEEE FP32. DFPVU and Ara are checked against different arithmetic references, and the matrix tests use bounded tiles rather than complete LLM layers. Consequently, the results support claims about hardware cycle efficiency, shape robustness, and native format coverage, but not end-to-end model accuracy, token throughput, or quality-per-watt.

## Paper-Level Conclusion

Under matched four-lane RTL cycle accounting, DFPVU delivers higher useful-work throughput than the evaluated Ara kernel in every measured row, subject to the arithmetic-semantic qualifications above. Its strongest demonstrated property is predictable lane-level throughput across workload shapes: it approaches four MACs/cycle on sufficiently large matrices, preserves the same 3.997072 MAC/cycle for row- and column-oriented GEMV, and reduces small-tail execution cycles by 15.08×–22.67×. The direct workloads further show 2.64×–3.64× cycle-efficiency gains for passing arithmetic and integer-conversion cases. Native Posit/IEEE conversion extends this performance result into a functionality advantage, enabling dual-format data paths that Ara's IEEE-only vector unit does not provide.

These observations support positioning DFPVU as a specialized RVV-compatible arithmetic engine whose advantage lies in the combination of high lane utilization, low per-request control overhead, shape-robust execution, and native Posit/IEEE interoperability. The claim should remain cycle-based until common-flow synthesis supplies frequency, area, and power results. A complete IPDPS evaluation should therefore add PPA-normalized throughput, repeated measurements or deterministic-cycle justification, instruction/stall breakdowns for Ara, resolution of the remaining DFPVU conformance mismatches, and end-to-end application measurements.

## Evidence Sources

- [`llm_p32_workload_matrix_results.md`](llm_p32_workload_matrix_results.md)
- [`matrix_gemm_p32_mnk_results.md`](matrix_gemm_p32_mnk_results.md)
- [`ara_4lane_workload_results.md`](ara_4lane_workload_results.md)
- [`../../results/ara_workload_4lane/direct_workloads.csv`](../../results/ara_workload_4lane/direct_workloads.csv)
- [`../../results/ara_workload_4lane/matrix_workloads.csv`](../../results/ara_workload_4lane/matrix_workloads.csv)
