# Ara 4-Lane OpenROAD PPA Design

## Goal

Produce a reproducible OpenROAD implementation-cost dataset for the local Ara
four-lane configuration and present it beside the existing DFPVU PPA table
without claiming a scope-normalized PPA advantage.

## Chosen boundary

- Top module: `ara` in `/root/ara/hardware/src/ara.sv`.
- Parameters and defines: `NR_LANES=4`, `VLEN=4096`, and
  `ARIANE_ACCELERATOR_PORT=1`, derived through Ara's Bender flow.
- Clock: the real top-level `clk_i` port. `rst_ni` is excluded from input delay
  constraints.
- Platform and physical policy: OpenROAD-flow-scripts `nangate45`, 45.00 ns
  target period, 55% core-utilization target, four OpenROAD threads, and the
  same stages reported for DFPVU (synthesis through global route plus a
  detailed route limited with `DETAILED_ROUTE_END_ITERATION=1`).

`ara` is intentionally not replaced by an arithmetic-only wrapper: it exposes
the actual local four-lane RVV coprocessor boundary, including its vector
register file, memory interface, dispatch, and control logic. It will therefore
be reported as a system-scope Ara baseline rather than a functionally identical
DFPVU counterpart.

## Components

1. An Ara-specific OpenROAD config in the DFPVU workspace references the
   Bender-generated ordered file list and selects `ara` as design top.
2. A dedicated SDC declares `[get_ports clk_i]` as the clock and applies the
   same 20% IO delay convention as DFPVU, excluding clock and reset.
3. A small regression verifies that the SDC binds a physical `clk_i` port and
   that the config retains the expected 45 ns/core-utilization defaults.
4. The run outputs remain under `openroad/ara_4lane_45ns/`, separate from all
   DFPVU evidence.
5. The result table reports scope, mapped area/count, floorplan dimensions,
   CTS, timing, routing, DRC, and the vectorless-power caveat. It adds an Ara
   row to the comparison boundary, but never computes an unqualified area,
   power, or energy ratio.

## Failure handling

- If the Bender file list cannot parse in Slang/Yosys, retain the generated
  list and report the first front-end error; do not silently drop packages or
  interfaces.
- If `ara` uses parameter types unresolved by the top-level Bender target,
  synthesize a transparent elaboration wrapper only after documenting its type
  bindings and validating the source list. The wrapper must have only the
  original clock/reset and ports, with no functional pruning.
- A virtual clock, zero CTS clock nets, or no sequential launch/capture paths
  invalidates the run.
- Nonzero WNS/TNS or DRC values are reported verbatim as near-closure or
  non-signoff conditions; they are not converted into an optimistic frequency
  or manufacturability claim.

## Acceptance criteria

- The constrained `clk_i` is recognized by CTS and reports a nonzero clock-net
  and sink count.
- Every reported numeric value has a raw log/report path under the Ara output
  directory.
- The paper-facing result includes the exact implementation boundary and the
  DFPVU/Ara comparability warning.
