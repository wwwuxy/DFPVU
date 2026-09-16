# LLM Posit32 Dot Workload Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Add an exactly selectable, trace-driven four-lane LLM Posit32 Dot benchmark runnable with `make run`.

**Architecture:** A header owns deterministic projection-vector coordinate sampling and argument parsing. A standalone Verilator driver feeds tagged `op=5` requests and compares each raw result against a SoftPosit four-lane recurrence. Kconfig and make select a model/trace and requested Dot vector count; shell and C++ regressions cover that public interface.

**Tech Stack:** C++17, Verilator, SoftPosit Posit32, Kconfig, GNU Make, Bash.

**Spec:** `docs/superpowers/specs/2026-09-11-llm-p32-dot-workload-design.md`

## Global Constraints

- Dot uses projection `input x weight`, fixed `vector_size=4`, and PVU `op=5`.
- SoftPosit `p32_mulAdd` is the exact oracle; FP32 is observational only.
- The workload must be selectable in Kconfig and execute with `make run` using a local trace.
- Existing RTL and trace-file format remain unchanged.

---

### Task 1: Dot sampling and selection helper

**Files:**
- Create: `csrc/pvu_llm_dot_workload.h`
- Create: `test_src/test_llm_p32_dot_workload.cpp`

**Interfaces:**
- Consumes: `pvu::LinearModuleTrace` from `pvu_qwen3_trace.h`.
- Produces: `pvu::LlmDotSamples`, `pvu::sample_llm_dot_vectors()`, and `pvu::parse_llm_dot_selection()`.

- [x] **Step 1: Write the failing test**

```cpp
const pvu::LlmDotSamples samples = pvu::sample_llm_dot_vectors(module, 4);
require(samples.source_vectors == 8 && samples.vectors.size() == 4,
        "Dot samples must cover evenly spaced token,row,K-group coordinates");
```

- [x] **Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -I./csrc -I/root/SoftPosit/source/include test_src/test_llm_p32_dot_workload.cpp -o /tmp/test_llm_p32_dot_workload && /tmp/test_llm_p32_dot_workload`

Expected: compilation fails because `pvu_llm_dot_workload.h` does not yet exist.

- [x] **Step 3: Write minimal implementation**

```cpp
struct LlmDotVector {
  size_t token = 0;
  size_t row = 0;
  size_t k = 0;
  std::array<float, 4> input{};
  std::array<float, 4> weight{};
};

struct LlmDotSamples {
  size_t source_vectors = 0;
  std::vector<LlmDotVector> vectors;
};

inline LlmDotSamples sample_llm_dot_vectors(const LinearModuleTrace& module,
                                             size_t requested);
```

Validate input and weight shapes/element counts/K agreement, select clamped
evenly spaced flattened coordinates, and parse `[trace-root [samples]]`.

- [x] **Step 4: Run test to verify it passes**

Run: `g++ -std=c++17 -I./csrc -I/root/SoftPosit/source/include test_src/test_llm_p32_dot_workload.cpp -o /tmp/test_llm_p32_dot_workload && /tmp/test_llm_p32_dot_workload`

Expected: `llm dot workload helper tests passed`.

### Task 2: Kconfig and make route

**Files:**
- Modify: `Kconfig`
- Modify: `makefile`
- Modify: `test_src/test_qwen3_kconfig_command.sh`

**Interfaces:**
- Consumes: `CONFIG_LLM_P32_DOT_WORKLOAD` and `CONFIG_LLM_P32_DOT_SAMPLE_COUNT`.
- Produces: `./obj_dir/VPvuTop "<trace-root>" "<sample-count>"` for the Dot runner.

- [x] **Step 1: Write the failing command-interface test**

```bash
dot_actual=$(make -C "$repo_root" -n run \
  "CONFIG_LLM_P32_MAC_WORKLOAD=n" \
  "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" \
  "CONFIG_LLM_P32_DOT_WORKLOAD=y" \
  "CONFIG_LLM_P32_MAC_TRACE_DIR=/tmp/llm-dot" \
  "CONFIG_LLM_P32_DOT_SAMPLE_COUNT=256" | rg '^./obj_dir/VPvuTop')
[[ "$dot_actual" == './obj_dir/VPvuTop  "/tmp/llm-dot" "256"' ]]
```

- [x] **Step 2: Run test to verify it fails**

Run: `bash test_src/test_qwen3_kconfig_command.sh`

Expected: the Dot command assertion fails because Kconfig and make have no Dot route.

- [x] **Step 3: Write minimal configuration implementation**

Add `LLM_P32_DOT_WORKLOAD` to the top workload choice and all shared LLM
guards. Add the Dot sample count choice (256/1024/4096), include Dot in the
exact-one make guard and SoftPosit prerequisites, and append trace root plus
sample count to `PVU_RUN_ARGS` only when Dot is selected.

- [x] **Step 4: Run test to verify it passes**

Run: `bash test_src/test_qwen3_kconfig_command.sh`

Expected: existing MAC/conversion checks and the Dot argument check pass.

### Task 3: Verilator driver, fixture regression, and documentation

**Files:**
- Create: `csrc/main_llm_p32_dot_workload.cpp`
- Create: `test_src/test_llm_p32_dot_workload.sh`
- Modify: `docs/qwen3-p32-workload.md`

**Interfaces:**
- Consumes: `pvu::LlmDotSamples` and CLI `[trace-root [samples]]`.
- Produces: line-per-metric Dot report and nonzero exit for tags/results/conformance failures.

- [x] **Step 1: Write the failing fixture test**

```bash
rg -x -F 'LLM P32 Dot workload' "$output"
rg -x -F 'conformance: PASS' "$output"
rg -x -F 'exact_mismatches: 0' "$output"
```

- [x] **Step 2: Run test to verify it fails**

Run: `bash test_src/test_llm_p32_dot_workload.sh`

Expected: build/run fails because the selected Dot driver does not exist.

- [x] **Step 3: Write minimal driver and document it**

Implement a pipelined tagged ready/valid driver for `op=5` and
`vector_size=4`; form raw Posit32 inputs, compute the exact four-term
SoftPosit oracle, accumulate cycle metrics, check output tag and raw Dot
result, report FP32 comparison statistics, and return failure on mismatches.
Document the Kconfig path, sampling semantics, and metric definitions.

- [x] **Step 4: Run test to verify it passes**

Run: `bash test_src/test_llm_p32_dot_workload.sh`

Expected: fixture report has zero exact mismatches and `conformance: PASS`.

### Task 4: Full regression and actual-trace evidence

**Files:**
- Modify: `.config` temporarily, then restore its original contents.

**Interfaces:**
- Consumes: selected model’s existing local trace and Kconfig configuration.
- Produces: real model Dot workload output through `make run`.

- [x] **Step 1: Run the helper and Kconfig regressions**

Run: `g++ -std=c++17 -I./csrc -I/root/SoftPosit/source/include test_src/test_llm_p32_dot_workload.cpp -o /tmp/test_llm_p32_dot_workload && /tmp/test_llm_p32_dot_workload && bash test_src/test_qwen3_kconfig_command.sh`

Expected: both tests exit zero.

- [x] **Step 2: Run fixture and selected-model workloads**

Run: `bash test_src/test_llm_p32_dot_workload.sh` followed by `make run` with Dot selected in `.config` and an existing selected-model trace.

Expected: both reports state `conformance: PASS` and `exact_mismatches: 0`.

- [x] **Step 3: Restore the user configuration**

Run: `cmp .config /tmp/dfpvu-config-before-llm-dot`

Expected: no output and exit zero.
