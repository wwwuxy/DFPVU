# LLM P32 MAC workload

该框架将本地开权重 LLM 指定 decoder layer 中实际执行的线性层导出为 trace，再通过既有四 lane `op=11` Posit<32,2> MAC 数据通路回放。当前测试范围固定为三个稠密模型：Qwen3-0.6B、Gemma 3 1B 和 Phi-4-mini-instruct。

模型权重和 Python 环境由使用者持有；`make` 不联网、不会下载模型，也不会回退到测试 fixture。启用默认的自动导出后，`make run` 会在 trace 首次缺失时仅从本地模型生成它。

## Kconfig profile

| Profile | 默认本地模型目录 | 默认 trace 目录 | 自动导出模块 |
| --- | --- | --- | --- |
| `Qwen3-0.6B` | `/root/models/Qwen3-0.6B` | `/tmp/qwen3-0.6b-p32-trace` | `self_attn.q_proj` |
| `Gemma 3 1B` | `/root/models/gemma-3-1b-it` | `/tmp/gemma-3-1b-p32-trace` | `self_attn.q_proj` |
| `Phi-4-mini-instruct` | `/root/models/phi-4-mini-instruct` | `/tmp/phi-4-mini-p32-trace` | `self_attn.qkv_proj` |

## 手动下载模型

使用当前 Hugging Face CLI 的 `hf` 子命令，而非已废弃的 `huggingface-cli`：

```bash
hf auth login  # Gemma 等受访问条款约束的模型需先授权并登录

hf download google/gemma-3-1b-it \
  --local-dir /root/models/gemma-3-1b-it

hf download microsoft/Phi-4-mini-instruct \
  --local-dir /root/models/phi-4-mini-instruct
```

## 导出 trace

导出器写入格式版本 2：每个模块都有 row-major、little-endian `float32` 的 `.input.f32`、`.weight.f32` 和 `.output.f32` 文件。`output` 始终是 `F.linear(input, weight, bias=None)` 的 FP32 参考值；若原始模型层有 bias，metadata 仅记录 `has_bias=true`，当前 MAC 硬件不会执行 bias。

Gemma 3 1B 示例：

```bash
/root/qwen312-venv/bin/python tools/export_llm_p32_trace.py \
  --profile gemma3-1b \
  --model /root/models/gemma-3-1b-it \
  --output /tmp/gemma-3-1b-p32-trace \
  --tokens 16 \
  --layer 0
```

Phi-4-mini 示例：

```bash
/root/qwen312-venv/bin/python tools/export_llm_p32_trace.py \
  --profile phi4-mini \
  --model /root/models/phi-4-mini-instruct \
  --output /tmp/phi-4-mini-p32-trace \
  --tokens 16 \
  --module self_attn.qkv_proj
```

导出器会枚举指定 layer 中具有 rank-2 weight、且在这次前向实际执行的线性模块。默认会覆盖 attention 和 MLP 的线性层；若需缩小范围，可重复给出相对 layer 路径：

```bash
/root/qwen312-venv/bin/python tools/export_llm_p32_trace.py \
  --profile phi4-mini \
  --model /root/models/phi-4-mini-instruct \
  --output /tmp/phi-4-mini-p32-trace \
  --tokens 16 \
  --module self_attn.qkv_proj \
  --module mlp.down_proj
```

模块路径由模型结构决定：Qwen3 与 Gemma 3 可用 `self_attn.q_proj`，Phi-4-mini-instruct 使用融合的 `self_attn.qkv_proj`；也可以不指定 `--module`，导出器会捕获当前 layer 全部实际执行的线性层。

旧命令 `tools/export_qwen3_p32_trace.py` 仍保留为 Qwen3-0.6B 的兼容包装器。

## Kconfig 配置与运行

从仓库根目录运行：

```bash
make menuconfig
```

在 `RESNET_TEST` -> `Posit<32, 2>` 中依次选择：

- `llm_p32_mac_workload`；
- `LLM P32 trace profile` 中的 Qwen3-0.6B、Gemma 3 1B 或 Phi-4-mini-instruct；
- `P32 MNK test shape` 中的待测 MNK。

三个 profile 已分别预置本地模型目录和 trace 目录。保持 `Automatically export a missing LLM trace` 为启用，然后直接运行：

```bash
make run
```

