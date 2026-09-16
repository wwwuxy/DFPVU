# Multi-linear Projection and Posit Conversion Workloads Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Export all decoder-layer linear projections into profile-default traces and validate trace-backed op=11, op=7, and op=6 workloads without claiming end-to-end LLM operator coverage.

**Architecture:** New profile-default linear roots isolate all-module traces from legacy single-module roots. The exporter marks whether a trace is an all-decoder default or an explicit subset; make enforces the all-module contract only for profile defaults. The existing MAC and FP32/P32 drivers consume the expanded module set, while a new P32-to-P16 driver reuses the conversion selection and the protocol regression's SoftPosit raw-bit convention.

**Tech Stack:** Python 3, NumPy, PyTorch/Transformers exporter hooks, C++17, Verilator, SoftPosit, Kconfig, GNU Make, Bash.

**Spec:** `docs/superpowers/specs/2026-09-14-multilinear-conversion-workloads-design.md`

## Global Constraints

- Replay only captured decoder-layer `input`, `weight`, and pre-bias `output` tensors for these workloads.
- Keep legacy trace directories intact; use the new profile-default `*-p32-linear-trace` roots for all-module exports.
- Preserve `--module` for manual subset exports and Custom paths for nonstandard locations.
- Treat raw Hardware-versus-SoftPosit equality as conformance; label P32/P16-versus-FP32 results as observational numerical data.
- Do not add QK^T, P-times-V, Softmax, RoPE, SwiGLU, residual, RMSNorm, or an end-to-end model-quality claim.
- Do not modify generated RTL. Functional workload validation uses the existing Verilator C++ driver flow.

---

### Task 1: Mark the linear trace scope at export time

**Files:**
- Modify: `tools/export_llm_p32_trace.py:147-326`
- Modify: `tools/test_export_qwen3_p32_trace.py`

**Interfaces:**
- Consumes: `capture_linear_modules(model, layer_index, input_ids, requested_paths, add_captures=None)` and parsed repeatable `--module` arguments.
- Produces: `write_trace(..., linear_module_scope)` metadata with either `"all-decoder-linear"` or `"explicit-subset"`.

- [ ] **Step 1: Write failing exporter contract tests**

  Add a toy decoder layer containing `self_attn.q_proj`, `self_attn.k_proj`,
  `self_attn.v_proj`, `self_attn.o_proj`, `mlp.gate_proj`, `mlp.up_proj`, and
  `mlp.down_proj`. Assert discovery with an empty requested-path list and
  scope metadata for both automatic and explicit selections:

  ```python
  captures = capture_linear_modules(model, 0, token_ids, [])
  self.assertEqual(
      [capture["name"] for capture in captures],
      ["mlp.down_proj", "mlp.gate_proj", "mlp.up_proj",
       "self_attn.k_proj", "self_attn.o_proj", "self_attn.q_proj",
       "self_attn.v_proj"],
  )
  write_trace(root, Path("/tmp/model"), "gemma3-1b", "fixture", [7], 0,
              captures, linear_module_scope="all-decoder-linear")
  self.assertEqual(metadata["linear_module_scope"], "all-decoder-linear")
  ```

  A second call with `requested_paths=["self_attn.q_proj"]` must write
  `"explicit-subset"`.

- [ ] **Step 2: Verify RED**

  Run: `python3 tools/test_export_qwen3_p32_trace.py`

  Expected: FAIL because `write_trace()` has no `linear_module_scope` keyword
  and metadata has no scope field.

- [ ] **Step 3: Implement the smallest scope contract**

  Add required keyword-only `linear_module_scope` to `write_trace()`. Reject
  any value other than `"all-decoder-linear"` and `"explicit-subset"`, then
  emit it next to `format_version`, `profile`, and `output_semantics`.
  In `main()`, derive the value directly from `arguments.module`:

  ```python
  linear_module_scope = (
      "explicit-subset" if arguments.module else "all-decoder-linear"
  )
  write_trace(..., captures, add_captures,
              linear_module_scope=linear_module_scope)
  ```

  Update all direct `write_trace()` test calls with the intended scope. Keep
  Add capture behavior unchanged; it may accompany either linear scope.

- [ ] **Step 4: Verify GREEN**

  Run: `python3 tools/test_export_qwen3_p32_trace.py`

  Expected: all exporter tests pass, including all-module discovery, metadata
  scope, explicit subset, and existing Add artifact tests.

