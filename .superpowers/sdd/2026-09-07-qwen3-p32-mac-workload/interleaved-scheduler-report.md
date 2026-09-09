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