首次运行若缺少 `metadata.json`，`make run` 会通过 `/root/qwen312-venv/bin/python` 从选中本地模型离线导出 layer 0 的代表性 attention projection（Qwen3/Gemma 为 `self_attn.q_proj`，Phi 为 `self_attn.qkv_proj`），并固定导出 128 个 token，覆盖所有内置 MNK preset。后续运行复用该 trace；v2 trace 的 metadata profile 必须与当前 Kconfig 模型一致，不一致时 make run 会拒绝运行；`make` 不联网、不下载模型，也不会覆盖不含 metadata 的非空目录。模型不在默认路径时，在 Kconfig 修改 `LLM local model directory`；Python 环境位于其他路径时，运行 `LLM_P32_PYTHON=/path/to/python make run`。

该菜单与普通矩阵 benchmark 共享同一组 preset，但 LLM 模式将其解释为真实 trace 的 `M×K · K×N` tile：取前 M 个 token、均匀选择 N 个输出通道、取前 K 个输入通道。当 K 小于模块原始 K 时，框架报告的是 tile 精度，不将其与完整线性层输出比较。

## 结果解释

`Hardware vs SoftPosit` 是硬件一致性门：每一个最终 raw P32 结果都必须等于同一顺序的 SoftPosit `p32_mulAdd` 递推；任一 mismatch 会使进程失败。

`Posit vs ordered FP32` 始终比较同一 MNK tile 的显式 host FP32 递推。`Posit vs exported pre-bias FP32` 仅在选择完整模块 K 时才有意义；截断 K 时该栏样本数为零。模块与总计均报告样本数、ULP 分布、相对误差及绝对误差。

`Cycle report` 和 `Workload summary` 仅是 Verilator 中标记线性 MAC 元素的周期数据；它们不是端到端模型推理、频率、内存流量、prefill/decode 系统吞吐或 TOPS 声明。

## LLM P32 conversion workload

转换 workload 通过同一份真实 LLM trace 回放 `op=7` 的两个方向：先将 FP32 标量转换为 Posit<32,2>，再将该硬件输出转换回 FP32。因此它测量的是实际转换链路的周期吞吐、RTL 与 SoftPosit/IEEE 参考的一致性，以及 FP32 往返量化误差；它不是完整模型推理吞吐。

在 `RESNET_TEST` -> `Posit<32, 2>` 中选择 `llm_p32_conversion_workload`。随后选择：

- `LLM P32 trace profile`：Qwen3-0.6B、Gemma 3 1B 或 Phi-4-mini-instruct；
- `LLM conversion trace tensor`：`Layer input activation`、`Projection weight` 或 `Linear output activation`；
- `LLM conversion samples per module`：256、1024 或 4096 个标量。

然后直接运行：

```bash
make run
```

每个模块在扁平化后的完整张量上等距、确定性抽样；若张量元素少于所选数量，则只回放一次全部元素。自动 trace 导出、离线限制和本地模型目录的行为与 MAC workload 完全相同。

输出中的 `fp32_to_p32_*` 与 `p32_to_fp32_*` 分别给出两个方向的 requests、cycles、request/clock、element/clock 和逐元素精确不匹配数。`round_trip_*` 是从原始 FP32 到硬件 P32、再到硬件 FP32 的数值误差统计。仅当两个 `*_exact_mismatches` 都为零且 `conformance: PASS` 时，才可将 round-trip 指标作为该 RTL 实现的实验结论。

## LLM P32-to-Int workload

P32-to-Int workload 将真实 LLM trace 中的标量先通过既有 FP32→Posit32 路径编码，再以四 lane tagged ready/valid 请求回放 `op=10`。每条有效 lane 都与 SoftPosit `p32_to_i32` 的 Int32 结果逐位比较，但不把 host 的浮点转整数行为当作硬件 oracle。workload 仅验证抽样到的真实数据分布；RNE-even、NaR 与饱和的显式边界覆盖由独立的 exact-reference protocol regression 提供。

在 `RESNET_TEST` -> `Posit<32, 2>` 中选择 `llm_p32_to_int_workload`，然后选择：

- `LLM P32 trace profile`：Qwen3-0.6B、Gemma 3 1B 或 Phi-4-mini-instruct；
- `LLM P32-to-Int trace tensor`：`Layer input activation`、`Projection weight` 或 `Linear output activation`；
- `LLM P32-to-Int samples per module`：256、1024 或 4096 个标量。

随后直接运行：

```bash
make run
```

