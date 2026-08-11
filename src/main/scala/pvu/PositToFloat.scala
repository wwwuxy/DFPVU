package pvu

import chisel3._
import chisel3.util._

/**
  * Posit到Float转换模块
  * 将Posit格式转换为IEEE-754浮点数
  * 
  * @param POSIT_WIDTH Posit数的位宽
  * @param ES Posit数的ES参数
  * @param FLOAT_EXP_WIDTH 浮点数指数域宽度
  * @param FLOAT_FRAC_WIDTH 浮点数尾数域宽度
  * @param VECTOR_SIZE 向量大小
  */
class PositToFloat(
  val POSIT_WIDTH: Int,
  val ES: Int,
  val FLOAT_EXP_WIDTH: Int,
  val FLOAT_FRAC_WIDTH: Int,
  val VECTOR_SIZE: Int
) extends Module {
  // 添加参数限制
  private val LIMITED_VECTOR_SIZE = Math.min(VECTOR_SIZE, 16)  // 限制最大向量大小为16
  private val LIMITED_POSIT_WIDTH = Math.min(POSIT_WIDTH, 64) // 限制最大位宽为64
  private val LIMITED_ES = Math.min(ES, 8)                     // 限制最大ES为8
  
  val FLOAT_WIDTH = 1 + FLOAT_EXP_WIDTH + FLOAT_FRAC_WIDTH
  val FLOAT_BIAS  = (1 << (FLOAT_EXP_WIDTH - 1)) - 1
  
  val nd         = log2Ceil(LIMITED_POSIT_WIDTH - 1)
  val EXP_WIDTH  = nd + LIMITED_ES + 1
  val FRAC_WIDTH = LIMITED_POSIT_WIDTH - LIMITED_ES - 3

  val io = IO(new Bundle {
    val posit_in  = Input(Vec(LIMITED_VECTOR_SIZE, UInt(LIMITED_POSIT_WIDTH.W)))
    val float_out = Output(Vec(LIMITED_VECTOR_SIZE, UInt(FLOAT_WIDTH.W)))
  })

  // 1. 解码Posit数
  val positDecoder = Module(new PositDecode(LIMITED_POSIT_WIDTH, LIMITED_VECTOR_SIZE, LIMITED_ES))
  positDecoder.io.posit := io.posit_in

  // 2. 准备Float编码所需的信号
  val float_sign = Wire(Vec(LIMITED_VECTOR_SIZE, Bool()))
  val float_exp  = Wire(Vec(LIMITED_VECTOR_SIZE, SInt((FLOAT_EXP_WIDTH + 1).W)))
  val float_frac = Wire(Vec(LIMITED_VECTOR_SIZE, UInt((FLOAT_FRAC_WIDTH + 1).W)))
  val isNaN      = Wire(Vec(LIMITED_VECTOR_SIZE, Bool()))
  val isInf      = Wire(Vec(LIMITED_VECTOR_SIZE, Bool()))
  val isZero     = Wire(Vec(LIMITED_VECTOR_SIZE, Bool()))

  for (i <- 0 until LIMITED_VECTOR_SIZE) {
    // 初始化
    float_sign(i) := positDecoder.io.Sign(i).asBool
    float_exp(i)  := 0.S
    float_frac(i) := 0.U
    isNaN(i)      := false.B
    isInf(i)      := false.B
    isZero(i)     := false.B

    // 检查Posit的特殊值
    val posit_word  = io.posit_in(i)
    val isNaR       = (posit_word(LIMITED_POSIT_WIDTH-1) === 1.U) && (posit_word(LIMITED_POSIT_WIDTH-2, 0) === 0.U)
    val isPositZero = posit_word === 0.U

    when (isNaR) {
      // NaR转换为NaN
      isNaN(i) := true.B
    } .elsewhen (isPositZero) {
      // 0转换为0
      isZero(i) := true.B
    } .otherwise {
      // 常规值转换
      val posit_exp  = positDecoder.io.Exp(i)
      val posit_frac = positDecoder.io.Frac(i)

      // 调整指数值，处理溢出情况
      when (posit_exp > (FLOAT_BIAS - 1).S) {
        // 指数上溢，转为无穷大
        isInf(i) := true.B
      } .elsewhen (posit_exp < (-(FLOAT_BIAS - 1)).S) {
        // 指数下溢，根据情况转为0或非规范化数
        val shift_amount = ((-FLOAT_BIAS + 1).S - posit_exp).asUInt
        
        when (shift_amount >= (FLOAT_FRAC_WIDTH + 1).U) {
          // 完全下溢，转为0
          isZero(i) := true.B
        } .otherwise {
          // 转为非规范化数
          float_exp(i) := (-(FLOAT_BIAS - 1)).S
          
          // 使用安全的方式进行右移
          val frac_width = posit_frac.getWidth
          val max_shift  = frac_width - 1
          
          // 限制shift_amount在有效范围内
          val safe_shift_amount = Mux(shift_amount > max_shift.asUInt, max_shift.asUInt, shift_amount)
          
          // 使用安全的右移操作并应用RNE舍入规则
          when (safe_shift_amount === 0.U) {
            // 不需要移位
            float_frac(i) := posit_frac & ((1.U << (FLOAT_FRAC_WIDTH + 1)) - 1.U)
          }.otherwise {
            // 需要移位，使用RNE舍入规则
            
            // 先进行右移，获取基本结果
            val shifted_base = posit_frac >> safe_shift_amount
            
            // 安全地计算舍入位
            val round_bit  = Wire(Bool())
                round_bit := false.B
            when(safe_shift_amount > 0.U) {
              // 只有当shift_amount > 0时才有舍入位
              val shift_for_round = safe_shift_amount - 1.U
              round_bit := ((posit_frac >> shift_for_round) & 1.U) === 1.U
            }
            
            // 获取粘滞位（sticky bits）- 所有低于舍入位的位的OR结果
            val sticky_bits  = Wire(Bool())
                sticky_bits := false.B
            
            when (safe_shift_amount > 1.U) {
              // 安全地计算粘滞位
              // 对于大的shift_amount，我们可以简化为检查posit_frac的低位是否有任何1
              when(safe_shift_amount >= frac_width.asUInt) {
                // 如果shift_amount大于等于frac_width，则所有位都会被移出
                sticky_bits := posit_frac.orR
              }.otherwise {
                // 否则，我们需要检查低于舍入位的所有位
                // 创建一个安全的掩码
                val shift_for_mask = safe_shift_amount - 1.U
                val mask_width     = (1.U << log2Ceil(frac_width + 1)).asUInt
                val mask = Mux(
                  shift_for_mask >= mask_width,
                  ((BigInt(1) << frac_width.toInt) - 1).U, // 如果shift太大，使用全1掩码
                  Mux(
                    shift_for_mask > 0.U,
                    ((1.U << shift_for_mask) - 1.U),  // 正常计算的掩码
                    0.U  // 安全处理，避免负数移位
                  )
                )
                sticky_bits := (posit_frac & mask).orR
              }
            }
            
            // 获取最低保留位
            val lsb = (shifted_base & 1.U) === 1.U
            
            // 根据RNE规则决定是否进位
            val round_up = round_bit && (sticky_bits || lsb)
            
            // 根据是否进位决定最终结果
            val final_result = Mux(round_up, shifted_base + 1.U, shifted_base)
            
            // 确保结果不超过FLOAT_FRAC_WIDTH + 1
            float_frac(i) := final_result & ((1.U << (FLOAT_FRAC_WIDTH + 1)) - 1.U)
          }
        }
      } .otherwise {
        // 正常范围，直接转换
        float_exp(i) := posit_exp

        // 处理尾数位宽差异
        when (FLOAT_FRAC_WIDTH.U >= FRAC_WIDTH.U) {
          // 浮点数尾数位宽更大，需要在低位补0
          // 确保位宽差值为正数，并且使用安全的方式进行拼接
          val width_diff = FLOAT_FRAC_WIDTH - FRAC_WIDTH

          if (width_diff > 0) {
            float_frac(i) := Cat(posit_frac, 0.U(width_diff.W))
          } else {
            // 如果位宽相等或计算出负数，直接使用posit_frac的低位
            float_frac(i) := posit_frac
          }
        } .otherwise {
          // 浮点数尾数位宽更小，需要截断和舍入（使用RNE规范）
          val truncated_width = FLOAT_FRAC_WIDTH + 1
          val source_width    = FRAC_WIDTH + 1
          
          if (truncated_width <= source_width) {
            // 如果目标宽度小于等于源宽度，需要进行截断和舍入
            // 计算需要舍弃的位数
            val drop_bits = source_width - truncated_width
            
            // 使用Wire来避免直接使用位索引
            val keep_part = Wire(UInt(truncated_width.W))
            
            // 默认值
            keep_part := 0.U
            
            // 安全地提取高位部分
            when(drop_bits.asUInt <= FRAC_WIDTH.U) {
              // 只有当drop_bits在有效范围内时才进行位提取
              // 使用移位操作代替直接的位索引
              val shifted = posit_frac >> drop_bits
              // 然后安全地提取位，确保不会越界
              keep_part := shifted & ((1.U << truncated_width) - 1.U)
            }
            
            // 安全地获取舍入位
            val round_bit = Wire(Bool())
            round_bit := false.B
            
            when(drop_bits.asUInt > 0.U && drop_bits.asUInt <= FRAC_WIDTH.U) {
              // 使用移位和掩码操作代替直接的位索引
              round_bit := ((posit_frac >> (drop_bits.asUInt - 1.U)) & 1.U) === 1.U
            }
            
            // 获取粘滞位（sticky bits）- 所有低于舍入位的位的OR结果
            val sticky_bits = Wire(Bool())
            sticky_bits := false.B
            
            when(drop_bits.asUInt > 1.U) {
              // 创建掩码获取所有低于舍入位的位
              val mask = Mux(
                (drop_bits.asUInt - 1.U) >= FRAC_WIDTH.U,
                ((BigInt(1) << FRAC_WIDTH) - 1).U,  // 如果drop_bits太大，使用全1掩码
                Mux(
                  drop_bits.asUInt > 1.U,
                  ((1.U << (drop_bits.asUInt - 1.U)) - 1.U),  // 正常计算的掩码
                  0.U  // 安全处理，避免负数移位
                )
              )
              sticky_bits := (posit_frac & mask).orR
            }
            
            // 获取最低保留位（least significant bit）
            val lsb = Wire(Bool())
            lsb := false.B
            
            when(drop_bits.asUInt >= 0.U && drop_bits.asUInt < FRAC_WIDTH.U) {
              // 使用移位和掩码操作代替直接的位索引
              lsb := ((posit_frac >> drop_bits.asUInt) & 1.U) === 1.U
            }
            
            // 根据RNE规则决定是否进位：
            // 1. 舍入位为1且粘滞位为1，则进位
            // 2. 舍入位为1且粘滞位为0但最低保留位为1，则进位（向偶数舍入）
            val round_up = round_bit && (sticky_bits || lsb)
            
            // 根据是否进位决定最终结果
            float_frac(i) := Mux(round_up, keep_part + 1.U, keep_part)
          } else {
            // 如果目标宽度大于源宽度，这种情况不应该发生
            // 因为已经在上面的when条件中处理了
            float_frac(i) := posit_frac
          }
        }
      }
    }
  }

  // 3. 编码为浮点数
  val floatEncoder = Module(new FloatEncode(FLOAT_EXP_WIDTH, FLOAT_FRAC_WIDTH, LIMITED_VECTOR_SIZE))
  floatEncoder.io.Sign   := float_sign
  floatEncoder.io.Exp    := float_exp
  floatEncoder.io.Frac   := float_frac
  floatEncoder.io.isNaN  := isNaN
  floatEncoder.io.isInf  := isInf
  floatEncoder.io.isZero := isZero
  io.float_out           := floatEncoder.io.float
} 