# Spec Review: DFPVU IPDPS Paper Revision Guide

## Scope check

The deliverable is a revision guide, not a rewritten manuscript or a request for new experiments. It keeps the user-approved constraints: no RTL changes, no requirement to eliminate the remaining MAC/Dot mismatches, and no new OpenROAD run.

## Required-artifact review

| Task-packet requirement | Guide location | Result |
|---|---|---|
| IPDPS venue constraints | Section 2 | Pass |
| Recommended paper positioning | Section 3 | Pass |
| Claim/evidence boundary | Section 4 | Pass |
| Ten-page section plan | Section 5 | Pass |
| Section-by-section instructions | Sections 6–14 | Pass |
| Table and figure plan | Section 12 | Pass |
| Safe and prohibited wording | Sections 6 and 15 | Pass |
| Evidence-file map | Section 16 | Pass |
| Reproducibility and anonymization | Section 17 | Pass |
| Reviewer-risk preparation | Section 18 | Pass |
| Revision workflow and checklist | Sections 19–20 | Pass |

## Rejection-check review

- The guide does not require a hardware change or a new PPA run.
- Trace-derived cases are labeled arithmetic workloads or microbenchmarks rather than end-to-end inference.
- The three trace MAC failures and the Gemma Dot failure remain visible.
- The guide prohibits normalized area, power, energy, and frequency ratios.
- DFPVU and Ara physical checkpoints are both described as non-signoff evidence.

## Outcome

Pass. The guide satisfies the approved task packet and stays within the requested evidence and implementation scope.
