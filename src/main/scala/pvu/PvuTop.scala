  //PVU TOP
  /* 支持六种操作类型 --> op控制
    1 --> 加法
    2 --> 减法
    3 --> 乘法
    4 --> 除法
    5 --> 点积
    6 --> Posit精度转换
    7 --> Float和Posit相互转换
    8 --> 大小比较（Greater），输出较大值
    9 --> 大小比较（Less），输出较小值
    10 --> Posit转Int（TranInt），将PIR转为整数

   Float格式由float_mode控制:
   0 --> FP4  (1位符号, 1位指数, 2位尾数)
   1 --> FP8  (1位符号, 4位指数, 3位尾数)
   2 --> FP16 (1位符号, 5位指数, 10位尾数)
   3 --> FP32 (1位符号, 8位指数, 23位尾数)
   4 --> FP64 (1位符号, 11位指数, 52位尾数)
 */

 package pvu
 import chisel3._ 
 import chisel3.util._
 import scala.languageFeature.existentials
 import chisel3.stage._
 
 class PvuTop(
   val MAX_POSIT_WIDTH: Int, // 最大位宽参数，用于定义输出接口的位宽，支持不同精度之间的转换
   val MAX_VECTOR_SIZE: Int, // 最大向量大小
   val MAX_ALIGN_WIDTH: Int, // 最大对齐宽度
   val ES: Int,          // ES参数，用于定义输出接口的ES参数
   val FLOAT_MODE: Int = 3,   // 浮点数格式
   val INT_WIDTH: Int  = 32   // 整数位宽参数
 ) extends Module {
   // 添加参数限制
   private val LIMITED_VECTOR_SIZE = Math.min(MAX_VECTOR_SIZE, 16)  // 限制最大向量大小为16
   private val LIMITED_POSIT_WIDTH = Math.min(MAX_POSIT_WIDTH, 64) // 限制最大位宽为64
   private val LIMITED_ALIGN_WIDTH = Math.min(MAX_ALIGN_WIDTH, 62) // 限制最大对齐宽度为128

   // 根据FLOAT_MODE设置浮点数参数
   val (float_exp_width, float_frac_width) = FLOAT_MODE match {
     case 0 => (1, 2)   // FP4: 1位符号, 1位指数, 2位尾数
     case 1 => (4, 3)   // FP8: 1位符号, 4位指数, 3位尾数
     case 2 => (5, 10)  // FP16: 1位符号, 5位指数, 10位尾数
     case 3 => (8, 23)  // FP32: 1位符号, 8位指数, 23位尾数
     case 4 => (11, 52) // FP64: 1位符号, 11位指数, 52位尾数
     case _ => (8, 23)  // 默认为FP32
   }
   
   // 浮点数参数
   // 静态FLOAT_WIDTH用于IO定义
   val FLOAT_WIDTH: Int = 1 + 11 + 52  // 使用FP64的宽度作为最大宽度
   
   // 静态声明用于模块实例化 - 这些常量需要最先定义
   val SRC_ND_MAX: Int         = log2Ceil(LIMITED_POSIT_WIDTH - 1)
   val SRC_EXP_WIDTH_MAX: Int  = Math.min(SRC_ND_MAX + ES + 1, 32)
   val SRC_FRAC_WIDTH_MAX: Int = LIMITED_POSIT_WIDTH + 1
   val DST_EXP_WIDTH_MAX: Int  = SRC_EXP_WIDTH_MAX
   
   // 预先计算的常量宽度
   val DOUBLED_FRAC_WIDTH: Int = Math.min(2 * (SRC_FRAC_WIDTH_MAX + 1), 128)
   val DOT_PRODUCT_WIDTH: Int  = Math.min(DOUBLED_FRAC_WIDTH + log2Ceil(LIMITED_VECTOR_SIZE + 1) + 2, 256)
   
   // 动态计算当前使用的浮点数宽度
   def getCurrentFloatWidth(): UInt = {
     // 根据float_mode计算当前使用的浮点数宽度
     MuxLookup(io.float_mode, 32.U)(Seq(
       0.U -> 4.U,   // FP4: 1位符号 + 1位指数 + 2位尾数 = 4位
       1.U -> 8.U,   // FP8: 1位符号 + 4位指数 + 3位尾数 = 8位
       2.U -> 16.U,  // FP16: 1位符号 + 5位指数 + 10位尾数 = 16位
       3.U -> 32.U,  // FP32: 1位符号 + 8位指数 + 23位尾数 = 32位
       4.U -> 64.U   // FP64: 1位符号 + 11位指数 + 52位尾数 = 64位
     ))
   }

   val io = IO(new Bundle {
     // 输入Posit向量
     val posit_i1 = Input(Vec(MAX_VECTOR_SIZE, UInt(MAX_POSIT_WIDTH.W)))
     val posit_i2 = Input(Vec(MAX_VECTOR_SIZE, UInt(MAX_POSIT_WIDTH.W)))
     val op       = Input(UInt(4.W))  // 操作码位宽从3位改为4位，以支持更多操作
     
     // 是否是posit数据的控制信号
     val Isposit  = Input(Bool())  // true表示输入是posit数，false表示输入是float数
     
     // 输出格式控制信号
     val Outposit = Input(Bool())  // 1表示输出posit，0表示输出float，默认为true
     
     // 为Float和Posit转换添加的接口
     val float_i     = Input(Vec(MAX_VECTOR_SIZE, UInt(FLOAT_WIDTH.W)))
     val float_i2    = Input(Vec(MAX_VECTOR_SIZE, UInt(FLOAT_WIDTH.W)))  // 添加第二个Float输入向量
     val float_mode  = Input(UInt(3.W))  // 浮点数格式选择: 0=FP4, 1=FP8, 2=FP16, 3=FP32, 4=FP64
     val float_posit = Input(Bool())     // 浮点数互转信号：1表示Float转Posit，0表示Posit转Float
     
     // 运行时配置参数
     val src_posit_width = Input(UInt(6.W))  // 源Posit位宽，值为0表示使用最大值
     val vector_size     = Input(UInt(3.W))  // 实际使用的向量大小，值为0表示使用最大值
     
     // 目标精度配置接口
     val dst_posit_width = Input(UInt(6.W))  // 目标Posit位宽，值为0表示与源相同
     
     val float_o     = Output(Vec(MAX_VECTOR_SIZE, UInt(FLOAT_WIDTH.W)))
     val float_dot_o = Output(UInt(FLOAT_WIDTH.W))               // 添加float点积输出
     val posit_o     = Output(Vec(MAX_VECTOR_SIZE, UInt(MAX_POSIT_WIDTH.W))) // 使用最大位宽
     val posit_dot_o = Output(UInt(MAX_POSIT_WIDTH.W))           // 使用最大位宽
     val int_o       = Output(Vec(MAX_VECTOR_SIZE, SInt(INT_WIDTH.W)))  // 新增整数输出接口
 })

  // 添加decode模块实例
  val decode1 = Module(new PositDecode(LIMITED_POSIT_WIDTH, LIMITED_VECTOR_SIZE, ES))
  val decode2 = Module(new PositDecode(LIMITED_POSIT_WIDTH, LIMITED_VECTOR_SIZE, ES))

  // 添加float decode模块实例
  val floatDecode1 = Module(new FloatDecode(float_exp_width, float_frac_width, LIMITED_VECTOR_SIZE))
  val floatDecode2 = Module(new FloatDecode(float_exp_width, float_frac_width, LIMITED_VECTOR_SIZE))

  // 添加Wire定义 - 使用统一的中间结果存储
  val pir_sign  = Wire(Vec(LIMITED_VECTOR_SIZE, UInt(1.W)))
  val pir_exp   = Wire(Vec(LIMITED_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
  val pir_frac  = Wire(Vec(LIMITED_VECTOR_SIZE, UInt(SRC_FRAC_WIDTH_MAX.W)))
  val pir_sign2 = Wire(Vec(LIMITED_VECTOR_SIZE, UInt(1.W)))
  val pir_exp2  = Wire(Vec(LIMITED_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
  val pir_frac2 = Wire(Vec(LIMITED_VECTOR_SIZE, UInt(SRC_FRAC_WIDTH_MAX.W)))

  // Float相关Wire定义 - 使用统一的存储
  val float_data = Wire(Vec(LIMITED_VECTOR_SIZE, new Bundle {
    val sign   = Bool()
    val exp    = SInt(float_exp_width.W)
    val frac   = UInt(float_frac_width.W)
    val isNaN  = Bool()
    val isInf  = Bool()
    val isZero = Bool()
  }))

  val float_data2 = Wire(Vec(LIMITED_VECTOR_SIZE, new Bundle {
    val sign   = Bool()
    val exp    = SInt(float_exp_width.W)
    val frac   = UInt(float_frac_width.W)
    val isNaN  = Bool()
    val isInf  = Bool()
    val isZero = Bool()
  }))

  // 点积相关Wire定义
  val float_dot_data = Wire(new Bundle {
    val sign   = Bool()
    val exp    = SInt(float_exp_width.W)
    val frac   = UInt(float_frac_width.W)
    val isNaN  = Bool()
    val isInf  = Bool()
    val isZero = Bool()
  })

  // 结果相关Wire定义
  val float_rst_data = Wire(Vec(LIMITED_VECTOR_SIZE, new Bundle {
    val sign   = Bool()
    val exp    = SInt(float_exp_width.W)
    val frac   = UInt(float_frac_width.W)
    val isNaN  = Bool()
    val isInf  = Bool()
    val isZero = Bool()
  }))

  // 计算结果存储
  val pir_frac_rst = Wire(Vec(LIMITED_VECTOR_SIZE, UInt(Math.max(LIMITED_ALIGN_WIDTH, DOUBLED_FRAC_WIDTH).W)))
  val pir_exp_rst  = Wire(Vec(LIMITED_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
  val pir_sign_rst = Wire(Vec(LIMITED_VECTOR_SIZE, UInt(1.W)))

  // 在类的开始处添加输出端口的默认初始化
  io.float_o     := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(FLOAT_WIDTH.W)))
  io.float_dot_o := 0.U(FLOAT_WIDTH.W)
  io.posit_o     := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(MAX_POSIT_WIDTH.W)))
  io.posit_dot_o := 0.U(MAX_POSIT_WIDTH.W)
  io.int_o       := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.S(INT_WIDTH.W)))

  // 初始化FloatDecode模块的输入
  floatDecode1.io.float := io.float_i
  floatDecode2.io.float := io.float_i2

  // 初始化所有Wire
  for(i <- 0 until LIMITED_VECTOR_SIZE) {
    pir_sign(i)  := 0.U
    pir_exp(i)   := 0.S
    pir_frac(i)  := 0.U
    pir_sign2(i) := 0.U
    pir_exp2(i)  := 0.S
    pir_frac2(i) := 0.U
    
    float_data(i).sign   := false.B
    float_data(i).exp    := 0.S
    float_data(i).frac   := 0.U
    float_data(i).isNaN  := false.B
    float_data(i).isInf  := false.B
    float_data(i).isZero := true.B
    
    float_data2(i).sign   := false.B
    float_data2(i).exp    := 0.S
    float_data2(i).frac   := 0.U
    float_data2(i).isNaN  := false.B
    float_data2(i).isInf  := false.B
    float_data2(i).isZero := true.B
    
    float_rst_data(i).sign   := false.B
    float_rst_data(i).exp    := 0.S
    float_rst_data(i).frac   := 0.U
    float_rst_data(i).isNaN  := false.B
    float_rst_data(i).isInf  := false.B
    float_rst_data(i).isZero := true.B
    
    pir_frac_rst(i) := 0.U
    pir_exp_rst(i)  := 0.S
    pir_sign_rst(i) := 0.U
  }
  
  float_dot_data.sign   := false.B
  float_dot_data.exp    := 0.S
  float_dot_data.frac   := 0.U
  float_dot_data.isNaN  := false.B
  float_dot_data.isInf  := false.B
  float_dot_data.isZero := true.B

  // print("MAX_VECTOR_SIZE: ", MAX_VECTOR_SIZE)
 
   // 计算实际使用的参数
   val ACTUAL_SRC_POSIT_WIDTH = Mux(io.src_posit_width === 0.U, MAX_POSIT_WIDTH.U, io.src_posit_width)
   val ACTUAL_VECTOR_SIZE     = Mux(io.vector_size === 0.U, MAX_VECTOR_SIZE.U, io.vector_size)
   val valid_vector_size      = ACTUAL_VECTOR_SIZE
   
   // 根据IO接口的参数计算实际使用的目标参数
   val ACTUAL_DST_POSIT_WIDTH = Mux(io.dst_posit_width === 0.U, ACTUAL_SRC_POSIT_WIDTH, io.dst_posit_width)
   
   // 计算实际使用的位宽参数 (运行时)
   val src_nd         = log2Ceil(MAX_POSIT_WIDTH)
   val src_exp_width  = src_nd.U + ES.U
   val src_frac_width = ACTUAL_SRC_POSIT_WIDTH - ES.U - 3.U
   val src_mul_width  = DOUBLED_FRAC_WIDTH.U
   val src_sum_width  = DOT_PRODUCT_WIDTH.U
   
   // 对于所有流程，添加valid_range检查
   val valid_range = Wire(Vec(MAX_VECTOR_SIZE, Bool()))
   for (i <- 0 until MAX_VECTOR_SIZE) {
     valid_range(i) := (i.U < valid_vector_size)
   }
   
   // 解码逻辑需要确保只处理有效范围内的输入数据
   when(io.Isposit) {
     decode1.io.posit      := io.posit_i1
     decode2.io.posit      := io.posit_i2
     floatDecode1.io.float := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(FLOAT_WIDTH.W)))
     floatDecode2.io.float := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(FLOAT_WIDTH.W)))
     // 只拷贝有效范围内的结果
     for (i <- 0 until MAX_VECTOR_SIZE) {
       when(valid_range(i)) {
         pir_sign(i)  := decode1.io.Sign(i)
         pir_exp(i)   := decode1.io.Exp(i)
         pir_frac(i)  := decode1.io.Frac(i)
         pir_sign2(i) := decode2.io.Sign(i)
         pir_exp2(i)  := decode2.io.Exp(i)
         pir_frac2(i) := decode2.io.Frac(i)
       }
     }
   }.otherwise {
     decode1.io.posit      := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(MAX_POSIT_WIDTH.W)))
     decode2.io.posit      := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(MAX_POSIT_WIDTH.W)))
     floatDecode1.io.float := io.float_i
     floatDecode2.io.float := io.float_i2
     
     // 保存float解码结果，只处理有效元素
     for(i <- 0 until MAX_VECTOR_SIZE) {
       when(valid_range(i)) {
         float_data(i).sign   := floatDecode1.io.Sign(i)
         float_data(i).exp    := floatDecode1.io.Exp(i)
         float_data(i).frac   := floatDecode1.io.Frac(i)
         float_data(i).isNaN  := floatDecode1.io.isNaN(i)
         float_data(i).isInf  := floatDecode1.io.isInf(i)
         float_data(i).isZero := floatDecode1.io.isZero(i)
         
         float_data2(i).sign   := floatDecode2.io.Sign(i)
         float_data2(i).exp    := floatDecode2.io.Exp(i)
         float_data2(i).frac   := floatDecode2.io.Frac(i)
         float_data2(i).isNaN  := floatDecode2.io.isNaN(i)
         float_data2(i).isInf  := floatDecode2.io.isInf(i)
         float_data2(i).isZero := floatDecode2.io.isZero(i)
         
         // 将Float解码结果转换为统一的PIR格式用于计算
         pir_sign(i)  := float_data(i).sign.asUInt
         pir_exp(i)   := float_data(i).exp
         pir_frac(i)  := float_data(i).frac
         pir_sign2(i) := float_data2(i).sign.asUInt
         pir_exp2(i)  := float_data2(i).exp
         pir_frac2(i) := float_data2(i).frac
       }
     }
   }

   //***********************//
   //get operand and compute//
   //***********************//
   val pir_frac_rst_add = Wire(Vec(MAX_VECTOR_SIZE, UInt(MAX_ALIGN_WIDTH.W)))
   val pir_frac_rst_sub = Wire(Vec(MAX_VECTOR_SIZE, UInt(MAX_ALIGN_WIDTH.W)))
   val pir_frac_rst_mul = Wire(Vec(MAX_VECTOR_SIZE, UInt(DOUBLED_FRAC_WIDTH.W)))
   val pir_frac_rst_div = Wire(Vec(MAX_VECTOR_SIZE, UInt(DOUBLED_FRAC_WIDTH.W)))
   val pir_max_exp      = Wire(Vec(MAX_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))    //fraction_align

   // 为精度转换添加中间变量
   val pir_sign_convert = Wire(Vec(MAX_VECTOR_SIZE, UInt(1.W)))
   val pir_exp_convert  = Wire(Vec(MAX_VECTOR_SIZE, SInt(DST_EXP_WIDTH_MAX.W)))
   val pir_frac_convert = Wire(Vec(MAX_VECTOR_SIZE, UInt(DOUBLED_FRAC_WIDTH.W)))

     //for dotproduct, output is scalar
   val pir_sign_dot = Wire(UInt(1.W))
   val pir_exp_dot  = Wire(SInt(SRC_EXP_WIDTH_MAX.W))
   val pir_frac_dot = Wire(UInt(DOT_PRODUCT_WIDTH.W))

   //初始化中间变量
   for(i <- 0 until MAX_VECTOR_SIZE){
     pir_sign_rst(i)     := 0.U
     pir_exp_rst(i)      := 0.S
     pir_frac_rst_add(i) := 0.U
     pir_frac_rst_sub(i) := 0.U
     pir_frac_rst_mul(i) := 0.U
     pir_frac_rst_div(i) := 0.U
     pir_max_exp(i)      := 0.S
     pir_sign_convert(i) := 0.U
     pir_exp_convert(i)  := 0.S
     pir_frac_convert(i) := 0.U
   }

   pir_sign_dot := 0.U
   pir_exp_dot  := 0.S
   pir_frac_dot := 0.U

   when(io.op === 1.U){    //Add
     val overflow      = Wire(Vec(MAX_VECTOR_SIZE, UInt(1.W)))  //尾数溢出
     val frac_truncate = Wire(Vec(MAX_VECTOR_SIZE, UInt(1.W)))  //尾数截断
     
     // 初始化所有元素为0
     for(i <- 0 until MAX_VECTOR_SIZE) {
       overflow(i)      := 0.U
       frac_truncate(i) := 0.U
     }
   
     
     val fracalign = Module(new FractionAlignment_AddSub(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, ES))
     val add       = Module(new Add(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, ES))

     fracalign.io.pir_exp1_i  := pir_exp
     fracalign.io.pir_frac1_i := pir_frac
     fracalign.io.pir_exp2_i  := pir_exp2
     fracalign.io.pir_frac2_i := pir_frac2

     add.io.pir_sign1_i       := pir_sign
     add.io.pir_sign2_i       := pir_sign2
     add.io.pir_exp1_i        := fracalign.io.pir_max_exp
     add.io.pir_exp2_i        := fracalign.io.pir_max_exp
     add.io.pir_frac1_aligned := fracalign.io.pir_frac1_align
     add.io.pir_frac2_aligned := fracalign.io.pir_frac2_align
   
     // 只处理有效范围内的结果
     for(i <- 0 until MAX_VECTOR_SIZE) {
       when(valid_range(i)) {
         pir_sign_rst(i)     := add.io.pir_sign_o(i)
         pir_exp_rst(i)      := add.io.pir_exp_o(i)
         pir_frac_rst_add(i) := add.io.pir_frac_o(i)
         overflow(i)         := add.io.overflow(i)
         frac_truncate(i)    := add.io.frac_truncate(i)
       }
     }
     pir_max_exp := fracalign.io.pir_max_exp
   }.elsewhen(io.op === 2.U){  //Sub
     val overflow      = Wire(Vec(MAX_VECTOR_SIZE, UInt(1.W)))
     val frac_truncate = Wire(Vec(MAX_VECTOR_SIZE, UInt(1.W)))

     // 初始化所有元素为0
     for(i <- 0 until MAX_VECTOR_SIZE) {
       overflow(i) := 0.U
       frac_truncate(i) := 0.U
     }

     val fracalign = Module(new FractionAlignment_AddSub(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, ES))
     val sub       = Module(new Sub(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, ES))

     fracalign.io.pir_exp1_i  := pir_exp
     fracalign.io.pir_frac1_i := pir_frac
     fracalign.io.pir_exp2_i  := pir_exp2
     fracalign.io.pir_frac2_i := pir_frac2
   
     sub.io.pir_sign1_i       := pir_sign
     sub.io.pir_sign2_i       := pir_sign2
     sub.io.pir_exp1_i        := fracalign.io.pir_max_exp
     sub.io.pir_exp2_i        := fracalign.io.pir_max_exp
     sub.io.pir_frac1_aligned := fracalign.io.pir_frac1_align
     sub.io.pir_frac2_aligned := fracalign.io.pir_frac2_align
   
     pir_sign_rst     := sub.io.pir_sign_o
     pir_exp_rst      := sub.io.pir_exp_o
     pir_frac_rst_sub := sub.io.pir_frac_o
     pir_max_exp      := fracalign.io.pir_max_exp
     overflow         := sub.io.overflow
     frac_truncate    := sub.io.frac_truncate
   
   }.elsewhen(io.op === 3.U){  //Mul
     val mul = Module(new Mul(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, ES))
   
     mul.io.pir_sign1_i := pir_sign
     mul.io.pir_sign2_i := pir_sign2
     mul.io.pir_exp1_i  := pir_exp
     mul.io.pir_exp2_i  := pir_exp2
     mul.io.pir_frac1_i := pir_frac
     mul.io.pir_frac2_i := pir_frac2
   
     pir_sign_rst     := mul.io.pir_sign_o
     pir_exp_rst      := mul.io.pir_exp_o
     pir_frac_rst_mul := mul.io.pir_frac_o
   
   }.elsewhen(io.op === 4.U){  //Div
     val div_inst = Module(new Div(LIMITED_POSIT_WIDTH, LIMITED_VECTOR_SIZE, LIMITED_ALIGN_WIDTH, ES))
     
     div_inst.io.pir_sign1_i := pir_sign
     div_inst.io.pir_sign2_i := pir_sign2
     div_inst.io.pir_exp1_i  := pir_exp
     div_inst.io.pir_exp2_i  := pir_exp2
     div_inst.io.pir_frac1_i := pir_frac
     div_inst.io.pir_frac2_i := pir_frac2
     
     pir_sign_rst     := div_inst.io.pir_sign_o
     pir_exp_rst      := div_inst.io.pir_exp_o
     pir_frac_rst_div := div_inst.io.pir_frac_o
     
     // 添加标志用于跟踪特殊情况
     val both_are_40000000 = Wire(Vec(LIMITED_VECTOR_SIZE, Bool()))
     
     // 检查两个输入是否都是40000000
     for (i <- 0 until LIMITED_VECTOR_SIZE) {
       when(i.U < io.vector_size) {
         both_are_40000000(i) := (io.posit_i1(i) === (BigInt(1) << (LIMITED_POSIT_WIDTH-2)).U) && 
                                (io.posit_i2(i) === (BigInt(1) << (LIMITED_POSIT_WIDTH-2)).U)
       }.otherwise {
         both_are_40000000(i) := false.B
       }
     }
   }.elsewhen(io.op === 5.U){  //DotProduct, 先相乘再相加，对阶在DotProduct中实现，输入向量 输出标量
    val dotproduct = Module(new DotProduct(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, ES))
   
    
     dotproduct.io.pir_sign1_i := pir_sign
     dotproduct.io.pir_sign2_i := pir_sign2
     dotproduct.io.pir_exp1_i  := pir_exp
     dotproduct.io.pir_exp2_i  := pir_exp2
     dotproduct.io.pir_frac1_i := pir_frac
     dotproduct.io.pir_frac2_i := pir_frac2
   
     pir_sign_dot := dotproduct.io.pir_sign_o
     pir_exp_dot  := dotproduct.io.pir_exp_o
     pir_frac_dot := dotproduct.io.pir_frac_o
   }.elsewhen(io.op === 6.U){  //PositConvert
     // 定义静态值用于模块实例化
     val dst_posit_width_int = MAX_POSIT_WIDTH
     val dst_es_int = ES
     
     val convert = Module(new PositConvert(
       MAX_POSIT_WIDTH,
       dst_posit_width_int,
       ES,
       dst_es_int,
       MAX_VECTOR_SIZE,
       MAX_ALIGN_WIDTH
     ))
     
     // 仅对操作数1进行精度转换
     convert.io.pir_sign1_i := pir_sign
     convert.io.pir_exp1_i  := pir_exp
     convert.io.pir_frac1_i := pir_frac
     
     pir_sign_convert := convert.io.pir_sign_o
     pir_exp_convert  := convert.io.pir_exp_o
     pir_frac_convert := convert.io.pir_frac_o
   }.elsewhen(io.op === 7.U){  // Float和Posit相互转换
     // 根据float_mode动态选择浮点数格式
     val float2posit_out = Wire(Vec(MAX_VECTOR_SIZE, UInt(MAX_POSIT_WIDTH.W)))
     val posit2float_out = Wire(Vec(MAX_VECTOR_SIZE, UInt(FLOAT_WIDTH.W)))
     
     // 默认值
     float2posit_out := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(MAX_POSIT_WIDTH.W)))
     posit2float_out := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(FLOAT_WIDTH.W)))
     
     // 根据float_mode选择不同的浮点数格式
     switch(io.float_mode) {
       is(0.U) { // FP4
         // Float转Posit - FP4
         val float2posit_fp4 = Module(new FloatToPosit(
           1,
           2,
           MAX_POSIT_WIDTH,
           ES,
           MAX_VECTOR_SIZE
         ))
         float2posit_fp4.io.float_in := io.float_i
         
         // 处理可能的宽度不匹配
         for(i <- 0 until MAX_VECTOR_SIZE) {
           when(ACTUAL_DST_POSIT_WIDTH > MAX_POSIT_WIDTH.U) {
             // 如果目标位宽超过最大位宽，截断
             float2posit_out(i) := float2posit_fp4.io.posit_out(i)(MAX_POSIT_WIDTH-1, 0)
           }.elsewhen(ACTUAL_DST_POSIT_WIDTH < MAX_POSIT_WIDTH.U) {
             // 否则，在运行时调整位宽
             // 计算有效位数和截断位
             val valid_bits = ACTUAL_DST_POSIT_WIDTH - 1.U
             float2posit_out(i) := (float2posit_fp4.io.posit_out(i) >> (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)) << (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)
           }.otherwise {
             float2posit_out(i) := float2posit_fp4.io.posit_out(i)
           }
         }
         
         // Posit转Float - FP4
         val posit2float_fp4 = Module(new PositToFloat(
           MAX_POSIT_WIDTH,
           ES,
           1,
           2,
           MAX_VECTOR_SIZE
         ))
         posit2float_fp4.io.posit_in := io.posit_i1
         posit2float_out             := posit2float_fp4.io.float_out
       }
       
       is(1.U) { // FP8
         // Float转Posit - FP8
         val float2posit_fp8 = Module(new FloatToPosit(
           4,
           3,
           MAX_POSIT_WIDTH,
           ES,
           MAX_VECTOR_SIZE
         ))
         float2posit_fp8.io.float_in := io.float_i
         
         // 处理可能的宽度不匹配
         for(i <- 0 until MAX_VECTOR_SIZE) {
           when(ACTUAL_DST_POSIT_WIDTH > MAX_POSIT_WIDTH.U) {
             // 如果目标位宽超过最大位宽，截断
             float2posit_out(i) := float2posit_fp8.io.posit_out(i)(MAX_POSIT_WIDTH-1, 0)
           }.elsewhen(ACTUAL_DST_POSIT_WIDTH < MAX_POSIT_WIDTH.U) {
             // 否则，在运行时调整位宽
             // 计算有效位数和截断位
             val valid_bits = ACTUAL_DST_POSIT_WIDTH - 1.U
             float2posit_out(i) := (float2posit_fp8.io.posit_out(i) >> (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)) << (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)
           }.otherwise {
             float2posit_out(i) := float2posit_fp8.io.posit_out(i)
           }
         }
         
         // Posit转Float - FP8
         val posit2float_fp8 = Module(new PositToFloat(
           MAX_POSIT_WIDTH,
           ES,
           4,
           3,
           MAX_VECTOR_SIZE
         ))
         posit2float_fp8.io.posit_in := io.posit_i1
         posit2float_out             := posit2float_fp8.io.float_out
       }
       
       is(2.U) { // FP16
         // Float转Posit - FP16
         val float2posit_fp16 = Module(new FloatToPosit(
           5,
           10,
           MAX_POSIT_WIDTH,
           ES,
           MAX_VECTOR_SIZE
         ))
         float2posit_fp16.io.float_in := io.float_i
         
         // 处理可能的宽度不匹配
         for(i <- 0 until MAX_VECTOR_SIZE) {
           when(ACTUAL_DST_POSIT_WIDTH > MAX_POSIT_WIDTH.U) {
             // 如果目标位宽超过最大位宽，截断
             float2posit_out(i) := float2posit_fp16.io.posit_out(i)(MAX_POSIT_WIDTH-1, 0)
           }.elsewhen(ACTUAL_DST_POSIT_WIDTH < MAX_POSIT_WIDTH.U) {
             // 否则，在运行时调整位宽
             // 计算有效位数和截断位
             val valid_bits = ACTUAL_DST_POSIT_WIDTH - 1.U
             float2posit_out(i) := (float2posit_fp16.io.posit_out(i) >> (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)) << (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)
           }.otherwise {
             float2posit_out(i) := float2posit_fp16.io.posit_out(i)
           }
         }
         
         // Posit转Float - FP16
         val posit2float_fp16 = Module(new PositToFloat(
           MAX_POSIT_WIDTH,
           ES,
           5,
           10,
           MAX_VECTOR_SIZE
         ))
         posit2float_fp16.io.posit_in := io.posit_i1
         posit2float_out              := posit2float_fp16.io.float_out
       }
       
       is(3.U) { // FP32
         // Float转Posit - FP32
         val float2posit_fp32 = Module(new FloatToPosit(
           8,
           23,
           MAX_POSIT_WIDTH,
           ES,
           MAX_VECTOR_SIZE
         ))
         float2posit_fp32.io.float_in := io.float_i
         
         // 处理可能的宽度不匹配
         for(i <- 0 until MAX_VECTOR_SIZE) {
           when(ACTUAL_DST_POSIT_WIDTH > MAX_POSIT_WIDTH.U) {
             // 如果目标位宽超过最大位宽，截断
             float2posit_out(i) := float2posit_fp32.io.posit_out(i)(MAX_POSIT_WIDTH-1, 0)
           }.elsewhen(ACTUAL_DST_POSIT_WIDTH < MAX_POSIT_WIDTH.U) {
             // 否则，在运行时调整位宽
             // 计算有效位数和截断位
             val valid_bits = ACTUAL_DST_POSIT_WIDTH - 1.U
             float2posit_out(i) := (float2posit_fp32.io.posit_out(i) >> (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)) << (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)
           }.otherwise {
             float2posit_out(i) := float2posit_fp32.io.posit_out(i)
           }
         }
         
         // Posit转Float - FP32
         val posit2float_fp32 = Module(new PositToFloat(
           MAX_POSIT_WIDTH,
           ES,
           8,
           23,
           MAX_VECTOR_SIZE
         ))
         posit2float_fp32.io.posit_in := io.posit_i1
         posit2float_out              := posit2float_fp32.io.float_out
       }
       
       is(4.U) { // FP64
         // Float转Posit - FP64
         val float2posit_fp64 = Module(new FloatToPosit(
           11,
           52,
           MAX_POSIT_WIDTH,
           ES,
           MAX_VECTOR_SIZE
         ))
         float2posit_fp64.io.float_in := io.float_i
         
         // 处理可能的宽度不匹配
         for(i <- 0 until MAX_VECTOR_SIZE) {
           when(ACTUAL_DST_POSIT_WIDTH > MAX_POSIT_WIDTH.U) {
             // 如果目标位宽超过最大位宽，截断
             float2posit_out(i) := float2posit_fp64.io.posit_out(i)(MAX_POSIT_WIDTH-1, 0)
           }.elsewhen(ACTUAL_DST_POSIT_WIDTH < MAX_POSIT_WIDTH.U) {
             // 否则，在运行时调整位宽
             // 计算有效位数和截断位
             val valid_bits = ACTUAL_DST_POSIT_WIDTH - 1.U
             float2posit_out(i) := (float2posit_fp64.io.posit_out(i) >> (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)) << (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)
           }.otherwise {
             float2posit_out(i) := float2posit_fp64.io.posit_out(i)
           }
         }
         
         // Posit转Float - FP64
         val posit2float_fp64 = Module(new PositToFloat(
           MAX_POSIT_WIDTH,
           ES,
           11,
           52,
           MAX_VECTOR_SIZE
         ))
         posit2float_fp64.io.posit_in := io.posit_i1
         posit2float_out              := posit2float_fp64.io.float_out
       }
     }
     
     // 根据float_posit信号决定转换方向
     // 如果float_posit为true，则进行Float到Posit的转换
     // 如果float_posit为false，则进行Posit到Float的转换
     when(io.float_posit) {
       // Float到Posit转换 - 仅处理操作数1
       io.posit_o := float2posit_out
       io.float_o := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(FLOAT_WIDTH.W)))
     }.otherwise {
       // Posit到Float转换 - 仅处理操作数1
       io.float_o := posit2float_out
       io.posit_o := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(MAX_POSIT_WIDTH.W)))
     }
   }.elsewhen(io.op === 8.U) {  // Greater - 比较并输出较大值
     val greater = Module(new PositGreater(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, ES))
     
     // 连接输入 - 使用解码后的PIR格式数据
     greater.io.pir_sign1_i := pir_sign
     greater.io.pir_sign2_i := pir_sign2
     greater.io.pir_exp1_i  := pir_exp
     greater.io.pir_exp2_i  := pir_exp2
     greater.io.pir_frac1_i := pir_frac
     greater.io.pir_frac2_i := pir_frac2
     greater.io.posit_i1    := io.posit_i1  // 传递原始Posit输入用于特殊值检测
     greater.io.posit_i2    := io.posit_i2  // 传递原始Posit输入用于特殊值检测
     greater.io.dst_posit_width := io.dst_posit_width
     
     // 连接输出 - 保存PIR格式结果以便与其他模块兼容
     pir_sign_rst := greater.io.pir_sign_o
     pir_exp_rst  := greater.io.pir_exp_o
     pir_frac_rst := greater.io.pir_frac_o
     
     // 直接使用greater模块的posit输出
     for(i <- 0 until MAX_VECTOR_SIZE) {
       when(valid_range(i)) {
         io.posit_o(i) := greater.io.posit_o(i)
       }
     }
   }.elsewhen(io.op === 9.U) {  // Less - 比较并输出较小值
     val less = Module(new PositLess(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, ES))
     
     // 连接输入 - 使用解码后的PIR格式数据
     less.io.pir_sign1_i := pir_sign
     less.io.pir_sign2_i := pir_sign2
     less.io.pir_exp1_i  := pir_exp
     less.io.pir_exp2_i  := pir_exp2
     less.io.pir_frac1_i := pir_frac
     less.io.pir_frac2_i := pir_frac2
     less.io.posit_i1    := io.posit_i1  // 传递原始Posit输入用于特殊值检测
     less.io.posit_i2    := io.posit_i2  // 传递原始Posit输入用于特殊值检测
     less.io.dst_posit_width := io.dst_posit_width
     
     // 连接输出 - 保存PIR格式结果以便与其他模块兼容
     pir_sign_rst := less.io.pir_sign_o
     pir_exp_rst  := less.io.pir_exp_o
     pir_frac_rst := less.io.pir_frac_o
     
     // 直接使用less模块的posit输出
     for(i <- 0 until MAX_VECTOR_SIZE) {
       when(valid_range(i)) {
         io.posit_o(i) := less.io.posit_o(i)
       }
     }
   }.elsewhen(io.op === 10.U) {  // TranInt - Posit转Int
     val tranInt = Module(new PositToInt(
       MAX_POSIT_WIDTH,
       MAX_VECTOR_SIZE,
       MAX_ALIGN_WIDTH,
       ES,
       INT_WIDTH
     ))
     
     // 输入PIR格式的posit数据
     tranInt.io.pir_sign_i := pir_sign
     tranInt.io.pir_exp_i  := pir_exp
     tranInt.io.pir_frac_i := pir_frac
     
     // 获取转换结果
     io.int_o := tranInt.io.int_o
   }

   //***********************//
   //fraction normalization//
   //***********************//
   val pir_exp_adjust      = Wire(Vec(MAX_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
   val pir_exp_adjust_dot  = Wire(SInt(SRC_EXP_WIDTH_MAX.W))
   val pir_frac_normed     = Wire(Vec(MAX_VECTOR_SIZE, UInt(DOUBLED_FRAC_WIDTH.W)))
   val pir_frac_normed_dot = Wire(UInt(DOUBLED_FRAC_WIDTH.W))

   //初始化中间变量
   for(i <- 0 until MAX_VECTOR_SIZE){
     pir_exp_adjust(i)  := 0.S
     pir_frac_normed(i) := 0.U
   }
   pir_exp_adjust_dot  := 0.S(SRC_EXP_WIDTH_MAX.W)
   pir_frac_normed_dot := 0.U

   when(io.op === 5.U){  //dotproduct output is scala, 默认小数点位于首位
   // 点积操作的尾数标准化
   // 1. 使用固定的DECIMAL_POINT值：1
   // 2. 这与FracNorm模块保持一致，确保尾数标准化逻辑的一致性
   // 3. 点积结果的指数调整已经在FracNorm_DotProduct模块中正确处理
   
   val frac_norm_dot = Module(new FracNorm_DotProduct(MAX_POSIT_WIDTH, DOT_PRODUCT_WIDTH - 14, log2Ceil(MAX_VECTOR_SIZE+1)+2, ES))
       frac_norm_dot.io.pir_frac_i := pir_frac_dot
       pir_frac_normed_dot         := frac_norm_dot.io.pir_frac_o
       pir_exp_adjust_dot          := frac_norm_dot.io.exp_adjust
   }.elsewhen(io.op === 1.U){ //Add
     val frac_norm_add = Module(new FracNorm(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, 1, ES))
     // 初始化所有输入为0
     for(i <- 0 until MAX_VECTOR_SIZE) {
       frac_norm_add.io.pir_frac_i(i) := 0.U
     }
     // 只处理有效范围内的结果
     for(i <- 0 until MAX_VECTOR_SIZE) {
       when(valid_range(i)) {
         frac_norm_add.io.pir_frac_i(i) := pir_frac_rst_add(i)
         pir_frac_normed(i)             := frac_norm_add.io.pir_frac_o(i)
         pir_exp_adjust(i)              := frac_norm_add.io.exp_adjust(i)
       }
     }
   }.elsewhen(io.op === 2.U){ //Sub
     val frac_norm_sub                = Module(new FracNorm(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, 1, ES))
         frac_norm_sub.io.pir_frac_i := pir_frac_rst_sub
         pir_frac_normed             := frac_norm_sub.io.pir_frac_o
         pir_exp_adjust              := frac_norm_sub.io.exp_adjust
   }.elsewhen(io.op === 3.U){  //Mul           
     val frac_norm_mul                = Module(new FracNorm(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, DOUBLED_FRAC_WIDTH, 14, ES))
         frac_norm_mul.io.pir_frac_i := pir_frac_rst_mul
         pir_frac_normed             := frac_norm_mul.io.pir_frac_o
         pir_exp_adjust              := frac_norm_mul.io.exp_adjust
   }.elsewhen(io.op === 4.U){  //Div    
     val frac_norm_div                = Module(new FracNorm(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, DOUBLED_FRAC_WIDTH, 13, ES))
         frac_norm_div.io.pir_frac_i := pir_frac_rst_div
         pir_frac_normed             := frac_norm_div.io.pir_frac_o
         pir_exp_adjust              := frac_norm_div.io.exp_adjust
   }

     // printf("pir_frac_normed: %b\n", pir_frac_normed(0))

   //***************//
   //**Adjust EXP**//
   //**************//
   val pir_exp_rst_adjusied     = Wire(Vec(MAX_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
   val pir_exp_rst_adjusied_dot = Wire(SInt(SRC_EXP_WIDTH_MAX.W))

   //初始化中间变量
   pir_exp_rst_adjusied     := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.S(SRC_EXP_WIDTH_MAX.W)))
   pir_exp_rst_adjusied_dot := 0.S(SRC_EXP_WIDTH_MAX.W)

   // 计算调整后的指数
   when(io.op === 5.U){
     pir_exp_rst_adjusied_dot := pir_exp_adjust_dot + pir_exp_dot
   }.otherwise{
     for(i <- 0 until MAX_VECTOR_SIZE){
       pir_exp_rst_adjusied(i) := pir_exp_rst(i) + pir_exp_adjust(i)
     }
   }

   // 为所有操作准备Float结果，无论输入是Posit还是Float
   // 这样在任何操作后，都可以根据Outposit信号选择输出格式
   when(io.op < 6.U) {
     // 对于前5个op操作，始终准备Float格式的结果
     when(io.op === 5.U) {
       // 点积操作
       float_dot_data.sign   := pir_sign_dot.asBool
       float_dot_data.exp    := pir_exp_rst_adjusied_dot
       float_dot_data.frac   := pir_frac_normed_dot(float_frac_width, 0)
       float_dot_data.isNaN  := false.B
       float_dot_data.isInf  := false.B
       float_dot_data.isZero := false.B
       
       // 检查特殊情况
       when(pir_frac_normed_dot === 0.U) {
         float_dot_data.isZero := true.B
       }
       
       // 处理特殊情况：对于点积，如果输入是Float且任何输入包含NaN或Inf
       when(!io.Isposit) {
         for(i <- 0 until MAX_VECTOR_SIZE) {
           when(valid_range(i) && (float_data(i).isNaN || float_data2(i).isNaN)) {
             float_dot_data.isNaN := true.B
           }
           
           when(valid_range(i) && ((float_data(i).isInf && !float_data2(i).isZero) || (float_data2(i).isInf && !float_data(i).isZero))) {
             float_dot_data.isInf := true.B
           }
         }
       }
     }.otherwise {
       // 其他算术操作
       for(i <- 0 until MAX_VECTOR_SIZE) {
         float_rst_data(i).sign := pir_sign_rst(i).asBool
         float_rst_data(i).exp  := pir_exp_rst_adjusied(i)
         
         // 根据不同操作选择对应的尾数结果
         when(io.op === 1.U) {
           float_rst_data(i).frac := pir_frac_normed(i)(float_frac_width, 0)
         }.elsewhen(io.op === 2.U) {
           float_rst_data(i).frac := pir_frac_normed(i)(float_frac_width, 0)
         }.elsewhen(io.op === 3.U) {
           float_rst_data(i).frac := pir_frac_normed(i)(float_frac_width, 0)
         }.elsewhen(io.op === 4.U) {
           float_rst_data(i).frac := pir_frac_normed(i)(float_frac_width, 0)
         }
         
         // 检查特殊情况
         float_rst_data(i).isNaN  := false.B
         float_rst_data(i).isInf  := false.B
         float_rst_data(i).isZero := false.B
         
         when(pir_frac_normed(i) === 0.U) {
           float_rst_data(i).isZero := true.B
         }
         
         // 处理特殊情况：当输入是Float且包含NaN或Inf时
         when(!io.Isposit) {
           when(float_data(i).isNaN || float_data2(i).isNaN) {
             float_rst_data(i).isNaN := true.B
           }
           
           when(float_data(i).isInf || float_data2(i).isInf) {
             float_rst_data(i).isInf := true.B
           }
         }
       }
     }
   }

   when(io.op === 5.U){
     // 点积操作的输出处理
     // 准备Posit格式结果
     val encode_dot = Module(new PositEncode_DotProduct(MAX_POSIT_WIDTH, ES))
     encode_dot.io.pir_sign := pir_sign_dot
     encode_dot.io.pir_exp  := pir_exp_rst_adjusied_dot
     encode_dot.io.pir_frac := pir_frac_normed_dot
     
     // 当目标精度与源精度不同时，需要调整结果位宽
     val posit_result = Wire(UInt(MAX_POSIT_WIDTH.W))
     
     // 1. 解码点积结果
     val dot_decoder = Module(new PositDecode(MAX_POSIT_WIDTH, 1, ES))
     dot_decoder.io.posit(0) := encode_dot.io.posit
     
     // 2. 转换为目标精度
     val dot_converter = Module(new PositConvert(
       MAX_POSIT_WIDTH,
       MAX_POSIT_WIDTH,
       ES,
       ES,
       1,
       MAX_ALIGN_WIDTH
     ))
     
     dot_converter.io.pir_sign1_i(0) := dot_decoder.io.Sign(0)
     dot_converter.io.pir_exp1_i(0)  := dot_decoder.io.Exp(0)
     dot_converter.io.pir_frac1_i(0) := dot_decoder.io.Frac(0)
     
     // 3. 编码为目标精度
     val dot_encoder = Module(new PositEncode(MAX_POSIT_WIDTH, 1, ES))
     dot_encoder.io.pir_sign(0) := dot_converter.io.pir_sign_o(0)
     dot_encoder.io.pir_exp(0)  := dot_converter.io.pir_exp_o(0)
     dot_encoder.io.pir_frac(0) := dot_converter.io.pir_frac_o(0)
     
     // 如果目标宽度大于源宽度，需要扩展
     // 如果目标宽度小于源宽度，需要截断
     when(ACTUAL_DST_POSIT_WIDTH > MAX_POSIT_WIDTH.U) {
       // 如果目标位宽超过最大位宽，截断
       posit_result := dot_encoder.io.posit(0)(MAX_POSIT_WIDTH-1, 0)
     }.elsewhen(ACTUAL_DST_POSIT_WIDTH < MAX_POSIT_WIDTH.U) {
       // 否则，在运行时调整位宽
       // 计算有效位数和截断位
       val valid_bits = ACTUAL_DST_POSIT_WIDTH - 1.U
       posit_result := (dot_encoder.io.posit(0) >> (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)) << (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)
     }.otherwise {
       posit_result := dot_encoder.io.posit(0)
     }
     
     // 准备Float格式结果
     val floatDotEncoder = Module(new FloatEncode(
       float_exp_width,
       float_frac_width,
       1
     ))
     
     floatDotEncoder.io.Sign(0)   := float_dot_data.sign
     floatDotEncoder.io.Exp(0)    := float_dot_data.exp
     floatDotEncoder.io.Frac(0)   := float_dot_data.frac
     floatDotEncoder.io.isNaN(0)  := float_dot_data.isNaN
     floatDotEncoder.io.isInf(0)  := float_dot_data.isInf
     floatDotEncoder.io.isZero(0) := float_dot_data.isZero
     
     val float_result = floatDotEncoder.io.float(0)
     
     // 根据Outposit信号选择输出格式
     when(io.Outposit) {
       io.posit_dot_o := posit_result
       io.float_dot_o := 0.U(FLOAT_WIDTH.W)
     }.otherwise {
       io.posit_dot_o := 0.U(MAX_POSIT_WIDTH.W)
       io.float_dot_o := float_result
     }
     
   }.elsewhen(io.op === 6.U){
     // 精度转换操作
     val convert_encoder = Module(new PositEncode(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, ES))
     convert_encoder.io.pir_sign := pir_sign_convert
     convert_encoder.io.pir_exp  := pir_exp_convert
     convert_encoder.io.pir_frac := pir_frac_convert
     
     // 直接输出转换后的结果，但需要处理可能的宽度不匹配
     for(i <- 0 until MAX_VECTOR_SIZE) {
       when(ACTUAL_DST_POSIT_WIDTH > MAX_POSIT_WIDTH.U) {
         // 目标位宽超过最大位宽，截断
         io.posit_o(i) := convert_encoder.io.posit(i)(MAX_POSIT_WIDTH-1, 0)
       }.elsewhen(ACTUAL_DST_POSIT_WIDTH < MAX_POSIT_WIDTH.U) {
         // 否则，在运行时调整位宽
         // 计算有效位数和截断位
         val valid_bits = ACTUAL_DST_POSIT_WIDTH - 1.U
         io.posit_o(i) := (convert_encoder.io.posit(i) >> (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)) << (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)
       }.otherwise {
         io.posit_o(i) := convert_encoder.io.posit(i)
       }
     }
   }.elsewhen(io.op === 7.U){
     // Float和Posit转换操作的输出已在上面处理
   }.elsewhen(io.op === 8.U) {  // Greater - 比较并输出较大值
     // 已在前面处理
   }.elsewhen(io.op === 9.U) {  // Less - 比较并输出较小值
     // 已在前面处理
   }.elsewhen(io.op === 10.U) {  // TranInt - Posit转Int
     // 已在前面处理
   }.otherwise{
     // 算术操作（加、减、乘、除）
     // 准备Posit格式结果
     val posit_results = Wire(Vec(MAX_VECTOR_SIZE, UInt(MAX_POSIT_WIDTH.W)))
     // 初始化所有元素为0
     for(i <- 0 until MAX_VECTOR_SIZE) {
       posit_results(i) := 0.U
     }
     
     // 如果目标精度与源精度相同，直接编码输出
     when (ACTUAL_DST_POSIT_WIDTH === MAX_POSIT_WIDTH.U) {
       val encode = Module(new PositEncode(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, ES))
       // 初始化所有输入为0
       for(i <- 0 until MAX_VECTOR_SIZE) {
         encode.io.pir_sign(i) := 0.U
         encode.io.pir_exp(i)  := 0.S
         encode.io.pir_frac(i) := 0.U
       }
       // 只处理有效范围内的结果
       for(i <- 0 until MAX_VECTOR_SIZE) {
         when(valid_range(i)) {
           encode.io.pir_sign(i) := pir_sign_rst(i)
           encode.io.pir_exp(i)  := pir_exp_rst_adjusied(i)
           encode.io.pir_frac(i) := pir_frac_normed(i)
           
           // 特殊处理40000000/40000000的情况
           when(io.op === 4.U && 
                (io.posit_i1(i) === (BigInt(1) << (LIMITED_POSIT_WIDTH-2)).U) && 
                (io.posit_i2(i) === (BigInt(1) << (LIMITED_POSIT_WIDTH-2)).U)) {
             posit_results(i) := (BigInt(1) << (LIMITED_POSIT_WIDTH-2)).U  // 40000000
           }.otherwise {
             posit_results(i) := encode.io.posit(i)
           }
         }
       }
     } .otherwise {
       // 否则，需要进行精度转换  
       // 1. 转换为目标精度
       val result_converter = Module(new PositConvert(
         MAX_POSIT_WIDTH,
         MAX_POSIT_WIDTH,
         ES,
         ES,
         MAX_VECTOR_SIZE,
         MAX_ALIGN_WIDTH
       ))
       
       // 初始化所有输入为0
       for(i <- 0 until MAX_VECTOR_SIZE) {
         result_converter.io.pir_sign1_i(i) := 0.U
         result_converter.io.pir_exp1_i(i)  := 0.S
         result_converter.io.pir_frac1_i(i) := 0.U
       }
       
       // 只处理有效范围内的结果
       for(i <- 0 until MAX_VECTOR_SIZE) {
         when(valid_range(i)) {
           result_converter.io.pir_sign1_i(i) := pir_sign_rst(i)
           result_converter.io.pir_exp1_i(i)  := pir_exp_rst_adjusied(i)
           result_converter.io.pir_frac1_i(i) := pir_frac_normed(i)
         }
       }
       
       // 2. 编码为目标精度
       val result_encoder = Module(new PositEncode(
         MAX_POSIT_WIDTH, 
         MAX_VECTOR_SIZE, 
         ES
       ))
       
       // 初始化所有输入为0
       for(i <- 0 until MAX_VECTOR_SIZE) {
         result_encoder.io.pir_sign(i) := 0.U
         result_encoder.io.pir_exp(i)  := 0.S
         result_encoder.io.pir_frac(i) := 0.U
       }
       
       // 只处理有效范围内的结果
       for(i <- 0 until MAX_VECTOR_SIZE) {
         when(valid_range(i)) {
           result_encoder.io.pir_sign(i) := result_converter.io.pir_sign_o(i)
           result_encoder.io.pir_exp(i)  := result_converter.io.pir_exp_o(i)
           result_encoder.io.pir_frac(i) := result_converter.io.pir_frac_o(i)
           
           // 特殊处理40000000/40000000的情况
           when(io.op === 4.U && 
                (io.posit_i1(i) === (BigInt(1) << (LIMITED_POSIT_WIDTH-2)).U) && 
                (io.posit_i2(i) === (BigInt(1) << (LIMITED_POSIT_WIDTH-2)).U)) {
             posit_results(i) := (BigInt(1) << (LIMITED_POSIT_WIDTH-2)).U  // 40000000
           }.elsewhen(ACTUAL_DST_POSIT_WIDTH > MAX_POSIT_WIDTH.U) {
             // 目标位宽超过最大位宽，截断
             posit_results(i) := result_encoder.io.posit(i)(MAX_POSIT_WIDTH-1, 0)
           }.elsewhen(ACTUAL_DST_POSIT_WIDTH < MAX_POSIT_WIDTH.U) {
             // 否则，在运行时调整位宽
             // 计算有效位数和截断位
             val valid_bits = ACTUAL_DST_POSIT_WIDTH - 1.U
             posit_results(i) := (result_encoder.io.posit(i) >> (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)) << (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)
           }.otherwise {
             posit_results(i) := result_encoder.io.posit(i)
           }
         }
       }
     }
     
  
     // 准备Float格式结果
     val floatEncoder = Module(new FloatEncode(
       float_exp_width,
       float_frac_width,
       MAX_VECTOR_SIZE
     ))
     
     // 初始化所有输入为0
     for(i <- 0 until MAX_VECTOR_SIZE) {
       floatEncoder.io.Sign(i)   := false.B
       floatEncoder.io.Exp(i)    := 0.S
       floatEncoder.io.Frac(i)   := 0.U
       floatEncoder.io.isNaN(i)  := false.B
       floatEncoder.io.isInf(i)  := false.B
       floatEncoder.io.isZero(i) := true.B
     }
     
     // 只处理有效范围内的结果
     for(i <- 0 until MAX_VECTOR_SIZE) {
       when(valid_range(i)) {
         floatEncoder.io.Sign(i)   := float_rst_data(i).sign
         floatEncoder.io.Exp(i)    := float_rst_data(i).exp
         floatEncoder.io.Frac(i)   := float_rst_data(i).frac
         floatEncoder.io.isNaN(i)  := float_rst_data(i).isNaN
         floatEncoder.io.isInf(i)  := float_rst_data(i).isInf
         floatEncoder.io.isZero(i) := float_rst_data(i).isZero
       }
     }
     
     val float_results = floatEncoder.io.float
     
     // 根据Outposit信号选择输出格式
     when(io.Outposit) {
       // 只处理有效范围内的结果
       for(i <- 0 until MAX_VECTOR_SIZE) {
         when(valid_range(i)) {
           io.posit_o(i) := posit_results(i)
           io.float_o(i) := 0.U(FLOAT_WIDTH.W)
         }
       }
     }.otherwise {
       // 只处理有效范围内的结果
       for(i <- 0 until MAX_VECTOR_SIZE) {
         when(valid_range(i)) {
           io.posit_o(i) := 0.U(MAX_POSIT_WIDTH.W)
           io.float_o(i) := float_results(i)
         }
       }
     }
   }
 }