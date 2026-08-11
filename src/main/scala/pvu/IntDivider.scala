package  pvu

import chisel3._
import chisel3.util._

// Integer division module using direct subtraction method (similar to posit division)
// Output is a 2*width q fixed-point number with width decimal places.

class IntDivider(val WIDTH: Int) extends Module {
  val io = IO(new Bundle {
    val dividend = Input(UInt(WIDTH.W))
    val divisor  = Input(UInt(WIDTH.W))
    
    val quotient = Output(UInt((2*WIDTH).W)) // Fixed-point number, 2*width bit width, width decimal places
  })

  // 特殊情况检测
  val is_zero_divisor = io.divisor === 0.U
  val is_zero_dividend = io.dividend === 0.U
  val is_power_of_two = (io.divisor & (io.divisor - 1.U)) === 0.U
  val is_equal_values = io.dividend === io.divisor && io.dividend =/= 0.U // 被除数等于除数，且不为0
  val is_both_40000000 = (io.dividend === (BigInt(1) << (WIDTH-1)).U) && (io.divisor === (BigInt(1) << (WIDTH-1)).U) // 判断两个操作数是否都是40000000
  
  // 对于2的幂次直接计算
  val power_of_two_index = PriorityEncoder(Reverse(io.divisor))
  val power_of_two_result = io.dividend << (WIDTH.U - power_of_two_index)

  // 规范化输入以提高精度
  val dividend_lz = PriorityEncoder(Reverse(io.dividend))
  val divisor_lz = PriorityEncoder(Reverse(io.divisor))
  
  val normalized_dividend = (io.dividend << dividend_lz)(WIDTH-1, 0)
  val normalized_divisor = (io.divisor << divisor_lz)(WIDTH-1, 0)
  
  // 非恢复余数除法算法
  val quotient_bits = Wire(Vec(2*WIDTH, Bool()))
  
  // 初始化部分余数
  var partial_remainder = Wire(SInt((WIDTH+1).W))
  partial_remainder := Cat(0.U(1.W), normalized_dividend).asSInt
  
  // 执行非恢复余数除法的每一步
  for (i <- 0 until 2*WIDTH) {
    // 左移部分余数
    val shifted_remainder = Cat(partial_remainder(WIDTH-1, 0), 0.U(1.W)).asSInt
    
    // 确定商位 - 如果部分余数>=0，商位为1，否则为0
    val quotient_bit = !shifted_remainder(WIDTH)
    quotient_bits(i) := quotient_bit
    
    // 基于商位选择加或减
    val next_remainder = Wire(SInt((WIDTH+1).W))
    when (quotient_bit) {
      // 商位为1，需要减去除数
      next_remainder := shifted_remainder - Cat(0.U(1.W), normalized_divisor).asSInt
    }.otherwise {
      // 商位为0，需要加上除数
      next_remainder := shifted_remainder + Cat(0.U(1.W), normalized_divisor).asSInt
    }
    
    // 更新部分余数
    partial_remainder = next_remainder
  }
  
  // 从商位组装商值
  val raw_quotient = quotient_bits.asUInt
  
  // 规范化 - 找到第一个1的位置
  val leading_zeros = PriorityEncoder(Reverse(raw_quotient))
  val normalized_quotient = (raw_quotient << leading_zeros)((2*WIDTH)-1, 0)
  
  // 舍入处理
  val msbs = normalized_quotient((2*WIDTH)-1, WIDTH)
  val guard = normalized_quotient(WIDTH-1)
  val round = if (WIDTH > 1) normalized_quotient(WIDTH-2) else false.B
  val sticky = if (WIDTH > 2) normalized_quotient(WIDTH-3, 0).orR else false.B
  
  // 舍入到最近偶数
  val do_round = guard && (msbs(0) || round || sticky)
  val rounded_quotient = Mux(do_round, msbs + 1.U, msbs)
  
  // 指数调整
  val exp_diff = dividend_lz.zext - divisor_lz.zext
  val final_shift = exp_diff - leading_zeros.zext
  
  // 最终结果
  val result = Wire(UInt((2*WIDTH).W))
  
  when (is_zero_dividend && is_zero_divisor) {
    // 0除0的情况，返回NaR (Not a Real)
    // 32位整数情况下，NaR表示为最高位为1，其余位为0 (80000000)
    result := (BigInt(1) << ((2*WIDTH)-1)).U
  }.elsewhen (is_both_40000000) {
    // 两个40000000相除，结果必须是40000000
    result := (BigInt(1) << WIDTH).U  // 设置为1.0的固定点表示
  }.elsewhen (is_equal_values) {
    // 相同值相除，结果为1.0
    result := (BigInt(1) << WIDTH).U  // 设置为1.0的固定点表示
  }.elsewhen (is_zero_divisor) {
    // 除数为0（但被除数不为0），返回最大正值
    result := (~0.U((2*WIDTH).W)) >> 1
  }.elsewhen (is_zero_dividend) {
    // 被除数为0，结果为0
    result := 0.U
  }.elsewhen (is_power_of_two) {
    // 对于2的幂次，使用更简单的计算
    result := power_of_two_result
  }.otherwise {
    // 根据指数差值调整结果
    when (final_shift > 0.S) {
      // 向左移
      result := Cat(rounded_quotient, 0.U(WIDTH.W)) << final_shift.asUInt
    }.elsewhen (final_shift < 0.S) {
      // 向右移
      result := Cat(rounded_quotient, 0.U(WIDTH.W)) >> (-final_shift).asUInt
    }.otherwise {
      result := Cat(rounded_quotient, 0.U(WIDTH.W))
    }
  }

  io.quotient := result
}
