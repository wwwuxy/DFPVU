// Alignment factorial for add or sub
package pvu

import chisel3._
import chisel3.util._

class FractionAlignment_AddSub(val POSIT_WIDTH: Int, val VECTOR_SIZE: Int, val ALIGN_WIDTH: Int, val ES: Int) extends Module {
  var nd: Int         = log2Ceil(POSIT_WIDTH - 1)
  var EXP_WIDTH: Int  = nd + ES + 1
  var FRAC_WIDTH: Int = POSIT_WIDTH - ES - 3
  
  val io = IO(new Bundle {
    val pir_frac1_i = Input(Vec(VECTOR_SIZE, UInt((FRAC_WIDTH+1).W)))
    val pir_frac2_i = Input(Vec(VECTOR_SIZE, UInt((FRAC_WIDTH+1).W)))
    val pir_exp1_i  = Input(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_exp2_i  = Input(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    
    val pir_frac1_align = Output(Vec(VECTOR_SIZE, UInt(ALIGN_WIDTH.W)))
    val pir_frac2_align = Output(Vec(VECTOR_SIZE, UInt(ALIGN_WIDTH.W)))
    val pir_max_exp     = Output(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))   //for encode and fraction_normalize 
  })

  // Calculate the maximum exponent of each vector element
  for (i <- 0 until VECTOR_SIZE) {
    io.pir_max_exp(i) := Mux(io.pir_exp1_i(i) > io.pir_exp2_i(i), io.pir_exp1_i(i), io.pir_exp2_i(i))
  }

  // Align score to ALIGN_WIDTH
  val frac1_shifted = Wire(Vec(VECTOR_SIZE, UInt(ALIGN_WIDTH.W)))
  val frac2_shifted = Wire(Vec(VECTOR_SIZE, UInt(ALIGN_WIDTH.W)))

  if (ALIGN_WIDTH > FRAC_WIDTH + 1) {
    // if ALIGN_WIDTH is greater than FRAC_WIDTH, left shift the fraction, 保证前Frac_WIDTH+1位是尾数的二进制表示
    for (i <- 0 until VECTOR_SIZE) {
      frac1_shifted(i) := io.pir_frac1_i(i) << (ALIGN_WIDTH - FRAC_WIDTH - 1)
      frac2_shifted(i) := io.pir_frac2_i(i) << (ALIGN_WIDTH - FRAC_WIDTH - 1)
    }
  } else {
    // if ALIGN_WIDTH is less than FRAC_WIDTH, right shift the fraction
    for (i <- 0 until VECTOR_SIZE) {
      frac1_shifted(i) := io.pir_frac1_i(i) >> (FRAC_WIDTH + 1 - ALIGN_WIDTH)
      frac2_shifted(i) := io.pir_frac2_i(i) >> (FRAC_WIDTH + 1 - ALIGN_WIDTH)
    }
  }

    // Calculate the amount of shift required
  for (i <- 0 until VECTOR_SIZE) {
    val shift_amount1 = (io.pir_max_exp(i) - io.pir_exp1_i(i)).asUInt
    val shift_amount2 = (io.pir_max_exp(i) - io.pir_exp2_i(i)).asUInt

    // printf("shift_amount1 = %d, shift_amount2 = %d\n", shift_amount1, shift_amount2)

    when(shift_amount1 === 0.U && shift_amount2 =/= 0.U){
      io.pir_frac1_align(i) := frac1_shifted(i)
      io.pir_frac2_align(i) := frac2_shifted(i) >> shift_amount2
    }.elsewhen(shift_amount2 === 0.U && shift_amount1 =/= 0.U){
      io.pir_frac2_align(i) := frac2_shifted(i)
      io.pir_frac1_align(i) := frac1_shifted(i) >> shift_amount1
    }.elsewhen(shift_amount1 === 0.U && shift_amount2 === 0.U){
      io.pir_frac1_align(i) := frac1_shifted(i)
      io.pir_frac2_align(i) := frac2_shifted(i)
    }.otherwise {
    // Ensure shift_amount <= ALIGN_WIDTH
    io.pir_frac1_align(i) := frac1_shifted(i) >> Mux(shift_amount1 + ALIGN_WIDTH.U - FRAC_WIDTH.U - 1.U> ALIGN_WIDTH.U, ALIGN_WIDTH.U, shift_amount1 + ALIGN_WIDTH.U - FRAC_WIDTH.U - 1.U)
    io.pir_frac2_align(i) := frac2_shifted(i) >> Mux(shift_amount2 + ALIGN_WIDTH.U - FRAC_WIDTH.U - 1.U> ALIGN_WIDTH.U, ALIGN_WIDTH.U, shift_amount2 + ALIGN_WIDTH.U - FRAC_WIDTH.U - 1.U)
    }
  }
}
