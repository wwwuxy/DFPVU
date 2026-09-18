# Task Packet: DFPVU IPDPS Paper Revision Guide

- Scope: Produce a detailed Chinese guide for revising the DFPVU manuscript for IPDPS 2027 using the existing RTL-workload and OpenROAD evidence. The guide must not require additional RTL fixes or new PPA runs.
- Target artifact: `docs/result/dfpvu_ipdps_paper_revision_guide_zh.md`.
- Files to read: all maintained Markdown summaries under `docs/result/`, `plan/project-overview.md`, `plan/outline.md`, `plan/experiment-protocol.md`, `plan/review/method-experiment-traceability.md`, and `tables/table-schema.md`.
- Files allowed to edit: the target guide, this task packet, `plan/progress.md`, and `plan/notes.md`.
- Required skills: paper orchestration, writing core, peer review, and verification; use the approved bounded-design workflow.
- Evidence/data inputs: the 21-case direct-workload matrix, the 15-shape synthetic GEMM baseline, the 45 trace-derived GEMM tiles, the four-lane Ara workload results, and the DFPVU/Ara VMFPU4 OpenROAD mapped-cost records.
- External requirements: official IPDPS 2027 call for papers and reproducibility initiative only; do not infer venue rules from secondary sources.
- Required artifacts: venue constraints, recommended paper positioning, claim/evidence boundary, section-by-section revision instructions, table/figure plan, page budget, safe wording examples, prohibited claims, reproducibility plan, and final revision checklist.
- Rejection checks: do not require a hardware change or new PPA run; do not present trace-derived microbenchmarks as end-to-end inference; do not claim full MAC conformance; do not form normalized area/power/energy/frequency ratios; do not characterize either implementation as signoff-clean.
- Validation commands: source-value scans, link/path existence checks, prohibited-claim scans, Markdown structure/style inspection, `git diff --check`, and final worktree status review.

## Review gates

- Spec review: confirm every required artifact and rejection check is represented in the guide.
- Quality review: confirm that all numerical claims trace to maintained result pages, current IPDPS requirements cite official pages, and the guide remains actionable within ten manuscript pages.

