# Pipelined and Functionally Verified DFPVU Design

## Objective

Replace the current fully combinational `PvuTop` execution path with a
transactional pipelined datapath that closes the Nangate45 10 ns target while
preserving defined Posit and floating-point bit-level behavior.  Fix the known
dot-product signed-accumulation defect and make every supported operation
subject to reproducible functional and PPA gates.

## Interface

`PvuTop` receives a request channel and emits a response channel:

- `in_valid`, `in_ready`, and `in_tag` accompany all current operand, opcode
  and format-control inputs.
- `out_valid`, `out_ready`, and `out_tag` accompany existing result outputs
  and an `out_op` value identifying the completed request.
- A request transfers only when `in_valid && in_ready`; a response transfers
  only when `out_valid && out_ready`.
- Results remain stable while `out_valid && !out_ready`.

The top may accept one request per cycle when its selected execution lane has
capacity.  Division is a separate multi-cycle lane and deasserts `in_ready`
when it cannot accept another division request.  All other operations use
fixed-latency pipelined lanes.

## Pipeline

Common request state is captured before computation, so controls cannot change
under an in-flight operation.  Non-division operations are partitioned into:

1. request capture and Posit/float decode;
2. arithmetic, comparison, conversion, or dot-product multiplication;
3. alignment/reduction where applicable, normalization and rounding;
4. encoding, result selection and response buffering.

The dot-product lane adds a registered signed reduction stage between product
alignment and normalization.  The implementation must use one explicit signed
representation for aligned product terms and feed that representation to the
reduction tree; no unused converted copy is permitted.

## Correctness contract

The current opcode and format meanings remain unchanged.  Operation-specific
latency may change, but output bit patterns must match the existing reference
model for Posit32 default configuration, except for a documented correction to
the previously erroneous dot-product signed accumulation.

Functional validation covers Add, Sub, Mul, Div, DotProduct, Posit conversion,
Float↔Posit conversion for FP4/FP8/FP16/FP32/FP64, Greater, Less and
Posit-to-Int.  Each operation must have exact encoded directed vectors for
zero, NaR/NaN, infinities where applicable, extrema, opposite signs, rounding
ties, overflow/underflow and variable-vector-size boundaries, plus seeded
random reference-model vectors.  The testbench obeys the new ready/valid
protocol and checks tags, latency-independent ordering and backpressure.

## Acceptance gates

An RTL change is accepted only when:

- every operation's Verilator regression passes with zero mismatches;
- dot product includes mixed-sign and cancellation cases that fail with the
  old unsigned accumulation behavior;
- no input/control value is observed to affect an accepted request after its
  handshake;
- the Nangate45 10 ns ORFS flow finishes with DRC, antenna and routing
  overflow all equal to zero;
- the final timing report has non-negative setup WNS/TNS; and
- the PPA summary records the exact RTL, ORFS and OpenROAD revisions and
  labels power as default-activity estimation unless a workload VCD/SAIF is
  explicitly supplied.
