# DFPVU 与 Ara VMFPU4 的 45 ns OpenROAD 硬件开销对比

## 文档目的与结论边界

本文档面向论文写作，对比 DFPVU `PvuTop` 与四个 Ara lane-local `vmfpu` 实例的 OpenROAD 实现开销。它替代了先前以完整 Ara 为基线的对比；完整 Ara 包含向量寄存器文件、访存路径、调度器和互连，不能作为算术边界参考。新的 Ara 封装排除了这些系统级子模块，同时保留四个 VMFPU 实例中的操作数/结果队列、控制、整数乘法、定点运算和 IEEE 浮点逻辑。

现有证据可分为三类。映射阶段支持同条件下的原始硬件开销观测；DFPVU 的原生 Posit/IEEE 数据通路支持功能覆盖结论；独立的 RTL 周期实验支持周期效率和矩阵形状适应性结论。它们不支持归一化面积、频率、功耗、能耗或面积效率比较。DFPVU 与 Ara VMFPU4 的指令语义、格式支持、精度覆盖范围和内部控制仍不相同。

## 共同实现条件

| 项目 | DFPVU `PvuTop` | Ara VMFPU4 | 对比使用方式 |
|---|---|---|---|
| 工艺平台 | OpenROAD-flow-scripts Nangate45 | OpenROAD-flow-scripts Nangate45 | 相同 |
| 目标时钟约束 | 物理 `clock` 端口上的 45.00 ns | 物理 `clk_i` 端口上的 45.00 ns | 相同 |
| 目标 core 利用率 | 55% | 55% | 相同 |
| 算术边界 | DFPVU Posit/IEEE 向量数据通路 | 四个 Ara lane-local `vmfpu` 实例 | 比完整 Ara 更接近，但非等操作集合 |
| 排除的 Ara 子系统 | 不适用 | 向量寄存器文件、访存单元、调度器、存储器接口和互连 | 消除了先前系统级边界失配 |
| 算术/格式覆盖 | Posit 与 IEEE 转换/数据通路 | 面向 IEEE 的 FPU/乘法器，支持 half/single/double、定点和单元内控制 | 未对齐 |
| 综合控制 | 绑定真实时钟的 DFPVU 流程 | Slang `--unroll-limit=20000`，Yosys `-noshare` 可扩展性设置 | 为完成实现所需的工具设置不同 |

工艺库、时钟约束和 floorplan 策略已经对齐，但这不代表两个算术操作完全相同。Ara VMFPU4 保留更宽的 IEEE 操作覆盖和单元内队列/控制，DFPVU 保留原生 Posit/IEEE 支持。因此，下表的映射开销是已实现边界的直接观测，不能被写成相同操作集合下的面积效率结论。

## 映射逻辑与 floorplan 观测

| 指标 | DFPVU `PvuTop` | Ara VMFPU4 | 论文中的解释 |
|---|---:|---:|---|
| 映射标准单元数 | 468,466 | 637,539 | 当前实现边界下 DFPVU 少 169,073 个单元；仅为原始观测 |
| 映射标准单元面积 | 541,907.702 um²（0.541908 mm²） | 748,853.308 um²（0.748853 mm²） | DFPVU 映射阶段少 206,945.606 um²；不构成归一化面积优势 |
| 映射时序单元面积 | 未按可比类别报告 | 156,829.344 um²（20.94%） | 不作跨设计结论 |
| DFF 类单元数 | 8,857 个 `DFF_X1` | 29,568 个 DFF 类单元 | 单元类别定义不同，分别报告 |
| Core 面积 | 983,637.144 um²（0.983637 mm²） | 1,360,488.920 um²（1.360489 mm²） | 在 55% floorplan 策略下，DFPVU core 少 376,851.776 um² |
| 流程生成的 die 面积 | 3.957036 mm² | 1.366222 mm² | 不可比较：die 轮廓边界不同，且两者均非封装面积 |
| 初始/有效 floorplan 利用率 | 55.1% | 55.0% | 目标利用率已对齐 |

映射单元数和映射面积是当前最可靠的实现成本证据。在所述条件下，DFPVU 边界映射为更少的标准单元和更小的标准单元面积，同时保留双格式数据通路。该观察可以用于说明实现紧凑性，但不能转化为面积比或一般性的面积效率结论，因为两个边界仍含不同的功能覆盖范围。

## 物理阶段指标及其不可比原因

| 指标 | DFPVU 证据 | Ara VMFPU4 证据 | 可用方式 |
|---|---|---|---|
| 布局/CTS 后面积 | post-GRT 设计面积 525,718 um²，利用率 53% | 筛选 CTS 面积 762,037 um²，利用率 56.0120% | 不可比较：实现阶段和新增单元策略不同 |
| 时钟树构建 | 1 个 root、8,857 个原始 sink、1,568 个插入缓冲 | 6 个时钟网络；主寄存器网络 CTS 前/后为 29,416/33,107 个 sink，含 5,366 个缓冲 | 仅描述时钟结构 |
| 时序 | post-GRT WNS/TNS = -0.02/-0.12 ns，最小周期 45.02 ns | 仅 setup 的 CTS WNS/TNS = 22.2029/0.00 ns，报告最小周期 4.10 ns | 不作频率结论 |
| 收敛质量 | 接近 45 ns 闭合，但非 timing-clean | 跳过 CTS 修复后保留 11,975 个 hold 违例和 1,401 个电容违例 | 两者均非签核质量 |
| 路由 | GRT 完成；一轮详细路由修复后仍有 43,770 个 DRC | 没有 global-route 或 detailed-route 结果 | 不作可布线性或可制造性比较 |
| 功耗 | post-GRT 默认活动率估计：总功耗 19.8 W，时钟功耗 1.09 mW | CTS 默认活动率估计：总功耗 0.381 W，时钟功耗 3.51 mW | 不作功耗或能耗比较 |

