# Interleaved P32 MAC scheduler report

Date: 2026-09-09

## Scope

Only the C++ workload driver and its focused fixture regression were changed.
`PvuTop.scala`, RTL, Kconfig, makefile, `.config`, `build/`, model files, and
real traces were not edited or staged.

## Scheduler behavior

The driver now keeps one backpressure-stable held request and an outstanding
`tag -> element` table. Each element starts with raw P32 accumulator zero. A
response is accepted only with `out_ready`, checked for `op=11`, located by its
returned tag, and then used as that element's next raw P32 accumulator. The
next K group cannot be issued until that response has cleared the element's
in-flight state. The selected elements for a module are batched, allowing other
ready elements to issue while one waits. Metrics count accepted requests and
the explicit scheduler clock edges.

## RED: serial throughput regression

Command:

```bash
cd /root/DFPVU
bash test_src/test_qwen3_p32_mac_workload.sh ./obj_dir/VPvuTop
```

Before replacing the serial driver, the existing fixture binary reported
`overall: requests=24 mac terms=96 cycles=168 requests/cycle=0.142857` and the
new test failed as intended:

```text
fixture scheduler throughput too low: 0.142857 requests/cycle
```

The test's final minimum is `0.350000` requests/cycle. The initially proposed
`0.500000` minimum was rejected by measurement: this four-element, single-K
fixture includes pipeline fill/drain and reaches `0.400000`; `0.350000` still
requires at least 2.45x the serial baseline.

## GREEN: focused compile and fixture workload

Commands:

```bash
cd /root/DFPVU
git diff --check
make run
bash test_src/test_qwen3_p32_mac_workload.sh ./obj_dir/VPvuTop
git diff --check
```

Output:

```text
verilator ... --top-module PvuTop ... csrc/main_qwen3_p32_mac_workload.cpp ... vsrc/PvuTop.sv
make[1]: Nothing to be done for 'default'.
./obj_dir/VPvuTop
Hardware vs SoftPosit (Verilator workload data)
  down_proj: elements=4 exact mismatches=0
  gate_proj: elements=4 exact mismatches=0
  k_proj: elements=4 exact mismatches=0
  q_proj: elements=4 exact mismatches=0
  up_proj: elements=4 exact mismatches=0
  v_proj: elements=4 exact mismatches=0
  overall: elements=24 exact mismatches=0
Posit vs ordered FP32 (Verilator workload data; observational)
  overall: samples=24 finite=24 zero=0 special=0 result-special=0
  ULP distribution: 0=24 1=0 2-4=0 >=5=0
Posit vs PyTorch output (Verilator workload data; observational)
  overall: samples=24 finite=24 zero=0 special=0 result-special=0
  ULP distribution: 0=24 1=0 2-4=0 >=5=0
Cycle report (Verilator workload data; not frequency/system TOPS)
  down_proj: requests=4 mac terms=16 cycles=10 requests/cycle=0.400000 mac terms/cycle=1.600000 lane utilization=1.000000
  gate_proj: requests=4 mac terms=16 cycles=10 requests/cycle=0.400000 mac terms/cycle=1.600000 lane utilization=1.000000
  k_proj: requests=4 mac terms=16 cycles=10 requests/cycle=0.400000 mac terms/cycle=1.600000 lane utilization=1.000000
  q_proj: requests=4 mac terms=16 cycles=10 requests/cycle=0.400000 mac terms/cycle=1.600000 lane utilization=1.000000
  up_proj: requests=4 mac terms=16 cycles=10 requests/cycle=0.400000 mac terms/cycle=1.600000 lane utilization=1.000000
  v_proj: requests=4 mac terms=16 cycles=10 requests/cycle=0.400000 mac terms/cycle=1.600000 lane utilization=1.000000
  overall: requests=24 mac terms=96 cycles=60 requests/cycle=0.400000 mac terms/cycle=1.600000 lane utilization=1.000000
fixture P32 MAC scheduler: exact mismatches=0 requests/cycle=0.400000
```

The fixture is exact against the existing ordered raw SoftPosit oracle and
improves request rate from `0.142857` to `0.400000` requests/cycle (2.8x).

## K=8 accumulator-chain coverage fix

The original checked-in fixture has `K=4`, so each selected element executes
only one `op=11` request. The focused test now creates an isolated temporary
trace from `test_src/qwen3-p32-k8-fixture/metadata.json`. It repeats each
module's finite first K=4 input and weight group into a `[1,8]` input and
weight, retains one selected row, and invokes the runner with `1 1`. That is
six elements with two groups each: `requests=12` and `mac terms=48`.

### RED mutation

Commands:

```bash
cp csrc/main_qwen3_p32_mac_workload.cpp /tmp/main_qwen3_p32_mac_workload.pre-k8-mutation.cpp
# Temporarily change: request.accumulator = element.accumulator; -> 0
make run
bash test_src/test_qwen3_p32_mac_workload.sh ./obj_dir/VPvuTop
```

The normal K=4 fixture still passed, but the K=8 case failed exactly as
required. The first raw-oracle failure was:

```text
Qwen3 trace error: P32 MAC mismatch: module=down_proj token=0 row=0 expected=0x58000000 actual=0x50000000
  overall: elements=6 exact mismatches=6
  overall: requests=12 mac terms=48 cycles=84 requests/cycle=0.142857 mac terms/cycle=0.571429 lane utilization=1.000000
```

This mutation proves that the second group must receive its own first-group
raw P32 response; starting it at zero is detected even though K=4 remains
exact.

### GREEN

After restoring the source, the focused commands were:

```bash
make run
bash test_src/test_qwen3_p32_mac_workload.sh ./obj_dir/VPvuTop
git diff --check
```

Output:

```text
  overall: elements=24 exact mismatches=0
  overall: requests=24 mac terms=96 cycles=60 requests/cycle=0.400000 mac terms/cycle=1.600000 lane utilization=1.000000
fixture P32 MAC scheduler: exact mismatches=0 requests/cycle=0.400000; K=8 recurrence exact=0 requests=12
```
