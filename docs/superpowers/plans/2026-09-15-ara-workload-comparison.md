# ARA Four-Lane DFPVU Workload Comparison Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce verified four-lane ARA RTL metrics for every applicable DFPVU workload, with Posit-only operations explicitly excluded or labelled as an IEEE proxy.

**Architecture:** A host-side Python generator selects bounded FP32 trace operands and scalar reference results from the existing DFPVU trace format.  A self-contained ARA bare-metal app uses RVV intrinsics to run the selected workloads and emits CSV records; a host collector verifies records, captures RTL cycle evidence, and renders the comparison report.

**Tech Stack:** Python 3 standard library, C99/RVV 1.0 intrinsics, ARA default configuration, Clang 20 RISC-V cross compiler, ARA Verilator RTL testbench, Bash.

**Spec:** `docs/superpowers/specs/2026-09-15-ara-workload-comparison-design.md`

## Global Constraints

- Target only `/root/ara` `config=default`: `NR_LANES=4`, `VLEN=4096`.
- Do not modify, clean, reset, checkout, or stage any pre-existing ARA change or artifact.
- Use no network, model download, or trace-directory overwrite.
- Report `mcycle` kernel timing and simulator whole-program hardware cycles separately.
- Retain semantic labels: `rvv-fp32-direct`, `rvv-fp32-to-fp16-proxy`, and `not-applicable-posit`.
- Fail a direct/proxy row when its independently computed IEEE reference mismatch count is nonzero.

---

### Task 1: Trace sampler and bounded data contract

**Files:**
- Create: `tools/test_generate_ara_workload_data.py`
- Create: `tools/generate_ara_workload_data.py`
- Create: `/root/ara/apps/dfpvu_workload_compare/generated_data.h` (generated)

**Interfaces:**
- Consumes: `metadata.json` plus little-endian `.f32` artifacts from a DFPVU trace directory.
- Produces: `generated_data.h` with `ARA_PROFILE`, `ara_case_t[]`, 32-bit FP operand arrays, IEEE FP32 reference arrays, and the case table count.

- [ ] **Step 1: Write the failing generator contract test**

```python
completed = subprocess.run([
    sys.executable, "tools/generate_ara_workload_data.py",
    "--trace", "test_src/qwen3-p32-fixture",
    "--add-trace", "test_src/llm-p32-add-fixture",
    "--output", generated_header,
], text=True, capture_output=True)
assert completed.returncode == 0, completed.stderr
header = pathlib.Path(generated_header).read_text()
assert '#define ARA_PROFILE "legacy-qwen3-v1"' in header
assert 'ARA_CASE_MAC' in header and 'ARA_CASE_ADD' in header
assert 'source_elements' not in header
```

The production change that must make this fail is a generator which omits a
workload class or produces a header that cannot identify its profile.

- [ ] **Step 2: Run the test and confirm RED**

Run: `python3 tools/test_generate_ara_workload_data.py`

Expected: nonzero exit because `tools/generate_ara_workload_data.py` does not exist.

- [ ] **Step 3: Implement the minimum trace parser and sampler**

Implement only standard-library JSON, `struct`, and `array` parsing.  Validate
each metadata shape against artifact length; reject non-finite operands; use
the same clamped/evenly-spaced scalar sampling and four-element vector order
documented by DFPVU.  Emit C arrays as IEEE hexadecimal bit patterns and
literal expected records for MAC, Dot, Multiply, To-Int, Add, and FP16 proxy.

- [ ] **Step 4: Run the generator contract test and syntax check**

Run: `python3 tools/test_generate_ara_workload_data.py && python3 -m py_compile tools/generate_ara_workload_data.py`

Expected: exit 0.

### Task 2: RVV workload application

**Files:**
- Create: `/root/ara/apps/dfpvu_workload_compare/main.c`
- Create: `/root/ara/apps/dfpvu_workload_compare/data.S`
- Create: `tools/test_ara_workload_compare.sh`

**Interfaces:**
- Consumes: `generated_data.h` from Task 1.
- Produces: `ARA_WORKLOAD_CSV` records with fields `profile,workload,semantic,shape,elements,mac_terms,kernel_mcycle,useful_per_cycle,max_abs_error,mismatches,status`.

- [ ] **Step 1: Write the failing application integration test**

```bash
python3 tools/generate_ara_workload_data.py \
  --trace test_src/qwen3-p32-fixture \
  --add-trace test_src/llm-p32-add-fixture \
  --output /root/ara/apps/dfpvu_workload_compare/generated_data.h
make -C /root/ara/apps --no-print-directory bin/dfpvu_workload_compare
/root/ara/install/riscv-isa-sim/bin/spike \
  --isa=rv64gcv_zfh_zvfh_zvl4096b \
  /root/ara/apps/bin/dfpvu_workload_compare > "$output"
grep -F 'ARA_WORKLOAD_CSV,legacy-qwen3-v1,mac,rvv-fp32-direct' "$output"
grep -F 'ARA_WORKLOAD_CSV,legacy-qwen3-v1,add,rvv-fp32-direct' "$output"
grep -F ',PASS' "$output"
```

The production change that must make this fail is an app that does not execute
the generated MAC/Add data, does not verify output, or hides failed rows.

- [ ] **Step 2: Run the integration test and confirm RED**

Run: `bash tools/test_ara_workload_compare.sh`

Expected: nonzero exit because the ARA app and generated header are absent.

- [ ] **Step 3: Implement direct and proxy kernels**

