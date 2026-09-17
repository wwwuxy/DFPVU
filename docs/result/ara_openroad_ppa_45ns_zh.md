# Ara 四通道 45 ns OpenROAD 硬件开销数据

## 实现范围与来源

该数据集以 Ara 上游版本 `34bd3bc1` 的原生 `ara` accelerator 模块为实现对象，外层 wrapper 只暴露时钟、复位和 accelerator 接口以适配 OpenROAD。配置为 `NR_LANES=4`、`VLEN=4096`、Nangate45、绑定物理 `clk_i` 端口的 45.00 ns `ara_clk` 约束，以及 55% 的目标核心利用率。Bender 生成的 manifest 包含 382 个 SystemVerilog/Verilog 源文件和 7 个 include 目录。这里的 Ara 是完整四通道 RVV accelerator，包含向量寄存器文件、访存接口、分派与控制逻辑以及向量功能单元，并非只包含算术数据通路。

综合使用 Slang 前端和 `--unroll-limit=20000`。完整设计上的 Yosys SAT resource-sharing 在有界时间内无法结束，因而可复现配置采用 `SYNTH_ARGS=-noshare`。这一设置是综合工具可扩展性回退，不改变 RTL 功能。`synth_check.txt` 报告零问题。

## 实测硬件开销

| 指标 | 数值 | 阶段与解释 |
|---|---:|---|
| 映射标准单元数 | 1,996,778 | 工艺映射综合 |
| 映射标准单元面积 | 2,730,267.092 um²（2.730267 mm²） | 主要逻辑面积指标 |
| 时序单元映射面积 | 933,198.224 um²（34.18%） | 综合报告 |
| DFF 类单元数 | 195,470 | `DFFR_X1` 61,581，`DFFS_X1` 177，`DFF_X1` 133,712 |
| 核面积 | 4,962,090.350 um²（4.962090 mm²） | 55% 目标利用率 floorplan |
| Die 外形 | 2.230030 mm × 2.230030 mm = 4.973030 mm² | 流程生成的 die outline，不是封装或 pad-ring 面积 |
| Floorplan 有效利用率 | 55.0% | placement 改动前的 1,996,778 个实例 |
| 全局布局可布线膨胀 | 713,009.79 um²（22.62%） | screening placement 诊断；属于人工 placement inflation，不是逻辑面积 |
| 全局布局最终拥塞 | 1.4885 weighted | screening placement 诊断；未达到目标拥塞 |
| Screening CTS 标准单元数 | 2,212,392 | 包含 CTS 后实现流程新增的单元 |
| Screening CTS 标准单元面积 | 2,879,063 um²（2.879063 mm²） | 相比映射面积增加 148,795.908 um²（5.45%） |
| Screening CTS 利用率 | 58.0212% | 核面积固定为 4.962090 mm² |
| Screening CTS 时钟网络 / sink | 42 / 215,976 | Ara 含多个 gated/vector-register 时钟域；最大网络有 62,038 个 sink |
| Screening CTS 时钟树构建 | 33,757 个 leaf buffer；58 个 long-wire buffer；98 个 delay-balance buffer | CTS 日志分别给出的计数，不将不同类别合并为单一 buffer 总数 |
| Screening CTS 时序 | WNS/TNS = 20.7537/0.00 ns；最小周期 = 21.23 ns（47.11 MHz） | 仅 pre-route screening；未执行 CTS timing repair |
| Screening CTS errors / warnings | 0 / 133,468 | OpenROAD metrics；大量 warning 来自被跳过的单 sink 时钟网络 |

2.730267 mm² 的综合面积和 4.962090 mm² 的 floorplan 核面积是在同一库、同一周期和同一利用率条件下直接得到的成本数据。CTS 行展示完整 Ara 边界的时钟与物理单元开销，但不代表完成布线或完成签核。

## 物理 screening 的边界

默认 timing-driven global placement 在 2,051,123 条待处理网络的全网 `repair_design` 阶段持续超过 30 分钟，且未写出可恢复 checkpoint。为获得有界的 placement 与 CTS 证据，单独采用 screening 配置。该配置保持 RTL、标准单元库、45 ns 约束、floorplan 和 CTS construction 不变，但设置 `GPL_TIMING_DRIVEN=0`、`SKIP_CTS_REPAIR_TIMING=1` 与 `ENABLE_DPO=0`。screening runner 还将 `3_3_place_gp.odb` 直接复用为 resize 输入，并仅跳过 stage-3 report aggregation；CTS final metrics 保持启用。

没有报告 Ara 的 global route、detailed route、post-route timing、DRC、布线线长/过孔或带工作负载活动的功耗。该 screening CTS 不能称为 route-complete、signoff-clean 或 DRC-clean，也不能与 DFPVU 的 post-GRT 和受限详细路由诊断数据直接并列为同一物理阶段。

## 可用于论文的比较表述

在相同 Nangate45 平台、45 ns 时钟目标和 55% floorplan 策略下，完整四通道 Ara accelerator 映射为 1,996,778 个标准单元、2.730267 mm² 映射面积，并使用 4.962090 mm² 核面积。其 screening CTS 含有 42 个时钟网络和 215,976 个报告的时钟 sink，体现了完整 RVV accelerator 边界的时钟开销。这些数值可以与 DFPVU `PvuTop` 成本表作为 raw implementation evidence 并列，但不能计算 DFPVU/Ara 的面积、功耗、频率或能耗比例，也不能以此表述 DFPVU 的面积优势；`PvuTop` 是算术单元实现，而 Ara 还包含向量寄存器文件、访存接口、分派与控制子系统。

## 证据路径

- Harness 与标准配置：`openroad/ara_4lane/config.mk`
- Screening 配置与运行器：`openroad/ara_4lane/config_screening.mk`、`openroad/run_ara_4lane_screening.sh`
- 综合证据：`openroad/ara_4lane/work/reports/nangate45/ara_4lane/base/synth_stat.txt`、`synth_check.txt`
- Floorplan 证据：`openroad/ara_4lane/work/logs/nangate45/ara_4lane/base/2_1_floorplan.log`、`openroad/ara_4lane/work/reports/nangate45/ara_4lane/base/2_floorplan_final.rpt`
- Screening global-placement 证据：`openroad/ara_4lane/work/reports/nangate45/ara_4lane/base/3_global_place.rpt`
- Screening CTS 证据：`openroad/ara_4lane/work/reports/nangate45/ara_4lane/base/4_cts_final.rpt`、`openroad/ara_4lane/work/logs/nangate45/ara_4lane/base/4_1_cts.json`、`4_1_cts.log`
