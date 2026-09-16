# ARA 4-lane workload comparison metrics

## Scope and configuration

- Target: ARA four-lane RTL (`NR_LANES=4`, `VLEN=4096`) under the repository's
  Verilator testbench.
- Direct cases are selected from the DFPVU Qwen3-0.6B, Gemma3-1B, and
  Phi4-mini workload traces. Matrix cases are the fifteen
  `MATRIX_GEMM_P32_BENCHMARK` Kconfig presets.
- `cycles` is measured around the kernel in the bare-metal ARA program. It is
  not host wall-clock time, and cannot be converted to time without choosing a
  target clock frequency.
- A positive `*_per_cycle_x1000000` value is the useful work divided by kernel
  cycles, scaled by 1,000,000. It is MAC/cycle for matrix rows; in direct rows
  it is the named workload's useful operation count/cycle.

## Semantic mapping

ARA implements RVV IEEE-754 FP32, not Posit32. Thus `rvv-fp32-direct` is a
direct FP32 execution of the captured operands. The P16 row is explicitly an
IEEE FP32-to-FP16 proxy. Posit32 conversion has no corresponding ARA
instruction and is deliberately not reported as an ARA hardware measurement.
For the add case, the expected result is recomputed in IEEE FP32 because a
DFPVU Posit32 output is not a valid oracle for ARA FP32 addition. Matrix
operands are all FP32 `1.0`, so every checked output must equal `K`; they are
shape/performance proxies rather than Posit32 numerical equivalents.

## Result files

- `direct_workloads.csv`: 18 trace-derived direct/proxy rows, all `PASS`.
- `matrix_workloads.csv`: 15 Kconfig matrix presets, all `PASS`.
- `matrix-final.log`: final corrected RTL run. `matrix.log` is a retained
  diagnostic run made before the B-buffer boundary fix and must not be used.

## Verification performed

- `python3 tools/test_generate_ara_workload_data.py`
- `bash tools/test_ara_workload_compare.sh` (forces rebuild after replacing
  generated input, preventing a stale ELF)
- `make -C /root/ara/apps spike-run-dfpvu_matrix_compare`
- Corrected full four-lane Verilator matrix run: 15/15 `PASS`; total
  application hardware-counter interval: 2,998,346 cycles.

The simulator's final whole-program execution count was `0x313e13` cycles;
its 540.598-second wall time is a host simulation speed measurement only.