- [ ] **Step 5: Commit the isolated exporter change**

  ```bash
  git add tools/export_llm_p32_trace.py tools/test_export_qwen3_p32_trace.py
  git commit -m "feat: mark all-linear LLM traces"
  ```

### Task 2: Route profile defaults to isolated all-linear traces

**Files:**
- Modify: `makefile:52-145`
- Modify: `test_src/test_qwen3_kconfig_command.sh`

**Interfaces:**
- Consumes: the profile selection, `CONFIG_LLM_P32_PATH_MODE_CUSTOM`, and
  `metadata.json` field `linear_module_scope`.
- Produces: new profile-default linear roots, exporter commands without
  `--module`, and an all-linear preflight for default paths only.

- [ ] **Step 1: Write failing default-route assertions**

  Change each profile-default linear test to expect these roots:

  ```bash
  qwen3-0.6b  /tmp/qwen3-0.6b-p32-linear-trace
  gemma3-1b   /tmp/gemma-3-1b-p32-linear-trace
  phi4-mini   /tmp/phi-4-mini-p32-linear-trace
  ```

  In `assert_profile_default_linear_route`, require all three facts:

  ```bash
  printf '%s\n' "$route" | rg -F -- '"linear_module_scope"' >/dev/null
  if printf '%s\n' "$route" | rg -F -- '--module ' >/dev/null; then
    printf 'profile-default linear export must not select one module\n' >&2
    exit 1
  fi
  ```

  Add a custom-root case containing v2 metadata with
  `"linear_module_scope":"explicit-subset"`; `llm_p32_trace_config` must
  reuse it rather than rejecting an explicitly selected custom trace.

- [ ] **Step 2: Verify RED**

  Run: `bash test_src/test_qwen3_kconfig_command.sh`

  Expected: FAIL because profile defaults still name `*-p32-trace`, retain
  `LLM_P32_EXPORT_MODULE`, and do not verify metadata scope.

- [ ] **Step 3: Implement the default-only contract**

  Replace the three `LLM_P32_DEFAULT_TRACE_DIR` values with the roots from
  Step 1 and remove `LLM_P32_EXPORT_MODULE` entirely. Set
  `LLM_P32_REQUIRE_ALL_LINEAR_TRACE := y` only when path mode is not Custom.
  In `llm_p32_trace_config`, after profile validation, fail default traces
  that lack the exact JSON field:

  ```make
  elif test "$(LLM_P32_REQUIRE_ALL_LINEAR_TRACE)" = "y" && \
       ! grep -Eq '"linear_module_scope"[[:space:]]*:[[:space:]]*"all-decoder-linear"' \
           "$(LLM_P32_TRACE_DIR)/metadata.json"; then \
    echo "LLM default linear trace is not an all-decoder-linear export: $(LLM_P32_TRACE_DIR)" >&2; exit 2;
  ```

  Remove `--module` from the automatic exporter command. Keep profile,
  model, output, token count, layer, and `$(LLM_P32_EXPORT_OPTIONS)` intact.
  Preserve the Add trace directory and `--capture-add` route exactly.

- [ ] **Step 4: Verify GREEN**

  Run: `bash test_src/test_qwen3_kconfig_command.sh`

  Expected: all three profile-default linear routes use the new isolated
  roots, no default exporter command contains `--module`, stale custom paths
  remain ignored by profile mode, and the Custom subset trace is accepted.

- [ ] **Step 5: Commit the routing change**

  ```bash
  git add makefile test_src/test_qwen3_kconfig_command.sh
  git commit -m "feat: export all linear modules by default"
  ```

### Task 3: Report multi-projection MAC dimensions and expand the fixture

**Files:**
- Modify: `csrc/main_qwen3_p32_mac_workload.cpp:309-395`
- Modify: `test_src/qwen3-p32-fixture/metadata.json`
- Create: `test_src/qwen3-p32-fixture/o_proj.input.f32`
- Create: `test_src/qwen3-p32-fixture/o_proj.weight.f32`
- Create: `test_src/qwen3-p32-fixture/o_proj.output.f32`
- Modify: `test_src/test_pvu_qwen3_trace.cpp`
- Modify: `test_src/test_qwen3_p32_mac_workload.sh`

