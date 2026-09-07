# Qwen3 P32 MAC Workload Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a reproducible Qwen3-0.6B layer-0 `Posit<32,2>` MAC trace exporter and DFPVU `op=11` replay workload with exact and numerical-loss reports.

**Architecture:** A Python exporter uses pre-hooks on Qwen layer-0 linear modules and writes a versioned, little-endian FP32 trace. A standalone guarded C++ Verilator main validates that trace, converts values to raw P32 with SoftPosit, serially maps each output to four-lane `op=11` requests, and reports exact hardware agreement plus FP32 loss and cycle metrics. No RTL changes are necessary.

**Tech Stack:** Python 3.12, PyTorch 2.14, Transformers 5.16, JSON, C++17, SoftPosit, Verilator, existing DFPVU Kconfig/makefile flow.

**Spec:** `docs/superpowers/specs/2026-09-07-qwen3-p32-mac-workload-design.md`

## Global Constraints

- DFPVU operands sent by this workload are raw `Posit<32,2>` only.
- Keep the scope to Qwen3-0.6B layer 0 and its six linear projections.
- Default trace is a deterministic 16-token Prefill and default replay is token 0, rows `[0, 4)`.
- Reuse `op=11`; do not modify `src/main/scala/pvu/PvuTop.scala`.
- Do not version a model, model weights, or real exported Qwen trace in the repository.
- New C++ code must compile as C++17 and use the existing checked-out SoftPosit source.
- Preserve the existing `PVU_MAC_REGRESSION` behavior.

---

### Task 1: Define the trace format and exporter validation

**Files:**
- Create: `tools/export_qwen3_p32_trace.py`
- Create: `tools/test_export_qwen3_p32_trace.py`
- Create: `test_src/qwen3-p32-fixture/metadata.json`
- Create: `test_src/qwen3-p32-fixture/{q_proj,k_proj,v_proj,gate_proj,up_proj,down_proj}.{input,weight,output}.f32`

**Interfaces:**
- Produces `metadata.json` with `format_version=1`, `model`, `prompt`, `input_token_ids`, `layer_index`, `modules`, and a tensor descriptor per module with `input_shape`, `weight_shape`, `output_shape`, `dtype="float32-le"`, `has_bias=false`.
- Produces each tensor as raw little-endian float32 in row-major order.
- CLI: `python3 tools/export_qwen3_p32_trace.py --model /root/models/Qwen3-0.6B --output /tmp/qwen3-trace --prompt "..." --tokens 16`.

- [ ] **Step 1: Write the failing exporter unit test**

```python
def test_write_tensor_is_little_endian_f32(tmp_path):
    import numpy as np
    from export_qwen3_p32_trace import write_tensor

    path = tmp_path / "tensor.f32"
    write_tensor(path, np.array([[1.5, -2.0]], dtype=np.float32))
    assert path.read_bytes() == bytes.fromhex("0000c03f000000c0")
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `PYTHONPATH=tools /root/.local/bin/python3.12 -m unittest tools/test_export_qwen3_p32_trace.py -v`

Expected: FAIL because `export_qwen3_p32_trace` does not exist.

- [ ] **Step 3: Implement the minimal exporter**

```python
def write_tensor(path: Path, tensor: np.ndarray) -> None:
    data = np.ascontiguousarray(tensor, dtype="<f4")
    path.write_bytes(data.tobytes(order="C"))

def capture_linear_inputs(model, layer_index: int, input_ids):
    modules = ("self_attn.q_proj", "self_attn.k_proj", "self_attn.v_proj",
               "mlp.gate_proj", "mlp.up_proj", "mlp.down_proj")
    # Register pre-hooks and forward hooks, invoke no-grad forward,
    # remove handles, then return CPU float32 inputs and outputs.
