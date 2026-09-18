# DFPVU 面向 IPDPS 2027 的论文修改指南

## 1. 指南用途与固定约束

本文档用于指导 DFPVU 论文在现有代码、RTL workload 结果和 OpenROAD 结果不再变更的条件下完成面向 IPDPS 2027 的修改。修改工作的核心不是扩大结论，而是重新组织贡献、实验口径和限制条件，使论文中的每一项主要论断都能追溯到当前仓库已有证据。

本轮改稿采用以下固定约束：

- 不再修改 DFPVU 或 Ara 硬件实现。
- 不要求解决真实 trace 中剩余的 MAC 和 Gemma Dot exact mismatch。
- 不重跑 DFPVU 或 Ara 的 OpenROAD 流程。
- 不增加端到端 LLM 推理、板级测量或 workload-activity 功耗实验。
- 允许重新组织已有数据、重新计算已有数据的描述性统计量、绘制新图和补充复现说明。
- 所有 MAC mismatch 必须保留并解释，不能从表格、正文或限制部分删除。

在这些约束下，论文可以形成完整投稿稿件，但研究定位必须限定为“体系结构设计、RTL 周期效率、trace-derived 算术 workload 和初步实现成本”，不能定位为“完全数值一致的 LLM 加速器”或“经过签核的 PPA 优势实现”。

## 2. IPDPS 2027 投稿约束与选轨建议

