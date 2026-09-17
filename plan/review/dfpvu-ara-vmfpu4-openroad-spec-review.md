# Specification Review: DFPVU–Ara VMFPU4 OpenROAD Comparison

## Review scope

This review checks `docs/result/dfpvu_ara_vmfpu4_openroad_comparison_45ns.{md,zh.md}` against the task packet and source records. The reviewed target is DFPVU `PvuTop` versus four real Ara lane-local `vmfpu` instances, not a complete Ara accelerator.

## Scope and metric traceability

| Requirement | Evidence checked | Review result |
|---|---|---|
| Replace the non-equivalent full-Ara baseline | Ara VMFPU4 cost record explicitly excludes VRF, LSU, dispatcher, memory interface, and interconnect | Satisfied |
| State common physical conditions | Both comparison pages list Nangate45, 45.00 ns physical-clock constraint, and 55% target utilization | Satisfied |
| Report available mapping and floorplan metrics | Mapped cells, standard-cell area, DFF category, core area, die area, and utilization match the two cost records | Satisfied |
| Explain physical-stage data | Both pages distinguish DFPVU post-GRT/limited route from Ara screening CTS | Satisfied |
| State DFPVU advantages only from evidence | Capability, raw mapping observation, passing direct-workload results, matrix cycles, and tail cycles are separated | Satisfied |
| Supply IPDPS-ready prose | Both pages contain a qualified paper-ready interpretation | Satisfied |

## Claim-boundary audit

| Prohibited or high-risk claim | Treatment in the reviewed pages | Review result |
|---|---|---|
| Full-Ara versus DFPVU arithmetic comparison | Not used; the pages explicitly say this comparison replaces the complete-Ara baseline | Pass |
| Normalized area, PPA, power, energy, frequency, or area-efficiency superiority | Explicitly disallowed; raw mapped values and absolute deltas are labelled as boundary observations | Pass |
| Positive use of failing trace MAC or Gemma dot rows | The 13/10/3 MAC mismatches and one Gemma-dot mismatch are excluded | Pass |
| Posit accuracy superiority | Explicitly prohibited without end-to-end model evidence | Pass |
| Signoff, DRC-clean, route-complete, or workload-power assertion | Explicitly prohibited; both implementations' physical limitations are recorded | Pass |

## Review conclusion

The documents satisfy the agreed scope: they provide reproducible raw hardware-cost observations and separately qualified cycle-efficiency evidence without promoting either into an unsupported normalized PPA conclusion. The remaining scientific risk is inherent operation-set non-equivalence, which is disclosed at the beginning, in every relevant table interpretation, and in the wording limits.
