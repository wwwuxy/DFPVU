# Multi-linear projection and Posit conversion workloads

## Goal and boundaries

Extend the existing LLM trace-backed workloads into a reproducible experiment
suite for decoder-layer linear projections and format conversion. The suite
will replay captured FP32 `input`, `weight`, and pre-bias `output` values from
a real local model forward pass. It will not claim coverage of complete
Attention, Softmax, RMSNorm, residual addition, SwiGLU, RoPE, or end-to-end
model quantization. A fused Phi projection remains a single reported module;
the implementation must not manufacture separate Q, K, and V measurements.

The test suite has two independent conclusions:

1. Hardware-versus-SoftPosit exact comparison establishes the correctness of
   the selected PVU operation for the replayed values.
2. Comparisons against captured FP32 values describe numerical observations of
   a format conversion or sampled linear tile. They are not model-quality
   measurements.

## Trace migration and contract

The profile-default linear trace directories become:

| Profile | Model root | linear trace root | Add trace root |
| --- | --- | --- | --- |
| Qwen3-0.6B | `/root/models/Qwen3-0.6B` | `/tmp/qwen3-0.6b-p32-linear-trace` | `/tmp/qwen3-0.6b-p32-add-trace` |
| Gemma 3 1B | `/root/models/gemma-3-1b-it` | `/tmp/gemma-3-1b-p32-linear-trace` | `/tmp/gemma-3-1b-p32-add-trace` |
| Phi-4-mini | `/root/models/phi-4-mini-instruct` | `/tmp/phi-4-mini-p32-linear-trace` | `/tmp/phi-4-mini-p32-add-trace` |

The new linear roots are deliberately distinct from the legacy single-module
roots. No existing trace is overwritten. A profile-default export invokes the
exporter without `--module`, which uses its existing discovery rule to capture
every invoked rank-2 linear module in the selected decoder layer. The exporter
records `linear_module_scope: "all-decoder-linear"` in metadata. A
profile-default preflight requires that value and the selected profile. Custom
paths remain opt-in and may contain a manually exported subset, preserving the
existing customization escape hatch.

The exporter retains repeatable `--module` for manual subset exports. Such a
trace is marked as an explicit subset, rather than being accepted as an
all-linear default trace. Module names and shapes are retained verbatim in the
metadata and workload report.

## Workloads

### Linear MAC (op=11)

The existing MAC runner replays every module in the trace. It will report each
module's captured input and weight shapes, the selected replay M/N/K tile, the
number of MAC terms, cycles, requests per cycle, MAC terms per cycle, lane
utilization, exact mismatch count, and P32-versus-FP32 observations. The
shared Kconfig MNK preset remains a bounded replay tile. Reports must label it
as such, not as a complete native GEMM for every projection.

The default bundle exposes attention projections and MLP projections whenever
the model exposes them as separate rank-2 modules. For Phi, the report uses
the actual fused path, for example `qkv_proj`, rather than inventing logical
sub-projections.

### IEEE FP32 and Posit32 conversion (op=7)

The existing conversion runner already accepts `input`, `weight`, and
`output`. Its fixture and Kconfig-route tests will run each source separately
against the multi-module trace. Each run reports both directions, elements per
cycle, cycles, exact conversion mismatches, ULP distribution, maximum and mean
relative error, absolute error, zero references, and special values.

### Posit32 to Posit16 conversion (op=6)

A new mutually exclusive `LLM_P32_TO_P16_WORKLOAD` reuses the same tensor
source and sample-count selection as op=7. It converts sampled FP32 values to
Posit32, replays the Posit32 values through `op=6` with 16-bit destination
width, and compares the raw result using the established SoftPosit
`p32_to_pX2(..., 16)` convention used by the protocol regression. It expands
the Posit16 result back to Posit32 only for numerical observation against the
captured FP32 value. The report contains exact mismatch count, elements per
cycle, cycle count, and P16 reconstruction error statistics. This is an
interface-width experiment, not a claim of whole-model P16 quantization.

## Configuration and build routing

`LLM_P32_TO_P16_WORKLOAD` joins the existing exact-one LLM workload guard and
the SoftPosit prerequisite. The conversion tensor and sample choices are
visible when either op=7 or op=6 workload is selected. Make forwards the
normal linear trace root, source, and sample count to either runner. The
profile-default export has no hard-coded module path; Add remains on its
dedicated trace root and continues to use `--capture-add`.

The SoftPosit reference subset is extended only with the required P16
conversion/re-expansion implementation. The production oracle follows the
existing protocol driver's raw-bit alignment semantics instead of introducing
a second format encoding convention.

## Regression strategy

1. Exporter unit tests verify all-module discovery on a multi-projection toy
   decoder, deterministic metadata scope, byte artifacts, and preservation of
   explicit-subset behavior.
2. Trace-reader fixtures contain representative attention and MLP module
   names, including `q_proj`, `k_proj`, `v_proj`, `o_proj`, `gate_proj`,
   `up_proj`, and `down_proj`. They check names, shapes, and artifacts without
   pretending the fixture is a full model.
3. Kconfig dry-run tests assert the new profile-default linear roots for
   Qwen, Gemma, and Phi, ensure default exports omit `--module`, and preserve
   Custom-path routing.
4. Verilator fixture tests assert per-module op=11 reports, execute op=7 for
   input, weight, and output independently, and execute op=6 for each source
   with both full and final-partial vectors. Every functional fixture requires
   zero hardware-versus-SoftPosit mismatches and `conformance: PASS`.
5. An integration regression uses a temporary copy of the repository and a
   temporary configuration to run `make run` against a checked-in fixture. It
   proves the `.config -> config.h -> selected C++ driver -> runner` path while
   leaving the user's working-tree `.config` untouched.

The existing Add fixture regression remains separate because its operands are
captured model additions, rather than being synthesized from linear tensors.

## Deferred trace-dependent work

QK^T and P-times-V require captured Q/K activations and post-Softmax
probabilities. RoPE requires Q/K plus sine and cosine operands. SwiGLU,
residual, and RMSNorm require their own operand traces; RMSNorm additionally
needs an operation or approximation for square root/reciprocal square root.
Those workloads are excluded from this implementation and require a separate
trace-contract design before code changes.
