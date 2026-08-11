package pvu

import chisel3._
import chisel3.util._

/**
  * Float到Posit转换模块
  * 将IEEE-754浮点数转换为Posit格式
  * 
  * @param FLOAT_EXP_WIDTH 浮点数指数域宽度
  * @param FLOAT_FRAC_WIDTH 浮点数尾数域宽度
  * @param POSIT_WIDTH Posit数的位宽
  * @param ES Posit数的ES参数
  * @param VECTOR_SIZE 向量大小
  */
class FloatToPosit(
  val FLOAT_EXP_WIDTH: Int,
  val FLOAT_FRAC_WIDTH: Int,
  val POSIT_WIDTH: Int,
  val ES: Int,
  val VECTOR_SIZE: Int
) extends Module {
  // 添加参数限制
  private val LIMITED_VECTOR_SIZE = Math.min(VECTOR_SIZE, 16)  // 限制最大向量大小为16
  private val LIMITED_POSIT_WIDTH = Math.min(POSIT_WIDTH, 64) // 限制最大位宽为64
  private val LIMITED_ES = Math.min(ES, 8)                     // 限制最大ES为8
  
  val FLOAT_WIDTH = 1 + FLOAT_EXP_WIDTH + FLOAT_FRAC_WIDTH
  val nd          = log2Ceil(LIMITED_POSIT_WIDTH - 1)
  val EXP_WIDTH   = nd + LIMITED_ES + 1
  val FRAC_WIDTH  = LIMITED_POSIT_WIDTH - LIMITED_ES - 3

  val io = IO(new Bundle {
    val float_in  = Input(Vec(LIMITED_VECTOR_SIZE, UInt(FLOAT_WIDTH.W)))
    val posit_out = Output(Vec(LIMITED_VECTOR_SIZE, UInt(LIMITED_POSIT_WIDTH.W)))
  })

  // 1. 解码浮点数
  val floatDecoder = Module(new FloatDecode(FLOAT_EXP_WIDTH, FLOAT_FRAC_WIDTH, LIMITED_VECTOR_SIZE))
  floatDecoder.io.float := io.float_in

  // 2. 转换为Posit格式的内部表示
  val pir_sign = Wire(Vec(LIMITED_VECTOR_SIZE, UInt(1.W)))
  val pir_exp  = Wire(Vec(LIMITED_VECTOR_SIZE, SInt(EXP_WIDTH.W)))
  val pir_frac = Wire(Vec(LIMITED_VECTOR_SIZE, UInt((FRAC_WIDTH + 1).W)))

  for (i <- 0 until LIMITED_VECTOR_SIZE) {
    // 2.1 符号位直接转换
    pir_sign(i) := floatDecoder.io.Sign(i).asUInt

    // 2.2 处理特殊情况
    when (floatDecoder.io.isZero(i)) {
      // 如果是零，Posit也表示为零
      pir_sign(i) := 0.U
      pir_exp(i)  := 0.S
      pir_frac(i) := 0.U
    } .elsewhen (floatDecoder.io.isInf(i) || floatDecoder.io.isNaN(i)) {
      // 无穷大或NaN转换为Posit的NaR (Not a Real)
      pir_sign(i) := 1.U
      pir_exp(i)  := 0.S
      pir_frac(i) := 0.U
    } .otherwise {
      // 2.3 正常数值转换

      // 调整浮点数指数，适应Posit的ES和位宽
      // Posit的指数需要拆分为regime和ES部分
      val float_exp = floatDecoder.io.Exp(i)
      
      // 确保指数在Posit的有效范围内
      val limited_exp = Mux(float_exp > ((BigInt(1) << (LIMITED_ES + nd - 1)) - 1).S,
                           ((BigInt(1) << (LIMITED_ES + nd - 1)) - 1).S,
                           Mux(float_exp < (-(BigInt(1) << (LIMITED_ES + nd - 1))).S,
                               (-(BigInt(1) << (LIMITED_ES + nd - 1))).S,
                               float_exp))
      
      pir_exp(i) := limited_exp

      // 转换尾数，处理位宽差异
      val float_frac = floatDecoder.io.Frac(i)
      
      when (FRAC_WIDTH.U >= FLOAT_FRAC_WIDTH.U) {
        // Posit尾数位宽更大，需要在低位补0
        // 确保位宽差值为正数，并且使用安全的方式进行拼接
        val width_diff = FRAC_WIDTH - FLOAT_FRAC_WIDTH
        if (width_diff > 0) {
          pir_frac(i) := Cat(float_frac, 0.U(width_diff.W))
        } else {
          // 如果位宽相等或计算出负数，直接使用float_frac的低位
          pir_frac(i) := float_frac
        }
      } .otherwise {
        // Posit尾数位宽更小，需要截断和舍入（使用RNE规范）
        val truncated_width = FRAC_WIDTH + 1
        val source_width    = FLOAT_FRAC_WIDTH + 1
        
        if (truncated_width <= source_width) {
          // 如果目标宽度小于等于源宽度，需要进行截断和舍入
          // 计算需要舍弃的位数
          val drop_bits = source_width - truncated_width
          
          // 提取需要保留的高位部分
          val keep_part  = Wire(UInt(truncated_width.W))
              keep_part := 0.U
          
          when(drop_bits.asUInt <= FLOAT_FRAC_WIDTH.U) {
            // 只有当drop_bits在有效范围内时才进行位提取
            // 使用移位操作代替直接的位索引
            // 先进行右移操作
            val shifted = float_frac >> drop_bits
            // 然后安全地提取位，确保不会越界
            keep_part := shifted & ((1.U << truncated_width) - 1.U)
          }
          
          // 安全地获取舍入位
          val round_bit  = Wire(Bool())
              round_bit := false.B
          
          when(drop_bits.asUInt > 0.U && drop_bits.asUInt <= FLOAT_FRAC_WIDTH.U) {
            // 使用移位和掩码操作代替直接的位索引
            round_bit := ((float_frac >> (drop_bits.asUInt - 1.U)) & 1.U) === 1.U
          }
          
          // 获取粘滞位（sticky bits）- 所有低于舍入位的位的OR结果
          val sticky_bits  = Wire(Bool())
              sticky_bits := false.B
          
          when(drop_bits.asUInt > 1.U) {
            // 创建掩码获取所有低于舍入位的位
            val mask = Mux(
              (drop_bits.asUInt - 1.U) >= FLOAT_FRAC_WIDTH.U,
              ((BigInt(1) << FLOAT_FRAC_WIDTH) - 1).U,  // 如果drop_bits太大，使用全1掩码
              Mux(
                drop_bits.asUInt > 1.U,
                ((1.U << (drop_bits.asUInt - 1.U)) - 1.U),  // 正常计算的掩码
                0.U  // 安全处理，避免负数移位
              )
            )
            sticky_bits := (float_frac & mask).orR
          }
          
          // 获取最低保留位（least significant bit）
          val lsb = Wire(Bool())
          lsb := false.B
          
          when(drop_bits.asUInt >= 0.U && drop_bits.asUInt < FLOAT_FRAC_WIDTH.U) {
            // 使用移位和掩码操作代替直接的位索引
            lsb := ((float_frac >> drop_bits.asUInt) & 1.U) === 1.U
          }
          
          // 根据RNE规则决定是否进位：
          // 1. 舍入位为1且粘滞位为1，则进位
          // 2. 舍入位为1且粘滞位为0但最低保留位为1，则进位（向偶数舍入）
          val round_up = round_bit && (sticky_bits || lsb)
          
          // 根据是否进位决定最终结果
          pir_frac(i) := Mux(round_up, keep_part + 1.U, keep_part)
        } else {
          // 如果目标宽度大于源宽度，这种情况不应该发生
          // 因为已经在上面的when条件中处理了
          pir_frac(i) := float_frac
        }
      }
    }
  }

  // 3. 编码为Posit格式
  val positEncoder = Module(new PositEncode(LIMITED_POSIT_WIDTH, LIMITED_VECTOR_SIZE, LIMITED_ES))
  positEncoder.io.pir_sign := pir_sign
  positEncoder.io.pir_exp := pir_exp
  positEncoder.io.pir_frac := pir_frac
  io.posit_out := positEncoder.io.posit
} 