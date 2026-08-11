// Posit Dot Product FracNorm Unit
//The input is a scalar rather than a vector
//The output is the adjusted mantissa and the amount of adjustment for the exponent, and the exponent also needs to be adjusted.
//WIDTH Specified when calling in pvu top (MUL_WIDTH or FRAC_WIDTH)
package pvu

import chisel3._
import chisel3.util._
import chisel3.experimental._

class FracNorm_DotProduct(val POSIT_WIDTH: Int, val WIDTH: Int, val DECIMAL_POINT: Int, val ES: Int) extends Module {
  var nd: Int         = log2Ceil(WIDTH - 1)
  var EXP_WIDTH: Int  = nd + ES + 1 
  var FRAC_WIDTH: Int = POSIT_WIDTH - ES - 3
  
  val io = IO(new Bundle {
    val pir_frac_i = Input(UInt(WIDTH.W))
    
    val exp_adjust = Output(SInt((EXP_WIDTH).W))
    val pir_frac_o = Output(UInt((FRAC_WIDTH+1).W))
  })

  val LZC_WIDTH = log2Ceil(WIDTH)  // The number of binary bits required to store the number of leading zeros

  // Count the number of leading zeros
    val lzcMod              = Module(new LZC(WIDTH, true, nd))
    lzcMod.io.in_i         := io.pir_frac_i
    val leading_zero_count  = lzcMod.io.cnt_o
    val lzc_zeroes          = lzcMod.io.empty_o
    
    val shift_flag  = Wire(Bool())
        shift_flag := false.B   //Default value

  // Calculate the exponential displacement (exponent will hardly overflow)
    val exp_adjust_reg = Wire(SInt((EXP_WIDTH+1).W))
    when(lzc_zeroes) {
      exp_adjust_reg := 0.S   // The trailing zeros do not need to be normalized.
    }.elsewhen(leading_zero_count <= (DECIMAL_POINT - 1).U) {
      val temp = DECIMAL_POINT.U - leading_zero_count - 1.U
      exp_adjust_reg := temp.asSInt
      shift_flag := true.B
    }.otherwise {
      val temp = leading_zero_count - (DECIMAL_POINT.U - 1.U)
      exp_adjust_reg := -(temp.asSInt)
      shift_flag := false.B
    }

    io.exp_adjust := exp_adjust_reg

   // Use barrel_shifter, make DECIMAL_POINT is 1
    val frac_shifted = Wire(UInt(WIDTH.W))
    when(shift_flag){
      val shifter      = Module(new BarrelShifter(WIDTH, LZC_WIDTH, true))
      shifter.io.operand_i    := io.pir_frac_i
      // 直接计算而不是使用中间变量，避免时序问题
      val shift_amount_value1 = DECIMAL_POINT.U - leading_zero_count - 1.U
      shifter.io.shift_amount := shift_amount_value1
      frac_shifted            := shifter.io.result_o
    }.otherwise{
      val shift_amount_value2 = leading_zero_count - (DECIMAL_POINT.U - 1.U)
      val shifter      = Module(new BarrelShifter(WIDTH, LZC_WIDTH, false))
      shifter.io.operand_i    := io.pir_frac_i
      val shift_amount_value = leading_zero_count - (DECIMAL_POINT.U - 1.U)
      shifter.io.shift_amount := shift_amount_value2
      frac_shifted            := shifter.io.result_o
    }

  // Keep the first FRAC_WITH + 1 digits, round down the lower digits
    when(WIDTH.asUInt > (FRAC_WIDTH + 1).U){
      val sticky_bits = frac_shifted(WIDTH - FRAC_WIDTH - DECIMAL_POINT - 1, 0)
      val sticky_bit  = sticky_bits.orR.asUInt
      io.pir_frac_o := Cat(frac_shifted(WIDTH - DECIMAL_POINT, WIDTH - DECIMAL_POINT - FRAC_WIDTH + 1) ,sticky_bit)
    }.otherwise{
      io.pir_frac_o := frac_shifted
    }
}