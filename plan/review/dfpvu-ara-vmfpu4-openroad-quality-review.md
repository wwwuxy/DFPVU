# Quality Review: DFPVU–Ara VMFPU4 OpenROAD Comparison

## Numerical consistency

| Checked quantity | Source values | Value in comparison pages | Result |
|---|---:|---:|---|
| Mapped cells | 468,466 versus 637,539 | Delta 169,073 cells | Correct |
| Mapped area | 541,907.702 versus 748,853.308 um² | Delta 206,945.606 um² (0.206946 mm² rounded) | Correct |
| Core area | 983,637.144 versus 1,360,488.920 um² | Delta 376,851.776 um² | Correct |
| Ara CTS limitations | 11,975 hold and 1,401 capacitance violations | Reported as non-signoff diagnostics | Correct |
| DFPVU detailed-route limitation | 43,770 DRC violations after one repair iteration | Reported as non-DRC-clean | Correct |
| Direct passing workloads | Multiply 3.64x; conversion 2.64x–2.67x; add 3.63x | Reported as useful-work-per-cycle evidence | Correct |
| Matrix shape results | 1.66x–88.42x; 6.87x descriptive geometric mean | Reported with semantic and statistical qualifiers | Correct |
| GEMV shape evidence | 3.997072, 2.402346, and 0.045203 MAC/cycle | Reported with evaluated-kernel qualification | Correct |

## Scientific-writing review

The pages use raw-unit deltas rather than cell-count or area ratios. They distinguish a shared implementation platform from an equal operation set, so readers cannot mistake matched Nangate45/clock/utilization settings for strict functional equivalence. The DFPVU advantages are separated by evidence type: functional coverage, raw mapping observation, direct RTL cycles, and synthetic matrix cycles.

The strongest language is limited to a “smaller mapped implementation boundary under the stated conditions” and “higher measured RTL cycle efficiency.” Timing, power, route completion, and signoff are specifically withheld. This makes the record suitable as a transparent source for an IPDPS results section, provided that the paper retains the stated qualifiers and cites the underlying result records.

## Residual risks for the paper

- Ara VMFPU4 and DFPVU do not execute an operation-identical benchmark interface; do not introduce normalized PPA ratios in later drafts.
- The matrix comparison is an RTL-cycle experiment with Posit32 and FP32 references, not a bit-identical ISA comparison or a population-level statistical study.
- A future publication-quality PPA comparison requires operation-matched microarchitectures, a common completed physical flow, workload activity files, and clean timing/DRC signoff for both designs.

## Review conclusion

All checked values and claim constraints are consistent with their named sources. The document is ready for documentation-level validation and main-branch submission.