```

Use module pre-hooks for inputs and forward hooks for outputs, resolve names via `get_submodule`, reject non-Linear modules and biases, encode exactly `--tokens` deterministic token IDs from the supplied prompt, and write metadata only after all six captures are complete.

- [ ] **Step 4: Run the exporter unit test to verify it passes**

Run: `PYTHONPATH=tools /root/.local/bin/python3.12 -m unittest tools/test_export_qwen3_p32_trace.py -v`

Expected: PASS.

- [ ] **Step 5: Export the real trace and inspect its contract**

Run: `/root/qwen312-venv/bin/python tools/export_qwen3_p32_trace.py --model /root/models/Qwen3-0.6B --output /tmp/qwen3-0.6b-p32-trace --tokens 16`

Expected: `metadata.json` names six modules; each tensor file size equals the product of its metadata shape times four; q/gate input feature sizes are 1024; down-projection input feature size is 3072.

- [ ] **Step 6: Add a tiny checked-in fixture trace**

Create a format-version-1 fixture with six modules, one token, four output rows, and exactly four input features for all modules. Use simple finite values and precomputed FP32 outputs. This fixture is for parser and hardware smoke tests only and remains small enough for source control.

- [ ] **Step 7: Commit**

```bash
git add tools/export_qwen3_p32_trace.py tools/test_export_qwen3_p32_trace.py test_src/qwen3-p32-fixture
git commit -m "feat: export qwen3 linear traces"
```

### Task 2: Add a reusable workload trace reader and metrics helpers

**Files:**
- Create: `csrc/pvu_qwen3_trace.h`
- Create: `csrc/pvu_qwen3_metrics.h`
- Create: `csrc/main_qwen3_p32_mac_workload.cpp`

**Interfaces:**
- `pvu::Qwen3Trace pvu::load_qwen3_trace(const std::string& root)` returns six validated module tensors or throws `std::runtime_error`.
- `pvu::Qwen3Selection pvu::parse_qwen3_selection(int argc, char** argv)` supplies `trace_root`, `token_count=1`, and `row_count=4`; command-line values must be positive and within metadata dimensions.
- `pvu::WorkloadMetrics` records cycles, requests and active lanes; `pvu::ComparisonStats` exposes ULP bins and loss stats for finite FP32 comparisons.

- [ ] **Step 1: Write the failing trace-reader smoke test in the workload main**

```cpp
const pvu::Qwen3Trace trace = pvu::load_qwen3_trace("test_src/qwen3-p32-fixture");
if (trace.modules.size() != 6 ||
    trace.modules.at("q_proj").input.shape != std::vector<size_t>{1, 4}) {
  throw std::runtime_error("fixture trace contract mismatch");
}
```

- [ ] **Step 2: Run the workload configuration to verify it fails**

Run: select the new Kconfig symbol in `.config`, then `make config.h && make run`.

Expected: build failure because `pvu_qwen3_trace.h` and the guarded workload main do not exist.

- [ ] **Step 3: Implement strict JSON and binary validation**

Use a small in-repository parser tailored to the fixed metadata schema, not a new external C++ JSON dependency. Reject missing keys, unknown format versions, non-`float32-le` dtype, bias-bearing modules, missing required modules, incompatible shapes, bad byte counts, NaN/infinite input or weight values, and K dimensions not divisible by four.

```cpp
struct Tensor { std::vector<size_t> shape; std::vector<float> values; };
struct LinearModuleTrace { Tensor input, weight, output; };
struct Qwen3Trace { std::map<std::string, LinearModuleTrace> modules; };
```

The metrics helper must convert raw P32 through SoftPosit to float, compute monotonic IEEE FP32 ULP distance, and separately count zero and non-finite references.

- [ ] **Step 4: Run the fixture reader smoke test to verify it passes**

Run: `make run` with `CONFIG_QWEN3_P32_MAC_WORKLOAD=y` and `QWEN3_TRACE_DIR=test_src/qwen3-p32-fixture`.

Expected: the runner prints the fixture identity and succeeds before MAC replay is added.

- [ ] **Step 5: Add malformed-trace checks**

Run: copy the fixture to `/tmp/qwen3-bad-trace`, truncate `q_proj.input.f32`, and run with `QWEN3_TRACE_DIR=/tmp/qwen3-bad-trace`.

Expected: nonzero exit with `q_proj.input.f32` and `byte count` in stderr.

- [ ] **Step 6: Commit**

```bash
git add csrc/pvu_qwen3_trace.h csrc/pvu_qwen3_metrics.h csrc/main_qwen3_p32_mac_workload.cpp test_src/qwen3-p32-fixture
git commit -m "feat: read qwen3 posit workload traces"
```

### Task 3: Replay P32 MAC groups and verify the SoftPosit oracle

**Files:**
- Modify: `csrc/main_qwen3_p32_mac_workload.cpp`
- Modify: `csrc/pvu_qwen3_metrics.h`

**Interfaces:**
- `uint32_t run_p32_mac_element(Driver&, const LinearModuleTrace&, size_t token, size_t row, WorkloadMetrics&)` sends `K/4` `op=11` requests and returns final raw P32.
- `uint32_t ref_p32_mac_element(...)` invokes `p32_mulAdd` once per lane in the same request/lane order.
- `float ref_fp32_mac_element(...)` uses the same ordered recurrence over exported float values.

- [ ] **Step 1: Add a failing one-element hardware/oracle test**

```cpp
const uint32_t hardware = run_p32_mac_element(driver, trace.modules.at("q_proj"), 0, 0, metrics);
const uint32_t expected = ref_p32_mac_element(trace.modules.at("q_proj"), 0, 0);
if (hardware != expected) throw std::runtime_error("q_proj[0,0] raw P32 mismatch");
```

- [ ] **Step 2: Run the fixture to verify it fails**

Run: `make run`

Expected: compile failure because element replay helpers are absent.

- [ ] **Step 3: Implement the serial dependency-correct mapping**

Use `op=11`, `vector_size=4`, raw P32 activation and weight words generated with `convertDoubleToP32`, and zero P32 accumulator for group zero. On each subsequent group, wait for its predecessor response and feed that exact raw word as `posit_i3`. Give every request an incrementing tag and verify returned tag/op before accepting `io_posit_dot_o`.

- [ ] **Step 4: Run the fixture hardware/oracle test to verify it passes**

Run: `make run`

Expected: six fixture modules replay with `exact mismatches=0` and exit 0.

- [ ] **Step 5: Add a negative oracle guard**

Temporarily alter the fixture's expected oracle word in a test-only copy and run the runner.

Expected: the run exits nonzero and prints module, token, row, expected P32 hex word, and actual P32 hex word.

- [ ] **Step 6: Commit**

```bash
git add csrc/main_qwen3_p32_mac_workload.cpp csrc/pvu_qwen3_metrics.h
git commit -m "feat: replay qwen3 traces through posit mac"
```

### Task 4: Report numerical loss, utilization, and configuration support

**Files:**
- Modify: `Kconfig`
- Modify: `makefile`
- Modify: `csrc/main_qwen3_p32_mac_workload.cpp`
- Modify: `csrc/pvu_qwen3_metrics.h`
- Create: `docs/qwen3-p32-workload.md`

**Interfaces:**
- Kconfig symbol: `CONFIG_QWEN3_P32_MAC_WORKLOAD`.
- Environment override: `QWEN3_TRACE_DIR`; default trace root `test_src/qwen3-p32-fixture`.
- Final report headings: `Hardware vs SoftPosit`, `Posit vs ordered FP32`, `Posit vs PyTorch output`, and `Cycle report`.

- [ ] **Step 1: Add failing report-content assertions to the fixture run**

```bash
make run > /tmp/qwen3-workload.out
rg "ULP distribution: 0=.*1=.*2-4=.*>=5=" /tmp/qwen3-workload.out
rg "max relative error=.*mean relative error=" /tmp/qwen3-workload.out
rg "requests=.*mac terms=.*cycles=.*lane utilization=" /tmp/qwen3-workload.out
```

Expected: FAIL because the workload has no aggregate report yet.

- [ ] **Step 2: Implement aggregate reports**

For each selected module, token and row, compare converted Posit result to both ordered FP32 recurrence and exported module output. Print per-module and total counts. Exact oracle mismatch controls process exit; floating-point error is observational. Count rising edges, derive rates from cycles, and print active-lane ratio.

- [ ] **Step 3: Add Kconfig/makefile integration**

Add `QWEN3_P32_MAC_WORKLOAD` as an exclusive Posit<32,2> test. Include it in `PVU_SOFTPOSIT_REFERENCE_TESTS` so the existing p32 SoftPosit subset is linked. Do not select other test mains simultaneously.

- [ ] **Step 4: Run report assertions and legacy MAC regression**

Run: `make menuconfig` (select the workload), `make config.h && make run`; then select `PVU_MAC_REGRESSION` and rerun.

Expected: fixture report contains all required fields with zero exact mismatches; legacy report retains `op11: samples=528 mismatches=0`.

- [ ] **Step 5: Run the real exported trace**

Run: `QWEN3_TRACE_DIR=/tmp/qwen3-0.6b-p32-trace make run`.

Expected: default selection executes six modules × one token × four rows, reports zero exact mismatches, and labels all floating-point loss results.

- [ ] **Step 6: Write operator-level usage documentation**

Document exporter invocation, menuconfig selection, trace override, selection arguments, report interpretation, and the explicit non-claim that the result is full-model inference throughput.

- [ ] **Step 7: Commit**

```bash
git add Kconfig makefile csrc/main_qwen3_p32_mac_workload.cpp csrc/pvu_qwen3_metrics.h docs/qwen3-p32-workload.md
git commit -m "feat: report qwen3 posit mac workload"
```

### Task 5: Final verification and delivery

**Files:**
- Modify: no source files unless verification finds a defect.

- [ ] **Step 1: Regenerate RTL and build the fixture workload**

Run: `make verilog && make config.h && make run` with `CONFIG_QWEN3_P32_MAC_WORKLOAD=y`.

Expected: elaboration succeeds and fixture replay exits 0.

- [ ] **Step 2: Verify source boundaries**

Run: `git diff e7cf947 -- src/main/scala/pvu/PvuTop.scala`.

Expected: no output; workload reuses existing MAC hardware unchanged.

- [ ] **Step 3: Verify the real trace and configuration isolation**

Run: real trace replay followed by the standalone `PVU_MAC_REGRESSION` build.

Expected: both runs exit 0; each selected Kconfig choice produces exactly one test main.

- [ ] **Step 4: Inspect status and commit only owned changes**

Run: `git status --short`.

Expected: do not stage user-owned `.config` or `build/`; stage and commit only workload implementation/documentation files.

- [ ] **Step 5: Commit final verification fixes, if any**

```bash
git add <only-files-fixed-by-verification>
git commit -m "test: verify qwen3 posit mac workload"
```

## Self-Review

- Spec coverage: Tasks 1–2 implement the trace contract; Task 3 implements the four-lane recurrence and exact oracle; Task 4 implements metrics, Kconfig and usage; Task 5 verifies hardware reuse and legacy safety.
- Placeholder scan: no task relies on an unspecified external format or unnamed function. The trace metadata, exported tensor names, C++ interfaces, defaults and commands are defined above.
- Type consistency: Task 1 emits the `float32-le` tensors consumed by Task 2; Task 2's `LinearModuleTrace` is consumed by Task 3; Task 3 and Task 4 share raw P32 results and `WorkloadMetrics`.

