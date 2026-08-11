//Posit Vector FracNorm Unit
//The output is the adjusted mantissa and the amount of adjustment for the exponent, and the exponent also needs to be adjusted.
//WIDTH Specified when calling in pvu top (MUL_WIDTH or FRAC_WIDTH)
package pvu

import chisel3._
import chisel3.util._
import chisel3.experimental._

class FracNorm(val POSIT_WIDTH: Int, val VECTOR_SIZE: Int, val WIDTH: Int, val DECIMAL_POINT: Int, val ES: Int) extends Module {
  var nd: Int         = log2Ceil(WIDTH - 1)
  var EXP_WIDTH: Int  = nd + ES + 1 
  var FRAC_WIDTH: Int = POSIT_WIDTH - ES - 3
  
  // printf("DECIMAL_POINT = %d\n", DECIMAL_POINT.asUInt)
  
  val io = IO(new Bundle {
    val pir_frac_i = Input(Vec(VECTOR_SIZE, UInt(WIDTH.W)))
    
    val exp_adjust = Output(Vec(VECTOR_SIZE, SInt((EXP_WIDTH).W)))
    val pir_frac_o = Output(Vec(VECTOR_SIZE, UInt((FRAC_WIDTH + 1).W)))
  })

  val LZC_WIDTH = log2Ceil(WIDTH)  // The number of binary bits required to store the number of leading zeros
  val leading_zero_count  = Wire(Vec(VECTOR_SIZE, UInt(nd.W)))
  val lzc_zeroes          = Wire(Vec(VECTOR_SIZE, UInt(1.W)))
  
  for(i <- 0 until VECTOR_SIZE){
    
  // Count the number of leading zeros
    val lzcMod              = Module(new LZC(WIDTH, true, nd))
        lzcMod.io.in_i     := io.pir_frac_i(i)
        leading_zero_count(i) := lzcMod.io.cnt_o
        lzc_zeroes(i)         := lzcMod.io.empty_o

  // Calculate the exponential displacement (exponent will hardly overflow)
    val exp_adjust_reg = Wire(SInt((EXP_WIDTH+1).W))
    when(lzc_zeroes(i) === 1.U) {
      exp_adjust_reg := 0.S   // The trailing zeros do not need to be normalized.
    }.elsewhen(leading_zero_count(i) <= (DECIMAL_POINT - 1).U) {
      exp_adjust_reg := (DECIMAL_POINT.U - leading_zero_count(i) - 1.U).asSInt
    }.otherwise {
      exp_adjust_reg := -((leading_zero_count(i) - (DECIMAL_POINT.U - 1.U)).asSInt)
    }

    // io.exp_adjust(i) := exp_adjust_reg + WIDTH.S - FRAC_WIDTH.S
    // io.exp_adjust(i) := exp_adjust_reg

  //Use barrel_shifter, make DECIMAL_POINT is 1
    val frac_shifted = Wire(UInt(WIDTH.W))
    val shifter      = Module(new BarrelShifter(WIDTH, LZC_WIDTH, false))
    shifter.io.operand_i    := io.pir_frac_i(i)
    shifter.io.shift_amount := leading_zero_count(i)
    frac_shifted            := shifter.io.result_o

  //Keep the first FRAC_WITH + 1 digits, round down the lower digits
    if(WIDTH > (FRAC_WIDTH + 1)){
      val sticky_bits = frac_shifted(WIDTH - FRAC_WIDTH - 2, 0)
      val sticky_bit  = sticky_bits.orR.asUInt
      io.pir_frac_o(i) := Cat(frac_shifted(WIDTH - 1, WIDTH - FRAC_WIDTH) ,sticky_bit)
      io.exp_adjust(i) := exp_adjust_reg 
    }else{
      io.pir_frac_o(i) := frac_shifted
      io.exp_adjust(i) := exp_adjust_reg
    }
  }

} 