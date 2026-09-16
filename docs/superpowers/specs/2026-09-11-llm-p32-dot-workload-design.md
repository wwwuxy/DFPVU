# LLM Posit32 Dot Workload Design

## Goal

Replay real linear-projection `input x weight` vector quads from the selected
local LLM trace through PVU operation `op=5`, then report exact Posit32
conformance, throughput, and the observation-only difference from FP32 dot
products.

## Scope

The workload is mutually exclusive with the existing LLM MAC and conversion
workloads. It uses the existing profile, trace-directory, offline export, and
metadata profile validation paths. No RTL change, model download, or trace
format change is required.

## Sampling

For a linear trace whose input shape is `[tokens, K]` and weight shape is
`[N, K]`, a Dot request contains four adjacent terms:

`dot(input[token, k:k+4], weight[row, k:k+4])`.

The valid coordinate space is `tokens * N * (K / 4)`. Requested samples are
clamped to that space and selected evenly, including the first and last valid
coordinate. The flattening order is K-group, then output row, then token.
Both operands must be rank-2, share K, contain exactly their declared number
of values, and have K divisible by four.

## Kconfig and command line

`LLM_P32_DOT_WORKLOAD` is a top-level workload choice. Its profile selection
uses the shared LLM profile section. A separate choice provides exactly
256, 1024, or 4096 Dot vectors per exported module and materializes its value
in `LLM_P32_DOT_SAMPLE_COUNT`. `make run` passes only trace root and the
selected sample count to the Dot runner.

## Oracle and report

Each vector is converted to raw Posit32 and evaluated by the exact SoftPosit
recurrence `p32_mulAdd(lhs, rhs, acc)` from zero over its four lanes. The
hardware response must have the same tag and raw result. FP32 uses the same
four operands accumulated with `std::fma` only for numerical-loss reporting.

The runner prints one metric per line: module count, requested and sampled
vectors, requests, valid MAC terms, cycles, request/Dot/MAC-terms per cycle,
exact mismatches, FP32 comparison statistics, and `conformance: PASS|FAIL`.

## Tests

A C++ helper regression proves coordinate sampling, clamping, malformed-trace
rejection, and command-line parsing. A shell regression checks the Kconfig
command. A Verilator fixture run checks tags, exact four-lane SoftPosit
outputs, metrics, and PASS. An actual selected-model trace run verifies the
full Kconfig route.
