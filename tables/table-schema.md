# Table Schema

| Table | Purpose | Rows | Metrics | Data source |
|---|---|---|---|---|
| Direct workload comparison | Compare three trace profiles on matched useful work | Six workload families with profile ranges | cycles, useful work/cycle, throughput ratio, correctness caveat | `docs/result/llm_p32_workload_matrix_results.md`; `docs/result/ara_4lane_workload_results.md` |
| Matrix comparison | Compare the fifteen common MNK presets | Fifteen shapes | DFPVU MAC/cycle, Ara MAC/cycle, ratio | `docs/result/matrix_gemm_p32_mnk_results.md`; `results/ara_workload_4lane/matrix_workloads.csv` |

All values are single-run RTL observations. No dispersion statistic is defined.
