# Pipelined Correct DFPVU Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox syntax.

**Goal:** Deliver a functionally verified, ready/valid pipelined PvuTop that closes Nangate45 10 ns timing.

**Architecture:** First make the arithmetic oracle and all existing operation paths trustworthy. Then capture complete requests, execute fixed-latency operations through registered stages, isolate division as a backpressured multi-cycle lane, and emit tagged buffered responses.

**Tech Stack:** Chisel 6, CIRCT, Verilator/C++, SoftPosit, OpenROAD/ORFS Nangate45.

**Spec:** `docs/superpowers/specs/2026-08-28-pipelined-correct-pvu-design.md`

## Global Constraints

- Preserve encoded Posit/FP boundary semantics and do not hand-edit `vsrc/PvuTop.sv`.
- Requests and responses use valid/ready plus tags; results must remain stable under response backpressure.
- 100 MHz Nangate45 closure requires non-negative setup WNS/TNS, plus zero DRC, antenna and routing overflow.
- Power remains default-activity estimation unless a documented VCD/SAIF is provided.

### Task 1: Establish operation-complete reference regressions

**Files:** Create `csrc/main_pvu_protocol_regression.cpp`; modify `Kconfig`, `makefile`; reuse exact binary data under `test_src/`.

**Interfaces:** The driver sends `PvuRequest` values and checks `PvuResponse(tag, outputs)` only on valid/ready handshakes.

- [ ] Write failing directed dot tests for positive+negative cancellation: `{+1,-1,+2,-2}·{+1,+1,+1,+1}=0`, plus NaR, zero, extrema and mixed signs; record exact SoftPosit bits.
- [ ] Run `make config.h && make run` with dot selected; expect mismatch on the cancellation vector before fixing arithmetic.
- [ ] Add seeded SoftPosit reference vectors and the same boundary categories for opcodes 1–10 and FP modes 0–4; print per-operation sample/mismatch counts.
- [ ] Commit test infrastructure: `git add Kconfig makefile csrc/main_pvu_protocol_regression.cpp test_src && git commit -m "test: cover all PVU operation paths"`.

### Task 2: Correct dot-product signed reduction

**Files:** Modify `src/main/scala/pvu/DotProduct.scala`; test `csrc/main_pvu_protocol_regression.cpp`.

- [ ] Confirm the new cancellation test fails because `CsaTree.operands_i` consumes unsigned `pir_frac_cmp` rather than the converted signed terms.
- [ ] Replace the unused registered `pir_frac_cmp_tmp` with a combinational, explicitly sized signed operand vector; feed the CSA/reduction implementation the two's-complement aligned terms and sign-extend each operand to `SUM_WIDTH`.
- [ ] Run `make verilog`, then the dot and complete operation regression; require zero mismatches.
- [ ] Commit: `git add src/main/scala/pvu/DotProduct.scala csrc test_src && git commit -m "fix: accumulate signed dot-product terms"`.

### Task 3: Add transactional top-level protocol

**Files:** Modify `src/main/scala/pvu/PvuTop.scala`; modify all C++ drivers in `csrc/`; update `README.md` only if interface documentation changes.

- [ ] Write a failing driver test that presents distinct tagged requests while `out_ready=0`; assert no response loss/reordering and stable output while stalled.
- [ ] Define `PvuRequest`/`PvuResponse` Bundles and add `in_valid`, `in_ready`, `in_tag`, `out_valid`, `out_ready`, `out_tag`, `out_op` to `PvuTop`.
- [ ] Register all request operands/controls on `in_valid && in_ready`; provide a one-entry response buffer obeying ready/valid stability; update drivers to sample only completed responses.
- [ ] Run `make verilog && make config.h && make run`; commit protocol and driver changes.

### Task 4: Pipeline datapaths and isolate division

**Files:** Modify `PvuTop.scala` and only the arithmetic modules whose stage boundaries require explicit registers.

- [ ] Add a failing latency/throughput test: accept adjacent non-division requests, prove tags remain ordered, and prove division causes `in_ready=0` only while its lane is occupied.
- [ ] Register decode, core execution, normalize/round, and encode/response boundaries. Add dot-product product, signed-reduction, and normalize stages. Keep each pipeline register's tag/op/control beside its data.
- [ ] Implement a division lane with explicit busy state and response handoff; do not allow a new division to overwrite its in-flight state.
- [ ] Run all functional regressions and a Verilator lint build; commit `feat: pipeline PVU execution`.

### Task 5: Re-measure and gate PPA

**Files:** Modify only `openroad/` PPA inputs if the new top-level port list requires SDC/runner adaptation; generated results stay ignored.

- [ ] Run the complete operation regression first; stop on any mismatch.
- [ ] Run `PPA_CLOCK_PERIOD_NS=10.0 NUM_CORES=16 bash openroad/run_nangate45_ppa.sh`.
- [ ] Validate `ppa-summary.json`, `6_finish.rpt`, WNS/TNS ≥ 0, and DRC/antenna/overflow = 0; otherwise record the limiting stage without publishing a successful PPA result.
- [ ] Commit only source/config/test changes; never commit `vsrc/`, `obj_dir/`, waveforms or `openroad/results/`.

## Self-review

Task 1 establishes a complete oracle; Task 2 fixes the known numerical defect; Tasks 3–4 implement protocol and timing architecture; Task 5 enforces the functional and physical acceptance gates. All interfaces, test commands and output conditions are explicit.

## Execution amendment: operation-complete correctness phase

The Task 1 oracle exposed pre-existing residuals in every non-dot operation family. Before the original Task 3, execute these bounded repair tasks and require the affected exact-reference bucket to reach zero mismatches:

1. Raw Posit special-value propagation and raw max/min selection (NaR, zero, extrema) for ops 1–5, 8 and 9.
2. Posit32-to-int SoftPosit-compatible RNE, saturation and NaR handling for op10.
3. Runtime P32-to-P16 left-aligned RNE conversion for op6.
4. Posit-to-FP finite, normal/subnormal and NaR packing for op7 modes 0–4.
5. FP-to-Posit subnormal normalization, range/saturation and single-rounding for op7 modes 0–4.
6. Shared add/sub alignment, canonical zero and GRS/RNE.
7. Multiplication signed exponent/product normalization, followed by dot semantic closure against the sequential p32_mulAdd oracle.
8. Division quotient, normalization and special-value rewrite.

Do not advance to transactional pipelining or PPA until the full protocol regression reports zero mismatches. Each amendment task needs directed red/green vectors, fresh Verilog generation, exact-reference regression, commit, and review.