以下要求来自 [IPDPS 2027 Call for Papers](https://www.ipdps.org/ipdps2027/2027-call-for-papers.html)，核对日期为 2026 年 9 月 18 日：

- 摘要注册截止时间为 2026 年 10 月 1 日 AOE，摘要不超过 500 词。
- 完整论文截止时间为 2026 年 10 月 8 日 AOE。
- 投稿正文不得超过 10 页，采用 IEEE 10-point、双栏格式；图和表计入 10 页，参考文献不计页数。
- 投稿阶段不得在页数限制之外增加 supplementary section 或 appendix。
- 评审采用 double-anonymous 两轮流程，稿件不得出现作者身份或可直接识别作者的信息。
- 评审关注 originality、correctness、technical strength、significance、impact、presentation quality 和 venue relevance。
- 正文需要明确给出研究动机、现有工作的限制、关键洞见与贡献、方法细节，以及方案自身的显著限制。
- 接收后，包含计算结果的论文需要提交 reproducibility appendix。具体要求见 [IPDPS 2027 Reproducibility Initiative](https://www.ipdps.org/ipdps2027/2027-reproducibility-initiative.html)。
- 若最终稿直接采用 AI 生成的论文文字，需要遵循 IPDPS 2027 页面列出的 IEEE 披露与引用要求；作者仍对数据、引用和贡献陈述的准确性承担全部责任。

推荐将 Architecture 作为 primary track，将 Measurements, Modeling, and Experiments 作为 secondary track。DFPVU 的主体贡献是四通道双格式向量算术体系结构、流水线请求组织和实现评估，符合 Architecture track 对专用体系结构、ML/AI 加速器和新型算术架构的覆盖。论文使用 LLM trace 作为输入分布和算术 workload，但没有提出新的机器学习方法，也没有端到端模型结果，因此不建议将 ML/AI 设为主 track。

## 3. 推荐的论文定位

### 3.1 一句话定位

推荐在摘要、引言和结论中保持同一定位：

> DFPVU is a four-lane vector arithmetic unit that integrates native Posit32 computation and explicit Posit/IEEE conversion paths under a RISC-V-vector-oriented request interface, and is evaluated through exact-reference RTL tests, trace-derived arithmetic workloads, shape-controlled GEMM kernels, and a clock-constrained OpenROAD implementation-cost study.

对应的中文理解是：DFPVU 是一个四通道向量算术单元，在面向 RISC-V 向量执行的请求接口下集成 Posit32 运算和 Posit/IEEE 显式转换通路，并通过精确参考 RTL 测试、真实 trace 派生的算术 workload、受控 GEMM 形状和有时钟约束的 OpenROAD 实现成本进行评估。

### 3.2 推荐标题

首选标题：

> DFPVU: A Dual-Format Posit/IEEE Vector Unit with Workload-Driven Evaluation

若需要突出 RISC-V 向量接口，可使用：

> DFPVU: A RISC-V-Vector-Oriented Posit/IEEE Arithmetic Unit with Four-Lane Execution

标题中不建议出现 `PPA-Efficient`、`Energy-Efficient`、`LLM Accelerator`、`Exact` 或 `Fully Compliant`。现有实验不能支持这些范围更大的限定词。

### 3.3 论文主线

整篇论文应围绕一个连续问题展开：通用 RVV 浮点单元主要面向 IEEE 格式，而需要 Posit 算术及 Posit/IEEE 互操作的 workload 缺少紧凑、持续供给的向量执行通路。DFPVU 通过四通道请求接口、流水线算术模块、原生格式转换和 MAC/GEMM 映射提供这一能力。实验关注三个层次：运算通路在明确测试范围内的数值行为、不同 workload 与矩阵形状下的 RTL 周期效率、统一工艺约束下的原始实现成本。

不应把“过去代码存在错误、近期完成修复”作为主要科研贡献。论文只描述最终设计、验证方法和剩余限制。修复过程可以在实现说明中用于解释为什么采用 exact-reference regression，但不应写成贡献列表中的“修复若干 bug”。

## 4. 贡献点的推荐写法

建议将贡献压缩为三项或四项。每项贡献都需要在正文中对应一个设计小节和一个实验结果。

### 4.1 可直接采用的英文贡献表述

1. We present DFPVU, a four-lane vector arithmetic unit that combines Posit32 arithmetic with explicit IEEE-to-Posit and Posit-to-IEEE conversion paths behind a tagged ready/valid request interface.
2. We design a pipelined execution organization for element-wise operations, dot products, and chained MAC workloads. Across fifteen synthetic GEMM shapes, the implementation matches the SoftPosit reference and approaches four useful MAC terms per cycle for fully utilized large matrices.
3. We develop trace-derived arithmetic workloads from Qwen3-0.6B, Gemma 3 1B, and Phi-4-mini and use them to characterize conversion, arithmetic, and matrix-shape behavior at RTL. The evaluation distinguishes cycle throughput from exact-reference conformance and reports the remaining trace-dependent mismatches.
4. We compare DFPVU with a four-lane Ara execution baseline and report a clock-constrained Nangate45 implementation-cost point. The comparison separates measured cycle efficiency and raw mapped cost from unsupported frequency-, power-, or area-normalized claims.

### 4.2 贡献点必须满足的证据关系

| 贡献 | 主要证据 | 允许的结论 | 必须同时披露的限制 |
|---|---|---|---|
| 双格式向量数据通路 | FP32→P32、P32→FP32、P32→P16 workload | 原生 Posit/IEEE 互操作能力 | 不能推导端到端模型精度优势 |
| 四通道流水线与 MAC/GEMM | 15 个合成 MNK preset | 15 项精确通过；大矩阵接近 4 MAC/cycle | 真实 trace MAC 不完全逐位一致 |
| trace-derived workload | 三个模型、七类 direct workload 和 45 个矩阵 tile | 真实数值分布下的算术微基准覆盖 | 不是完整 Attention、完整线性层或端到端推理 |
| Ara 对比 | 四通道 kernel 周期数据 | 所测 workload 的 RTL 周期效率差异 | Posit32 与 FP32 语义不同；不是指令级等价比较 |
| OpenROAD 实现 | Nangate45、45 ns、55% floorplan | 原始映射单元数和面积观测 | 功能边界和物理阶段未完全对齐，不定义归一化 PPA 比值 |

## 5. 建议的十页论文结构

推荐采用以下正文结构。相关工作不要扩展为长篇独立综述，而应服务于研究动机、设计选择和对比边界。

| 内容 | 建议页数 | 写作目标 |
|---|---:|---|
| 标题与摘要 | 0.45 | 给出问题、方法、两项主结果和限制后的结论 |
| I. Introduction | 1.05 | 建立 Posit/IEEE 互操作与向量执行问题，明确研究空白和贡献 |
| II. Background and Design Requirements | 0.75 | 定义 Posit32、RVV 执行语义、Ara 参照边界和设计要求 |
| III. DFPVU Architecture | 2.15 | 描述接口、四通道组织、流水线、转换、Dot 和 MAC/GEMM 数据流 |
| IV. Experimental Methodology | 1.15 | 固定正确性 oracle、workload 来源、周期口径、Ara 和 OpenROAD 条件 |
| V. Evaluation | 3.55 | 呈现正确性范围、direct workload、矩阵形状、Ara 对比和实现成本 |
| VI. Discussion and Limitations | 0.65 | 解释 mismatch、语义差异、单次确定性仿真和物理实现边界 |
| VII. Conclusion | 0.25 | 只重述已验证贡献，不加入新指标 |
| 合计 | 10.00 | 图表已计入页数预算 |

如果最终排版超过十页，压缩顺序应是背景说明、次要转换数据和重复限制文字。不能通过删除 MAC mismatch、实验口径或 PPA 限制来节省版面。

## 6. 摘要修改指南

### 6.1 推荐信息顺序

摘要控制在五至六句：

1. 说明现有 IEEE-oriented 向量单元无法直接覆盖 Posit 算术和显式双格式互操作这一问题。
2. 引出 DFPVU 及四通道、流水线、双格式数据通路和 MAC/GEMM 能力。
3. 说明评估使用 exact-reference RTL、三个模型的 trace-derived workload、15 个合成矩阵形状和四通道 Ara 基线。
4. 报告最稳固的数值结果：15 个合成 GEMM 形状全部通过 SoftPosit，完全占用的大矩阵接近 4 MAC/cycle。
5. 报告公平限定后的对比结果：通过一致性门的 direct workload 为 2.64×–3.64× useful-work/cycle，15 个矩阵形状的描述性几何均值为 6.87×。
6. 用原始实现成本和限制收束：DFPVU 映射为 468,466 个单元和 0.541908 mm²；该数据和 Ara VMFPU4 数值是实现边界观测，不构成归一化 PPA 优势。

### 6.2 摘要中的 MAC 处理

如果摘要报告 trace MAC 吞吐，必须在同一句中限定其用途，例如：

> Trace-derived MAC runs sustain 3.976699 useful MAC terms per cycle and are used as cycle-characterization results; they exhibit 3--13 exact mismatches per profile and are not presented as full conformance evidence.

如果版面紧张，可以不在摘要中报告 trace MAC 数字，只保留 15 个全部通过的合成 GEMM 结果。摘要不能一方面使用 trace MAC 作为主要性能结果，另一方面完全省略其 correctness 状态。

### 6.3 摘要禁用表述

- `DFPVU is fully compliant with SoftPosit.`
- `DFPVU accelerates LLM inference.`
- `DFPVU achieves higher accuracy than FP32.`
- `DFPVU is 6.87× faster than Ara.`
- `DFPVU is more area- or energy-efficient than Ara.`
- `The implementation closes timing and routing at 45 ns.`

其中 6.87× 是十五种形状的 MAC/cycle 比值几何均值，不是频率归一化执行时间加速比。推荐写成 `a 6.87× descriptive geometric mean in useful MACs per RTL kernel cycle across the fifteen evaluated shapes`。

## 7. 引言修改指南

### 7.1 段落职责

引言建议采用五段结构。

第一段从并行算术需求切入，说明向量处理器需要在吞吐、数值格式和数据移动之间平衡。LLM trace 只能作为代表性数值分布与矩阵形状来源，不能在开头把研究问题扩大为完整 LLM 推理系统。

第二段介绍 Posit 在动态范围和编码方式上的特点，以及实际系统仍需与 IEEE 754 数据交换。这里需要引用公开的 Posit、RISC-V Vector 和相关硬件研究，不能用本仓库结果替代外部文献。

第三段指出现有 RVV 浮点路径以 IEEE 格式为主，直接加入 Posit 算术会遇到格式转换、流水线调度、尾数利用和操作链累加等问题。Ara 应作为可编程 RVV 基线，而不是被描述为一个设计落后的对手。

第四段给出 DFPVU 的核心洞见：将双格式互操作、四通道请求执行和 MAC/GEMM 请求组织放在同一向量算术边界中，并用明确的 ready/valid 协议隔离运算延迟。该段只介绍方法，不提前堆叠全部实验数字。

第五段列出三至四项贡献，并用一句话交代评估边界：论文报告 RTL kernel 周期和原始映射成本，不报告端到端 token/s、工作负载能耗或签核频率。

### 7.2 研究空白的安全表达

可以写：

> Existing IEEE-oriented RVV floating-point paths do not directly expose the native Posit arithmetic and explicit Posit/IEEE conversion operations evaluated in this work.

不要写：

> Existing vector processors cannot support Posit efficiently.

后一句需要更广泛的系统综述和等功能实现对比，当前证据无法支撑。

## 8. 背景与设计需求修改指南

本节应简洁解释读者理解 DFPVU 所需的技术背景，而不是重复教科书定义。

建议保留四个要点：

- Posit32 编码、NaR、舍入和 SoftPosit oracle 在本文中的角色。
- 四 lane 请求中有效元素、valid MAC term 和 lane utilization 的定义。
- tagged ready/valid 协议如何允许不同延迟操作共享顶层接口。
- DFPVU 与 Ara 的语义边界：前者执行 Posit32 及格式转换，后者在本实验中执行 IEEE FP32 或明确标注的 FP16 proxy。

设计需求可归纳为持续接收请求、保持 lane 级有效性、支持格式边界、正确处理尾数请求和提供可测的 kernel 周期。不要把“修复历史错误”列为设计需求。

## 9. 架构章节修改指南

### 9.1 顶层接口与四通道组织

说明请求携带的操作码、四 lane 操作数、tag、valid/ready 和响应字段。图中应区分控制路径、逐 lane 算术路径、跨 lane Dot/MAC 归约路径和格式转换路径。需要解释为什么每个 request 可以代表四个有效算术项，以及不足四项时如何用有效位屏蔽填充 lane。

### 9.2 流水线与不同延迟操作

描述请求接受、操作选择、流水级寄存、结果返回和 backpressure。不要仅给模块框图，应说明同周期接受能力与内部结果延迟的区别。`request/cycle` 接近 1 说明接口持续供给能力，不等于每个请求只有一个周期延迟。

### 9.3 Posit/IEEE 转换路径

集中说明 FP32→P32、P32→FP32、P32→P16 和 P32→Int 的数据路径及语义。P32→P16 与 Ara FP32→FP16 只构成功能代理对照，不能被描述为同一转换操作的速度比较。

### 9.4 Dot 与 MAC/GEMM 映射

区分以下三种计算：

- 四项 Dot：一个请求产生四项乘积并归约为一个结果。
- chained MAC：以累加状态连接多个四项请求。
- GEMM：将 `M×N×K` 映射为 `M×N×ceil(K/4)` 个请求，并在 K 尾数不足四项时屏蔽无效 lane。

合成 GEMM 的固定六周期启动/排空开销可以用于解释大矩阵接近 4 MAC/cycle。真实 trace 中的 exact mismatch 只在评估章节分析，不应在架构章节把 MAC 描述为“bit-exact fused implementation”。

## 10. 实验方法修改指南

### 10.1 研究问题

建议显式列出四个研究问题：

- RQ1：DFPVU 在哪些操作和输入范围内通过 SoftPosit exact-reference 检查？
- RQ2：四通道流水线在 direct workload 和不同 MNK 形状下达到怎样的有效工作量/周期？
- RQ3：与四通道 Ara 相比，DFPVU 在对应算术 workload 上表现出怎样的 RTL kernel 周期效率和形状敏感性？
- RQ4：在统一 Nangate45、45 ns 和 55% floorplan 策略下，两个算术边界具有怎样的原始实现成本？

RQ4 不要写成“DFPVU 是否具有更高 PPA efficiency”，因为当前实现阶段、功能集合和活动率不一致。

### 10.2 正确性 oracle

硬件输出与 SoftPosit 的 raw Posit bits 相同才计为 exact match。`PASS` 只表示指定 workload、样本和操作路径通过该门槛，不代表完整模型 FP32 输出一致。Ordered-FP32 ULP 和相对误差是数值观察，不替代 Hardware-vs-SoftPosit conformance。

论文应同时报告通过和失败结果。当前 direct workload 共 21 个有效 case，其中 17 个 PASS、4 个 FAIL。四个失败项为三个模型的 MAC 和 Gemma Dot。合成矩阵 15 项全部通过；三个模型的 45 个 trace-derived 矩阵 tile 中有 19 项 PASS、26 项 FAIL。

### 10.3 workload 来源

direct workload 使用 Qwen3-0.6B、Gemma 3 1B 和 Phi-4-mini 的 layer-0 decoder 线性模块或 Add trace。转换和 P32→Int 每模块抽取 256 个真实 input 样本，Dot/Multiply 每模块使用 256 个四元素向量，Add 每个真实 Add operation 使用 256 个元素。MAC 使用 `M=N=K=16` tile，因此不能代表完整投影 K。

矩阵形状实验由 15 个 preset 组成，覆盖 K 尾数、小方阵、K=63/64/65 边界、两个 GEMV 方向、wide、tall 和 cube 形状。三个模型的 45 项矩阵数据是 layer-0 单个 attention projection 的 tile 回放，不是完整线性层输出。

### 10.4 周期指标

`element/cycle` 和 `MAC/cycle` 均为有效工作量除以 RTL kernel cycles。主机运行 Verilator 的墙钟时间不能作为目标硬件时间。单次确定性 RTL 仿真的周期计数可以作为描述性观测；论文应说明相同 RTL、输入和 testbench 下周期数是确定的，因此没有对周期计数计算均值或置信区间。该说明不能扩展到主机耗时、物理频率或功耗。

### 10.5 Ara 对比边界

两种设计均使用四个算术 lane，但执行语义不同。DFPVU 使用 Posit32，Ara direct workload 使用 IEEE FP32；矩阵测试保持相同 MNK 和有效 MAC 数，但各自对照不同数值参考。倍率表示对应实现完成相同数量有效工作时的 kernel 周期效率，不是 bit-identical instruction latency。

### 10.6 OpenROAD 方法边界

可以并列报告 DFPVU `PvuTop` 和四个 Ara lane-local `vmfpu` 的 mapped cells、mapped area 和 55% floorplan core area。工艺库、目标周期和利用率策略相同，但操作集合、内部队列、精度覆盖和综合控制不完全相同。DFPVU 到达 post-GRT 和受限详细路由，Ara VMFPU4 为受限 CTS screening checkpoint，后续阶段不得直接形成比例。

## 11. 结果章节修改指南

### 11.1 正确性结果先于性能结果

结果章节应先定义哪些数据可以同时支撑正确性和性能，哪些只能支撑周期行为。推荐用一个紧凑表格汇总：

| 结果组 | 样本范围 | exact 状态 | 论文用途 |
|---|---|---|---|
| FP32↔P32、P32→P16、Multiply、P32→Int、Add | 三个模型的 direct trace | 全部通过 | 功能与周期结果 |
| 四项 Dot | 三个模型 | Qwen/Phi 通过，Gemma 1 mismatch | 通过项可用于正确性；三项均可报告周期 |
| trace MAC | 三个模型 | 13、10、3 mismatch | 只作周期与误差观察，不作完整一致性结论 |
| 合成 GEMM | 15 个 MNK preset | 15/15 通过 | 主要 MAC 正确性与形状吞吐证据 |
| trace-derived GEMM tiles | 三模型共 45 项 | 19 PASS、26 FAIL | 形状调度和误差分布观察 |

这种呈现方式让审稿人看到失败结果没有被隐藏，同时把论文主正确性证据放在全部通过的合成 GEMM 和通过的 direct workload 上。

### 11.2 direct workload 主结果

正文重点报告通过一致性门且语义较接近的项目：

- Multiply：DFPVU/Ara 为约 3.64× useful elements per cycle。
- P32→Int 对 Ara FP32→Int：2.64×–2.67×，需要明确两侧输入编码语义不同。
- Add：约 3.63× useful elements per cycle。
- Dot 与 MAC 可以报告周期值，但不能进入“全部正确的 direct workload speedup”汇总。
- P32→P16 对 FP32→FP16 的 65.66×–66.25× 只能作为能力代理，不能作为同语义加速比。

推荐主文写法：

> For the direct workload classes that pass their respective reference checks and have corresponding arithmetic roles, DFPVU provides 2.64×--3.64× higher useful work per RTL kernel cycle than the evaluated four-lane Ara kernels.

### 11.3 合成矩阵形状结果

这是当前最完整、最适合作为论文主图的数据。正文需要突出三种现象：

- 15 个 DFPVU preset 全部与 SoftPosit 精确一致。
- 大规模且 lane 完全占用的形状达到 3.997–4.000 MAC/cycle；`CUBE_128` 为 3.999954 MAC/cycle。
- `GEMV_ROW` 和 `GEMV_COLUMN` 在 DFPVU 上均为 3.997072 MAC/cycle，而 Ara 分别为 2.402346 和 0.045203 MAC/cycle，反映两种请求/向量化组织对矩阵方向的敏感性差异。

15 个形状的 DFPVU/Ara MAC/cycle 比值范围为 1.66×–88.42×，描述性几何均值为 6.87×。88.42× 是 `N=1` 时 Ara kernel 向量化方向受限的特定结果，不能当作通用加速比标题。图中应同时显示绝对 MAC/cycle，避免只画倍率放大离群值。

### 11.4 trace-derived 矩阵结果

45 项 trace-derived tile 不应全部放入主文大表。建议按模型汇总 PASS/FAIL 数量，并用一张图显示 shape 对 MAC/cycle 的影响。三个模型同一 shape 的周期行为一致，说明调度吞吐主要由 MNK 形状决定；数值一致性状态则随输入分布变化。

正文必须保留以下限定：所有 preset 的 K 都小于完整投影输入维度，Ordered-FP32 误差是 tile 级观察，不能解释为完整 pre-bias 线性层误差。

### 11.5 OpenROAD 实现成本

主表只保留同阶段最可解释的数据：

| 指标 | DFPVU `PvuTop` | Ara VMFPU4 | 解释 |
|---|---:|---:|---|
| Mapped cells | 468,466 | 637,539 | 当前实现边界的原始观测 |
| Mapped area | 0.541908 mm² | 0.748853 mm² | 相同库、45 ns 和利用率策略；功能集合不同 |
| Core area | 0.983637 mm² | 1.360489 mm² | 55% floorplan 策略结果 |
| Physical status | post-GRT；受限详细路由后 43,770 DRC | 受限 CTS；11,975 hold 和 1,401 capacitance violations | 均非 signoff 结果 |

DFPVU 的 19.8 W 和 Ara 的 0.381 W 均来自不同物理阶段的默认活动率估计，不应放入对比主表。若正文提及，只能用于说明为什么本文不进行功耗比较。

推荐结果表述：

> Under the same Nangate45 library, 45 ns constraint, and 55% floorplan policy, the implemented DFPVU boundary maps to fewer cells and less mapped cell area than the four-VMFPU Ara boundary. We report these values as raw implementation-boundary observations rather than a normalized area-efficiency result because the operation sets and local control structures are not equivalent.

## 12. 图表规划

建议将主文限制在三幅图和三张表，防止十页篇幅被重复数据占满。

### 12.1 图 1：DFPVU 架构

图中包含请求接口、操作选择、四个 lane、转换路径、Dot/MAC 归约、tag/response 和 backpressure。颜色只区分三类路径：控制、lane-local、cross-lane。图注说明它是逻辑结构，不代表物理 floorplan。

### 12.2 图 2：请求流水和 MAC/GEMM 映射

上半部分用时序示意区分 request acceptance interval 与 result latency；下半部分展示 K 维如何按四项分组以及 K 尾数如何屏蔽。该图直接解释 6-cycle 固定开销和大矩阵接近 4 MAC/cycle的原因。

### 12.3 图 3：矩阵形状结果

推荐双面板：左图为 DFPVU 与 Ara 的绝对 MAC/cycle，右图为 DFPVU/Ara 描述性比值。右图若使用对数坐标，需要在图注中明确标出；`GEMV_COLUMN` 的 88.42× 需要在正文解释为当前 Ara kernel 的向量化方向效应。

### 12.4 表 1：实验配置与语义边界

包括 lane 数、VLEN、数值格式、正确性 oracle、周期口径、trace 范围和 PPA 条件。该表取代散落在多个小节中的重复配置描述。

### 12.5 表 2：direct workload 与 correctness 状态

按 workload family 汇总三个 profile 的吞吐范围、Ara 范围、倍率和 correctness 状态。MAC 与 Gemma Dot 行必须显示 mismatch；窄格式转换行标注 `proxy`。

### 12.6 表 3：OpenROAD 实现成本

只放 mapped cells、mapped area、core area 和 physical-status note。不放不可比的 die ratio、默认活动率 power ratio 或频率 ratio。

## 13. Discussion and Limitations 写法

限制部分应承担科学解释，而不是把所有问题推给 future work。建议包含四段。

第一段解释数值一致性边界。合成 GEMM 和多数 direct workload 通过 exact-reference gate，但真实 trace MAC 和 Gemma Dot 存在少量 raw-bit mismatch。论文因此将这些失败项用于周期和误差观察，不把它们作为完整 Posit conformance 证据。不要猜测 mismatch 根因，除非已有独立定位证据。

第二段解释 workload 边界。真实 trace 提供了模型相关的输入分布和形状，但实验只覆盖 layer-0 算术 tile，缺少完整层、模型质量和 token throughput。论文的结论对象是向量算术内核，不是完整推理系统。

第三段解释基线公平性。四 lane 和相同工作量建立了周期比较基础，但 Posit32 与 IEEE FP32 的语义和操作集合不同。矩阵形状结果更适合说明调度、lane 利用率和 kernel 周期行为，不适合推导数值格式间的指令等价性。

第四段解释物理实现边界。映射阶段提供实现成本观测，但两个设计未在等功能、等阶段、等活动率和 signoff-clean 条件下完成。因此论文不报告面积效率、能效或频率优势。

这些限制不会否定体系结构和周期结果。它们界定了本文能回答的问题，也能减少评审人在 correctness 和 fairness 上产生的误解。

## 14. 结论修改指南

结论只保留三项已经验证的内容：DFPVU 提供四通道 Posit/IEEE 双格式向量算术边界；合成 GEMM 在 15 个形状上全部通过 SoftPosit 且大矩阵接近 4 MAC/cycle；在限定语义下，DFPVU 对通过的 direct workload 和共同矩阵形状表现出更高 RTL kernel 周期效率。实现成本可以用一个从句报告，但必须保留“raw mapped-cost observation”的限定。

结论不要再次展开 88.42× 极值，也不要用 future work 掩盖当前 mismatch。可以用一句话说明 trace-dependent conformance 和完整系统评估超出本文论断范围。

## 15. 可直接使用的安全措辞

| 想表达的内容 | 推荐措辞 | 避免措辞 |
|---|---|---|
| MAC 已加入 | `DFPVU implements a four-lane chained-MAC path.` | `DFPVU provides a fully exact MAC for all inputs.` |
| 合成测试正确 | `All fifteen synthetic GEMM configurations match SoftPosit exactly.` | `The complete MAC implementation is error-free.` |
| trace MAC 性能 | `Trace-derived MAC runs sustain 3.976699 useful MAC terms per cycle; their exact mismatches are reported separately.` | `DFPVU accelerates LLM MAC without accuracy loss.` |
| workload 来源 | `trace-derived arithmetic microbenchmarks from three model profiles` | `end-to-end LLM workloads` |
| Ara 性能比较 | `higher useful work per RTL kernel cycle on the evaluated kernels` | `faster hardware` |
| 面积结果 | `a smaller mapped implementation boundary under the stated conditions` | `better area efficiency` |
| 功耗 | `vectorless estimates are excluded from cross-design comparison` | `lower power` 或 `higher TOPS/W` |
| 物理实现 | `near closure after global routing, with remaining routing violations` | `timing-clean`, `DRC-clean`, `tapeout-ready` |
| Posit 能力 | `native Posit/IEEE interoperability` | `higher LLM accuracy than FP32` |

## 16. 证据文件与论文用途

| 证据文件 | 主要用途 | 不应承担的论断 |
|---|---|---|
| [`llm_p32_workload_matrix_results.md`](llm_p32_workload_matrix_results.md) | direct workload、样本来源、正确性状态、周期吞吐 | 完整 LLM 推理或所有 MAC 一致性 |
| [`matrix_gemm_p32_mnk_results.md`](matrix_gemm_p32_mnk_results.md) | 15 个合成形状和 45 个 trace tile | 完整线性层误差或 token/s |
| [`ara_4lane_workload_results.md`](ara_4lane_workload_results.md) | Ara kernel 周期、正确性和语义代理说明 | Posit 指令级等价性 |
| [`dfpvu_vs_ara_ipdps_en.md`](dfpvu_vs_ara_ipdps_en.md) | 英文周期对比段落和倍率 | 频率、功耗或面积归一化加速 |
| [`dfpvu_vs_ara_ipdps_zh.md`](dfpvu_vs_ara_ipdps_zh.md) | 中文改稿参考和边界说明 | 直接作为英文终稿翻译来源 |
| [`dfpvu_openroad_ppa_45ns.md`](dfpvu_openroad_ppa_45ns.md) | DFPVU mapped cost、clock、route 状态 | workload power 或 signoff |
| [`ara_vmfpu4_openroad_compute_cost_45ns.md`](ara_vmfpu4_openroad_compute_cost_45ns.md) | Ara VMFPU4 mapped cost 和 CTS 状态 | 可达到频率或 route quality |
| [`dfpvu_ara_vmfpu4_openroad_comparison_45ns.md`](dfpvu_ara_vmfpu4_openroad_comparison_45ns.md) | 英文实现成本表述边界 | normalized PPA superiority |

论文中的每个表格数字应从上述单一来源提取，避免在多个手工副本之间更新。若中文说明与英文主记录不一致，以带有完整 provenance 的英文记录和原始 CSV/日志为准，并记录修订原因。

## 17. 可复现性与匿名化处理

当前仓库已保存 workload 配置、运行状态、trace metadata、Ara CSV 和结果汇总，但部分 stdout/stderr 与 OpenROAD 工作目录受 `.gitignore` 管理。即使不重跑实验，也应在提交前完成以下整理：

- 将论文实际引用的 CSV、配置、关键日志和 OpenROAD 摘要复制到一个只读 artifact 包。
- 写入 DFPVU commit、Ara revision、SoftPosit revision、Verilator/OpenROAD-flow-scripts 版本和操作系统信息。
- 为 artifact 文件生成校验和，并提供从原始结果生成论文表格的脚本或明确命令。
- 区分可公开文件、依赖本地模型权重的文件和不可复现的绝对路径。
- 匿名评审阶段使用匿名 artifact 地址，不在正文、脚本输出或 metadata 中暴露 `/root/models`、用户名、机构路径或仓库所有者身份。
- IPDPS 2027 投稿阶段没有页外 appendix；接收后的 Artifact Description appendix 再按照官方模板补充。

这项整理不改变硬件和实验数据，但能降低“结果只存在于作者本机”的评审风险。

## 18. 高风险审稿问题与正文预防

### 18.1 “MAC 有 mismatch，为什么仍然报告性能？”

正文应提前区分 correctness gate 与 cycle measurement。周期数来自确定的请求执行路径，不因输出是否逐位匹配而失效；但失败项不能作为正确性证据。论文以 15 个全部通过的合成 GEMM 作为 MAC 正确性主证据，以 trace MAC 作为输入分布相关的周期和误差观察。

### 18.2 “DFPVU 与 Ara 不是同一数值格式，比较是否公平？”

回答重点是比较对象和限制：两者使用相同 lane 数、相同有效工作量和 kernel 周期口径，用于观察执行组织；论文没有声称 bit-identical instruction speedup。矩阵输入和各自 oracle 保证两侧结果在自身语义下有效。

### 18.3 “为什么没有端到端 LLM 结果？”

论文的研究对象是向量算术单元，不是包含存储系统、运行时和模型质量评估的完整加速器。LLM trace 用于提高输入分布和形状的现实性。标题、摘要和贡献不得把范围扩大为完整 LLM 加速。

### 18.4 “面积更小是否只是因为功能更少？”

论文应承认操作集合不同，只报告边界观测。DFPVU 保留 Posit/IEEE 路径，Ara VMFPU4 保留 half/single/double、定点、队列和本地控制。面积数字用于量化当前实现成本，不用于证明等功能面积优势。

### 18.5 “为什么没有平均值和误差条？”

RTL kernel 周期在固定设计、输入和 testbench 下是确定的。论文可以说明周期表是确定性单次观测，不对其进行统计推断。数值误差分布应报告样本数量和 mismatch，而不能将一次仿真包装成随机重复实验。

## 19. 推荐的具体改稿顺序

1. 统一标题、摘要、引言和结论的研究范围，删除端到端 LLM、全面正确性和归一化 PPA 暗示。
2. 重写贡献列表，使每项贡献分别对应架构、合成 GEMM、trace workload 和实现成本证据。
3. 在方法章节加入 correctness oracle、周期定义、trace 范围和 DFPVU/Ara 语义边界。
4. 将结果章节改为“正确性范围 → direct workload → 矩阵形状 → Ara 对比 → 实现成本”的顺序。
5. 合并重复表格，主文只保留三图三表；45 项 trace tile 改为汇总，不复制完整大表。
6. 增加独立的 Discussion and Limitations，完整披露 MAC/Dot mismatch、非端到端 workload 和 PPA 阶段差异。
7. 扫描全文中的 `faster`、`speedup`、`PPA-efficient`、`energy-efficient`、`LLM inference`、`exact`、`timing-clean` 等词，逐项检查是否带有证据范围。
8. 完成匿名 artifact 清单和结果到表格的生成记录，为接收后的 reproducibility appendix 留出材料。

## 20. 投稿前检查清单

### 20.1 论断与证据

- [ ] 每个摘要数字都能定位到 `docs/result` 中的维护文件。
- [ ] 贡献列表没有把修 bug 当作科研贡献。
- [ ] 合成 GEMM 的 15/15 PASS 与 trace MAC/矩阵失败被明确区分。
- [ ] 21 个 direct case 的 17 PASS、4 FAIL 没有被改写为全部通过。
- [ ] 45 个 trace-derived tile 的 19 PASS、26 FAIL 没有被遗漏或模糊化。
- [ ] 2.64×–3.64× 只用于通过正确性门且语义较接近的 direct workload。
- [ ] 6.87× 被称为十五个形状的描述性 MAC/cycle 几何均值。
- [ ] 88.42× 极值带有 Ara kernel 向量化方向解释。

### 20.2 workload 与性能口径

- [ ] 全文使用 `trace-derived arithmetic workload/microbenchmark`，不写成端到端 LLM inference。
- [ ] `cycle` 明确为 RTL kernel cycle，不是主机 Verilator 时间。
- [ ] 未报告 token/s、系统 TOPS 或实际执行时间。
- [ ] 未把 tile 级 Ordered-FP32 误差写成完整模型精度。
- [ ] 单次确定性仿真的解释没有被扩大为统计显著性结论。

### 20.3 PPA 与实现状态

- [ ] 面积表使用 mapped area，而不是不可比的 die area ratio。
- [ ] DFPVU 0.541908 mm² 与 Ara VMFPU4 0.748853 mm² 被称为原始边界观测。
- [ ] 未计算或暗示 normalized area、power、energy、frequency 或 efficiency ratio。
- [ ] 19.8 W 和 0.381 W 未被放在同一功耗比较中。
- [ ] DFPVU 的 43,770 个 DRC 与 Ara 的 hold/capacitance 违例没有被隐藏。
- [ ] 全文没有 `signoff-clean`、`DRC-clean`、`timing-clean` 或 `tapeout-ready` 结论。

### 20.4 IPDPS 合规性

- [ ] 正文、图和表合计不超过 10 页，参考文献完整且不计页数。
- [ ] 稿件和 artifact 均完成 double-anonymous 检查。
- [ ] 正文明确说明 motivation、state-of-the-art limitation、key insight、methodology 和 approach limitation。
- [ ] 投稿版本没有额外 appendix 或 supplementary section。
- [ ] 参考文献没有虚构条目，并尽可能包含 DOI 或直接网页链接。
- [ ] 若正文采用 AI 生成文字，已按 IPDPS 2027/IEEE 要求完成披露和引用处理。
- [ ] 已为接收后的 mandatory Artifact Description appendix 保存复现材料。

## 21. 最终判断

在不修改硬件、不消除剩余 mismatch、不重跑 PPA 的条件下，现有材料仍足以形成面向 IPDPS 的完整论文。稿件的竞争力来自双格式向量算术设计、四通道持续供给、合成 GEMM 的完整正确性、矩阵形状适应性、trace-derived 微基准和透明的实现成本分析，而不是来自端到端 LLM、完全数值一致性或归一化 PPA 优势。

改稿是否成立取决于范围纪律：性能结论保持在 RTL kernel cycle，MAC 正确性以合成 GEMM 为主并披露 trace mismatch，Ara 对比保留格式语义差异，OpenROAD 数据保持 raw implementation-cost 定位。只要摘要、贡献、结果和结论遵守同一证据边界，当前实验包可以支撑投稿稿件的撰写。
