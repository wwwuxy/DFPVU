# LLM P32：三 profile、七 workload 的实测结果

基准运行：2026-09-14 14:59:33–15:02:06 UTC；Qwen Add trace 修复后于 2026-09-15 单独重跑。每个 profile/workload 组合执行一次，共 21 个有效 case。执行方式为保存的 Kconfig `.config` 加干净 Verilator `obj_dir` 的 `make run`，不修改当前工作区 `.config`。

## 状态与数据有效性

- 21 个有效 case 中，17 个 `PASS`、4 个 `FAIL`。`FAIL` 均为 Hardware-vs-SoftPosit 的 `exact_mismatches` 非零；数据被保留，绝不能描述为硬件数值一致性通过。
- Qwen、Gemma、Phi 的默认线性 trace 分别回放所有 layer-0 decoder 线性模块；MAC 固定使用 `SQUARE_16`（每模块 `M=N=K=16`）。
- 转换与 P32→Int 使用每模块 256 个真实 `input` 样本；Dot/Multiply 使用每模块 256 个真实四元素向量；Add 使用每个真实 Add operation 256 个元素。
- 初次 Qwen Add 目录的 `profile=qwen3-0.6b`，但 `model=/root/models/gemma-3-1b-it`。该条初次运行已排除：错误 metadata 已存档，目录已离线重导出为 Qwen，并以正确 trace 重跑。

`PASS` 仅说明该 workload 的 PVU 输出与 SoftPosit 参考逐位一致；不表示与完整模型 FP32 输出一致。

## 指标定义

- `requests`：送入 PVU 的请求数；每个请求具有四 lane。
- `elements` / `valid MAC terms`：实际有效元素或 MAC 项数，不计 lane 填充。
- `element_per_cycle`、`mac_terms_per_cycle`：本次 Verilator 仿真的有效工作量除以周期数；不包含频率、I/O 或系统级开销。
- `exact_mismatches`：PVU 与 SoftPosit 精确参考不相同的结果数；`0` 才是 `PASS`。
- `ULP >=5` 与误差：相对 Ordered-FP32 的观察性统计，不能替代端到端模型精度指标。

## Trace 来源

| Profile | 默认模型目录 | 线性 scope / 模块数 | 回放模块 | Add operations |
| --- | --- | --- | --- | ---: |
| Qwen3-0.6B | `/root/models/Qwen3-0.6B` | `all-decoder-linear` / 7 | q/k/v/o、gate/up/down projection | 56 |
| Gemma 3 1B | `/root/models/gemma-3-1b-it` | `all-decoder-linear` / 7 | q/k/v/o、gate/up/down projection | 52 |
| Phi-4-mini-instruct | `/root/models/phi-4-mini-instruct` | `all-decoder-linear` / 4 | qkv/o、fused gate-up、down projection | 64 |

`MAC` 的 tile K=16，小于真实投影 K；因此全部 `pre-bias FP32` 全-K 比较样本均为 0，表中只报告 tile 级 Ordered-FP32 观察值。

## 结果：线性投影 MAC（op11）

| Profile | tile elements | requests | valid MAC terms | cycles | request/cycle | MAC/cycle | exact mismatches / status | ULP >=5 | max / mean relative error |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | --- |
| Qwen3-0.6B | 1,792 | 7,168 | 28,672 | 7,210 | 0.994175 | 3.976699 | 13 / FAIL | 17 | 6.210e-6 / 2.8e-8 |
| Gemma 3 1B | 1,792 | 7,168 | 28,672 | 7,210 | 0.994175 | 3.976699 | 10 / FAIL | 16 | 3.4388e-5 / 4.0e-8 |
| Phi-4-mini-instruct | 1,024 | 4,096 | 16,384 | 4,120 | 0.994175 | 3.976699 | 3 / FAIL | 5 | 5.590e-6 / 1.9e-8 |

三个 MAC case 的进度吞吐一致，但均存在 exact mismatch，故不对 MAC 硬件准确性作正面结论。

## 结果：FP32↔P32 转换（op7）

| Profile | source / sampled elements | F32→P32 requests / cycles / elem-cycle | P32→F32 requests / cycles / elem-cycle | exact mismatches | round-trip max / mean abs error | status |
| --- | ---: | --- | --- | ---: | --- | --- |
| Qwen3-0.6B | 1,310,720 / 1,792 | 448 / 454 / 3.947137 | 448 / 454 / 3.947137 | 0 / 0 | 0 / 0 | PASS |
| Gemma 3 1B | 1,753,088 / 1,792 | 448 / 454 / 3.947137 | 448 / 454 / 3.947137 | 0 / 0 | 0 / 0 | PASS |
| Phi-4-mini-instruct | 2,228,224 / 1,024 | 256 / 262 / 3.908397 | 256 / 262 / 3.908397 | 0 / 0 | 0 / 0 | PASS |

## 结果：P32→P16 格式收缩（op6）

