# LLM Posit32 Add Workload Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (\`- [ ]\`) syntax for tracking.

**Goal:** Add a Kconfig-selectable, real-forward-pass LLM Posit32 Add workload runnable with \`make run\`.

**Architecture:** The Python exporter captures qualifying binary Add operands and outputs from a normal local LLM forward pass into a dedicated trace format. A focused C++ trace/sampling helper creates four-lane requests, while a standalone Verilator driver replays tagged \`op=1\` traffic and compares raw outputs against SoftPosit. Kconfig, make, and fixture regressions expose the route without changing existing shared linear traces.

**Tech Stack:** Python 3 with PyTorch/Transformers, C++17, Verilator, SoftPosit Posit32, Kconfig, GNU Make, Bash.

**Spec:** \`docs/superpowers/specs/2026-09-11-llm-p32-add-workload-design.md\`

## Global Constraints

- Add uses actual model-forward binary tensor operands, fixed maximum width four, and PVU \`op=1\`.
- SoftPosit \`p32_add\` is the exact oracle; captured FP32 output is observational only.
- The workload must be Kconfig-selectable and run with \`make run\` using only an offline local model.
- Existing reusable linear/MAC traces must not be overwritten to add artifacts.
- No RTL change or model download is permitted.

---

### Task 1: Add trace representation and deterministic sampling

**Files:**

- Modify: \`csrc/pvu_qwen3_trace.h\`
- Create: \`csrc/pvu_llm_add_workload.h\`
- Create: \`test_src/test_llm_p32_add_workload.cpp\`

**Interfaces:**

- Consumes: \`metadata.json\` Add descriptors and \`<prefix>.lhs.f32\`, \`<prefix>.rhs.f32\`, \`<prefix>.output.f32\`.
- Produces: \`pvu::AddOperationTrace\`, \`pvu::LlmAddSamples\`, \`pvu::sample_llm_add_elements()\`, and \`pvu::parse_llm_add_selection()\`.

- [ ] **Step 1: Write the failing helper test**

\`\`\`cpp
const pvu::LlmAddSamples samples = pvu::sample_llm_add_elements(operation, 5);
require(samples.source_elements == 6 && samples.elements.size() == 5,
        "Add samples must clamp and remain evenly spaced");
require(samples.elements.front().lhs == 1.0F &&
            samples.elements.back().rhs == 60.0F,
        "Add sampling must include both endpoints");
\`\`\`

- [ ] **Step 2: Run the helper test to verify RED**

Run: \`g++ -std=c++17 -Wall -Wextra -Werror -I csrc test_src/test_llm_p32_add_workload.cpp -o /tmp/test_llm_p32_add_workload && /tmp/test_llm_p32_add_workload\`

Expected: compilation fails because the Add trace/sampling public interface is absent.

- [ ] **Step 3: Implement the smallest Add trace and sampling interface**

\`\`\`cpp
struct AddOperationTrace {
  Tensor lhs;
  Tensor rhs;
  Tensor output;
};

struct LlmAddElement { float lhs; float rhs; float output; };
struct LlmAddSamples {
  size_t source_elements = 0;
  std::vector<LlmAddElement> elements;
};

inline LlmAddSamples sample_llm_add_elements(const AddOperationTrace& operation,
                                              size_t requested);
\`\`\`

Extend the metadata parser with a required non-empty \`add_operations\` array
only for Add traces. Validate safe prefixes, identical non-empty rank-2
shapes, finite input values, byte lengths, and unique names; clamp and evenly
sample scalar coordinates. Parse \`[trace-root [samples]]\` with a positive
bounded request count.

- [ ] **Step 4: Run the helper test to verify GREEN**

Run: \`g++ -std=c++17 -Wall -Wextra -Werror -I csrc test_src/test_llm_p32_add_workload.cpp -o /tmp/test_llm_p32_add_workload && /tmp/test_llm_p32_add_workload\`

Expected: \`llm P32 Add workload helper tests passed\`.

### Task 2: Offline exporter and isolated Add trace

**Files:**

- Modify: \`tools/export_llm_p32_trace.py\`
- Modify: \`tools/test_export_qwen3_p32_trace.py\`

**Interfaces:**

- Consumes: \`--capture-add\` and an offline local model forward pass.
- Produces: Add metadata descriptors and three FP32 artifact files per captured operation.

- [ ] **Step 1: Write the failing exporter test**

\`\`\`python
write_trace(root, model, "phi4-mini", "fixture", [7], [], add_captures)
metadata = json.loads((root / "metadata.json").read_text())
self.assertEqual(metadata["add_operations"][0]["shape"], [1, 4])
self.assertEqual((root / "000_add.lhs.f32").read_bytes(), expected_lhs)
\`\`\`

- [ ] **Step 2: Run the exporter test to verify RED**

Run: \`python3 tools/test_export_qwen3_p32_trace.py\`

Expected: the test fails because \`write_trace\` does not accept Add captures or write Add metadata.

- [ ] **Step 3: Implement additive export without changing linear output**

Use a PyTorch dispatch mode during the real model forward pass to collect
binary tensor additions with identical tensor/result shapes. Normalize only
leading-batch-one tensors to rank two, detach them as CPU float32 arrays, and
write the deterministic safe-prefix artifacts. Add \`--capture-add\` so the
normal linear export remains version-2 compatible; error if no eligible Add
operation is captured.

- [ ] **Step 4: Run the exporter test to verify GREEN**

Run: \`python3 tools/test_export_qwen3_p32_trace.py\`

Expected: all exporter tests pass, including linear version-2 compatibility
and Add metadata/artifact checks.

### Task 3: Kconfig, make, and command-line route

**Files:**

- Modify: \`Kconfig\`
- Modify: \`makefile\`
- Modify: \`test_src/test_qwen3_kconfig_command.sh\`

**Interfaces:**

- Consumes: \`CONFIG_LLM_P32_ADD_WORKLOAD\`, \`CONFIG_LLM_P32_ADD_TRACE_DIR\`, and \`CONFIG_LLM_P32_ADD_SAMPLE_COUNT\`.
- Produces: \`./obj_dir/VPvuTop "<add-trace-root>" "<sample-count>"\` and offline export with \`--capture-add\`.

- [ ] **Step 1: Write the failing Kconfig command assertion**

\`\`\`bash
add_actual=$(make -C "$repo_root" -n run \\
  "CONFIG_LLM_P32_ADD_WORKLOAD=y" \\
  "CONFIG_LLM_P32_ADD_TRACE_DIR=/tmp/llm-add" \\
  "CONFIG_LLM_P32_ADD_SAMPLE_COUNT=256" | rg '^./obj_dir/VPvuTop')
[[ "$add_actual" == './obj_dir/VPvuTop  "/tmp/llm-add" "256"' ]]
\`\`\`

- [ ] **Step 2: Run the command test to verify RED**

Run: \`bash test_src/test_qwen3_kconfig_command.sh\`

Expected: the new Add assertion fails because make has no Add route.

- [ ] **Step 3: Implement the minimal selectable route**

Add the mutually exclusive workload choice, Add trace-directory defaults for
every profile, the 256/1024/4096 request choice, exact-one workload guard,
SoftPosit prerequisite, active trace preflight, and conditional runner
arguments. The selected Add trace exports with \`--capture-add\` only when its
metadata is absent, leaving existing linear roots untouched.

- [ ] **Step 4: Run the command test to verify GREEN**

Run: \`bash test_src/test_qwen3_kconfig_command.sh\`

Expected: all existing checks and the Add argument/export assertion pass.

### Task 4: Verilator Add runner and fixture conformance

**Files:**

- Create: \`csrc/main_llm_p32_add_workload.cpp\`
- Create: \`test_src/test_llm_p32_add_workload.sh\`
- Create: \`test_src/llm-p32-add-fixture/metadata.json\` and Add FP32 artifacts

**Interfaces:**

- Consumes: \`pvu::LlmAddSamples\` and CLI \`[trace-root [samples]]\`.
- Produces: exact Add report metrics and nonzero exit for protocol or oracle errors.

- [ ] **Step 1: Write the failing fixture checks**

\`\`\`bash
rg -x -F 'LLM P32 Add workload' "$output"
rg -x -F 'exact_mismatches: 0' "$output"
rg -x -F 'conformance: PASS' "$output"
\`\`\`

- [ ] **Step 2: Run the fixture test to verify RED**

Run: \`bash test_src/test_llm_p32_add_workload.sh ./obj_dir/VPvuTop\`

Expected: it fails because no Add runner or fixture exists.

- [ ] **Step 3: Implement the runner and fixture**

Pack sampled elements into up-to-four-lane requests, set \`vector_size\` to the
active lane count, and apply a pipelined tagged ready/valid schedule. Convert
inputs with \`float_to_p32\`, require \`io_out_op==1\`, and compare each active
\`io_posit_o\` lane with \`p32_add\`. Report request/element/cycle metrics and
FP32 error statistics. The fixture must cover a final partial vector.

- [ ] **Step 4: Run the fixture test to verify GREEN**

Run: \`bash test_src/test_llm_p32_add_workload.sh ./obj_dir/VPvuTop\`

Expected: full and partial fixture runs have zero exact mismatches and PASS.

### Task 5: Documentation and end-to-end evidence

**Files:**

- Modify: \`docs/qwen3-p32-workload.md\`
- Modify: \`.config\` only in an isolated temporary copy; do not alter the shared selected MAC configuration.

**Interfaces:**

- Consumes: Kconfig-selected local profile/model and generated Add trace.
- Produces: a documented direct \`make run\` path and real-model PASS output.

- [ ] **Step 1: Write the documentation expectation into the fixture route**

\`\`\`bash
rg -F 'LLM P32 Add workload' docs/qwen3-p32-workload.md
rg -F 'actual model-forward binary additions' docs/qwen3-p32-workload.md
\`\`\`

- [ ] **Step 2: Run it to verify RED**

Run: \`rg -F 'LLM P32 Add workload' docs/qwen3-p32-workload.md\`

Expected: no matching Add workload documentation exists.

- [ ] **Step 3: Document and run all regression layers**

Document capture eligibility, isolated Add trace behavior, exact oracle versus
FP32 observation, and sample semantics. Run the C++ helper, exporter tests,
Kconfig command test, fixture runner, \`git diff --check\`, and an isolated
temporary-copy \`make run\` with Add selected and the configured local model.

- [ ] **Step 4: Verify GREEN and preserve main configuration**

Run: the commands from Step 3 and \`git diff -- .config\`.

Expected: every test passes; \`main\` retains its pre-existing MAC \`.config\`
selection and no temporary trace/configuration modification is left behind.
