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
    10 --> Posit转Int（TranInt），将原始Posit32转为整数

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

 class PvuRequest(
   maxPositWidth: Int,
   maxVectorSize: Int,
   floatWidth: Int
 ) extends Bundle {
   val tag = UInt(32.W)
   val posit_i1 = Vec(maxVectorSize, UInt(maxPositWidth.W))
   val posit_i2 = Vec(maxVectorSize, UInt(maxPositWidth.W))
   val op = UInt(4.W)
   val Isposit = Bool()
   val Outposit = Bool()
   val float_i = Vec(maxVectorSize, UInt(floatWidth.W))
   val float_i2 = Vec(maxVectorSize, UInt(floatWidth.W))
   val float_mode = UInt(3.W)
   val float_posit = Bool()
   val src_posit_width = UInt(6.W)
   val vector_size = UInt(3.W)
   val dst_posit_width = UInt(6.W)
 }

 class PvuResponse(
   maxPositWidth: Int,
   maxVectorSize: Int,
   floatWidth: Int,
   intWidth: Int
 ) extends Bundle {
   val tag = UInt(32.W)
   val op = UInt(4.W)
   val float_o = Vec(maxVectorSize, UInt(floatWidth.W))
   val float_dot_o = UInt(floatWidth.W)
   val posit_o = Vec(maxVectorSize, UInt(maxPositWidth.W))
   val posit_dot_o = UInt(maxPositWidth.W)
   val int_o = Vec(maxVectorSize, SInt(intWidth.W))
 }


/** Core-stage results are intentionally tag/op-free.  Only the response
  * arbiter creates a PvuResponse, so arithmetic data cannot bypass the typed
  * pipeline payloads as a prebuilt final response.
  */
class PvuCoreResults(
  maxPositWidth: Int,
  maxVectorSize: Int,
  floatWidth: Int,
  intWidth: Int
) extends Bundle {
  val float_o = Vec(maxVectorSize, UInt(floatWidth.W))
  val float_dot_o = UInt(floatWidth.W)
  val posit_o = Vec(maxVectorSize, UInt(maxPositWidth.W))
  val posit_dot_o = UInt(maxPositWidth.W)
  val int_o = Vec(maxVectorSize, SInt(intWidth.W))
}

class PvuCorePayload(
  maxPositWidth: Int,
  maxVectorSize: Int,
  floatWidth: Int,
  intWidth: Int,
  expWidth: Int,
  coreFracWidth: Int,
  productWidth: Int
) extends Bundle {
  val request = new PvuRequest(maxPositWidth, maxVectorSize, floatWidth)
  val sign = Vec(maxVectorSize, UInt(1.W))
  val exp = Vec(maxVectorSize, SInt(expWidth.W))
  val frac = Vec(maxVectorSize, UInt(coreFracWidth.W))
  val productSign = Vec(maxVectorSize, UInt(1.W))
  val productExp = Vec(maxVectorSize, SInt(expWidth.W))
  val productFrac = Vec(maxVectorSize, UInt(productWidth.W))
  // These are results of genuinely core-complete operations (conversion,
  // compare and integer conversion) and exact raw-P32 arithmetic packers.
  // They are not a prebuilt protocol response: tag/op and routing remain in
  // the request, while normalize/encode still consume the PIR fields above.
  val corePosit = Vec(maxVectorSize, UInt(maxPositWidth.W))
  val corePositDot = UInt(maxPositWidth.W)
  val coreFloat = Vec(maxVectorSize, UInt(floatWidth.W))
  val coreFloatDot = UInt(floatWidth.W)
  val coreInt = Vec(maxVectorSize, SInt(intWidth.W))
}

class PvuReducedPayload(
  maxPositWidth: Int,
  maxVectorSize: Int,
  floatWidth: Int,
  intWidth: Int,
  expWidth: Int,
  coreFracWidth: Int,
  dotWidth: Int
) extends Bundle {
  val request = new PvuRequest(maxPositWidth, maxVectorSize, floatWidth)
  val sign = Vec(maxVectorSize, UInt(1.W))
  val exp = Vec(maxVectorSize, SInt(expWidth.W))
  val frac = Vec(maxVectorSize, UInt(coreFracWidth.W))
  val dotSign = UInt(1.W)
  val dotExp = SInt(expWidth.W)
  val dotFrac = UInt(dotWidth.W)
  val corePosit = Vec(maxVectorSize, UInt(maxPositWidth.W))
  val corePositDot = UInt(maxPositWidth.W)
  val coreFloat = Vec(maxVectorSize, UInt(floatWidth.W))
  val coreFloatDot = UInt(floatWidth.W)
  val coreInt = Vec(maxVectorSize, SInt(intWidth.W))
}