trace 自动导出、离线限制及本地模型目录行为与其他 LLM workload 相同。运行器在每个导出模块的所选张量上做确定性等距抽样；当张量小于所选数量时，全部元素仅回放一次。

输出的 `exact_mismatches` 是硬件对 SoftPosit P32→Int32 oracle 的强制一致性门，非零即失败。`saturation_results` 统计硬件输出等于 `INT32_MIN` 或 `INT32_MAX` 的有效 lane 数。`integerization_*` 指标则将硬件 Int32 与原始 trace 的 FP32 标量比较，描述包含前置 P32 编码的整数化误差；仅当 `conformance: PASS` 时可将这些误差和周期吞吐作为实验结果。非活动 lane 不进入请求、饱和或误差统计。

## LLM P32 Dot workload

Dot workload 回放真实线性投影的局部四元素点积：`dot(input[token, k:k+4], weight[row, k:k+4])`。它对应 PVU `op=5` 的自然四 lane 请求粒度，适合测量单个向量块的吞吐、标签流水化行为，以及 Posit32 与 FP32 四项点积之间的数值差异；它不跨请求累加，完整 K 维的累加性能仍由 MAC workload 测量。

在 `RESNET_TEST` -> `Posit<32, 2>` 中选择 `llm_p32_dot_workload`。随后选择：

- `LLM P32 trace profile`：Qwen3-0.6B、Gemma 3 1B 或 Phi-4-mini-instruct；
- `LLM Dot samples per module`：256、1024 或 4096 个 Dot vectors。

然后直接运行：

```bash
make run
```

Dot 固定使用导出模块的 `input` 与 `weight`；不能单独选择 `output`，因为一个点积请求需要一对同 K 维的操作数。对输入形状 `[tokens, K]` 与权重形状 `[N, K]`，可用请求空间为 `tokens × N × (K/4)`。框架按“四元素 K 块、输出行、token”的固定顺序对该空间等距抽样，并包含首尾坐标；选择数大于可用请求数时，每个请求只回放一次。K 必须能被 4 整除。

输出一行一个指标。`source_dot_vectors` 是全部可抽样四 lane 点积数，`sampled_dot_vectors` 是实际回放数，`request_per_cycle` 与 `dot_per_cycle` 是请求吞吐，`mac_terms_per_cycle` 将每个 Dot 计为 4 个乘加项。`exact_mismatches` 用逐 lane `p32_mulAdd` 的 SoftPosit 原始 P32 递推检查；仅当它为 0 且 `conformance: PASS` 时，`p32_vs_fp32_*` 的 FP32 误差统计才可作为实验结果。

## LLM P32 Multiply workload

Multiply workload 从同一份真实线性层 trace 取得局部操作数对：`input[token, k:k+4]` 与 `weight[row, k:k+4]`，并将它们作为一个四 lane PVU `op=3` 请求执行逐元素乘法。它测量投影计算中尚未做跨 K 维规约的乘法阶段；因此不等价于完整矩阵乘法，也不与 Dot workload 的四项累加结果混淆。

在 `RESNET_TEST` -> `Posit<32, 2>` 中选择 `llm_p32_mul_workload`。随后选择：

- `LLM P32 trace profile`：Qwen3-0.6B、Gemma 3 1B 或 Phi-4-mini-instruct；
- `LLM Multiply samples per module`：256、1024 或 4096 个四 lane Multiply vectors。

然后直接运行：

```bash
make run
```

采样空间、顺序和约束与 Dot workload 相同：对输入 `[tokens, K]` 和权重 `[N, K]`，共有 `tokens × N × (K/4)` 个候选向量；框架按四元素 K 块、输出行、token 的固定顺序等距选取，包含首尾，超出可用数时不重复回放，且 K 必须能被 4 整除。它不使用 MNK preset，也不重新导出模型或修改 RTL。

输出一行一个指标。`source_mul_vectors` 和 `sampled_mul_vectors` 分别是可用和实际回放的四 lane 请求数；`request_per_cycle`/`vector_per_cycle` 是向量请求吞吐，`element_per_cycle` 将每个请求计为 4 次独立乘法。每一条 lane 的两个 FP32 操作数都会先按既有路径转换为 P32，硬件输出逐条与 SoftPosit `p32_mul` 的原始 P32 结果比较；只有 `exact_mismatches: 0` 且 `conformance: PASS` 时，`p32_vs_fp32_*`（硬件 P32 值相对原始 FP32 乘积）的误差统计才可用于实验结论。
