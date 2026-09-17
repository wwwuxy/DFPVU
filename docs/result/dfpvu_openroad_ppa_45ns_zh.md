# DFPVU 的 45 ns OpenROAD 硬件开销数据（中文说明）

这份说明对应英文主表 `dfpvu_openroad_ppa_45ns.md`。实现对象为 `PvuTop`，工艺库为 OpenROAD-flow-scripts 的 Nangate45，目标周期为 45.00 ns，核心利用率目标为 55%。SDC 将 `dfpvu_vclk` 明确绑定到顶层真实 `clock` 端口，因此 CTS 和时序报告具有真实的寄存器 launch/capture 路径。

| 指标 | 数据 | 可用于论文的解释 |
|---|---:|---|
| 映射标准单元数 | 468,466 | 逻辑硬件开销 |
| 映射标准单元面积 | 0.541908 mm² | 推荐作为主要的逻辑面积指标 |
| DFF_X1 数量 | 8,857 | 时序状态量的可复核指标 |
| 核面积 | 0.983637 mm² | 55% 目标利用率；不含 pad、ESD、封装 |
| CTS 时钟树 | 1 个根网、8,857 个原始 sink、1,568 个插入缓冲器 | 证明约束为真实时钟而非虚拟时钟 |
| post-GRT 时序 | WNS = -0.02 ns，TNS = -0.12 ns，最小周期 45.02 ns | 45 ns 下 near-closure，不能称 timing-clean |
| 全局路由资源 | 40.49%，零 overflow | 全局路由级指标 |
| 详细路由 DRC | 108,619 → 43,770 | 仅执行第 0 和第 1 次迭代；仍未 DRC-clean |
| 详细路由线长 / 过孔 | 6.382115 m / 2,977,052 | 受限迭代的互连诊断数据 |
| post-GRT 总功耗 / 时钟功耗 | 19.8 W / 1.09 mW | OpenSTA 默认 vectorless 活动估计，不能当工作负载功耗或能耗 |

可在论文中写成：在绑定真实时钟端口的 45 ns Nangate45 流程下，DFPVU 映射为 468,466 个标准单元（0.5419 mm²）和 8,857 个触发器；55% 目标核心利用率对应 0.9836 mm² 核面积。CTS 为 8,857 个原始时钟 sink 插入 1,568 个时钟缓冲器。全局路由后 WNS/TNS 为 -0.02/-0.12 ns，因而属于 near-closure；受限详细路由仍保留 43,770 个 DRC，因此该结果量化当前实现开销和优化空间，不应称为后端签核完成。

与 Ara 的实现开销参照限定为四个 lane-local `vmfpu`。该边界不包含向量寄存器文件、访存路径、调度器和互连；在相同 Nangate45、45 ns、55% floorplan 条件下，映射为 637,539 个单元、面积为 0.748853 mm²。它与 `PvuTop` 构成同一实现阶段的原始算术边界观测，但不构成归一化 PPA 对比。

面积、功耗、能耗、频率和效率比例均不定义。DFPVU 使用 Posit/IEEE 向量数据通路，Ara `vmfpu` 是带有自身队列、控制和 half/single/double 支持的面向 IEEE FPU/乘法器。Ara CTS 仍是关闭 timing/routability-driven placement、CTS repair 和 DPO 的筛选检查点。边界、实测数值和物理限制见 `ara_vmfpu4_openroad_compute_cost_45ns_zh.md`。

原始证据目录：`openroad/clock_bound_45ns_u55_repro/work/`（综合、CTS、GRT）和 `openroad/clock_bound_45ns/work/logs/nangate45/dfpvu/base/5_2_route.log`（完成的详细路由）。
