# P32 GEMM Throughput Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Kconfig-selected, deterministic P32 GEMM benchmark that reports correct cycle-level throughput for configurable M, N, and K.

**Architecture:** A guarded C++ Verilator main maps every GEMM output to a dependent sequence of up-to-four-lane `op=11` MAC requests. A tag-aware round-robin scheduler interleaves independent output chains. SoftPosit is the bit-exact oracle; a CSV report exposes normalized metrics.

**Tech Stack:** C++17, Verilator, SoftPosit, GNU Make, Kconfig, Bash.

**Spec:** `docs/superpowers/specs/2026-09-09-matrix-gemm-p32-throughput-design.md`

## Global Constraints

- Keep `PvuTop.scala` and generated RTL unchanged; use raw P32 `op=11` only.
- Construct DUT operands from direct, fixed raw P32 words, never host float or double conversions.
- Drive `out_ready=1`; count only accepted DUT handshakes after reset.
- Count tail requests by valid lanes, not as full four-term requests.
- Use the existing Verilator C++ regression path; do not add Scala or ChiselTest tests.
- Preserve user-owned `build/` and pre-existing configuration changes outside this feature; do not auto-commit in the dirty checkout.

---

### Task 1: Wire the selected test and prove the missing benchmark fails

**Files:**
- Modify: `Kconfig`
- Modify: `.config`
- Modify: `makefile`
- Create: `test_src/test_matrix_gemm_p32.sh`

**Interfaces:**
- Kconfig symbol: `CONFIG_MATRIX_GEMM_P32_BENCHMARK`.
- Make variable: `MATRIX_GEMM_P32_ARGS`, passed unchanged to `VPvuTop` only for this selection.
- Shell test invokes `make run MATRIX_GEMM_P32_ARGS="--m 2 --n 3 --k 5 --seed 7"`.

- [ ] Add the exclusive Kconfig option, select it in `.config`, include it in the SoftPosit link predicate, and route its arguments through `make run`.
- [ ] Write the shell integration test asserting the benchmark output includes the shape, `requests=12`, `valid_mac_terms=30`, `lane_utilization=0.625000`, and `exact_mismatches=0`.
- [ ] Run the shell test before creating the C++ main; verify it fails because the selected configuration has no matching `main`.

### Task 2: Implement one scheduled raw-P32 GEMM case

**Files:**
- Create: `csrc/main_matrix_gemm_p32.cpp`

**Interfaces:**
- `BenchmarkOptions parse_options(int argc, char** argv)` accepts positive `--m`, `--n`, `--k`, and `--seed` values.
- `std::vector<uint32_t> make_matrix(size_t rows, size_t columns, uint64_t seed)` returns direct finite raw P32 words.
- `std::vector<uint32_t> run_gemm(Driver&, const Matrix&, const Matrix&, WorkloadMetrics&)` returns C in row-major order.

- [ ] Implement raw P32 corpus sampling and checked shape arithmetic.
- [ ] Implement a resettable ready/valid driver and tag-aware scheduler that holds requests until accepted, interleaves chains, handles K tails with the runtime `vector_size`, and checks response tag/op.
- [ ] Implement the independently ordered SoftPosit reference recurrence and fail with coordinate plus raw expected/actual words on mismatch.
- [ ] Run the shell test and verify the selected `(2,3,5)` case passes.

### Task 3: Report normalized metrics and validate input failures

**Files:**
- Modify: `csrc/main_matrix_gemm_p32.cpp`
- Modify: `test_src/test_matrix_gemm_p32.sh`

**Interfaces:**
- Summary labels: `shape`, `requests`, `valid_mac_terms`, `cycles`, `request_per_cycle`, `mac_per_cycle`, `lane_utilization`, `max_chain_latency`, and `exact_mismatches`.
- CSV header and one CSV data record use those field names plus `seed`.

- [ ] Extend the integration assertions to inspect the CSV header/data record as well as the readable summary.
- [ ] Add runner checks for zero dimensions, unknown options, and missing option values; each must exit nonzero without instantiating the DUT benchmark.
- [ ] Re-run the shell test, a K-tail case, and an invalid-dimension command.

### Task 4: Verify source boundaries and regressions

**Files:**
- Modify: no source unless verification finds a defect.

- [ ] Run `make config.h && test_src/test_matrix_gemm_p32.sh` with the matrix configuration.
- [ ] Run `make verilog` to ensure unchanged Chisel still elaborates.
- [ ] Switch only `.config` to `CONFIG_PVU_MAC_REGRESSION=y`, regenerate `config.h`, and run `make run`; then restore the matrix selection.
- [ ] Inspect `git diff` and `git status`; confirm no source RTL changes, no staged files, and no changes outside the benchmark scope.

## Self-Review

- Spec coverage: Task 1 supplies Kconfig and a red integration test; Task 2 supplies raw-P32 GEMM mapping, tail handling, scheduling, and exact oracle; Task 3 supplies report and invalid-input behavior; Task 4 covers elaboration and legacy regression.
- Placeholder scan: every produced option, command, metric, and test shape is named with an exact expected behavior.
- Type consistency: Task 2's `WorkloadMetrics` flows into Task 3's summary and CSV report; Task 1's Make variable is consumed by Task 2's option parser.
