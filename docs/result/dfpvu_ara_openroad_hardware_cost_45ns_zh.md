# DFPVU 与 Ara 的 45 ns OpenROAD 硬件开销证据

表 1 并列展示 raw implementation observations，而不是归一化 PPA 对比。两行都采用 OpenROAD Nangate45、45.00 ns 目标时钟和 55% 目标核心利用率。RTL 边界存在实质差异：DFPVU 是 `PvuTop` 算术单元实现，Ara 则是完整四通道（`NR_LANES=4`、`VLEN=4096`）RVV accelerator。因此不定义 DFPVU/Ara 比例。

| 指标 | DFPVU `PvuTop` | Ara 完整四通道 accelerator | 证据边界 |
|---|---:|---:|---|
| RTL 范围 | 算术单元 | 向量寄存器文件、访存接口、分派/控制和向量功能单元 | 实现范围不同 |
| 映射标准单元数 | 468,466 | 1,996,778 | 工艺映射综合 |
| 映射标准单元面积 | 0.541908 mm² | 2.730267 mm² | 主要逻辑面积指标；只并列 raw values |
| 映射 DFF 类单元数 | 8,857 个 `DFF_X1` | 195,470 个 DFF 类单元 | 综合 cell statistics |
| 核面积 | 0.983637 mm² | 4.962090 mm² | 55% 目标利用率 floorplan |
| 流程生成的 die 面积 | 3.957036 mm² | 4.973030 mm² | 不是封装或 pad-ring 面积 |
| Floorplan 利用率 | 55.1% | 55.0% | placement 改动前 |
| CTS 物理状态 | 标准 CTS：1 个 root、8,857 个原始 sink、1,568 个插入 buffer | Screening CTS：42 个时钟网络、215,976 个报告 sink、33,757 个 leaf buffer、58 个 long-wire buffer、98 个 delay buffer | Ara CTS 仅用于 screening |
| CTS 面积 / 利用率 | 不作为 DFPVU 主面积指标 | 2.879063 mm² / 58.0212% | Ara 值含实现流程新增单元 |
| CTS 时序 | WNS/TNS = 0.00/0.00 ns；最小周期 44.99 ns | WNS/TNS = 20.7537/0.00 ns；最小周期 21.23 ns | 不构成频率比较：Ara 未执行 placement/CTS timing repair，且为 pre-route |
| 路由证据 | GRT near closure；一轮详细路由后仍有 43,770 个 DRC | 未生成 | Ara 没有 route-complete 点 |
| 功耗证据 | 仅有 post-GRT vectorless 估计 | 未报告 | 没有共同活动模型或路由阶段 |

论文中的硬件开销结论应保持限制。完整 Ara accelerator 在所述 45 ns/55% 策略下映射为 2.730267 mm² Nangate45 标准单元，并使用 4.962090 mm² 核面积。其 screening CTS 含 42 个时钟网络和 215,976 个报告 sink，体现了多时钟域的实现规模。DFPVU `PvuTop` 映射为 0.541908 mm²、使用 0.983637 mm² 核面积，但这个边界不包括 Ara 所含的系统级子模块。该组数据展示的是实现范围和成本组成，不能用于证明面积、频率、功耗、能耗或面积效率优势。

Ara 的综合/floorplan 数据来自标准流程。CTS 采用有界 screening 流程：关闭 timing-driven global placement、CTS timing repair 与 DPO；将 global-placement checkpoint 直接作为 resize 输入，并且只跳过 stage-3 metric aggregation。Ara 结果尚未完成路由、签核或 DRC 清理，不能与 DFPVU 的 post-GRT/详细路由数据当作同一物理阶段比较。

数据来源：[`dfpvu_openroad_ppa_45ns_zh.md`](dfpvu_openroad_ppa_45ns_zh.md)、[`ara_openroad_ppa_45ns_zh.md`](ara_openroad_ppa_45ns_zh.md)。