**Interfaces:**
- Consumes: each `LinearModuleTrace` input shape `[source_m, source_k]` and
  weight shape `[source_n, source_k]`, plus the shared selected tile.
- Produces: one deterministic per-module report line named
  `trace_shape` and one named `replay_tile` before the existing exact mismatch
  report.

- [ ] **Step 1: Write failing fixture assertions**

  Add `self_attn.o_proj` descriptor and reuse the checked-in `q_proj` binary
  values for its three new deterministic fixture artifacts. Update the trace
  reader assertion from six to seven modules and require `o_proj` loading.
  Require MAC output such as:

  ```bash
  rg -q '^  self_attn.o_proj: trace_shape=M1 N4 K4 replay_tile=M1 N3 K2$' "$output"
  rg -q '^  mlp.down_proj: trace_shape=M1 N4 K4 replay_tile=M1 N3 K2$' "$output"
  rg -q '^  overall: elements=28 exact mismatches=0$' "$output"
  ```

  Include `o_proj` in the K=8 fixture loop and update its expected overall
  element/request counts from 6/12 to 7/14.

- [ ] **Step 2: Verify RED**

  Run: `bash test_src/test_qwen3_p32_mac_workload.sh ./obj_dir/VPvuTop`

  Expected: FAIL because the runner does not print source/replay dimensions.

- [ ] **Step 3: Implement focused report lines**

  Before calling `verify_elements()`, print one line per module using the
  source shapes and selected values already computed by the runner:

  ```cpp
  std::cout << "  " << named_module.first << ": trace_shape=M"
            << module.input.shape.at(0) << " N" << module.weight.shape.at(0)
            << " K" << module.input.shape.at(1) << " replay_tile=M" << m
            << " N" << n << " K" << k << std::endl;
  ```

  Keep the existing scheduler, raw SoftPosit oracle, and aggregate metrics
  unchanged. Copy the exact fixture bytes with `cp` before adding the three
  new artifact paths, so every fixture tensor remains reproducible.

- [ ] **Step 4: Verify GREEN**

  Run: `bash test_src/test_qwen3_p32_mac_workload.sh ./obj_dir/VPvuTop`

  Expected: seven named projections, the new dimensions, exact zero
  mismatches, and the existing K=8 accumulator-chain regression all pass.

- [ ] **Step 5: Commit the MAC reporting and fixture change**

  ```bash
  git add csrc/main_qwen3_p32_mac_workload.cpp test_src/qwen3-p32-fixture \
          test_src/test_pvu_qwen3_trace.cpp test_src/test_qwen3_p32_mac_workload.sh
  git commit -m "test: cover multilinear MAC projections"
  ```

### Task 4: Exercise all three trace tensor sources through op=7

**Files:**
- Modify: `test_src/test_llm_p32_conversion_workload.sh`
- Modify: `test_src/test_llm_p32_conversion_workload.cpp`

**Interfaces:**
- Consumes: runner arguments `[trace-root input|weight|output samples]`.
- Produces: fixture evidence for independent input, weight, and pre-bias
  output op=7 conversion runs.

- [ ] **Step 1: Write failing three-source fixture checks**

  Replace the single default invocation with `input`, `weight`, and `output`
  invocations at five samples per module. Require the source label, sampled
  count, exact zero mismatch counters in both conversion directions, and
  `conformance: PASS`. With the seven-module fixture, use these counts:

  ```bash
  check_conversion input  28 28 7
  check_conversion weight 112 35 14
  check_conversion output 28 28 7
  ```

  `check_conversion source source_elements sampled_elements requests` must
  assert the same request count for both `fp32_to_p32` and `p32_to_fp32`.
  Extend the C++ helper to name all three sources in the failure message.

- [ ] **Step 2: Verify RED**

  Run: `bash test_src/test_llm_p32_conversion_workload.sh ./obj_dir/VPvuTop`

  Expected: FAIL until the runner is rebuilt with the seven-module fixture;
  the old script only checks the default input source.

