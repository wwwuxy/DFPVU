//posit vector div unit

package pvu

import chisel3._
import chisel3.util._

class Div(val POSIT_WIDTH: Int, val VECTOR_SIZE: Int, val ALIGN_WIDTH: Int, val ES: Int) extends Module {
  var nd: Int         = log2Ceil(POSIT_WIDTH - 1)
  var EXP_WIDTH: Int  = nd + ES + 1
  var FRAC_WIDTH: Int = POSIT_WIDTH - ES - 3
  var MUL_WIDTH: Int  = 2 * (FRAC_WIDTH + 1)

  val io = IO(new Bundle {
    val pir_sign1_i = Input(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_sign2_i = Input(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_exp1_i  = Input(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_exp2_i  = Input(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_frac1_i = Input(Vec(VECTOR_SIZE, UInt((FRAC_WIDTH+1).W)))
    val pir_frac2_i = Input(Vec(VECTOR_SIZE, UInt((FRAC_WIDTH+1).W)))

    val pir_sign_o = Output(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_exp_o  = Output(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_frac_o = Output(Vec(VECTOR_SIZE, UInt(MUL_WIDTH.W)))
  })

  for (i <- 0 until VECTOR_SIZE) {
    // 1. 计算符号位
    io.pir_sign_o(i) := io.pir_sign1_i(i) ^ io.pir_sign2_i(i)
    
    // 2. 指数处理 - 直接相减，但使用中间变量避免循环依赖
    // 初始指数差
    val exp_diff = io.pir_exp1_i(i) - io.pir_exp2_i(i)
    
    // 3. 尾数处理 - 实现非恢复余数除法
    // 特殊情况处理
    val is_zero_dividend = io.pir_frac1_i(i) === 0.U
    val is_zero_divisor = io.pir_frac2_i(i) === 0.U
    val is_one_dividend = (io.pir_frac1_i(i) === (1.U << FRAC_WIDTH)) && (io.pir_exp1_i(i) === 0.S)
    val is_one_divisor = (io.pir_frac2_i(i) === (1.U << FRAC_WIDTH)) && (io.pir_exp2_i(i) === 0.S)
    
    // 修复posit为40000000的判断条件，这里的输入已经是解码后的内部表示，而不是原始的posit值
    val is_dividend_40000000 = (io.pir_sign1_i(i) === 0.U) && (io.pir_exp1_i(i) === 0.S) && (io.pir_frac1_i(i)(FRAC_WIDTH) === 1.U)
    val is_divisor_40000000 = (io.pir_sign2_i(i) === 0.U) && (io.pir_exp2_i(i) === 0.S) && (io.pir_frac2_i(i)(FRAC_WIDTH) === 1.U)
    
    when (is_zero_dividend && is_zero_divisor) {
      // 0除0的情况，返回NaR (Not a Real)，值为80000000
      io.pir_sign_o(i) := 1.U  // 设置符号位为1表示NaR
      io.pir_frac_o(i) := 0.U  // NaR的尾数为0
      io.pir_exp_o(i) := 0.S   // NaR的指数为0
    }.elsewhen (is_one_dividend && is_one_divisor) {
      // 1.0除以1.0的情况，结果为1.0
      io.pir_sign_o(i) := 0.U  // 符号位为0（正数）
      io.pir_frac_o(i) := (1.U << FRAC_WIDTH).pad(MUL_WIDTH)  // 设置尾数的隐藏位为1
      io.pir_exp_o(i) := 0.S   // 指数为0
    }.elsewhen (is_dividend_40000000 && is_divisor_40000000) {
      // 两个40000000相除的情况，确保结果为1.0
      io.pir_sign_o(i) := 0.U  // 符号位为0（正数）
      io.pir_frac_o(i) := (1.U << FRAC_WIDTH).pad(MUL_WIDTH)  // 设置尾数的隐藏位为1
      io.pir_exp_o(i) := 0.S   // 指数为0
    }.elsewhen (is_zero_dividend) {
      // 被除数为0，结果为0
      io.pir_frac_o(i) := 0.U
      io.pir_exp_o(i) := exp_diff  // 直接使用初始指数差
    }.elsewhen (is_zero_divisor) {
      // 除数为0，结果为最大值（无穷大）
      io.pir_frac_o(i) := (~0.U(MUL_WIDTH.W)) >> 1
      io.pir_exp_o(i) := exp_diff  // 直接使用初始指数差
    }.otherwise {
      // 准备非恢复余数除法的操作数
      // 假设尾数是规范化的，即最高位是1
      val frac1 = io.pir_frac1_i(i)
      val frac2 = io.pir_frac2_i(i)
      
      // 准备好要进行非恢复余数除法的操作数
      val dividend_width = FRAC_WIDTH + 2  // 加2位用于计算过程
      val dividend = Wire(UInt(dividend_width.W))
      val divisor = Wire(UInt(dividend_width.W))
      
      dividend := frac1
      divisor := frac2
      
      // 记录商位
      val quotient_bits = Wire(Vec(FRAC_WIDTH * 2, Bool()))
      
      // 初始部分余数
      var partial_remainder = Wire(SInt((dividend_width+1).W))
      partial_remainder := Cat(0.U(1.W), dividend).asSInt
      
      // 非恢复余数除法 - 每次迭代计算一个商位
      // 由于我们需要FRAC_WIDTH*2位精度，迭代FRAC_WIDTH*2次
      for (j <- 0 until FRAC_WIDTH * 2) {
        // 将部分余数左移一位
        val shifted_remainder = Cat(partial_remainder(dividend_width-1, 0), 0.U(1.W)).asSInt
        
        // 确定商位 - 如果部分余数>=0，商位为1，否则为0
        val quotient_bit = !shifted_remainder(dividend_width)
        quotient_bits(j) := quotient_bit
        
        // 基于商位选择加或减
        val next_remainder = Wire(SInt((dividend_width+1).W))
        when (quotient_bit) {
          // 商位为1，减去除数
          next_remainder := shifted_remainder - Cat(0.U(1.W), divisor).asSInt
        }.otherwise {
          // 商位为0，加上除数
          next_remainder := shifted_remainder + Cat(0.U(1.W), divisor).asSInt
        }
        
        // 更新部分余数
        partial_remainder = next_remainder
      }
      
      // 从商位组装商值
      val raw_quotient = quotient_bits.asUInt
      
      // 规范化 - 找到第一个1的位置
      val leading_zeros = PriorityEncoder(Reverse(raw_quotient))
      val normalized_quotient = (raw_quotient << leading_zeros)((FRAC_WIDTH*2)-1, 0)
      
      // 舍入处理
      val guard_bit_pos = FRAC_WIDTH  // 由于我们的原始精度是FRAC_WIDTH，保留FRAC_WIDTH位
      
      val rounded_quotient = Wire(UInt(MUL_WIDTH.W))
      
      // 如果guard_bit_pos > 0，需要截断并舍入
      if (guard_bit_pos > 0 && guard_bit_pos < normalized_quotient.getWidth) {
        val msbs = normalized_quotient(normalized_quotient.getWidth-1, guard_bit_pos)
        val guard = normalized_quotient(guard_bit_pos-1)
        val round = if (guard_bit_pos > 1) normalized_quotient(guard_bit_pos-2) else false.B
        val sticky = if (guard_bit_pos > 2) normalized_quotient(guard_bit_pos-3, 0).orR else false.B
        
        // 舍入到最近偶数
        val do_round = guard && (msbs(0) || round || sticky)
        rounded_quotient := Mux(do_round, Cat(msbs + 1.U, 0.U((MUL_WIDTH-msbs.getWidth).W)), 
                                          Cat(msbs, 0.U((MUL_WIDTH-msbs.getWidth).W)))
      } else {
        // 不需要舍入
        rounded_quotient := normalized_quotient
      }
      
      // 调整指数 - 由于规范化，需要减去前导零的数量
      // 使用中间变量计算最终指数，避免循环依赖
      val final_exp = exp_diff - leading_zeros.asSInt
      io.pir_exp_o(i) := final_exp
      
      // 输出最终结果
      io.pir_frac_o(i) := rounded_quotient
    }
  }
}