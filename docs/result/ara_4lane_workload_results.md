# ARA 4-lane workload 测试结果

## 测试范围与口径

- 配置：ARA `NR_LANES=4`、`VLEN=4096`，使用仓库 Verilator RTL testbench。
- 范围：DFPVU Kconfig 中的 direct workload（Qwen3-0.6B、Gemma3-1B、Phi4-mini）以及 15 个 `MATRIX_GEMM_P32_BENCHMARK` 形状。
- `cycles` 是裸机程序在 ARA 硬件计数器中测得的 kernel 周期数；不包含主机 Verilator 墙钟时间。
- `工作量/周期` 对矩阵行为 MAC/cycle；对 direct workload 为该项操作的有效元素/周期。所有数值均保留 6 位小数。

## Direct workload（trace-derived）

| Profile | Workload | 语义 | 元素数 | MAC 项数 | Cycles | 工作量/周期 | 最大误差位模式 | Mismatch | 状态 |
|---|---|---|---:|---:|---:|---:|---|---:|---|
| Qwen3-0.6B | MAC | FP32 direct | 1,792 | 28,672 | 123,830 | 0.231543 | 0x00000000 | 0 | PASS |
| Qwen3-0.6B | Dot (K=4) | FP32 direct | 1,792 | 7,168 | 31,045 | 0.230890 | 0x00000000 | 0 | PASS |
| Qwen3-0.6B | Multiply | FP32 direct | 7,168 | — | 6,542 | 1.095689 | 0x00000000 | 0 | PASS |
| Qwen3-0.6B | FP32 to int | FP32 direct | 1,792 | — | 1,197 | 1.497076 | 0x00000000 | 0 | PASS |
| Qwen3-0.6B | Add | FP32 direct | 14,336 | — | 13,031 | 1.100145 | 0x00000000 | 0 | PASS |
| Qwen3-0.6B | FP32 to FP16 | FP16 proxy | 1,792 | — | 30,078 | 0.059578 | 0x00000000 | 0 | PASS |
| Gemma3-1B | MAC | FP32 direct | 1,792 | 28,672 | 123,830 | 0.231543 | 0x00000000 | 0 | PASS |
| Gemma3-1B | Dot (K=4) | FP32 direct | 1,792 | 7,168 | 31,045 | 0.230890 | 0x00000000 | 0 | PASS |
| Gemma3-1B | Multiply | FP32 direct | 7,168 | — | 6,542 | 1.095689 | 0x00000000 | 0 | PASS |
| Gemma3-1B | FP32 to int | FP32 direct | 1,792 | — | 1,197 | 1.497076 | 0x00000000 | 0 | PASS |
| Gemma3-1B | Add | FP32 direct | 13,312 | — | 12,103 | 1.099892 | 0x00000000 | 0 | PASS |
| Gemma3-1B | FP32 to FP16 | FP16 proxy | 1,792 | — | 30,078 | 0.059578 | 0x33000000 | 0 | PASS |
| Phi4-mini | MAC | FP32 direct | 1,024 | 16,384 | 70,790 | 0.231445 | 0x00000000 | 0 | PASS |
| Phi4-mini | Dot (K=4) | FP32 direct | 1,024 | 4,096 | 17,774 | 0.230448 | 0x00000000 | 0 | PASS |
| Phi4-mini | Multiply | FP32 direct | 4,096 | — | 3,746 | 1.093432 | 0x00000000 | 0 | PASS |
| Phi4-mini | FP32 to int | FP32 direct | 1,024 | — | 699 | 1.464949 | 0x00000000 | 0 | PASS |
| Phi4-mini | Add | FP32 direct | 16,384 | — | 14,892 | 1.100188 | 0x00000000 | 0 | PASS |
| Phi4-mini | FP32 to FP16 | FP16 proxy | 1,024 | — | 17,203 | 0.059524 | 0x00000000 | 0 | PASS |

## Matrix GEMM workload（Kconfig presets）

| 名称 | M×N×K | MAC 项数 | Cycles | MAC/cycle | Mismatch | 状态 |
|---|---:|---:|---:|---:|---:|---|
| tail_k1 | 2×3×1 | 6 | 210 | 0.028571 | 0 | PASS |
| tail_k2 | 2×3×2 | 12 | 181 | 0.066298 | 0 | PASS |
| tail_k3 | 2×3×3 | 18 | 227 | 0.079295 | 0 | PASS |
| tail_k4 | 2×3×4 | 24 | 272 | 0.088235 | 0 | PASS |
| tail_k5 | 2×3×5 | 30 | 316 | 0.094936 | 0 | PASS |
| square_16 | 16×16×16 | 4,096 | 6,909 | 0.592849 | 0 | PASS |
| square_32_k63 | 32×32×63 | 64,512 | 59,083 | 1.091887 | 0 | PASS |
| square_32_k64 | 32×32×64 | 65,536 | 60,375 | 1.085482 | 0 | PASS |
| square_32_k65 | 32×32×65 | 66,560 | 60,965 | 1.091773 | 0 | PASS |
| gemv_row | 1×128×256 | 32,768 | 13,640 | 2.402346 | 0 | PASS |
| gemv_column | 128×1×256 | 32,768 | 724,893 | 0.045203 | 0 | PASS |
| wide | 16×128×64 | 131,072 | 55,079 | 2.379709 | 0 | PASS |
| tall | 128×16×64 | 131,072 | 208,663 | 0.628151 | 0 | PASS |
| cube_64 | 64×64×64 | 262,144 | 153,687 | 1.705700 | 0 | PASS |
| cube_128 | 128×128×128 | 2,097,152 | 874,647 | 2.397712 | 0 | PASS |

最终矩阵运行的全部 kernel 计数区间为 2,998,346 cycles。程序全程的 RTL 仿真计数为 `0x313e13`；后者包含启动、输出和退出，不能替代表中的 kernel cycle。

## 与 DFPVU 对比时的语义限制

ARA 的向量单元实现 IEEE-754 FP32，而 DFPVU workload 的核心算术为 Posit32。因此：

- `FP32 direct` 复用 DFPVU 捕获的输入，但在 ARA 上按 IEEE FP32 执行和校验。
- `FP16 proxy` 是 FP32 到 IEEE FP16 的代理项，不是 Posit32 到 P16 的指令级测量。
- Posit32 conversion 在 ARA 上无对应指令，不报告为 ARA 硬件测试结果。
- Add 的预期值按 IEEE FP32 重算；DFPVU Posit32 输出不能作为 ARA FP32 加法的正确性 oracle。
- 矩阵输入为 FP32 `1.0`，期望输出为 `K`，因此其结果用于比较 Kconfig 形状和周期，不用于宣称 Posit32 数值等价。

## 可复核原始数据

- [Direct workload CSV](../../results/ara_workload_4lane/direct_workloads.csv)
- [Matrix workload CSV](../../results/ara_workload_4lane/matrix_workloads.csv)
- [最终矩阵 RTL 日志](../../results/ara_workload_4lane/matrix-final.log)
- [采集方法与验证说明](../../results/ara_workload_4lane/README.md)

已执行生成器测试、Direct fixture 回归、矩阵 Spike 基线和最终 4-lane Verilator RTL 回归；18 个 direct/proxy 行与 15 个矩阵行均为 `PASS`。