class PvuNormalizedPayload(
  maxPositWidth: Int,
  maxVectorSize: Int,
  floatWidth: Int,
  intWidth: Int,
  expWidth: Int,
  normFracWidth: Int
) extends Bundle {
  val request = new PvuRequest(maxPositWidth, maxVectorSize, floatWidth)
  val sign = Vec(maxVectorSize, UInt(1.W))
  val exp = Vec(maxVectorSize, SInt(expWidth.W))
  val frac = Vec(maxVectorSize, UInt(normFracWidth.W))
  val dotSign = UInt(1.W)
  val dotExp = SInt(expWidth.W)
  val dotFrac = UInt(normFracWidth.W)
  val corePosit = Vec(maxVectorSize, UInt(maxPositWidth.W))
  val corePositDot = UInt(maxPositWidth.W)
  val coreFloat = Vec(maxVectorSize, UInt(floatWidth.W))
  val coreFloatDot = UInt(floatWidth.W)
  val coreInt = Vec(maxVectorSize, SInt(intWidth.W))
}
 
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
   val DIV_SCALE_WIDTH: Int    = Math.max(SRC_EXP_WIDTH_MAX, float_exp_width + 1) + 1
   val FLOAT_BIAS: Int            = (1 << (float_exp_width - 1)) - 1
   val FLOAT_MAX_NORMAL_EXP: Int  = FLOAT_BIAS
   val FLOAT_MIN_NORMAL_EXP: Int  = 1 - FLOAT_BIAS
   val FLOAT_MIN_SUBNORMAL_EXP: Int = 1 - FLOAT_BIAS - float_frac_width
   
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
     // Transactional request/response channel.  Operand and control ports
     // below are sampled only on in_valid && in_ready.
     val in_valid = Input(Bool())
     val in_ready = Output(Bool())
     val in_tag = Input(UInt(32.W))
     val out_valid = Output(Bool())
     val out_ready = Input(Bool())
     val out_tag = Output(UInt(32.W))
     val out_op = Output(UInt(4.W))
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

  val inputRequest = Wire(new PvuRequest(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, FLOAT_WIDTH))
  inputRequest.tag := io.in_tag
  inputRequest.posit_i1 := io.posit_i1
  inputRequest.posit_i2 := io.posit_i2
  inputRequest.op := io.op
  inputRequest.Isposit := io.Isposit
  inputRequest.Outposit := io.Outposit
  inputRequest.float_i := io.float_i
  inputRequest.float_i2 := io.float_i2
  inputRequest.float_mode := io.float_mode
  inputRequest.float_posit := io.float_posit
  inputRequest.src_posit_width := io.src_posit_width
  inputRequest.vector_size := io.vector_size
  inputRequest.dst_posit_width := io.dst_posit_width

  private val PIPE_FRAC_WIDTH = MAX_POSIT_WIDTH - ES - 2
  private val PIPE_PRODUCT_WIDTH = 2 * PIPE_FRAC_WIDTH
  private val PIPE_DOT_WIDTH = PIPE_PRODUCT_WIDTH + log2Ceil(MAX_VECTOR_SIZE) + 1
  private val PIPE_CORE_FRAC_WIDTH = Math.max(MAX_ALIGN_WIDTH, PIPE_PRODUCT_WIDTH)

  // Decode, core/product, signed reduction, normalization and encode are five
  // distinct registered boundaries.  A shared clock-enable gives this fixed
  // lane precise freeze-on-backpressure behavior.
  val decodedRequest = Reg(new PvuRequest(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, FLOAT_WIDTH))
  val decodedSign1 = Reg(Vec(MAX_VECTOR_SIZE, UInt(1.W)))
  val decodedSign2 = Reg(Vec(MAX_VECTOR_SIZE, UInt(1.W)))
  val decodedExp1 = Reg(Vec(MAX_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
  val decodedExp2 = Reg(Vec(MAX_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
  val decodedFrac1 = Reg(Vec(MAX_VECTOR_SIZE, UInt(PIPE_FRAC_WIDTH.W)))
  val decodedFrac2 = Reg(Vec(MAX_VECTOR_SIZE, UInt(PIPE_FRAC_WIDTH.W)))
  val decodedValid = RegInit(false.B)
  val corePayload = Reg(new PvuCorePayload(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE,
    FLOAT_WIDTH, INT_WIDTH, SRC_EXP_WIDTH_MAX, PIPE_CORE_FRAC_WIDTH, PIPE_PRODUCT_WIDTH))
  val coreValid = RegInit(false.B)
  val reducedPayload = Reg(new PvuReducedPayload(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE,
    FLOAT_WIDTH, INT_WIDTH, SRC_EXP_WIDTH_MAX, PIPE_CORE_FRAC_WIDTH, PIPE_DOT_WIDTH))
  val reducedValid = RegInit(false.B)
  val normalizedPayload = Reg(new PvuNormalizedPayload(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE,
    FLOAT_WIDTH, INT_WIDTH, SRC_EXP_WIDTH_MAX, PIPE_FRAC_WIDTH))
  val normalizeValid = RegInit(false.B)
  val encodeResponse = Reg(new PvuResponse(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, FLOAT_WIDTH, INT_WIDTH))
  val encodeValid = RegInit(false.B)
  val response = Reg(new PvuResponse(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, FLOAT_WIDTH, INT_WIDTH))
  val combinationalCoreResult = Wire(new PvuCoreResults(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE,
    FLOAT_WIDTH, INT_WIDTH))
  val responseValid = RegInit(false.B)

  // Division owns an independent multi-cycle lane.  Its core and result slots
  // are independent of the fixed-latency lane, so a busy divide does not block
  // later non-divides.  A new divide is admitted only after all older traffic
  // has retired, which makes the priority response arbiter strictly ordered.
  val divisionCore = Reg(new PvuResponse(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, FLOAT_WIDTH, INT_WIDTH))
  val divisionCoreValid = RegInit(false.B)
  val divisionResult = Reg(new PvuResponse(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, FLOAT_WIDTH, INT_WIDTH))
  val divisionResultValid = RegInit(false.B)
  val divisionBusy = RegInit(false.B)

  val outputCanAccept = !responseValid || io.out_ready
  val nonDivisionWillIssue = encodeValid && outputCanAccept && !divisionResultValid
  val pipelineAdvance = !encodeValid || nonDivisionWillIssue
  val fixedLaneEmpty = !decodedValid && !coreValid && !reducedValid &&
    !normalizeValid && !encodeValid && !responseValid
  val inputIsDivision = io.op === 4.U
  io.in_ready := pipelineAdvance && Mux(inputIsDivision,
    !divisionBusy && fixedLaneEmpty && !divisionCoreValid && !divisionResultValid,
    true.B)
  val inFire = io.in_valid && io.in_ready
  val nonDivisionFire = inFire && !inputIsDivision
  val divisionFire = inFire && inputIsDivision

  when(divisionFire) {
    divisionBusy := true.B
  }

  when(responseValid && io.out_ready && response.op === 4.U) {
    divisionBusy := false.B
  }

  io.out_valid := responseValid
  io.out_tag := response.tag
  io.out_op := response.op

  // From this point through final result selection, every historic io operand
  // reference resolves to the accepted request register, never to live pins.
  locally {
  val io = decodedRequest

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
    val exp    = SInt((float_exp_width + 1).W)
    val frac   = UInt((float_frac_width + 1).W)
    val isNaN  = Bool()
    val isInf  = Bool()
    val isZero = Bool()
  }))

  val float_data2 = Wire(Vec(LIMITED_VECTOR_SIZE, new Bundle {
    val sign   = Bool()
    val exp    = SInt((float_exp_width + 1).W)
    val frac   = UInt((float_frac_width + 1).W)
    val isNaN  = Bool()
    val isInf  = Bool()
    val isZero = Bool()
  }))

  // 点积相关Wire定义
  val float_dot_data = Wire(new Bundle {
    val sign   = Bool()
    val exp    = SInt((float_exp_width + 1).W)
    val frac   = UInt((float_frac_width + 1).W)
    val isNaN  = Bool()
    val isInf  = Bool()
    val isZero = Bool()
  })

  // 结果相关Wire定义
  val float_rst_data = Wire(Vec(LIMITED_VECTOR_SIZE, new Bundle {
    val sign   = Bool()
    val exp    = SInt((float_exp_width + 1).W)
    val frac   = UInt((float_frac_width + 1).W)
    val isNaN  = Bool()
    val isInf  = Bool()
    val isZero = Bool()
  }))

  // 计算结果存储
  val pir_frac_rst = Wire(Vec(LIMITED_VECTOR_SIZE, UInt(Math.max(LIMITED_ALIGN_WIDTH, DOUBLED_FRAC_WIDTH).W)))
  val pir_exp_rst  = Wire(Vec(LIMITED_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
  val pir_sign_rst = Wire(Vec(LIMITED_VECTOR_SIZE, UInt(1.W)))

  // 在类的开始处添加输出端口的默认初始化
  combinationalCoreResult.float_o     := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(FLOAT_WIDTH.W)))
  combinationalCoreResult.float_dot_o := 0.U(FLOAT_WIDTH.W)
  combinationalCoreResult.posit_o     := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(MAX_POSIT_WIDTH.W)))
  combinationalCoreResult.posit_dot_o := 0.U(MAX_POSIT_WIDTH.W)
  combinationalCoreResult.int_o       := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.S(INT_WIDTH.W)))

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
   
   val rawPositNaR = (BigInt(1) << (MAX_POSIT_WIDTH - 1)).U(MAX_POSIT_WIDTH.W)
   val divUsesRawP32 = io.Isposit && ACTUAL_SRC_POSIT_WIDTH === 32.U
   val addSubUsesRawP32 = io.Isposit && io.Outposit &&
     (io.op === 1.U || io.op === 2.U) && ACTUAL_SRC_POSIT_WIDTH === 32.U &&
     ACTUAL_DST_POSIT_WIDTH === 32.U
   val dotHasRawPositNaR = (0 until MAX_VECTOR_SIZE)
     .map(i => valid_range(i) && (io.posit_i1(i) === rawPositNaR || io.posit_i2(i) === rawPositNaR))
     .reduce(_ || _)
   val exactP32Mul = Module(new ExactP32Mul(MAX_VECTOR_SIZE))
   for (i <- 0 until MAX_VECTOR_SIZE) {
     val rawP32 = io.Isposit && ACTUAL_SRC_POSIT_WIDTH === 32.U
     val lhsMax = io.posit_i1(i) === "h7fffffff".U || io.posit_i1(i) === "h80000001".U
     val rhsMax = io.posit_i2(i) === "h7fffffff".U || io.posit_i2(i) === "h80000001".U
     exactP32Mul.io.sign1_i(i) := pir_sign(i)
     exactP32Mul.io.sign2_i(i) := pir_sign2(i)
     exactP32Mul.io.exp1_i(i) := Mux(rawP32 && lhsMax, 120.S, pir_exp(i))
     exactP32Mul.io.exp2_i(i) := Mux(rawP32 && rhsMax, 120.S, pir_exp2(i))
     exactP32Mul.io.frac1_i(i) := pir_frac(i)
     exactP32Mul.io.frac2_i(i) := pir_frac2(i)
   }
   val dotUsesSequentialP32 = io.Isposit && io.Outposit && io.op === 5.U &&
     ACTUAL_SRC_POSIT_WIDTH === 32.U && ACTUAL_DST_POSIT_WIDTH === 32.U
   val sequentialP32Dot = Wire(Vec(MAX_VECTOR_SIZE + 1, UInt(32.W)))
   sequentialP32Dot(0) := 0.U
   for (i <- 0 until MAX_VECTOR_SIZE) {
     val fusedStage = Module(new Posit32MulAdd(SRC_EXP_WIDTH_MAX))
     fusedStage.io.multiplicand_i := io.posit_i1(i)
     fusedStage.io.multiplier_i := io.posit_i2(i)
     fusedStage.io.accumulator_i := sequentialP32Dot(i)
     sequentialP32Dot(i + 1) := Mux(valid_range(i), fusedStage.io.posit_o, sequentialP32Dot(i))
   }


   val divInputInvalid = Wire(Vec(MAX_VECTOR_SIZE, Bool()))
   val divInputInfinite = Wire(Vec(MAX_VECTOR_SIZE, Bool()))
   val divInputZero = Wire(Vec(MAX_VECTOR_SIZE, Bool()))
   for (i <- 0 until MAX_VECTOR_SIZE) {
     val rawInvalid = io.posit_i1(i) === rawPositNaR || io.posit_i2(i) === rawPositNaR || io.posit_i2(i) === 0.U
     val rawZero = !rawInvalid && io.posit_i1(i) === 0.U
     val decodedPositInvalid =
       (pir_frac(i) === 0.U && pir_sign(i) === 1.U) || pir_frac2(i) === 0.U
     val decodedPositZero = !decodedPositInvalid && pir_frac(i) === 0.U
     val floatInvalid = float_data(i).isNaN || float_data2(i).isNaN ||
       (float_data(i).isInf && float_data2(i).isInf) || (float_data(i).isZero && float_data2(i).isZero)
     val floatInfinite = !floatInvalid && ((float_data(i).isInf && !float_data2(i).isInf) ||
       (!float_data(i).isInf && !float_data(i).isZero && float_data2(i).isZero))
     val floatZero = !floatInvalid && ((float_data(i).isZero && !float_data2(i).isZero) ||
       (!float_data(i).isInf && float_data2(i).isInf))
     divInputInvalid(i) := Mux(divUsesRawP32, rawInvalid,
       Mux(io.Isposit, decodedPositInvalid, floatInvalid))
     divInputInfinite(i) := !io.Isposit && floatInfinite
     divInputZero(i) := Mux(divUsesRawP32, rawZero, Mux(io.Isposit, decodedPositZero, floatZero))
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
   val posit_rst_div    = Wire(Vec(MAX_VECTOR_SIZE, UInt(MAX_POSIT_WIDTH.W)))
   val pir_exp_rst_div  = Wire(Vec(MAX_VECTOR_SIZE, SInt(DIV_SCALE_WIDTH.W)))
   val pir_max_exp      = Wire(Vec(MAX_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))    //fraction_align

   val p32ToP16 = Module(new Posit32ToPosit16(MAX_VECTOR_SIZE))
   p32ToP16.io.posit_i := io.posit_i1
   val divP32ToP16 = Module(new Posit32ToPosit16(MAX_VECTOR_SIZE))
   divP32ToP16.io.posit_i := posit_rst_div
   val rawAddSub = Module(new PositAddSub(MAX_VECTOR_SIZE, SRC_EXP_WIDTH_MAX))
   rawAddSub.io.posit1_i := io.posit_i1
   rawAddSub.io.posit2_i := io.posit_i2
   rawAddSub.io.subtract_i := io.op === 2.U
   for(i <- 0 until MAX_VECTOR_SIZE) {
     rawAddSub.io.pir_exp1_i(i) := pir_exp(i)
     rawAddSub.io.pir_exp2_i(i) := pir_exp2(i)
     rawAddSub.io.pir_frac1_i(i) := pir_frac(i)(MAX_POSIT_WIDTH - ES - 3, 0)
     rawAddSub.io.pir_frac2_i(i) := pir_frac2(i)(MAX_POSIT_WIDTH - ES - 3, 0)
   }

   // For dot product, output is scalar.
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
     posit_rst_div(i)    := 0.U
     pir_exp_rst_div(i)  := 0.S
     pir_max_exp(i)      := 0.S
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
     for (i <- 0 until MAX_VECTOR_SIZE) {
       val rawP32 = io.Isposit && ACTUAL_SRC_POSIT_WIDTH === 32.U
       val lhsMax = io.posit_i1(i) === "h7fffffff".U || io.posit_i1(i) === "h80000001".U
       val rhsMax = io.posit_i2(i) === "h7fffffff".U || io.posit_i2(i) === "h80000001".U
       mul.io.pir_exp1_i(i) := Mux(rawP32 && lhsMax, 120.S, pir_exp(i))
       mul.io.pir_exp2_i(i) := Mux(rawP32 && rhsMax, 120.S, pir_exp2(i))
     }
     mul.io.pir_frac1_i := pir_frac
     mul.io.pir_frac2_i := pir_frac2
   
     pir_sign_rst     := mul.io.pir_sign_o
     pir_exp_rst      := mul.io.pir_exp_o
     pir_frac_rst_mul := mul.io.pir_frac_o
   
   }.elsewhen(io.op === 4.U){  //Div
     val div_inst = Module(new Div(LIMITED_POSIT_WIDTH, LIMITED_VECTOR_SIZE, LIMITED_ALIGN_WIDTH, ES, DIV_SCALE_WIDTH))

     div_inst.io.posit1_i    := io.posit_i1
     div_inst.io.is_posit_i   := io.Isposit
     div_inst.io.use_raw_p32_i := divUsesRawP32
     div_inst.io.pir_sign1_i  := pir_sign
     div_inst.io.pir_sign2_i  := pir_sign2
     div_inst.io.posit2_i    := io.posit_i2
     for(i <- 0 until MAX_VECTOR_SIZE) {
       val floatLeadingZeros1 = PriorityEncoder(Reverse(float_data(i).frac))
       val floatLeadingZeros2 = PriorityEncoder(Reverse(float_data2(i).frac))
       val normalizedFloatFrac1 = (float_data(i).frac << floatLeadingZeros1)(float_frac_width, 0)
       val normalizedFloatFrac2 = (float_data2(i).frac << floatLeadingZeros2)(float_frac_width, 0)
       val normalizedFloatExp1 = float_data(i).exp.pad(DIV_SCALE_WIDTH) - floatLeadingZeros1.zext
       val normalizedFloatExp2 = float_data2(i).exp.pad(DIV_SCALE_WIDTH) - floatLeadingZeros2.zext

       div_inst.io.pir_exp1_i(i) := Mux(io.Isposit,
         pir_exp(i).pad(DIV_SCALE_WIDTH), normalizedFloatExp1)
       div_inst.io.pir_exp2_i(i) := Mux(io.Isposit,
         pir_exp2(i).pad(DIV_SCALE_WIDTH), normalizedFloatExp2)
       div_inst.io.pir_frac1_i(i) := Mux(io.Isposit, pir_frac(i), normalizedFloatFrac1)
       div_inst.io.pir_frac2_i(i) := Mux(io.Isposit, pir_frac2(i), normalizedFloatFrac2)
     }

     pir_sign_rst     := div_inst.io.pir_sign_o
     pir_exp_rst_div  := div_inst.io.pir_exp_o
     pir_frac_rst_div := div_inst.io.pir_frac_o
     posit_rst_div    := div_inst.io.posit_o
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
       combinationalCoreResult.posit_o := float2posit_out
       combinationalCoreResult.float_o := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(FLOAT_WIDTH.W)))
     }.otherwise {
       // Posit到Float转换 - 仅处理操作数1
       combinationalCoreResult.float_o := posit2float_out
       combinationalCoreResult.posit_o := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(MAX_POSIT_WIDTH.W)))
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
         combinationalCoreResult.posit_o(i) := Mux(
           io.posit_i1(i) === rawPositNaR || io.posit_i2(i) === rawPositNaR,
           rawPositNaR,
           Mux(io.posit_i1(i).asSInt >= io.posit_i2(i).asSInt, io.posit_i1(i), io.posit_i2(i))
         )
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
         combinationalCoreResult.posit_o(i) := Mux(
           io.posit_i1(i) === rawPositNaR || io.posit_i2(i) === rawPositNaR,
           rawPositNaR,
           Mux(io.posit_i1(i).asSInt <= io.posit_i2(i).asSInt, io.posit_i1(i), io.posit_i2(i))
         )
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
     
     tranInt.io.posit_i := io.posit_i1
     
     for (i <- 0 until MAX_VECTOR_SIZE) {
       when (valid_range(i)) {
         combinationalCoreResult.int_o(i) := tranInt.io.int_o(i)
       }
     }
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
   val pir_exp_rst_adjusied     = Wire(Vec(MAX_VECTOR_SIZE, SInt(DIV_SCALE_WIDTH.W)))
   val pir_exp_rst_adjusied_dot = Wire(SInt(SRC_EXP_WIDTH_MAX.W))

   //初始化中间变量
   pir_exp_rst_adjusied     := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.S(DIV_SCALE_WIDTH.W)))
   pir_exp_rst_adjusied_dot := 0.S(SRC_EXP_WIDTH_MAX.W)

   // 计算调整后的指数
   when(io.op === 5.U){
     pir_exp_rst_adjusied_dot := pir_exp_adjust_dot + pir_exp_dot
   }.otherwise{
     for(i <- 0 until MAX_VECTOR_SIZE){
       when(io.op === 4.U) {
         pir_exp_rst_adjusied(i) := pir_exp_rst_div(i) + pir_exp_adjust(i).pad(DIV_SCALE_WIDTH)
       }.otherwise {
         pir_exp_rst_adjusied(i) := pir_exp_rst(i) + pir_exp_adjust(i)
       }
     }
   }
   val pir_exp_for_posit = Wire(Vec(MAX_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
   for(i <- 0 until MAX_VECTOR_SIZE) {
     when(io.op === 4.U && pir_exp_rst_adjusied(i) > 120.S(DIV_SCALE_WIDTH.W)) {
       pir_exp_for_posit(i) := 120.S
     }.elsewhen(io.op === 4.U && pir_exp_rst_adjusied(i) < -120.S(DIV_SCALE_WIDTH.W)) {
       pir_exp_for_posit(i) := -120.S
     }.otherwise {
       pir_exp_for_posit(i) := pir_exp_rst_adjusied(i)
     }
   }

   val DIV_NORM_FRAC_WIDTH = MAX_POSIT_WIDTH - ES - 2
   val DIV_FLOAT_NORMAL_DROP = DIV_NORM_FRAC_WIDTH - (float_frac_width + 1)
   require(DIV_FLOAT_NORMAL_DROP >= 2, "division float packing needs guard and sticky bits")
   val div_float_results = Wire(Vec(MAX_VECTOR_SIZE, UInt(FLOAT_WIDTH.W)))
   div_float_results := VecInit(Seq.fill(MAX_VECTOR_SIZE)(0.U(FLOAT_WIDTH.W)))
   for(i <- 0 until MAX_VECTOR_SIZE) {
     val normalizedFraction = pir_frac_normed(i)
     val normalRetained = normalizedFraction(DIV_NORM_FRAC_WIDTH - 1, DIV_FLOAT_NORMAL_DROP)
     val normalGuard = normalizedFraction(DIV_FLOAT_NORMAL_DROP - 1)
     val normalSticky = normalizedFraction(DIV_FLOAT_NORMAL_DROP - 2, 0).orR
     val normalRoundUp = normalGuard && (normalSticky || normalRetained(0))
     val normalRounded = Cat(0.U(1.W), normalRetained) + normalRoundUp.asUInt
     val normalCarry = normalRounded(float_frac_width + 1)
     val normalRoundedExp = pir_exp_rst_adjusied(i) + normalCarry.asUInt.zext
     val normalFraction = Mux(normalCarry, 0.U(float_frac_width.W), normalRounded(float_frac_width - 1, 0))
     val normalExponent = (normalRoundedExp + FLOAT_BIAS.S(DIV_SCALE_WIDTH.W)).asUInt

     val subnormalShiftSigned =
       (DIV_NORM_FRAC_WIDTH - 1 + FLOAT_MIN_SUBNORMAL_EXP).S(DIV_SCALE_WIDTH.W) - pir_exp_rst_adjusied(i)
     val subnormalShift = subnormalShiftSigned.asUInt
     val subnormalTruncated = normalizedFraction >> subnormalShift
     val subnormalGuard = (normalizedFraction >> (subnormalShift - 1.U))(0)
     val subnormalStickyTerms = (0 until DIV_NORM_FRAC_WIDTH - 1).map { bit =>
       (subnormalShift > (bit + 1).U) && normalizedFraction(bit)
     }
     val subnormalSticky = subnormalStickyTerms.reduce(_ || _)
     val subnormalRoundUp = subnormalGuard && (subnormalSticky || subnormalTruncated(0))
     val subnormalRounded = Cat(0.U(1.W), subnormalTruncated(float_frac_width - 1, 0)) + subnormalRoundUp.asUInt
     val subnormalCarry = subnormalRounded(float_frac_width)
     val subnormalFraction = Mux(subnormalCarry, 0.U(float_frac_width.W), subnormalRounded(float_frac_width - 1, 0))

     val exponentOnes = ((BigInt(1) << float_exp_width) - 1).U(float_exp_width.W)
     val zeroExponent = 0.U(float_exp_width.W)
     val canonicalNaNFraction = 1.U(float_frac_width.W)
     val packed = Wire(UInt((1 + float_exp_width + float_frac_width).W))
     packed := 0.U
     when(divInputInvalid(i)) {
       packed := Cat(0.U(1.W), exponentOnes, canonicalNaNFraction)
     }.elsewhen(divInputInfinite(i)) {
       packed := Cat(pir_sign_rst(i), exponentOnes, 0.U(float_frac_width.W))
     }.elsewhen(divInputZero(i)) {
       packed := Cat(pir_sign_rst(i), zeroExponent, 0.U(float_frac_width.W))
     }.elsewhen(pir_exp_rst_adjusied(i) >= FLOAT_MIN_NORMAL_EXP.S(DIV_SCALE_WIDTH.W)) {
       when(normalRoundedExp > FLOAT_MAX_NORMAL_EXP.S(DIV_SCALE_WIDTH.W)) {
         packed := Cat(pir_sign_rst(i), exponentOnes, 0.U(float_frac_width.W))
       }.otherwise {
         packed := Cat(pir_sign_rst(i), normalExponent(float_exp_width - 1, 0), normalFraction)
       }
     }.otherwise {
       val subnormalExponent = Mux(subnormalCarry, 1.U(float_exp_width.W), zeroExponent)
       packed := Cat(pir_sign_rst(i), subnormalExponent, subnormalFraction)
     }
     div_float_results(i) := packed
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
           float_rst_data(i).frac := pir_frac_normed(i)(MAX_POSIT_WIDTH - ES - 3,
             MAX_POSIT_WIDTH - ES - 3 - float_frac_width)
         }
         
         // 检查特殊情况
         float_rst_data(i).isNaN  := false.B
         float_rst_data(i).isInf  := false.B
         float_rst_data(i).isZero := false.B
         
         when(pir_frac_normed(i) === 0.U) {
           float_rst_data(i).isZero := true.B
         }
         
         when(io.op === 4.U) {
           val exponentOverflow = pir_exp_rst_adjusied(i) > FLOAT_MAX_NORMAL_EXP.S(DIV_SCALE_WIDTH.W)
           val exponentUnderflow = pir_exp_rst_adjusied(i) < FLOAT_MIN_SUBNORMAL_EXP.S(DIV_SCALE_WIDTH.W)
           val finiteOverflow = !divInputInvalid(i) && !divInputInfinite(i) && !divInputZero(i) && exponentOverflow
           val finiteUnderflow = !divInputInvalid(i) && !divInputInfinite(i) && !divInputZero(i) && exponentUnderflow
           float_rst_data(i).isNaN := divInputInvalid(i)
           float_rst_data(i).isInf := divInputInfinite(i) || finiteOverflow
           float_rst_data(i).isZero := divInputZero(i) || finiteUnderflow
         }.elsewhen(!io.Isposit) {
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
       combinationalCoreResult.posit_dot_o := Mux(io.Isposit && dotHasRawPositNaR, rawPositNaR,
         Mux(dotUsesSequentialP32, sequentialP32Dot(MAX_VECTOR_SIZE), posit_result))
       combinationalCoreResult.float_dot_o := 0.U(FLOAT_WIDTH.W)
     }.otherwise {
       combinationalCoreResult.posit_dot_o := 0.U(MAX_POSIT_WIDTH.W)
       combinationalCoreResult.float_dot_o := float_result
     }
     
   }.elsewhen(io.op === 6.U){
     // This runtime operation implements only raw P32 to left-aligned P16.
     val isP32ToP16 = ACTUAL_SRC_POSIT_WIDTH === 32.U && ACTUAL_DST_POSIT_WIDTH === 16.U
     for(i <- 0 until MAX_VECTOR_SIZE) {
       when(valid_range(i) && isP32ToP16) {
         combinationalCoreResult.posit_o(i) := p32ToP16.io.posit_o(i)
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
     when (ACTUAL_SRC_POSIT_WIDTH === MAX_POSIT_WIDTH.U &&
       ACTUAL_DST_POSIT_WIDTH === MAX_POSIT_WIDTH.U) {
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
           encode.io.pir_exp(i)  := pir_exp_for_posit(i)
           encode.io.pir_frac(i) := pir_frac_normed(i)
           
           val isSameWidthP32Divide = io.op === 4.U && io.Isposit &&
             ACTUAL_SRC_POSIT_WIDTH === MAX_POSIT_WIDTH.U &&
             ACTUAL_DST_POSIT_WIDTH === MAX_POSIT_WIDTH.U
           when(isSameWidthP32Divide) {
             posit_results(i) := posit_rst_div(i)
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
           result_converter.io.pir_exp1_i(i)  := pir_exp_for_posit(i)
           val divFractionForConversion =
             Cat(pir_frac_normed(i)(MAX_POSIT_WIDTH - ES - 3, 1), 0.U(1.W))
           result_converter.io.pir_frac1_i(i) := Mux(io.op === 4.U, divFractionForConversion, pir_frac_normed(i))
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
           
           when(io.op === 4.U && io.Isposit &&
             ACTUAL_SRC_POSIT_WIDTH === 32.U && ACTUAL_DST_POSIT_WIDTH === 16.U) {
             posit_results(i) := divP32ToP16.io.posit_o(i)
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
           when(addSubUsesRawP32) {
             combinationalCoreResult.posit_o(i) := rawAddSub.io.posit_o(i)
           }.elsewhen(io.op === 4.U && (divInputInvalid(i) || divInputInfinite(i))) {
             combinationalCoreResult.posit_o(i) := rawPositNaR
           }.elsewhen(io.op === 4.U && divInputZero(i)) {
             combinationalCoreResult.posit_o(i) := 0.U(MAX_POSIT_WIDTH.W)
           }.elsewhen(io.Isposit && (io.posit_i1(i) === rawPositNaR || io.posit_i2(i) === rawPositNaR)) {
             combinationalCoreResult.posit_o(i) := rawPositNaR
           }.elsewhen(io.op === 3.U && io.Isposit && ACTUAL_SRC_POSIT_WIDTH === 32.U &&
             ACTUAL_DST_POSIT_WIDTH === 32.U) {
             combinationalCoreResult.posit_o(i) := exactP32Mul.io.posit_o(i)

           }.otherwise {
             combinationalCoreResult.posit_o(i) := posit_results(i)
           }
           combinationalCoreResult.float_o(i) := 0.U(FLOAT_WIDTH.W)
         }
       }
     }.otherwise {
       // 只处理有效范围内的结果
       for(i <- 0 until MAX_VECTOR_SIZE) {
         when(valid_range(i)) {
           combinationalCoreResult.posit_o(i) := 0.U(MAX_POSIT_WIDTH.W)
           combinationalCoreResult.float_o(i) := Mux(io.op === 4.U, div_float_results(i), float_results(i))
         }
       }
     }
   }

  } // captured-request execution scope

  // Decode boundary.  These modules see live pins, but their outputs and the
  // complete control word are sampled only on an input handshake.
  val incomingPosit1 = Module(new PositDecode(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, ES))
  val incomingPosit2 = Module(new PositDecode(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, ES))
  val incomingFloat1 = Module(new FloatDecode(float_exp_width, float_frac_width, MAX_VECTOR_SIZE))
  val incomingFloat2 = Module(new FloatDecode(float_exp_width, float_frac_width, MAX_VECTOR_SIZE))
  incomingPosit1.io.posit := io.posit_i1
  incomingPosit2.io.posit := io.posit_i2
  incomingFloat1.io.float := io.float_i
  incomingFloat2.io.float := io.float_i2

  val incomingSign1 = Wire(Vec(MAX_VECTOR_SIZE, UInt(1.W)))
  val incomingSign2 = Wire(Vec(MAX_VECTOR_SIZE, UInt(1.W)))
  val incomingExp1 = Wire(Vec(MAX_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
  val incomingExp2 = Wire(Vec(MAX_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
  val incomingFrac1 = Wire(Vec(MAX_VECTOR_SIZE, UInt(PIPE_FRAC_WIDTH.W)))
  val incomingFrac2 = Wire(Vec(MAX_VECTOR_SIZE, UInt(PIPE_FRAC_WIDTH.W)))
  for (lane <- 0 until MAX_VECTOR_SIZE) {
    incomingSign1(lane) := Mux(io.Isposit, incomingPosit1.io.Sign(lane), incomingFloat1.io.Sign(lane))
    incomingSign2(lane) := Mux(io.Isposit, incomingPosit2.io.Sign(lane), incomingFloat2.io.Sign(lane))
    incomingExp1(lane) := Mux(io.Isposit, incomingPosit1.io.Exp(lane), incomingFloat1.io.Exp(lane))
    incomingExp2(lane) := Mux(io.Isposit, incomingPosit2.io.Exp(lane), incomingFloat2.io.Exp(lane))
    incomingFrac1(lane) := Mux(io.Isposit, incomingPosit1.io.Frac(lane), incomingFloat1.io.Frac(lane))
    incomingFrac2(lane) := Mux(io.Isposit, incomingPosit2.io.Frac(lane), incomingFloat2.io.Frac(lane))
  }

  // Core/product boundary.  Vector arithmetic consumes only decoded registers.
  val coreAlignment = Module(new FractionAlignment_AddSub(
    MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, ES))
  coreAlignment.io.pir_exp1_i := decodedExp1
  coreAlignment.io.pir_exp2_i := decodedExp2
  coreAlignment.io.pir_frac1_i := decodedFrac1
  coreAlignment.io.pir_frac2_i := decodedFrac2
  val coreAdd = Module(new Add(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, ES))
  val coreSub = Module(new Sub(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, ES))
  coreAdd.io.pir_sign1_i := decodedSign1
  coreAdd.io.pir_sign2_i := decodedSign2
  coreAdd.io.pir_exp1_i := coreAlignment.io.pir_max_exp
  coreAdd.io.pir_exp2_i := coreAlignment.io.pir_max_exp
  coreAdd.io.pir_frac1_aligned := coreAlignment.io.pir_frac1_align
  coreAdd.io.pir_frac2_aligned := coreAlignment.io.pir_frac2_align
  coreSub.io.pir_sign1_i := decodedSign1
  coreSub.io.pir_sign2_i := decodedSign2
  coreSub.io.pir_exp1_i := coreAlignment.io.pir_max_exp
  coreSub.io.pir_exp2_i := coreAlignment.io.pir_max_exp
  coreSub.io.pir_frac1_aligned := coreAlignment.io.pir_frac1_align
  coreSub.io.pir_frac2_aligned := coreAlignment.io.pir_frac2_align
  val coreMul = Module(new Mul(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, ES))
  coreMul.io.pir_sign1_i := decodedSign1
  coreMul.io.pir_sign2_i := decodedSign2
  coreMul.io.pir_exp1_i := decodedExp1
  coreMul.io.pir_exp2_i := decodedExp2
  coreMul.io.pir_frac1_i := decodedFrac1
  coreMul.io.pir_frac2_i := decodedFrac2

  val coreNext = Wire(new PvuCorePayload(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE,
    FLOAT_WIDTH, INT_WIDTH, SRC_EXP_WIDTH_MAX, PIPE_CORE_FRAC_WIDTH, PIPE_PRODUCT_WIDTH))
  coreNext := 0.U.asTypeOf(coreNext)
  coreNext.request := decodedRequest
  coreNext.corePosit := combinationalCoreResult.posit_o
  coreNext.corePositDot := combinationalCoreResult.posit_dot_o
  coreNext.coreFloat := combinationalCoreResult.float_o
  coreNext.coreFloatDot := combinationalCoreResult.float_dot_o
  coreNext.coreInt := combinationalCoreResult.int_o
  val decodedVectorSize = Mux(decodedRequest.vector_size === 0.U,
    MAX_VECTOR_SIZE.U, decodedRequest.vector_size)
  for (lane <- 0 until MAX_VECTOR_SIZE) {
    val active = lane.U < decodedVectorSize
    when(active) {
      when(decodedRequest.op === 1.U) {
        coreNext.sign(lane) := coreAdd.io.pir_sign_o(lane)
        coreNext.exp(lane) := coreAdd.io.pir_exp_o(lane)
        coreNext.frac(lane) := coreAdd.io.pir_frac_o(lane)
      }.elsewhen(decodedRequest.op === 2.U) {
        coreNext.sign(lane) := coreSub.io.pir_sign_o(lane)
        coreNext.exp(lane) := coreSub.io.pir_exp_o(lane)
        coreNext.frac(lane) := coreSub.io.pir_frac_o(lane)
      }.elsewhen(decodedRequest.op === 3.U) {
        coreNext.sign(lane) := coreMul.io.pir_sign_o(lane)
        coreNext.exp(lane) := coreMul.io.pir_exp_o(lane)
        coreNext.frac(lane) := coreMul.io.pir_frac_o(lane)
      }
      coreNext.productSign(lane) := coreMul.io.pir_sign_o(lane)
      coreNext.productExp(lane) := coreMul.io.pir_exp_o(lane)
      coreNext.productFrac(lane) := Mux(decodedFrac1(lane) === 0.U || decodedFrac2(lane) === 0.U,
        0.U, coreMul.io.pir_frac_o(lane))
    }
  }

  // Signed-reduction boundary.  Dot products carry real products out of core,
  // align them here, and reduce explicitly sized two's-complement operands.
  val dotAlignment = Module(new FractionAlignment_DotProduct(
    MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, ES))
  dotAlignment.io.pir_exp_i := corePayload.productExp
  dotAlignment.io.pir_frac_i := corePayload.productFrac
  val dotSignedOperands = Wire(Vec(MAX_VECTOR_SIZE, SInt(PIPE_DOT_WIDTH.W)))
  for (lane <- 0 until MAX_VECTOR_SIZE) {
    val magnitude = dotAlignment.io.pir_frac_align(lane).pad(PIPE_DOT_WIDTH).asSInt
    dotSignedOperands(lane) := Mux(corePayload.productSign(lane) === 1.U, -magnitude, magnitude)
  }
  val dotTree = Module(new CsaTree(MAX_VECTOR_SIZE, PIPE_DOT_WIDTH, PIPE_DOT_WIDTH))
  dotTree.io.operands_i := VecInit(dotSignedOperands.map(_.asUInt))
  val dotReducedSigned = (dotTree.io.sum_o + dotTree.io.carry_o).asSInt
  val dotReducedMagnitude = Mux(dotReducedSigned < 0.S,
    -dotReducedSigned, dotReducedSigned).asUInt
  val reducedNext = Wire(new PvuReducedPayload(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE,
    FLOAT_WIDTH, INT_WIDTH, SRC_EXP_WIDTH_MAX, PIPE_CORE_FRAC_WIDTH, PIPE_DOT_WIDTH))
  reducedNext := 0.U.asTypeOf(reducedNext)
  reducedNext.request := corePayload.request
  reducedNext.sign := corePayload.sign
  reducedNext.exp := corePayload.exp
  reducedNext.frac := corePayload.frac
  reducedNext.dotSign := dotReducedSigned < 0.S
  reducedNext.dotExp := Mux(dotReducedMagnitude === 0.U, 0.S, dotAlignment.io.pir_max_exp)
  reducedNext.dotFrac := dotReducedMagnitude
  reducedNext.corePosit := corePayload.corePosit
  reducedNext.corePositDot := corePayload.corePositDot
  reducedNext.coreFloat := corePayload.coreFloat
  reducedNext.coreFloatDot := corePayload.coreFloatDot
  reducedNext.coreInt := corePayload.coreInt

  // Normalize boundary.  The selected normalizer consumes only reduction-stage
  // registers; normalization is no longer in the core's combinational cone.
  val normalizeAddSub = Module(new FracNorm(
    MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, MAX_ALIGN_WIDTH, 1, ES))
  val normalizeMul = Module(new FracNorm(
    MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, PIPE_PRODUCT_WIDTH, PIPE_FRAC_WIDTH / 2, ES))
  for (lane <- 0 until MAX_VECTOR_SIZE) {
    normalizeAddSub.io.pir_frac_i(lane) := reducedPayload.frac(lane)(MAX_ALIGN_WIDTH - 1, 0)
    normalizeMul.io.pir_frac_i(lane) := reducedPayload.frac(lane)(PIPE_PRODUCT_WIDTH - 1, 0)
  }
  val normalizeDot = Module(new FracNorm_DotProduct(
    MAX_POSIT_WIDTH, PIPE_DOT_WIDTH, log2Ceil(MAX_VECTOR_SIZE + 1) + 2, ES))
  normalizeDot.io.pir_frac_i := reducedPayload.dotFrac
  val normalizedNext = Wire(new PvuNormalizedPayload(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE,
    FLOAT_WIDTH, INT_WIDTH, SRC_EXP_WIDTH_MAX, PIPE_FRAC_WIDTH))
  normalizedNext := 0.U.asTypeOf(normalizedNext)
  normalizedNext.request := reducedPayload.request
  normalizedNext.corePosit := reducedPayload.corePosit
  normalizedNext.corePositDot := reducedPayload.corePositDot
  normalizedNext.coreFloat := reducedPayload.coreFloat
  normalizedNext.coreFloatDot := reducedPayload.coreFloatDot
  normalizedNext.coreInt := reducedPayload.coreInt
  for (lane <- 0 until MAX_VECTOR_SIZE) {
    normalizedNext.sign(lane) := reducedPayload.sign(lane)
    when(reducedPayload.request.op === 3.U) {
      normalizedNext.frac(lane) := normalizeMul.io.pir_frac_o(lane)
      normalizedNext.exp(lane) := reducedPayload.exp(lane) + normalizeMul.io.exp_adjust(lane)
    }.otherwise {
      normalizedNext.frac(lane) := normalizeAddSub.io.pir_frac_o(lane)
      normalizedNext.exp(lane) := reducedPayload.exp(lane) + normalizeAddSub.io.exp_adjust(lane)
    }
  }
  normalizedNext.dotSign := reducedPayload.dotSign
  normalizedNext.dotFrac := normalizeDot.io.pir_frac_o
  normalizedNext.dotExp := reducedPayload.dotExp + normalizeDot.io.exp_adjust

  // Encode boundary.  Generic-width posit arithmetic is produced from the
  // normalized registers.  Raw P32 add/sub/mul/dot retain their dedicated exact
  // packers captured at core, preserving the established SoftPosit bit contract.
  val pipelineEncoder = Module(new PositEncode(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, ES))
  pipelineEncoder.io.pir_sign := normalizedPayload.sign
  pipelineEncoder.io.pir_exp := normalizedPayload.exp
  pipelineEncoder.io.pir_frac := normalizedPayload.frac
  val pipelineDotEncoder = Module(new PositEncode_DotProduct(MAX_POSIT_WIDTH, ES))
  pipelineDotEncoder.io.pir_sign := normalizedPayload.dotSign
  pipelineDotEncoder.io.pir_exp := normalizedPayload.dotExp
  pipelineDotEncoder.io.pir_frac := normalizedPayload.dotFrac
  val encodedNext = Wire(new PvuResponse(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, FLOAT_WIDTH, INT_WIDTH))
  encodedNext := 0.U.asTypeOf(encodedNext)
  encodedNext.tag := normalizedPayload.request.tag
  encodedNext.op := normalizedPayload.request.op
  encodedNext.posit_o := normalizedPayload.corePosit
  encodedNext.posit_dot_o := normalizedPayload.corePositDot
  encodedNext.float_o := normalizedPayload.coreFloat
  encodedNext.float_dot_o := normalizedPayload.coreFloatDot
  encodedNext.int_o := normalizedPayload.coreInt
  val normalizedSrcWidth = Mux(normalizedPayload.request.src_posit_width === 0.U,
    MAX_POSIT_WIDTH.U, normalizedPayload.request.src_posit_width)
  val normalizedDstWidth = Mux(normalizedPayload.request.dst_posit_width === 0.U,
    normalizedSrcWidth, normalizedPayload.request.dst_posit_width)
  val genericVectorArithmetic = normalizedPayload.request.Isposit && normalizedPayload.request.Outposit &&
    normalizedPayload.request.op >= 1.U && normalizedPayload.request.op <= 3.U &&
    !(normalizedSrcWidth === 32.U && normalizedDstWidth === 32.U)
  val normalizedVectorSize = Mux(normalizedPayload.request.vector_size === 0.U,
    MAX_VECTOR_SIZE.U, normalizedPayload.request.vector_size)
  when(genericVectorArithmetic) {
    for (lane <- 0 until MAX_VECTOR_SIZE) {
      when(lane.U < normalizedVectorSize) {
        val narrowed = (pipelineEncoder.io.posit(lane) >>
          (MAX_POSIT_WIDTH.U - normalizedDstWidth)) << (MAX_POSIT_WIDTH.U - normalizedDstWidth)
        encodedNext.posit_o(lane) := Mux(normalizedDstWidth < MAX_POSIT_WIDTH.U,
          narrowed, pipelineEncoder.io.posit(lane))
      }.otherwise {
        encodedNext.posit_o(lane) := 0.U
      }
    }
  }
  val genericDot = normalizedPayload.request.op === 5.U &&
    !(normalizedPayload.request.Isposit && normalizedPayload.request.Outposit &&
      normalizedSrcWidth === 32.U && normalizedDstWidth === 32.U)
  when(genericDot && normalizedPayload.request.Outposit) {
    encodedNext.posit_dot_o := pipelineDotEncoder.io.posit
  }

  val executedDivision = Wire(new PvuResponse(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, FLOAT_WIDTH, INT_WIDTH))
  executedDivision := 0.U.asTypeOf(executedDivision)
  executedDivision.tag := decodedRequest.tag
  executedDivision.op := decodedRequest.op
  executedDivision.posit_o := combinationalCoreResult.posit_o
  executedDivision.posit_dot_o := combinationalCoreResult.posit_dot_o
  executedDivision.float_o := combinationalCoreResult.float_o
  executedDivision.float_dot_o := combinationalCoreResult.float_dot_o
  executedDivision.int_o := combinationalCoreResult.int_o

  // Independent division lane progression.
  val divisionResultCanAccept = !divisionResultValid || (outputCanAccept && divisionResultValid)
  when(divisionResultCanAccept) {
    divisionResultValid := divisionCoreValid
    when(divisionCoreValid) {
      divisionResult := divisionCore
    }
    divisionCoreValid := false.B
  }

  // Global elastic progression of the fixed-latency lane.
  when(pipelineAdvance) {
    encodeValid := normalizeValid
    when(normalizeValid) { encodeResponse := encodedNext }
    normalizeValid := reducedValid
    when(reducedValid) { normalizedPayload := normalizedNext }
    reducedValid := coreValid
    when(coreValid) { reducedPayload := reducedNext }
    coreValid := decodedValid && decodedRequest.op =/= 4.U
    when(decodedValid && decodedRequest.op =/= 4.U) { corePayload := coreNext }
    when(decodedValid && decodedRequest.op === 4.U) {
      divisionCore := executedDivision
      divisionCoreValid := true.B
    }
    decodedValid := inFire
    when(inFire) {
      decodedRequest := inputRequest
      decodedSign1 := incomingSign1
      decodedSign2 := incomingSign2
      decodedExp1 := incomingExp1
      decodedExp2 := incomingExp2
      decodedFrac1 := incomingFrac1
      decodedFrac2 := incomingFrac2
    }
  }

  // Ordered response arbitration: an older divide wins over later fixed-lane
  // traffic; both sources hold their data until the output slot can accept it.
  when(outputCanAccept) {
    responseValid := divisionResultValid || encodeValid
    when(divisionResultValid) {
      response := divisionResult
    }.elsewhen(encodeValid) {
      response := encodeResponse
    }
  }

  io.float_o := response.float_o
  io.float_dot_o := response.float_dot_o
  io.posit_o := response.posit_o
  io.posit_dot_o := response.posit_dot_o
  io.int_o := response.int_o
 }
