//Posit Encode Unit For DotProduct
package pvu

import chisel3._
import chisel3.util._

class PositEncode_DotProduct(val POSIT_WIDTH: Int, val ES: Int) extends Module {
  var nd: Int         = log2Ceil(POSIT_WIDTH - 1)
  var EXP_WIDTH: Int  = nd + ES + 1
  var FRAC_WIDTH: Int = POSIT_WIDTH - ES - 3

  val io = IO(new Bundle {
    val pir_sign = Input(UInt(1.W))
    val pir_exp  = Input(SInt(EXP_WIDTH.W))
    val pir_frac = Input(UInt(FRAC_WIDTH.W + 1.W))
    
    val posit = Output(UInt(POSIT_WIDTH.W))
  })

    // Extract the hidden digits of the mantissa
  val frac_hide  = Wire(UInt(1.W))
      frac_hide := io.pir_frac(FRAC_WIDTH)
  
  // Get the binary of es and rigime, the base of regime is 16, which is 2 to the power of 4.
  val regime_k  = Wire(UInt(nd.W))
  val es_value  = Wire(UInt(ES.W))
      regime_k := io.pir_exp(EXP_WIDTH - 1, ES)
      es_value := io.pir_exp(ES - 1, 0)

  // Initialize regime
  val k_sign      = Wire(UInt(1.W))
  val regime_init = Wire(UInt((POSIT_WIDTH - 1).W))
  val regime      = Wire(UInt((POSIT_WIDTH - 1).W))

  regime_init := 1.U

  k_sign := io.pir_exp(EXP_WIDTH - 1)
  regime := Mux(k_sign === 1.U, regime_init, ~regime_init)
    // If k_sign is 1，regime is negative number，0000...1（-k），if k_sign is 0，regime is positive number，1111...0（k-1）

    // Calculate regime width
  val regime_width  = Wire(UInt(nd.W))
      regime_width := Mux(k_sign === 1.U, ((~regime_k + 1.U) + 1.U), regime_k + 2.U)

    // Concatenate each part (bit-width overflow)
  var TMP_WIDTH    = POSIT_WIDTH - 1 + ES + FRAC_WIDTH
  val reg_es_frac  = Wire(UInt(TMP_WIDTH.W))
      reg_es_frac := Cat(regime, es_value, io.pir_frac(FRAC_WIDTH - 1, 0))

  // Perform a right shift operation (first left shift, then right shift)
  var MAX_SHIFT   = FRAC_WIDTH + ES + 1
  var SHIFT_WIDTH = log2Ceil(MAX_SHIFT)
  val shift       = Wire(UInt(SHIFT_WIDTH.W))

  shift := Mux(regime_width >= POSIT_WIDTH.U, MAX_SHIFT.U, regime_width + FRAC_WIDTH.U + ES.U  - POSIT_WIDTH.U + 1.U)
  //If regime bits >= n, it means the regime occupies all bits. At this point, the right shift count is the maximum right shift count. After two shifts, the posit is entirely regime bits, and the bit width is n bits.
  //If regime_bits < n, it indicates that rounding is needed for the lower bits of mant after the right shift. The number of bits to be rounded is the shift amount, denoted as x. reg + esp + mant - x = n - 1, x = reg + esp + mant - n + 1
  //The calculation of the amount of data shifted to the right is to make the lower MAX_SHIFT_AMOUNT bits after the right shift become rounding bits, and the n bits above them are valid data bits.

  val value_before_shift = Wire(UInt(TMP_WIDTH.W + MAX_SHIFT.W))
  val value_after_shift  = Wire(UInt(TMP_WIDTH.W + MAX_SHIFT.W))

      value_before_shift             := reg_es_frac << MAX_SHIFT
  val barrel_shifter                  = Module(new BarrelShifter(TMP_WIDTH + MAX_SHIFT, SHIFT_WIDTH, true))
      barrel_shifter.io.operand_i    := value_before_shift
      barrel_shifter.io.shift_amount := shift
      value_after_shift              := barrel_shifter.io.result_o


  // Perform rounding operation  --> RNE rounding
  val value_before_round = Wire(UInt((POSIT_WIDTH - 1).W))
  val round_bits         = Wire(UInt(MAX_SHIFT.W))
  val value_after_round  = Wire(UInt((POSIT_WIDTH - 1).W))


  value_before_round := value_after_shift(MAX_SHIFT + POSIT_WIDTH - 2, MAX_SHIFT)
  round_bits         := value_after_shift(MAX_SHIFT - 1, 0)

  val round_bit          = round_bits(MAX_SHIFT - 1)
  val sticky_bit         = round_bits(MAX_SHIFT - 2, 0).orR
  val round_value        = round_bit & (sticky_bit | value_before_round(0))
      value_after_round := value_before_round + round_value

  // OutputPosit --> ConvertToComplement
  val result = Wire(UInt(POSIT_WIDTH.W))

  result   := Mux(io.pir_sign === 1.U, Cat(1.U, ~value_after_round + 1.U), Cat(0.U, value_after_round))

when(io.pir_exp === 0.S && io.pir_frac === 0.U) {
  io.posit := 0.U
}.otherwise {
  io.posit := result
}

}


