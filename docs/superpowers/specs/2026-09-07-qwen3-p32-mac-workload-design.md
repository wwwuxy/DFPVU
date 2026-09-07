# Qwen3-0.6B Posit<32,2> MAC Workload Design

## Goal

Exercise the existing DFPVU four-lane `op=11` MAC on real Qwen3-0.6B
Prefill tensors, while preserving an exact SoftPosit hardware oracle and
reporting numerical loss relative to floating-point references.

## Scope

- Model: local `/root/models/Qwen3-0.6B`, loaded by Python 3.12 with
  Transformers 5.16.1 and PyTorch 2.14.0+cpu.
- Workload: a deterministic 16-token Prefill invocation, layer 0 only.
- Linear operators: `q_proj`, `k_proj`, `v_proj`, `gate_proj`, `up_proj`,
  and `down_proj`.
- Default replay sample: token index 0 and output rows `[0, 4)` for every
  listed projection.  The runner must support a positive token count and
  output-row count so a user may enlarge the sample without changing source.
- DFPVU operands: every activation, weight and recurrence value sent to the
  DUT is a raw `Posit<32,2>` bit pattern.  Model BF16 is used only to obtain
  numerical values at export time; host-side SoftPosit converts those values
  to raw P32 before the first DUT request.

## Non-goals

- This workload is not an end-to-end Qwen generation benchmark and must not
  report tokens/s or whole-model TOPS.
- It does not add BF16 hardware conversion, tiled GEMM storage, DMA, SRAM,
  KV cache, attention scheduling, RMSNorm, RoPE, Softmax, or activation
  functions.
- It does not change `PvuTop.scala` or the `op=11` datapath.

## Data Flow

```text
Qwen3 16-token Prefill
  -> PyTorch forward pre-hooks on layer-0 Linear modules
  -> trace directory: metadata + f32 activation/weight/reference-output files
  -> C++ runner: f32 values -> SoftPosit P32 raw words
  -> per output: op11(c=0, four K terms), then op11(c=previous result, ...)
  -> DFPVU/Verilator response stream
  -> exact SoftPosit comparison + FP32 numerical-loss report + cycle report
```

## Trace Contract

The exporter writes a user-chosen output directory that is not checked into
the repository.  It contains:

- `metadata.json`: format version, model path, model configuration,
  library versions, prompt, input token IDs, layer index, selected modules,
  tensor shapes, and element type (`float32`, little endian).
- `<module>.input.f32`: contiguous row-major tensor `[tokens, in_features]`.
- `<module>.weight.f32`: contiguous row-major PyTorch Linear weight tensor
  `[out_features, in_features]`.
- `<module>.output.f32`: contiguous row-major PyTorch module output
  `[tokens, out_features]`.

The C++ runner accepts the trace root as its only required command-line
argument.  It validates the metadata format version, module names, required
files, tensor byte counts, the 16-token baseline, and that every selected
`in_features` is divisible by four.  It rejects malformed or incompatible
traces with a nonzero exit status and a useful error.

## MAC Mapping

For each selected module, token `t`, and output row `o`, the software mapping
is `y[t,o] = sum_k(input[t,k] * weight[o,k])`.  Qwen3-0.6B linear layers in
the selected scope have no bias; the runner must reject a future trace that
declares one rather than silently omit it.

K is partitioned into consecutive groups of four.  A group produces one
`op=11` request with `posit_i1[lane] = input[t,k+lane]` and
`posit_i2[lane] = weight[o,k+lane]`.  The initial request uses `posit_i3=0`;
each later request uses the raw P32 response from its predecessor.  Thus the
hardware and oracle both use the implementation's sequential four-term fused
P32 rounding.  The runner issues the next group only after receiving the
previous output for that element, while different output elements may remain
serial in this first correctness-oriented implementation.

## Oracles and Metrics

For each output element the runner computes, in the same group and lane order:

1. SoftPosit `p32_mulAdd` recurrence.  The DFPVU response must match this raw
   P32 word exactly; any mismatch is a test failure.
2. A host FP32 recurrence using the exported numeric activation and weight
   values.  This isolates Posit quantization and per-group recurrence effects.
3. The exported PyTorch module output.  This records the model-library result
   and may differ from the host recurrence because its kernel reduction order
   differs; it is reported separately rather than used as a bit-exact oracle.

The report prints counts per module and overall: exact mismatches, finite
samples, zero/reference-special samples, ULP bins (`0`, `1`, `2-4`, `>=5`),
maximum relative error, mean relative error, and maximum absolute error.  ULP
is IEEE FP32 distance between the Posit result converted to FP32 and the host
FP32 recurrence.  The runner also reports the same non-bit-exact error
summary against the exported PyTorch output.

Cycle reporting counts explicit rising clock edges in the driver from reset
release through the final output handshake.  It reports request count, active
four-term MAC count (`requests * 4`), cycles, requests/cycle, MAC terms/cycle,
and lane utilization.  The default full-four-lane trace must report 100%
lane utilization.  These are Verilator workload measurements only, not a
post-synthesis frequency or system throughput claim.

## Build and Configuration

Add one exclusive Kconfig option named `QWEN3_P32_MAC_WORKLOAD` under the
existing Posit<32,2> test choice.  Selecting it builds one new guarded C++
main.  The existing SoftPosit reference library rule applies to it.  `make
menuconfig`, `make verilog`, and `make run` remain the user workflow.  The
runner reads `QWEN3_TRACE_DIR` when set and otherwise uses
`test_src/qwen3-0.6b-trace`.

## Acceptance Criteria

1. A deterministic exporter runs in the validated Python 3.12 environment
   against the downloaded Qwen3-0.6B model and produces a self-describing
   16-token layer-0 trace.
2. The C++ runner refuses invalid trace metadata and truncated binary files.
3. A small checked-in fixture trace lets the workload configuration compile
   and execute without model files.
4. On an exported real trace, every DFPVU result is bit-exact to the SoftPosit
   recurrence; the runner exits nonzero on any mismatch.
5. The report contains both requested ULP distribution and maximum/mean
   relative-error metrics, plus request/cycle/lane metrics and clear labels
   for the two floating-point baselines.
6. Existing `PVU_MAC_REGRESSION` remains passing and the new configuration
   does not alter `PvuTop.scala`.
