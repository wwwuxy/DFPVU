# Experiment Protocol: DFPVU versus Ara

## Systems

- DFPVU: four-lane Posit/IEEE vector processing unit, evaluated with Verilator RTL simulation.
- Ara: `NR_LANES=4`, `VLEN=4096`, evaluated with the repository Verilator RTL testbench.

## Workloads

- Trace-derived direct cases: MAC, four-term dot product, multiply, conversion to integer, add, and narrow-format conversion proxy across Qwen3-0.6B, Gemma3-1B, and Phi4-mini.
- Matrix cases: the same fifteen Kconfig MNK presets, from `TAIL_K1` through `CUBE_128`.

## Metrics and Aggregation

- Kernel cycles and useful operations per cycle are taken from one RTL run per case.
- Speedup is `DFPVU useful-work/cycle / Ara useful-work/cycle`, equivalently `Ara cycles / DFPVU cycles` where useful work is identical.
- Results are descriptive. No mean, standard deviation, confidence interval, or significance test is reported because repeated independent runs are unavailable.

## Fairness Boundary

- Both designs use four arithmetic lanes and report kernel-level RTL cycles.
- DFPVU executes Posit32 workloads; Ara executes IEEE FP32 or explicitly labeled IEEE conversion proxies. Ratios characterize implementation throughput under corresponding arithmetic semantics, not bit-identical operation latency.
- Matrix comparison uses identical MNK shapes and useful MAC counts. DFPVU correctness is checked against SoftPosit; Ara correctness is checked against the FP32 analytical result.
- No frequency, area, power, energy, TOPS, or token/s claim is permitted before controlled PPA and end-to-end evaluation.

## Clock-Bound OpenROAD PPA Protocol

- The DFPVU PPA baseline uses the OpenROAD-flow-scripts `nangate45` platform, `PvuTop` top module, and a 7.5 ns target period.
- The SDC must bind `dfpvu_vclk` to the real top-level `clock` port. A virtual clock, a CTS run with zero clock nets, or a report with no launch/capture paths is rejected.
- The reported implementation cost includes mapped standard-cell area and count, core/die area, placement utilization, clock-tree statistics, global-routing wire/via metrics, detailed-routing DRC status, and vectorless power only when its activity assumptions are stated.
- Ara PPA may be placed beside this baseline only after its synthesizable RTL, identical `nangate45` library, matching 7.5 ns constraint, and the same implementation stages are available. Otherwise the Ara comparison remains cycle-level only.