- [ ] **Step 3: Implement the test-only source loop**

  Add this shell helper without changing the conversion driver:

  ```bash
  check_conversion() {
    local source=$1 source_elements=$2 sampled_elements=$3 requests=$4
    "$runner" "$fixture" "$source" 5 >"$output"
    rg -x -F "  tensor_source: $source" "$output"
    rg -x -F "  source_elements: $source_elements" "$output"
    rg -x -F "  sampled_elements: $sampled_elements" "$output"
    rg -x -F "  fp32_to_p32_requests: $requests" "$output"
    rg -x -F "  p32_to_fp32_requests: $requests" "$output"
    rg -x -F '  fp32_to_p32_exact_mismatches: 0' "$output"
    rg -x -F '  p32_to_fp32_exact_mismatches: 0' "$output"
    rg -x -F '  conformance: PASS' "$output"
  }
  ```

- [ ] **Step 4: Verify GREEN**

  Run: `bash test_src/test_llm_p32_conversion_workload.sh ./obj_dir/VPvuTop`

  Expected: all three real trace tensor classes report exact op=7 conversion
  conformance with the stated per-source sample counts.

- [ ] **Step 5: Commit the op=7 test expansion**

  ```bash
  git add test_src/test_llm_p32_conversion_workload.cpp \
          test_src/test_llm_p32_conversion_workload.sh
  git commit -m "test: replay all LLM conversion tensor sources"
  ```

### Task 5: Add trace-distribution P32-to-P16 conversion workload

**Files:**
- Modify: `Kconfig:104-400`
- Modify: `makefile:4-165`
- Create: `csrc/main_llm_p32_to_p16_workload.cpp`
- Create: `test_src/test_llm_p32_to_p16_workload.sh`
- Modify: `test_src/test_qwen3_kconfig_command.sh`

**Interfaces:**
- Consumes: `pvu::LlmConversionSelection`, a normal linear trace root, and
  `pvu::sample_llm_conversion_tensor()` values.
- Produces: `op=6`, `src_posit_width=32`, `dst_posit_width=16`, raw
  P32-to-P16 conformance metrics and reconstructed-P16 observations.

- [ ] **Step 1: Write the failing Kconfig and fixture tests**

  Add an `LLM_P32_TO_P16_WORKLOAD=y` dry-run case that requires:

  ```bash
  ./obj_dir/VPvuTop  "/tmp/llm-p16" "weight" "256"
  ```

  Add a fixture script that runs all three sources with five samples per
  module and requires, for weight, the final partial vector:

  ```bash
  rg -x -F 'LLM P32-to-P16 workload' "$output"
  rg -x -F '  tensor_source: weight' "$output"
  rg -x -F '  sampled_elements: 35' "$output"
  rg -x -F '  requests: 14' "$output"
  rg -x -F '  exact_mismatches: 0' "$output"
  rg -x -F '  conformance: PASS' "$output"
  ```

- [ ] **Step 2: Verify RED**

  Run: `bash test_src/test_qwen3_kconfig_command.sh`

  Expected: FAIL because Kconfig has no `LLM_P32_TO_P16_WORKLOAD` and make
  has no op=6 runner route.

- [ ] **Step 3: Add the configuration and linkage surface**

  Add `LLM_P32_TO_P16_WORKLOAD` beside the other LLM workload bools; include
  it in the enclosing LLM configuration condition, exact-one workload list,
  and `PVU_SOFTPOSIT_REFERENCE_TESTS`. Make the existing tensor-source and
  sample-count choices visible under:

  ```kconfig
  if LLM_P32_CONVERSION_WORKLOAD || LLM_P32_TO_P16_WORKLOAD
  ```

  Route op=6 with the same three CLI arguments as op=7. Add
  `$(SOFTPOSIT_ROOT)/source/p16_to_p32.c` to `SOFTPOSIT_REF_SRCS` for numeric
  reconstruction, while retaining `p32_to_pX2.c` as the raw conversion oracle.

- [ ] **Step 4: Implement the minimal op=6 driver**

  Create a four-lane tagged ready/valid driver following the conversion
  driver's request lifecycle. For each sampled FP32 value, form a Posit32
  input with `pvu::float_to_p32()`. Set `io_op=6`, `io_Isposit=1`,
  `io_Outposit=1`, `io_src_posit_width=32`, `io_dst_posit_width=16`, and the
  request's active lane count. Use this raw oracle:

  ```cpp
  uint32_t expected_p32_to_p16(uint32_t raw) {
    posit32_t value{};
    value.v = raw;
    return p32_to_pX2(value, 16).v;
  }
  ```

  For observations only, read the high 16-bit Posit16 encoding from the raw
  op=6 response, call `p16_to_p32()`, and add that expanded Posit32 value to
  `pvu::ComparisonStats` against the original captured FP32 sample. Fail on
  a tag, operation, deadlock, or raw oracle mismatch. Print trace identity,
  source, source/sample counts, requests, cycles, requests/elements per
  cycle, exact mismatch count, reconstructed P16 error statistics, and
  `conformance: PASS|FAIL`.

