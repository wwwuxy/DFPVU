# DFPVU

DFPVU（Dynamic Floating-point / Posit Vector Processing Unit）是一个用
[Chisel](https://www.chisel-lang.org/) 编写的可参数化向量处理单元。它以
Posit 为主要内部数值表示，同时支持 IEEE-754 浮点输入、输出及与 Posit
之间的转换。默认配置生成一个 `Posit<32,2>`、4 元素向量的 `PvuTop`，适合
用于探索混合精度计算、神经网络推理和科学计算中的数值运算单元。

## 功能

- 逐元素 Posit 加、减、乘、除。
- 向量点积（向量输入、标量输出）。
- Posit 精度转换，以及 Posit 与 FP4/FP8/FP16/FP32/FP64 的相互转换。
- Posit 比较：输出每对元素中的较大值或较小值。
- Posit 截断为有符号整数。
- 运行时选择源/目标 Posit 位宽与有效向量长度；顶层将最大 Posit 位宽限制为
  64 位、最大有效向量长度限制为 16。

## 技术栈

| 用途 | 技术 |
| --- | --- |
| 硬件描述 | Scala 2.13.12、Chisel 6（Mill 配置为 6.5.0；sbt 配置为 6.2.0） |
| 生成 RTL | Chisel/CIRCT，输出 SystemVerilog |
| RTL 仿真 | Verilator、C++ |
| 构建入口 | `make` + sbt；另提供 Mill 0.11.7 构建定义 |
| 参考模型/测试数据 | SoftPosit、`test_src/` 二进制向量 |

## 架构

`PvuTop` 接收两路 Posit 向量或两路浮点向量，并由 `op` 选择运算。输入先由
`PositDecode` 或 `FloatDecode` 转换为内部表示（符号、指数、尾数）；随后进入
算术、点积、转换或比较单元；最后由 `PositEncode` 或 `FloatEncode` 编码为所选
输出格式。

顶层采用事务通道：请求仅在 `in_valid && in_ready` 时被接受，`in_tag` 随请求
锁存；响应以 `out_valid`、`out_tag` 与 `out_op` 标识，并仅在
`out_valid && out_ready` 时完成传输。响应缓冲为单槽，因此当
`out_valid && !out_ready` 时所有结果位保持稳定，且 `in_ready` 会去断言。

| 层次 | 主要模块 |
| --- | --- |
| 顶层与接口 | `PvuTop.scala`、`Elaborate.scala` |
| 格式处理 | `PositDecode/Encode`、`FloatDecode/Encode`、`FloatToPosit`、`PositToFloat`、`PositConvert`、`PositToInt` |
| 运算 | `Add`、`Sub`、`Mul`、`Div`、`DotProduct`、`PositGreater`、`PositLess` |
| 算术基础单元 | Booth 乘法器、压缩树、桶形移位器、前导零计数器、整数除法/倒数单元 |

### 顶层默认参数

`Elaborate.scala` 当前生成：`MAX_POSIT_WIDTH=32`、`MAX_VECTOR_SIZE=4`、
`MAX_ALIGN_WIDTH=30`、`ES=2`、`FLOAT_MODE=3`（FP32）。修改这些参数后重新生成
RTL 即可得到其他静态配置。

### 操作码

| `op` | 操作 |
| ---: | --- |
| 1–5 | Add、Sub、Mul、Div、DotProduct |
| 6 | Posit 精度转换 |
| 7 | Float ↔ Posit 转换（由 `float_posit` 决定方向） |
| 8–10 | Greater、Less、Posit 转 Int |

浮点格式由 `float_mode` 选择：0=FP4、1=FP8、2=FP16、3=FP32、4=FP64。

## 快速开始

### 前置条件

- JDK、sbt 和可用的网络/本地 Maven 缓存，以解析 Chisel 依赖。
- GNU Make、C++ 编译器和 Verilator。
- 可选：`menuconfig`（选择 C++ 仿真用例）和 GTKWave（查看 VCD 波形）。
- C++ 回归测试包含 `../SoftPosit/source/include/softposit.h`；请将 SoftPosit
  放在 DFPVU 仓库的同级目录，或按你的目录布局修改 `csrc/` 中的 include 路径。

从仓库根目录生成 RTL：

```bash
make verilog
```

这会运行 `pvu.Elaborate`，并将生成的顶层 RTL 写入 `vsrc/PvuTop.sv`。随后选择
一个仿真用例并运行：

```bash
make menuconfig
make config.h
make run
```

`make run` 使用 Verilator 编译 `vsrc/` 和 `csrc/`，然后执行
`obj_dir/VPvuTop`。当前提交的 `.config` 选择的是 Posit32 除法回归；用
`menuconfig` 可选择加、减、乘、除、点积、比较、格式转换或截断测试。也可以一次
完成生成与仿真：

```bash
make debug
```

若测试程序生成 `pvu_top_wave.vcd`，可打开波形：

```bash
make wave
```

## 测试

- `csrc/`：Verilator C++ 驱动程序。通过 `config.h` 中的 `CONFIG_*` 宏选择一个
  `main`；`Kconfig` 定义了可选用例。
- `test_src/`：Posit32/FP32 的输入数据和预期结果（二进制文件）。
本项目只使用 Verilator 和 `csrc/` 中的 C++ 驱动进行功能回归，不维护或运行 Scala
测试及 ChiselTest。sbt 和 Mill 仅用于编译、展开 Chisel 设计及生成 RTL。

## 项目结构

```text
src/main/scala/pvu/   Chisel 顶层、数值格式和运算模块
src/main/resources/   Verilog 资源
vsrc/                 生成的 SystemVerilog 顶层
csrc/                 Verilator C++ 测试驱动
test_src/             二进制测试向量与 golden results
Kconfig, .config      仿真用例选择
makefile              RTL 生成、仿真与波形查看命令
build.sbt, build.sc   sbt 与 Mill 构建配置
```

## 开发说明

- 生成 RTL 前先在 `Elaborate.scala` 中确认需要的静态参数；C++ 测试驱动目前按默认
  4 路、32 位接口编写。
- 修改 C++ 用例选择后执行 `make config.h`，再运行 `make run`。
- 生成的 Verilator 产物位于 `obj_dir/`；生成 RTL 位于 `vsrc/`。提交前请确认是否
  有意纳入这些生成文件。
- 目前仓库未提供贡献指南、分支策略、代码格式配置或许可证文件。提交贡献前请先与
  维护者确认相应要求。

## 许可证

本仓库当前未包含许可证声明。使用、复制或分发前请联系项目维护者确认授权条款。
