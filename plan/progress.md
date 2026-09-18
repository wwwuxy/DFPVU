# Progress

## Current Stage

S3/S5: experimental-results comparison, paper-facing OpenROAD analysis, and review.

## Decisions

- Target venue: IEEE IPDPS.
- Primary language: English; a Chinese companion version is also required.
- Main argument: RVV-compatible high-throughput Posit/IEEE dual-format computation with low scheduling overhead.
- Evidence boundary: four-lane RTL cycle results plus raw OpenROAD mapped-cost observations for DFPVU and Ara VMFPU4. Normalized PPA ratios remain prohibited.

## Status

- Scope and section structure confirmed by the user.
- Experiment protocol, traceability map, table schema, and task packet created.
- Numerical ratios recomputed from the verified DFPVU and Ara results.
- English and Chinese comparison drafts written under `docs/result`.
- Spec-compliance and quality reviews passed.
- A clock-bound 45 ns OpenROAD PPA baseline is available for DFPVU. Four Ara VMFPU instances now provide a closer arithmetic-boundary mapped-cost reference under the same Nangate45/45 ns/55% policy. Both designs retain explicit stage and semantic limits, so the paper reports no normalized PPA ratio.



### Capability-use audit

- Required skills: paper orchestration, research brainstorming, experiment-results planning, statistical analysis, writing core, and verification.
- Skills actually used: all required skills above.
- Inputs consumed: DFPVU direct workload results, DFPVU matrix results, Ara four-lane Markdown/CSV results, and Ara kernel structure.
- Inputs not used and why: host simulator wall-clock speed was excluded because it does not represent target hardware performance; stale diagnostic matrix data were excluded in favor of the corrected final run.
- Artifacts produced: English and Chinese comparison sections, experiment protocol, traceability map, table schema, task packet, and two review records.
- Verification run: recomputed all direct and matrix ratios; confirmed the 6.873921 matrix geometric mean; confirmed fifteen matrix rows per language; checked bilingual ratio consistency; scanned for unsupported PPA/statistical claims and mechanical Chinese transitions.
- Remaining risk: single-run cycle data, non-identical Posit32/FP32 semantics, unresolved DFPVU MAC and Gemma Dot exact mismatches, no common-flow PPA, and no end-to-end application result.

### DFPVU–Ara VMFPU4 OpenROAD documentation audit

- Required skills: paper orchestration, experiment-results planning, writing core, peer review, and verification.
- Skills used: experiment planning defined the source/claim boundary; writing core guided paper-facing bilingual prose; peer review checked scope, numerical consistency, and scientific wording; verification required source scans before submission.
- Inputs consumed: the DFPVU and Ara VMFPU4 Nangate45 records and the existing passing four-lane direct-workload and matrix-cycle records.
- Inputs excluded: complete-Ara OpenROAD data, trace MAC and Gemma-dot correctness-failure rows, default-activity power, physical-stage timing, and all unsupported normalized PPA claims.
- Artifacts: bilingual OpenROAD comparison pages, protocol/traceability/schema updates, task packet, and two review records.
- Verification: source values and deltas were literally scanned in both pages; all linked source pages exist; obsolete full-Ara result names are absent; staged diff has no whitespace errors. No Markdown-lint configuration is present, so structural review was manual.
- Remaining risk: operation-set and arithmetic-semantic non-equivalence, incomplete physical closure, vectorless power, and no end-to-end model measurement.

### IPDPS 2027 paper revision guide

- Stage: S5, evidence-bound manuscript revision planning and submission-risk review.
- Required skills: paper orchestration, writing core, peer review, and verification; bounded-design approval was obtained before drafting.
- Skills actually used: paper orchestration established the task packet and review gates; writing core controlled Chinese academic phrasing and Markdown structure; peer review separated supported claims from reviewer risks; verification checked source values, local links, venue requirements, and worktree quality.
- Inputs consumed: maintained DFPVU/Ara workload and OpenROAD summaries, the experiment protocol, traceability map, table schema, and official IPDPS 2027 CFP/reproducibility pages.
- Inputs not used and why: stale workload attempts, host simulation wall time, vectorless power ratios, and obsolete full-Ara PPA evidence do not support the allowed claims.
- Artifacts produced: `docs/result/dfpvu_ipdps_paper_revision_guide_zh.md`, its task packet, and two review records.
- Verification run: checked status and mismatch counts, GEMM throughput, Ara comparison values, mapped cells/area, physical limitations, local links, headings, placeholders, writing style, and whitespace.
- Remaining risk: the manuscript has not yet been reviewed against this guide; literature novelty and citations remain outside this deliverable; ignored raw logs should be packaged before artifact release.