- [ ] **Step 5: Verify GREEN**

  Run: `bash test_src/test_qwen3_kconfig_command.sh && bash test_src/test_llm_p32_to_p16_workload.sh ./obj_dir/VPvuTop`

  Expected: dry-run route forwards root/source/count exactly; all three
  fixture sources pass raw SoftPosit conformance, including the weight
  source's partial final vector.

- [ ] **Step 6: Commit the op=6 workload**

  ```bash
  git add Kconfig makefile csrc/main_llm_p32_to_p16_workload.cpp \
          test_src/test_llm_p32_to_p16_workload.sh \
          test_src/test_qwen3_kconfig_command.sh
  git commit -m "feat: add LLM P32-to-P16 workload"
  ```

### Task 6: Run a configuration-isolated make-run integration regression

**Files:**
- Create: `test_src/test_llm_p32_workload_make_run.sh`
- Modify: `docs/qwen3-p32-workload.md`

**Interfaces:**
- Consumes: a temporary copy of the repository, the checked-in linear fixture,
  and a temporary Custom-path configuration selecting op=6.
- Produces: evidence that `.config`, generated `config.h`, make routing, the
  selected C++ main, and the fixture runner agree without modifying the
  caller's repository.

- [ ] **Step 1: Write the failing isolated integration assertion**

  Create a test that copies the repository to `mktemp -d`, writes a temporary
  configuration selecting only `LLM_P32_TO_P16_WORKLOAD`, Custom paths, and
  `test_src/qwen3-p32-fixture`, then runs `make --no-print-directory run` in
  the copy. Require:

  ```bash
  rg -x -F 'LLM P32-to-P16 workload' "$output"
  rg -x -F '  tensor_source: weight' "$output"
  rg -x -F '  exact_mismatches: 0' "$output"
  rg -x -F '  conformance: PASS' "$output"
  cmp -s "$original_config" "$repo_root/.config"
  ```

- [ ] **Step 2: Verify RED**

  Run: `bash test_src/test_llm_p32_workload_make_run.sh`

  Expected: FAIL before the op=6 driver and route exist.

- [ ] **Step 3: Implement the isolated harness and experiment boundaries**

  Use `mktemp -d` and `cp -a "$repo_root/." "$temporary_root/repo"`; remove
  the temporary directory in `trap`. Edit only the copied `.config` to set
  the required bools, source `weight`, sample count `5`, Custom mode, and the
  copied fixture path. Do not edit the caller's config, source, or generated
  outputs. In the workload documentation, add one paragraph that calls these
  linear projection and conversion workloads trace-backed experiments, states
  the exact oracle versus observational metric split, and lists the deferred
  Attention/normalization traces.

- [ ] **Step 4: Verify GREEN**

  Run: `bash test_src/test_llm_p32_workload_make_run.sh`

  Expected: `make run` in the temporary repository reports op=6 PASS, and the
  original `.config` remains byte-for-byte unchanged.

- [ ] **Step 5: Run the complete focused validation set**

  Run:

  ```bash
  python3 tools/test_export_qwen3_p32_trace.py
  bash test_src/test_qwen3_kconfig_command.sh
  bash test_src/test_qwen3_p32_mac_workload.sh ./obj_dir/VPvuTop
  bash test_src/test_llm_p32_conversion_workload.sh ./obj_dir/VPvuTop
  bash test_src/test_llm_p32_to_p16_workload.sh ./obj_dir/VPvuTop
  bash test_src/test_llm_p32_workload_make_run.sh
  git diff --check
  ```

  Expected: every regression exits zero, the fixture reports zero exact
  mismatches, the isolated make-run passes, and `git diff --check` emits no
  whitespace diagnostics.

- [ ] **Step 6: Commit the integration evidence and documentation**

  ```bash
  git add test_src/test_llm_p32_workload_make_run.sh docs/qwen3-p32-workload.md
  git commit -m "test: integrate LLM conversion workload routes"
  ```
