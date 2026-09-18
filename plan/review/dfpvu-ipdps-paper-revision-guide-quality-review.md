# Quality Review: DFPVU IPDPS Paper Revision Guide

## Evidence consistency

The following values were checked against the maintained result pages:

- Direct workload status: 17 PASS and 4 FAIL across 21 effective cases.
- Trace MAC mismatches: 13, 10, and 3; Gemma Dot has one mismatch.
- Synthetic GEMM: 15/15 exact-reference passes and 3.999954 MAC/cycle for `CUBE_128`.
- Trace-derived GEMM: 19 PASS and 26 FAIL across 45 tiles.
- Matrix comparison: 1.66×–88.42× and a 6.87× descriptive geometric mean.
- DFPVU mapped result: 468,466 cells and 0.541908 mm².
- Ara VMFPU4 mapped result: 637,539 cells and 0.748853 mm².
- Physical limitations: 43,770 DFPVU DRC violations; 11,975 Ara hold and 1,401 capacitance violations.

All checked values match their named sources.

## Venue alignment

The guide uses only the official IPDPS 2027 call for papers and reproducibility page for current requirements. It records the ten-page limit, reference treatment, double-anonymous review, no submission-stage supplementary appendix, review criteria, post-acceptance reproducibility appendix, and AI-text accountability requirement.

## Scientific-writing quality

The guide separates architecture claims, exact-reference status, cycle measurements, and raw physical-cost observations. Negative results are not hidden, and the proposed prose does not convert a mismatch into an accuracy result. The page plan prioritizes the fully passing synthetic GEMM evidence while retaining trace-dependent failures in the main correctness summary.

The wording scan found no placeholders or mechanical Chinese transitions from the writing-core prohibited list. All eight local evidence files linked by the guide exist. The research-writing package did not provide a runnable style-check script, so structure and wording were checked with targeted scans and manual review.

## Residual risks

- The manuscript itself is not in this repository, so compliance depends on applying the guide consistently to the actual draft.
- Literature novelty and citation coverage require a separate review once the manuscript bibliography is available.
- Local raw logs and OpenROAD work directories remain partly ignored; the guide recommends packaging existing evidence without requiring reruns.
- IPDPS requirements should be rechecked immediately before submission.

## Outcome