Use explicit RVV intrinsics with `vsetvl_e32m1` chunking.  Implement FMA GEMM
for MAC/matrix, four-term FMA dot, elementwise add/multiply, `vfcvt.rtz.x.f`
for To-Int, and `vfncvt.f.f` plus scalar FP32 reconstruction for the FP16
proxy.  Store all outputs before scalar verification; prevent dead-code
elimination with an observable result checksum.  Measure only the kernel
between `start_timer()` and `stop_timer()`.  Emit one record per case and
return nonzero after all records if any mismatch is found.

- [ ] **Step 4: Run the integration test and inspect generated assembly**

Run: `bash tools/test_ara_workload_compare.sh && /root/ara/install/riscv-llvm/bin/llvm-objdump --mattr=v -d /root/ara/apps/bin/dfpvu_workload_compare | rg 'vfmacc|vfmul|vfadd|vfcvt'`

Expected: test exit 0 and output contains RVV arithmetic/conversion instructions.

### Task 3: Profile runner and all GEMM Kconfig presets

**Files:**
- Create: `tools/run_ara_workload_comparison.sh`
- Modify: `/root/ara/apps/dfpvu_workload_compare/main.c`
- Test: `tools/test_ara_workload_compare.sh`

**Interfaces:**
- Consumes: profile trace directories and Task 2 CSV records.
- Produces: `docs/result/ara_4lane_workload_raw.csv` and per-profile RTL logs in `/tmp/ara-dfpvu-workload-runs/`.

- [ ] **Step 1: Extend the failing integration test with a GEMM-tail assertion**

```bash
grep -F 'ARA_WORKLOAD_CSV,legacy-qwen3-v1,matrix_gemm_tail_k3,rvv-fp32-direct,M2N3K3' "$output"
grep -F 'ARA_WORKLOAD_CSV,legacy-qwen3-v1,matrix_gemm_cube_128,rvv-fp32-direct,M128N128K128' "$output"
```

The production change that must make this fail is omission of a DFPVU Kconfig
matrix preset or a tail calculation with an incorrect useful-MAC count.

- [ ] **Step 2: Run the extended test and confirm RED**

Run: `bash tools/test_ara_workload_compare.sh`

Expected: nonzero exit because matrix preset records are not yet emitted.

- [ ] **Step 3: Add every named Kconfig matrix preset and the profile runner**

Encode the fifteen presets `tail_k1` through `tail_k5`, `square_16`,
`square_32_k63`, `square_32_k64`, `square_32_k65`, `gemv_row`, `gemv_column`,
`wide`, `tall`, `cube_64`, and `cube_128` in a constant case table.  The
runner must generate bounded profile data, build the app, invoke
`make -C /root/ara/hardware verilate` only when the executable is absent or
stale, then invoke `make -C /root/ara/hardware simv app=dfpvu_workload_compare`.
It must parse both `ARA_WORKLOAD_CSV` records and `[hw-cycles]` from each log;
it must stop on a non-PASS row or missing simulator completion marker.

- [ ] **Step 4: Run fixture, then real-profile collection**

Run: `bash tools/test_ara_workload_compare.sh && bash tools/run_ara_workload_comparison.sh --profiles qwen3-0.6b,gemma3-1b,phi4-mini-instruct`

Expected: fixture integration exit 0; raw CSV has direct/proxy rows for all
three profiles and individual `not-applicable-posit` conversion rows without
a numerical timing value.

### Task 4: Result validation and comparison report

**Files:**
- Create: `tools/render_ara_workload_report.py`
- Create: `docs/result/ara_4lane_workload_results.csv`
- Create: `docs/result/ara_4lane_workload_results.md`
- Test: `tools/test_ara_workload_compare.sh`

**Interfaces:**
- Consumes: raw hardware CSV, profile metadata, simulator logs, and DFPVU
  result documentation.
- Produces: final CSV and Chinese Markdown report with methodology, metrics,
  case status, and semantic mapping.

- [ ] **Step 1: Add a failing report assertion**

```bash
python3 tools/render_ara_workload_report.py \
  --raw docs/result/ara_4lane_workload_raw.csv \
  --csv docs/result/ara_4lane_workload_results.csv \
  --markdown docs/result/ara_4lane_workload_results.md
grep -F '不适用（ARA 无 Posit32 编码/转换指令）' docs/result/ara_4lane_workload_results.md
grep -F 'rvv-fp32-to-fp16-proxy' docs/result/ara_4lane_workload_results.csv
```

The production change that must make this fail is a report that presents
Posit-specific conversion as an ARA measurement or drops the proxy semantics.

- [ ] **Step 2: Run the report assertion and confirm RED**

Run: `bash tools/test_ara_workload_compare.sh`

Expected: nonzero exit because the renderer and final result files are absent.

- [ ] **Step 3: Implement rendering and hard validation**

Reject missing profiles, non-PASS direct/proxy cases, nonpositive cycle counts,
unknown semantics, duplicate rows, and a mismatch between a GEMM row's useful
MAC count and `M*N*K`.  Include tool versions, four-lane configuration,
trace paths, simulator log names, metric formulas, and the direct/proxy/N/A
mapping in the Markdown report.

- [ ] **Step 4: Run complete validation**

Run: `bash tools/test_ara_workload_compare.sh && python3 tools/render_ara_workload_report.py --raw docs/result/ara_4lane_workload_raw.csv --csv docs/result/ara_4lane_workload_results.csv --markdown docs/result/ara_4lane_workload_results.md && git -C /root/ara status --short && git status --short`

Expected: all tests exit 0; ARA status differs from the baseline only by the
new `apps/dfpvu_workload_compare/` directory and its associated app binary;
DFPVU status differs only by this plan, the design, tools, and result files.
