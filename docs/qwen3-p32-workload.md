# Qwen3 P32 MAC workload

This workload replays selected Qwen3-0.6B layer-0 linear-output elements through the existing four-lane `op=11` Posit<32,2> MAC path. It is a Verilator workload measurement.

## Export a trace

Use the validated Python 3.12 virtual environment with Transformers 5.16.1 and CPU PyTorch 2.14.0:

```bash
/root/qwen312-venv/bin/python tools/export_qwen3_p32_trace.py \
  --model /root/models/Qwen3-0.6B \
  --output /tmp/qwen3-0.6b-p32-trace \
  --tokens 16
```

The exporter records the six supported layer-0 linear modules, their FP32 inputs, weights, PyTorch outputs, and metadata. Keep model files and real traces outside the repository; `/tmp` is appropriate.

## Configure and run

From the repository root, select exactly one Posit<32,2> test:

```bash
make menuconfig
```

Open `RESNET_TEST`, then `Posit<32, 2>`, and choose `qwen3_p32_mac_workload` (`CONFIG_QWEN3_P32_MAC_WORKLOAD`). The Kconfig choice is exclusive, so do not select another test main at the same time. Generate the header and run the checked-in small fixture:

```bash
make config.h
make run
```

By default, the workload reads `test_src/qwen3-p32-fixture`. Override the trace root for an exported trace:

```bash
QWEN3_TRACE_DIR=/tmp/qwen3-0.6b-p32-trace make run
```

After a build, the generated runner also accepts optional positional selection parameters:

```bash
./obj_dir/VPvuTop [trace-root [token-count [row-count]]]
```

`token-count` and `row-count` are positive prefixes bounded by every selected module dimension. The default is one token and four output rows for each of the six modules.

## Read the report

`Hardware vs SoftPosit` is the conformance gate: each final raw P32 result must exactly equal the same ordered SoftPosit `p32_mulAdd` recurrence. Any mismatch makes the process fail after the report is printed.

`Posit vs ordered FP32` and `Posit vs PyTorch output` are observational. They compare the final P32 value converted to FP32 with, respectively, the explicit host recurrence and the exported module output. Each module and the overall total list sample, finite, zero, and special counts; IEEE FP32 ULP bins (`0`, `1`, `2-4`, `>=5`); maximum and mean relative error; and maximum absolute error. PyTorch output can differ from the host recurrence because the library reduction order can differ.

`Cycle report` counts explicit rising clock edges from reset release through the final output handshake. It reports requests, four-term MAC terms, cycles, requests/cycle, MAC terms/cycle, and active-lane utilization. All report values are Verilator workload data.

## Non-goals

This is not full-model inference, a prefill benchmark, a frequency estimate, or a system TOPS measurement. It exercises only selected scalar linear-output elements through the existing MAC datapath; it does not claim synthesized timing, memory traffic, tokenizer/model runtime, or end-to-end throughput.
