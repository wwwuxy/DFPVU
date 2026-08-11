//Posit Vector Add
package pvu

import chisel3._
import chisel3.util._

class Add(val POSIT_WIDTH: Int, val VECTOR_SIZE: Int, val ALIGN_WIDTH: Int, val ES :Int) extends Module {
  var nd: Int         = log2Ceil(POSIT_WIDTH - 1)
  var EXP_WIDTH: Int  = nd + ES + 1 

  val io = IO(new Bundle {
    val pir_sign1_i       = Input(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_sign2_i       = Input(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_exp1_i        = Input(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_exp2_i        = Input(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_frac1_aligned = Input(Vec(VECTOR_SIZE, UInt(ALIGN_WIDTH.W)))
    val pir_frac2_aligned = Input(Vec(VECTOR_SIZE, UInt(ALIGN_WIDTH.W)))

    val pir_sign_o    = Output(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_exp_o     = Output(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_frac_o    = Output(Vec(VECTOR_SIZE, UInt(ALIGN_WIDTH.W)))
    val overflow      = Output(Vec(VECTOR_SIZE, UInt(1.W)))  // Overflow flag
    val frac_truncate = Output(Vec(VECTOR_SIZE, UInt(1.W)))  // Fraction_Truncate flag
  })

// Check the sign bits. If they are the same, perform addition; if they are different, perform subtraction.
  val diff_sign = Wire(Vec(VECTOR_SIZE, UInt(1.W)))
  for (i <- 0 until VECTOR_SIZE) {
    diff_sign(i) := Mux(io.pir_sign1_i(i) === io.pir_sign2_i(i), 0.U, 1.U)
  }

// Perform addition operation
  for (i <- 0 until VECTOR_SIZE) {
  when (diff_sign(i) === 0.U) { // The same sign, perform addition
    // Perform addition with carry
    val sum   = io.pir_frac1_aligned(i) +& io.pir_frac2_aligned(i)
    val carry = sum(ALIGN_WIDTH)                                     // Check carry

    //If there is a carry, shift the mantissa to the right by one bit and increment index by one
    val new_frac = Mux(carry, sum >> 1, sum(ALIGN_WIDTH - 1, 0))
    val new_exp  = Mux(carry, io.pir_exp1_i(i) + 1.S, io.pir_exp1_i(i))

    // Assignment result symbol, exponent, and mantissa
    io.pir_sign_o(i) := io.pir_sign1_i(i)
    io.pir_exp_o(i)  := new_exp
    io.pir_frac_o(i) := new_frac

    // Set overflow flag
    io.overflow(i) := carry

    // Set the mantissa truncation flag: if there is a carry and there is a 1 in the mantissa
    io.frac_truncate(i) := (carry.asBool && sum(ALIGN_WIDTH - 1, 0).orR).asUInt
  } .otherwise { // The symbols are different, perform subtraction.
    // Determine which suffix is larger
    val mant1         = io.pir_frac1_aligned(i)
    val mant2         = io.pir_frac2_aligned(i)
    val mant1_greater = (mant1 > mant2)

    // Calculate the difference in the last digit
    val mant_diff = Mux(mant1_greater, mant1 - mant2, mant2 - mant1)
    io.pir_frac_o(i) := mant_diff

    // Determine the sign of the result
    val res_sign = Mux(mant1_greater, io.pir_sign1_i(i), io.pir_sign2_i(i))
    io.pir_sign_o(i) := res_sign

    // Subtraction exponent remains unchanged
    io.pir_exp_o(i)     := io.pir_exp1_i(i)
    io.overflow(i)      := 0.U
    io.frac_truncate(i) := 0.U
    }
  }
}
