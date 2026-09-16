# Progress

## Current Stage

S3/S4: experimental-results analysis and comparison-section drafting.

## Decisions

- Target venue: IEEE IPDPS.
- Primary language: English; a Chinese companion version is also required.
- Main argument: RVV-compatible high-throughput Posit/IEEE dual-format computation with low scheduling overhead.
- Evidence boundary: four-lane RTL cycle results only; PPA will be evaluated later.

## Status

- Scope and section structure confirmed by the user.
- Experiment protocol, traceability map, table schema, and task packet created.
- Numerical ratios recomputed from the verified DFPVU and Ara results.
- English and Chinese comparison drafts written under `docs/result`.
- Spec-compliance and quality reviews passed.
- A clock-bound OpenROAD PPA baseline is in progress. It is a DFPVU-only result until a matching Ara RTL implementation is found locally.



### Capability-use audit

- Required skills: paper orchestration, research brainstorming, experiment-results planning, statistical analysis, writing core, and verification.
- Skills actually used: all required skills above.
- Inputs consumed: DFPVU direct workload results, DFPVU matrix results, Ara four-lane Markdown/CSV results, and Ara kernel structure.
- Inputs not used and why: host simulator wall-clock speed was excluded because it does not represent target hardware performance; stale diagnostic matrix data were excluded in favor of the corrected final run.
- Artifacts produced: English and Chinese comparison sections, experiment protocol, traceability map, table schema, task packet, and two review records.
- Verification run: recomputed all direct and matrix ratios; confirmed the 6.873921 matrix geometric mean; confirmed fifteen matrix rows per language; checked bilingual ratio consistency; scanned for unsupported PPA/statistical claims and mechanical Chinese transitions.
- Remaining risk: single-run cycle data, non-identical Posit32/FP32 semantics, unresolved DFPVU MAC and Gemma Dot exact mismatches, no common-flow PPA, and no end-to-end application result.
