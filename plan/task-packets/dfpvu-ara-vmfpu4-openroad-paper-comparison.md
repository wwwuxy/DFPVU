# Task Packet: DFPVU–Ara VMFPU4 OpenROAD Paper Comparison

- Scope: Create English and Chinese paper-facing comparison records for DFPVU `PvuTop` and Ara's four-`vmfpu` arithmetic boundary. Explain every available implementation metric, separate measured observations from normalized claims, and identify only evidence-supported DFPVU advantages.
- Files to read: `docs/result/dfpvu_openroad_ppa_45ns.md`, `docs/result/ara_vmfpu4_openroad_compute_cost_45ns.md`, `docs/result/dfpvu_vs_ara_ipdps_en.md`, `docs/result/ara_4lane_workload_results.md`, `docs/result/matrix_gemm_p32_mnk_results.md`, and their Chinese counterparts where available.
- Files allowed to edit: `docs/result/dfpvu_ara_vmfpu4_openroad_comparison_45ns.md`, `docs/result/dfpvu_ara_vmfpu4_openroad_comparison_45ns_zh.md`, the experiment protocol, traceability map, table schema, notes, progress record, and this task packet.
- Required skills: paper orchestration, experiment-results planning, writing core, peer review, and verification; use brainstorming approval before drafting.
- Evidence/data inputs: Nangate45, 45.00 ns, and 55% target-utilization configurations; DFPVU and Ara VMFPU4 OpenROAD reports; existing four-lane RTL direct-workload and matrix results.
- Required artifacts: bilingual comparison pages, a scope/metric/claim boundary, supported-advantage table, IPDPS-ready prose, planning-record updates, and two reviews.
- Rejection checks: no full-Ara data; no normalized DFPVU/Ara area, frequency, power, energy, or efficiency ratio; no positive use of trace-derived DFPVU MAC or Gemma dot correctness-failure rows; no workload power or signoff claim; no claim of Posit accuracy superiority over FP32.
- Validation commands: verify source values and document values by literal scans; check bilingual file existence and key metric consistency; scan for removed full-Ara document names and prohibited comparative claims; run Markdown style checks if available; run `git diff --check`.

## Capability-use audit

- Required skills: paper orchestration, experiment-results planning, writing core, peer review, and verification.
- Skills used: the research-writing workflow established a traceable result packet, result-planning separated raw implementation cost from performance evidence, writing-core guided bilingual paper prose, peer review produced independent scope and quality reviews, and verification requires source/value scans before submission.
- Inputs used: the DFPVU and Ara VMFPU4 Nangate45 records plus the existing four-lane direct-workload and matrix-cycle records.
- Inputs deliberately not used: obsolete complete-Ara OpenROAD records, failing trace MAC and Gemma dot rows, unsupported power/frequency/energy comparisons, and any Posit-accuracy assumption.
- Artifacts: two bilingual comparison pages, protocol/traceability/schema updates, and two review records.
- Verification status: passed literal source/value scans, bilingual source-existence checks, obsolete-record scan, and staged-diff validation. No repository Markdown-lint configuration was available; Markdown structure was manually inspected.
