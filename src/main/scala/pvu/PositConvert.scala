package pvu

import chisel3._
import chisel3.util._

/**
  * Posit数精度转换模块
  * 输入：源精度的Posit数
  * 输出：目标精度的Posit数
  * 
  * @param SRC_WIDTH 源Posit数的位宽
  * @param DST_WIDTH 目标Posit数的位宽
  * @param SRC_ES 源Posit数的ES参数
  * @param DST_ES 目标Posit数的ES参数
  * @param VECTOR_SIZE 向量大小
  */

class PositConvert(
  val SRC_WIDTH: Int, 
  val DST_WIDTH: Int, 
  val SRC_ES: Int, 
  val DST_ES: Int,
  val VECTOR_SIZE: Int,
  val ALIGN_WIDTH: Int
) extends Module {
  require(SRC_WIDTH > 0 && DST_WIDTH > 0, "位宽必须大于0")
  require(SRC_ES >= 0 && DST_ES >= 0, "ES必须大于等于0")
  require(VECTOR_SIZE > 0, "向量大小必须大于0")

  // 计算源和目标格式的参数（使用安全的方式）
  val src_nd         = log2Ceil(SRC_WIDTH - 1)
  val dst_nd         = log2Ceil(DST_WIDTH - 1)
  val src_exp_width  = src_nd + SRC_ES + 1
  val dst_exp_width  = dst_nd + DST_ES + 1
  val src_frac_width = Math.max(0, SRC_WIDTH - SRC_ES - 3) // 确保不会出现负值
  val dst_frac_width = Math.max(0, DST_WIDTH - DST_ES - 3) // 确保不会出现负值

  // 计算目标指数的最大和最小值
  val dst_exp_max = (1 << (dst_exp_width - 1)) - 1
  val dst_exp_min = -(1 << (dst_exp_width - 1))

  val io = IO(new Bundle {
    // 输入
    val pir_sign1_i = Input(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_exp1_i  = Input(Vec(VECTOR_SIZE, SInt(src_exp_width.W)))
    val pir_frac1_i = Input(Vec(VECTOR_SIZE, UInt((src_frac_width + 1).W)))
    
    // 输出 - 修改输出尾数位宽
    val pir_sign_o = Output(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_exp_o  = Output(Vec(VECTOR_SIZE, SInt(dst_exp_width.W)))
    val pir_frac_o = Output(Vec(VECTOR_SIZE, UInt((dst_frac_width + 1).W)))
  })

  // 处理指数和尾数的转换
  for (i <- 0 until VECTOR_SIZE) {
    // 提供默认赋值，确保所有信号在所有路径下都有值
    io.pir_sign_o(i) := 0.U
    io.pir_exp_o(i)  := 0.S
    io.pir_frac_o(i) := 0.U

    // 检查是否为特殊值（0或无穷大）
    val is_zero    = io.pir_frac1_i(i) === 0.U
    val is_special = is_zero   // 在posit格式中，0就是特殊值，可以根据需要扩展其他特殊值

    when(is_special) {
      // 处理特殊值
      when(is_zero) {
        // 对于0，保持符号位不变，指数和尾数都为0
        io.pir_sign_o(i) := io.pir_sign1_i(i)
        io.pir_exp_o(i)  := 0.S
        io.pir_frac_o(i) := 0.U
      }
    }.otherwise {
      // 1. 符号位直接传递
      io.pir_sign_o(i) := io.pir_sign1_i(i)

      // 2. 指数转换
      // 根据ES的差异调整指数值，避免使用负数UInt
      val adjusted_exp = Wire(SInt(dst_exp_width.W))
      
      if (SRC_ES == DST_ES) {
        // ES相同，直接传递
        adjusted_exp := io.pir_exp1_i(i)
      } else if (SRC_ES > DST_ES) {
        // 源ES大于目标ES，需要右移指数
        val shift_amount = SRC_ES - DST_ES
        adjusted_exp := io.pir_exp1_i(i) >> shift_amount.U
      } else {
        // 源ES小于目标ES，需要左移指数
        val shift_amount = DST_ES - SRC_ES
        adjusted_exp := io.pir_exp1_i(i) << shift_amount.U
      }
      
      // 添加指数范围检查，确保不会溢出
      when(adjusted_exp > dst_exp_max.S) {
        // 指数上溢，饱和到最大值
        io.pir_exp_o(i) := dst_exp_max.S
      }.elsewhen(adjusted_exp < dst_exp_min.S) {
        // 指数下溢，饱和到最小值
        io.pir_exp_o(i) := dst_exp_min.S
      }.otherwise {
        // 正常范围内，使用调整后的指数
        io.pir_exp_o(i) := adjusted_exp
      }

      // 3. 尾数转换
      val frac_result = Wire(UInt((dst_frac_width + 1).W))
      frac_result := 0.U  // 默认值
      
      if (dst_frac_width == src_frac_width) {
        // 目标精度与源精度相同，直接使用源尾数
        frac_result := io.pir_frac1_i(i)
      } else if (dst_frac_width > src_frac_width) {
        // 目标精度更高，补0
        val extension_width = dst_frac_width - src_frac_width
        frac_result := Cat(io.pir_frac1_i(i), 0.U(extension_width.W))
      } else {
        // 目标精度更低，需要舍入
        val round_pos     = src_frac_width - dst_frac_width
        val frac_to_round = io.pir_frac1_i(i)
        
        // 安全获取舍入位
        val round_bit = Wire(Bool())
        round_bit := Mux(round_pos.U < frac_to_round.getWidth.U,
                         frac_to_round(round_pos), 
                         false.B)
        
        // 安全获取粘滞位(sticky bit)
        val sticky_bits = Wire(UInt(src_frac_width.W))
        sticky_bits := 0.U
        when (round_pos.U > 0.U) {
          for (j <- 0 until src_frac_width) {
            when (j.U < round_pos.U) {
              sticky_bits(j) := frac_to_round(j)
            }
          }
        }
        val sticky_bit = sticky_bits.orR
        
        // 安全获取保护位(guard bit)
        val guard_bit = Wire(Bool())
        guard_bit := Mux((round_pos + 1).U < frac_to_round.getWidth.U,
                         frac_to_round(round_pos + 1),
                         false.B)
        
        // 使用RNE(Round to Nearest Even)规则
        val round_up = round_bit & (sticky_bit | guard_bit)
        
        // 安全的右移和舍入
        val shifted_frac = frac_to_round >> round_pos.U
        val rounded_frac = Mux(round_up, shifted_frac + 1.U, shifted_frac)
        
        // 确保结果不超过目标宽度
        for (j <- 0 to dst_frac_width) {
          when (j.U < rounded_frac.getWidth.U) {
            frac_result(j) := rounded_frac(j)
          }.otherwise {
            frac_result(j) := 0.U
          }
        }
      }
      
      io.pir_frac_o(i) := frac_result
    }
  }
} 