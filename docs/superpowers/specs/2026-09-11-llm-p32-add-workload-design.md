# LLM Posit32 Add Workload Design

## Goal

Replay actual elementwise binary additions observed while the selected local
LLM executes its forward pass. The workload converts both FP32 operands to
Posit32, sends four lanes through PVU \`op=1\`, and reports exact Posit32
conformance, ready/valid throughput, and an observational comparison with
the captured FP32 result.

## Trace contract

The current linear-projection trace does not record the operands of a model
addition, so the exporter gains an Add capture mode. During a normal offline
model forward pass it records binary tensor additions whose two operands and
result have the same non-empty rank-2 feature shape after removing a leading
batch dimension of one. Thus every replayed operand pair was used by the real
model, while broadcast additions and scalar bookkeeping are excluded.

Each captured operation has a unique, safe artifact prefix and stores
little-endian FP32 \`lhs\`, \`rhs\`, and \`output\` arrays plus their shapes in
metadata. The original output is used only for FP32 numerical observation;
the exact functional oracle is independently computed from the two converted
Posit32 operands with SoftPosit \`p32_add\`.

Add traces use a workload-specific default directory under \`/tmp\` so a new
Add export never overwrites a reusable version-2 linear/MAC trace. \`make
run\` exports a missing Add trace from the local selected model, offline.

## Kconfig and workload behavior

\`LLM_P32_ADD_WORKLOAD\` is mutually exclusive with the existing LLM P32
workloads. It selects a per-operation request count of 256, 1024, or 4096
and a dedicated Add trace directory. The make route supplies only the Add
trace root and selected request count to the runner.

Sampling clamps the requested request count per captured operation to its
available scalar elements. It chooses deterministic, evenly spaced elements,
then packs them in order into up-to-four-lane requests. The final request is
allowed to have one to three active lanes and the runner must not compare
inactive lanes.

The Verilator driver uses tagged ready/valid transactions, \`op=1\`, 32-bit
Posit source/destination widths, Posit input/output modes, and the active
lane count as \`vector_size\`. It fails on response operation/tag errors or a
raw Posit mismatch, and prints trace source, sampled elements, request and
element throughput, exact mismatch count, FP32 observation metrics, and
\`conformance: PASS|FAIL\`.

## Validation

A C++ helper regression covers Add artifact sampling, clamping, malformed
shape/data rejection, and command-line parsing. The exporter unit regression
checks the Add metadata and byte artifacts. Kconfig dry-run coverage checks
the mutually exclusive selection and argument forwarding. A dedicated
fixture validates full and partial vectors through Verilator; an isolated,
temporary selected-model configuration provides the final \`make run\` route.
