# DFPVU
## 1. 整体架构
DFPVU项目是一个使用Chisel（基于Scala的硬件描述语言）开发的向量处理单元，主要支持浮点数和Posit数格式的各种运算操作。项目的整体架构如下：
### 核心模块结构
PvuTop: 系统顶层模块，整合了所有功能单元
### 运算单元:
基本算术运算: Add, Sub, Mul, Div
- 向量运算: DotProduct
- 格式转换: PositConvert, FloatToPosit, PositToFloat
### 数据处理流程
输入处理:
- 输入数据可以是Posit格式或Float格式（由Isposit信号控制）
- 支持向量化处理（VECTOR_SIZE可配置）
解码阶段:
- PositDecode/FloatDecode: 将输入数据解码为内部表示形式（符号位、指数、尾数）
计算阶段:
- 根据op信号选择不同的运算单元
- 执行相应的浮点运算或Posit运算
编码阶段:
- PositEncode/FloatEncode: 将计算结果编码回输出格式
- 输出格式由Outposit信号控制
## 2. 关键组件
### 浮点数处理模块
- FloatDecode.scala: 负责将浮点数解码为内部表示
- FloatEncode.scala: 负责将内部表示编码为浮点数格式
- FracNorm.scala: 负责尾数的规格化处理
- FractionAlignment_AddSub.scala: 处理加减法中的尾数对齐
### Posit数处理模块
- PositDecode.scala: 负责将Posit数解码为内部表示
- PositEncode.scala: 负责将内部表示编码为Posit数格式
- PositConvert.scala: 负责不同精度Posit数之间的转换
### 运算单元
- Add.scala/Sub.scala: 实现加减法运算
- Mul.scala: 实现乘法运算
- Div.scala: 实现除法运算
- DotProduct.scala: 实现向量点积运算
- FloatToPosit.scala/PositToFloat.scala: 实现Float和Posit之间的格式转换
### 辅助模块
- BarrelShift.scala: 桶形移位器，用于快速移位操作
- Lzc.scala: 前导零计数器，用于规格化过程
- CsaTree.scala/CompTree.scala: 压缩树，用于高效进行多操作数加法
- Compressor3to2.scala/Compressor4to2.scala: 3-2和4-2压缩器，用于加速乘法运算
- BoothEncoder.scala/Radix4_Booth_Multiplier.scala: 用Booth算法实现的乘法器
## 3. 构建与测试系统
### 构建系统
- 使用Mill作为构建工具
- build.sc定义了项目的依赖与构建配置
- 支持BSP（Build Server Protocol）便于IDE集成
### 测试框架
- 使用ScalaTest进行单元测试
- C++测试程序（csrc目录）用于功能验证
- 测试向量和参考结果存储在test_src目录中
- 提供多种测试场景（add, sub, mul, div, dot_product等）
## 4. 配置与参数化设计
- DFPVU采用高度参数化设计，主要配置参数包括：
- POSIT_WIDTH: Posit数的位宽
- VECTOR_SIZE: 向量大小
- ES: Posit数的指数位域大小
- DST_POSIT_WIDTH/DST_ES: 目标Posit格式参数（用于格式转换）
- FLOAT_MODE: 浮点数格式选择（FP4/FP8/FP16/FP32/FP64）
## 5. 仿真与验证
- 使用Verilator进行RTL仿真
- 生成波形文件（waveform.vcd）用于调试分析
- 提供各种测试向量和预期结果用于验证
- 这种架构设计使DFPVU成为一个高度可配置、灵活的向量处理单元，特别适合需要处理多种精度浮点数和Posit数的应用场景，如机器学习加速、科学计算等领域。项目通过模块化设计实现了各种运算单元的复用和组合，同时支持不同数据格式之间的高效转换。
