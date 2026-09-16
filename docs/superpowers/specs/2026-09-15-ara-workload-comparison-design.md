# ARA Four-Lane DFPVU Workload Comparison Design

## Goal

Measure the DFPVU workload-class operations on the checked-out four-lane ARA
RTL, using the same trace-backed FP32 operands, workload shapes, and useful
work accounting wherever ARA has an IEEE-754 RVV equivalent.  Produce a
machine-readable result file and a concise Chinese comparison report for use
alongside the existing DFPVU workload results.

## Test boundary

The target is ARA's `config=default` RTL (`NR_LANES=4`, `VLEN=4096`) executed
by its Verilator testbench.  This is a cycle-accurate RTL simulation result,
not an FPGA board measurement and not a frequency/TOPS claim.  Kernel cycles
come from the program's `mcycle` counter, with the simulator's whole-program
hardware-cycle count retained separately when available.

The test corpus is limited to DFPVU Kconfig workload entries:

| DFPVU workload | ARA treatment |
| --- | --- |
| LLM P32 MAC and matrix GEMM | RVV FP32 fused multiply-add / GEMM with the same M,N,K and MAC-term count |
| LLM P32 Dot | RVV FP32 four-term dot, batched across vector elements |
| LLM P32 Multiply and Add | RVV FP32 elementwise operation on the same sampled operands |
| LLM P32-to-Int | RVV FP32-to-Int32 round-toward-zero conversion on the same samples |
| LLM P32-to-P16 | separately labelled RVV FP32-to-IEEE-FP16 proxy, including round-trip error |
| LLM FP32↔P32 conversion | not applicable: ARA implements IEEE RVV and has no Posit32 representation or conversion instruction |

DFPVU's Posit-specific raw-bit conformance, numerical-loss statistics, tagged
ready/valid protocol, and request count are not substituted by an ARA value.
ARA rows use a `fp32_reference` correctness check and explicitly state their
IEEE semantics.

## Inputs and data selection

The generator consumes the existing profile traces in `/tmp` for Qwen3-0.6B,
Gemma 3 1B, and Phi-4-mini-instruct.  It follows DFPVU's deterministic
sampling definitions: 256 scalar samples per module for conversion and
To-Int; 256 four-element vectors per module for Dot/Multiply; 256 elements
per captured Add operation; and the DFPVU `SQUARE_16` MAC tile.  The generated
arrays contain only selected operands and independent IEEE FP32 reference
results, so no model weight is copied into the report.

The deterministic matrix-GEMM set covers every Kconfig preset from tails
through 128 cubed.  It uses a documented seed and the same `M*N*K` MAC-term
accounting as DFPVU.

## Components

- `/root/ara/apps/dfpvu_workload_compare/`: one isolated bare-metal ARA app.
  It runs RVV kernels, checks every output against a scalar IEEE FP32
  reference, and emits one parseable row per case.
- `tools/generate_ara_workload_data.py`: reads DFPVU v2 trace metadata and
  materializes the bounded selected operands/reference arrays for one profile.
- `tools/run_ara_workload_comparison.sh`: repeats generate, build, and RTL
  simulation for each profile and collects raw logs without cleaning unrelated
  ARA artifacts.
- `tools/test_ara_workload_comparison.sh`: integration test for the emitted
  report contract and nonzero-error failure path.
- `docs/result/ara_4lane_workload_results.{csv,md}`: derived results and
  methodology, generated only from successful simulator runs.

## Metric contract

Each direct or proxy row records: profile, workload, semantic class, shape or
sample count, useful elements, useful MAC terms/FLOPs where applicable,
kernel `mcycle`, useful work per kernel cycle, maximum absolute error, mismatch
count, and PASS/FAIL.  FP32 GEMM/Dot use `2 * MAC terms` FLOPs; Add/Multiply
use one element operation; conversion uses elements.  Results are invalid if
the app reports any mismatch or the RTL simulation signals failure.

## Safety and reproducibility

Existing modified/untracked ARA files remain untouched.  The new app owns only
its own source and generated data.  No clean, reset, checkout, dependency
install, model download, or overwrite of existing trace directories is
permitted.  Commands, tool versions, configuration, input trace path, and
run timestamp are retained in the report.