Ara VMFPU4 使用受限筛选配置：关闭 timing-driven placement、routability-driven placement、CTS timing repair 和 DPO。其 CTS 检查点可用于报告物理单元开销和时钟网络规模，但不能证明时序闭合。DFPVU 到达更后的物理阶段，但受限详细路由同样未 DRC-clean。布线后时序和功耗行只能作为各自设计的诊断信息，不能成为跨设计 PPA 证据。

## 有证据支持的 DFPVU 优势

| 优势类别 | 证据 | 可以成立的表述 | 不能成立的表述 |
|---|---|---|---|
| 双格式功能 | DFPVU 支持 FP32→P32、P32→FP32 和 P32→P16；Ara VMFPU4 面向 IEEE | DFPVU 提供未经修改的 Ara 浮点向量路径所不具备的原生 Posit/IEEE 互操作 | Posit 相比 FP32 具有更高模型精度 |
| 原始映射实现成本 | DFPVU 为 468,466 个单元、0.541908 mm²；Ara VMFPU4 为 637,539 个单元、0.748853 mm² | 在相同库、时钟和利用率策略下，测得的 DFPVU 边界映射单元数和面积更小 | 归一化面积或面积效率优势 |
| 通过一致性门的 direct RTL workload | Multiply 为 3.64×；整数转换为 2.64×–2.67×；Add 为 3.63× 的 DFPVU/Ara 有效工作量/周期 | DFPVU 在这些通过各自参考模型、语义较接近的操作类别上具有更高实测周期效率 | 频率归一化吞吐或端到端加速比 |
| 矩阵形状吞吐率 | 15 个合成 MNK 形状均通过各自参考；倍率为 1.66×–88.42×，描述性几何均值为 6.87× | DFPVU 在每个已测形状上具有更高 MAC/cycle，且两个 GEMV 方向均保持 3.997072 MAC/cycle | Posit/FP32 逐位等价指令延迟，或统计总体推断 |
| 小形状控制开销 | `TAIL_K1`–`TAIL_K4`：DFPVU 为 12 cycles，Ara 为 181–272 cycles；`TAIL_K5` 为 19 对 316 cycles | 在这些 preset 中，DFPVU 的请求/流水线组织具有更低的实测 kernel 周期开销 | 将全部增益归因到单个硬件模块 |

上述 direct workload 优势不包含 Qwen、Gemma 和 Phi 的 trace 驱动 DFPVU MAC 行，它们分别有 13、10 和 3 个 exact mismatch；也不包含 Gemma Dot 行的 1 个 mismatch。窄格式转换只作为功能代理，不作为直接加速比，因为它比较的是 P32→P16 与 FP32→FP16。矩阵测试使用相同形状和有效 MAC 项数，分别通过 Posit32 和 FP32 参考模型；格式语义仍然不同。

## 可用于 IPDPS 的解释

在相同 Nangate45 平台、45 ns 时钟约束和 55% core 利用率目标下，DFPVU `PvuTop` 映射为 468,466 个标准单元和 0.541908 mm²，四 VMFPU Ara 边界映射为 637,539 个标准单元和 0.748853 mm²，二者的绝对映射面积差为 0.206946 mm²。该数据应作为实现边界成本观测，而不是归一化面积优势；DFPVU 提供 Posit/IEEE 支持，Ara VMFPU4 则保留不同的 IEEE 操作集合和单元内控制结构。

独立的四通道 RTL 实验表明，DFPVU 在全部 15 个已测矩阵形状上具有更高的有效 MAC 吞吐率，描述性几何均值为 6.87×。其最突出的形状适应性证据是行向和列向 GEMV 均达到 3.997072 MAC/cycle，而评估的 Ara kernel 在向量化输出维度为 1 时从 2.402346 降至 0.045203 MAC/cycle。对于通过一致性检查的整数转换、Multiply 和 Add，DFPVU 的有效工作量/周期高出 2.64×–3.64×。这些都是周期效率结果，不是频率、能耗或面积归一化结论。

## 论文写作限制

- 使用“在所述条件下映射更小的实现边界”，不要使用“PPA-efficient”或“面积效率更高”。
- 对通过的 direct workload 和合成矩阵形状使用“更高的 RTL 周期效率”，不要省略周期级限定而直接写“硬件更快”。
- 将 Posit/IEEE 互操作称为功能优势，不要在没有端到端模型评估时推导 Posit 精度优势。
- 不要将 Ara VMFPU4 的筛选 CTS 时序、功耗或物理面积与 DFPVU 的 post-GRT 或详细路由诊断数据构成比例。
- 不要将任一设计描述为 route-complete、DRC-clean、timing-clean、signoff-clean 或已完成能耗测量。

## 证据来源

- [`dfpvu_openroad_ppa_45ns_zh.md`](dfpvu_openroad_ppa_45ns_zh.md)
- [`ara_vmfpu4_openroad_compute_cost_45ns_zh.md`](ara_vmfpu4_openroad_compute_cost_45ns_zh.md)
- [`dfpvu_vs_ara_ipdps_zh.md`](dfpvu_vs_ara_ipdps_zh.md)
- [`matrix_gemm_p32_mnk_results.md`](matrix_gemm_p32_mnk_results.md)
- [`ara_4lane_workload_results.md`](ara_4lane_workload_results.md)