| Profile | sampled elements | requests | cycles | element/cycle | exact mismatches / status | reconstructed-P16 max / mean relative error | max abs error |
| --- | ---: | ---: | ---: | ---: | --- | --- | ---: |
| Qwen3-0.6B | 1,792 | 448 | 454 | 3.947137 | 0 / PASS | 223.946746 / 4.406274 | 0.3203125 |
| Gemma 3 1B | 1,792 | 448 | 454 | 3.947137 | 0 / PASS | 438.061453 / 2.397329 | 67.25 |
| Phi-4-mini-instruct | 1,024 | 256 | 262 | 3.908397 | 0 / PASS | 459.800000 / 11.629266 | 0.25 |

## 结果：4-lane Dot（op5）

| Profile | sampled vectors | valid MAC terms | cycles | MAC terms/cycle | exact mismatches / status | ULP >=5 | max / mean relative error |
| --- | ---: | ---: | ---: | ---: | --- | ---: | --- |
| Qwen3-0.6B | 1,792 | 7,168 | 1,798 | 3.986652 | 0 / PASS | 0 | 1.9760e-7 / 3.6712e-10 |
| Gemma 3 1B | 1,792 | 7,168 | 1,798 | 3.986652 | 1 / FAIL | 1 | 6.1685e-7 / 1.0736e-9 |
| Phi-4-mini-instruct | 1,024 | 4,096 | 1,030 | 3.976699 | 0 / PASS | 0 | 1.2697e-7 / 3.4558e-10 |

## 结果：逐元素 Multiply（op3）

| Profile | sampled vectors / elements | cycles | element/cycle | exact mismatches / status | FP32 ULP >=5 | max relative error |
| --- | ---: | ---: | ---: | --- | ---: | ---: |
| Qwen3-0.6B | 1,792 / 7,168 | 1,798 | 3.986652 | 0 / PASS | 0 | 0 |
| Gemma 3 1B | 1,792 / 7,168 | 1,798 | 3.986652 | 0 / PASS | 0 | 0 |
| Phi-4-mini-instruct | 1,024 / 4,096 | 1,030 | 3.976699 | 0 / PASS | 0 | 0 |

## 结果：P32→Int（op10）

| Profile | sampled elements | cycles | element/cycle | exact mismatches / status | saturation results | max / mean absolute error |
| --- | ---: | ---: | ---: | --- | ---: | --- |
| Qwen3-0.6B | 1,792 | 454 | 3.947137 | 0 / PASS | 0 | 0.49609375 / 0.13992371 |
| Gemma 3 1B | 1,792 | 454 | 3.947137 | 0 / PASS | 0 | 0.5 / 0.22261200 |
| Phi-4-mini-instruct | 1,024 | 262 | 3.908397 | 0 / PASS | 0 | 0.431640625 / 0.03176586 |

## 结果：真实 Add trace（op1）

| Profile | Add ops | sampled elements | cycles | element/cycle | exact mismatches / status | ULP >=5 | max / mean relative error |
| --- | ---: | ---: | ---: | ---: | --- | ---: | --- |
| Qwen3-0.6B | 56 | 14,336 | 3,590 | 3.993315 | 0 / PASS | 7,766 | 3.90625e-3 / 1.036294e-3 |
| Gemma 3 1B | 52 | 13,312 | 3,334 | 3.992801 | 0 / PASS | 7,487 | 3.90625e-3 / 1.063073e-3 |
| Phi-4-mini-instruct | 64 | 16,384 | 4,102 | 3.994149 | 0 / PASS | 9,005 | 3.90625e-3 / 1.053899e-3 |

## 结果解释与边界

- 真实数据驱动的转换、P32→P16、Multiply、P32→Int 和 Add 组合均通过本次 SoftPosit conformance gate；Gemma Dot 和三 profile 的 MAC 不通过，需单独调试后才可作为硬件功能结果使用。
- Add/Multiply/Dot 的输入来自真实线性 trace 或真实 Add trace 的数值分布，但它们是微基准，不能写成完整 Attention、Softmax、RMSNorm、SwiGLU 或端到端 LLM workload。
- P32→P16 与 P32→Int 报告的是接口收缩/整数化的样本级数值影响，不是模型量化精度，也不是端到端推理质量。
- 全部数值为单次 RTL 仿真；不能据此报告时钟频率、系统 TOPS、token/s、均值、方差或置信区间。

## 可复现数据

- 逐 case 的配置、完整 stdout/stderr、退出码索引：[`raw/llm_p32_workload_matrix_2026-09-14`](raw/llm_p32_workload_matrix_2026-09-14)。
- 原始 21-case 索引为 [`run-status.tsv`](raw/llm_p32_workload_matrix_2026-09-14/run-status.tsv)；Qwen Add 正确模型补跑为 [`replays.tsv`](raw/llm_p32_workload_matrix_2026-09-14/replays.tsv)。
- 所有 trace metadata 快照在 [`trace-metadata`](raw/llm_p32_workload_matrix_2026-09-14/trace-metadata)；`qwen3-0.6b-p32-add-trace.pre-provenance.metadata.json` 是明确排除的错误来源证据。
- 默认路径现同时验证 metadata 的 `profile` 和 `model`；Custom 模式仍允许非标准路径及已有 legacy trace。
