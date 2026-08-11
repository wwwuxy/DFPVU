//Posit Vector Decode Unit
package pvu

import chisel3._
import chisel3.util._

class PositEncode(val POSIT_WIDTH: Int, val VECTOR_SIZE: Int, val ES: Int) extends Module {
  var nd: Int         = log2Ceil(POSIT_WIDTH - 1)
  var EXP_WIDTH: Int  = nd + ES + 1 
  var FRAC_WIDTH: Int = POSIT_WIDTH - ES - 3

  val io = IO(new Bundle {
    val pir_sign = Input(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_exp  = Input(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_frac = Input(Vec(VECTOR_SIZE, UInt((FRAC_WIDTH + 1).W)))
    
    val posit    = Output(Vec(VECTOR_SIZE, UInt(POSIT_WIDTH.W)))
  })

  // Extract the hidden digits of the mantissa
  val frac_hide = Wire(Vec(VECTOR_SIZE, UInt(1.W)))
  for(i <- 0 until VECTOR_SIZE){
    frac_hide(i) := io.pir_frac(i)(FRAC_WIDTH)
  }
  
  // Get the binary of es and rigime, the base of regime is 16, which is 2 to the power of 4.
  val regime_k = Wire(Vec(VECTOR_SIZE, UInt(nd.W)))
  val es_value = Wire(Vec(VECTOR_SIZE, UInt(ES.W)))
  for(i <- 0 until VECTOR_SIZE){
    regime_k(i) := io.pir_exp(i)(EXP_WIDTH - 1, ES)
    es_value(i) := io.pir_exp(i)(ES - 1, 0)
  }


  // Initialize regime
  val k_sign       = Wire(Vec(VECTOR_SIZE, UInt(1.W)))
  val regime_init  = Wire(UInt((POSIT_WIDTH - 1).W))
  val regime       = Wire(Vec(VECTOR_SIZE, UInt((POSIT_WIDTH- 1).W)))

  regime_init     := 1.U

  for(i <- 0 until VECTOR_SIZE){
    k_sign(i) := io.pir_exp(i)(EXP_WIDTH - 1)
    regime(i) := Mux(k_sign(i) === 1.U, regime_init, ~regime_init)
    // If k_sign is 1，regime is negative number，0000...1（-k），if k_sign is 0，regime is positive number，1111...0（k-1）
  }

  // Calculate regime width
  val regime_width = Wire(Vec(VECTOR_SIZE, UInt(nd.W)))
  for(i <- 0 until VECTOR_SIZE){
    regime_width(i) := Mux(k_sign(i) === 1.U, ((~regime_k(i) + 1.U) + 1.U), regime_k(i) + 2.U)
  }

  // Concatenate each part (bit-width overflow)
  var TMP_WIDTH   = POSIT_WIDTH - 1 + ES + FRAC_WIDTH
  val reg_es_frac = Wire(Vec(VECTOR_SIZE, UInt(TMP_WIDTH.W)))
  for(i <- 0 until VECTOR_SIZE){
    reg_es_frac(i) := Cat(regime(i), es_value(i), io.pir_frac(i)(FRAC_WIDTH - 1, 0))
  }

  // Perform a right shift operation (first left shift, then right shift)
  var MAX_SHIFT   = FRAC_WIDTH + ES + 1
  var SHIFT_WIDTH = log2Ceil(MAX_SHIFT)
  val shift = Wire(Vec(VECTOR_SIZE, UInt(SHIFT_WIDTH.W)))

  for(i <- 0 until VECTOR_SIZE){
    shift(i) := Mux(regime_width(i) >= POSIT_WIDTH.U, MAX_SHIFT.U, regime_width(i) + FRAC_WIDTH.U + ES.U  - POSIT_WIDTH.U + 1.U)
  }
  //If regime bits >= n, it means the regime occupies all bits. At this point, the right shift count is the maximum right shift count. After two shifts, the posit is entirely regime bits, and the bit width is n bits.
  //If regime_bits < n, it indicates that rounding is needed for the lower bits of mant after the right shift. The number of bits to be rounded is the shift amount, denoted as x. reg + esp + mant - x = n - 1, x = reg + esp + mant - n + 1
  //The calculation of the amount of data shifted to the right is to make the lower MAX_SHIFT_AMOUNT bits after the right shift become rounding bits, and the n bits above them are valid data bits.

  val value_before_shift = Wire(Vec(VECTOR_SIZE, UInt(TMP_WIDTH.W + MAX_SHIFT.W)))
  val value_after_shift  = Wire(Vec(VECTOR_SIZE, UInt(TMP_WIDTH.W + MAX_SHIFT.W)))

  for(i <- 0 until VECTOR_SIZE){
    value_before_shift(i) := reg_es_frac(i) << MAX_SHIFT
    val barrel_shifter = Module(new BarrelShifter(TMP_WIDTH + MAX_SHIFT, SHIFT_WIDTH, true))
    barrel_shifter.io.operand_i    := value_before_shift(i)
    barrel_shifter.io.shift_amount := shift(i)
    value_after_shift(i)           := barrel_shifter.io.result_o
  }

  // Perform rounding operation  --> RNE rounding
  val value_before_round = Wire(Vec(VECTOR_SIZE, UInt((POSIT_WIDTH - 1).W)))
  val round_bits         = Wire(Vec(VECTOR_SIZE, UInt(MAX_SHIFT.W)))
  val value_after_round  = Wire(Vec(VECTOR_SIZE, UInt((POSIT_WIDTH - 1).W)))

  for(i <- 0 until VECTOR_SIZE){
   value_before_round(i) := value_after_shift(i)(MAX_SHIFT + POSIT_WIDTH - 2, MAX_SHIFT)
   round_bits(i)         := value_after_shift(i)(MAX_SHIFT - 1, 0)

   val round_bit         = round_bits(i)(MAX_SHIFT - 1)
   val sticky_bit        = round_bits(i)(MAX_SHIFT - 2, 0).orR
   val round_value       = round_bit & (sticky_bit | value_before_round(i)(0))
   value_after_round(i) := value_before_round(i) + round_value
  }

  // OutputPosit --> ConvertToComplement
  for(i <- 0 until VECTOR_SIZE){
    val result   = Wire(UInt(POSIT_WIDTH.W))
    
    // 检测NaR条件：符号位为1且尾数为0
    val isNaR = (io.pir_sign(i) === 1.U) && (io.pir_frac(i) === 0.U)
    
    // 检测1.0条件：符号位为0，指数为0，尾数的隐藏位为1，其余尾数位为0
    val isOne = (io.pir_sign(i) === 0.U) && (io.pir_exp(i) === 0.S) && 
                (io.pir_frac(i)(FRAC_WIDTH) === 1.U) && (io.pir_frac(i)(FRAC_WIDTH-1, 0) === 0.U)
    
    result      := Mux(io.pir_sign(i) === 1.U, Cat(1.U, ~value_after_round(i) + 1.U), Cat(0.U, value_after_round(i)))
    
    // 特殊情况处理：
    // 1. 检测到NaR时，输出80000000
    // 2. 检测到1.0时，直接输出40000000
    // 3. 否则，根据frac_hide决定输出result或0
    when(isNaR) {
      io.posit(i) := (BigInt(1) << (POSIT_WIDTH-1)).U
    }.elsewhen(isOne) {
      io.posit(i) := (BigInt(1) << (POSIT_WIDTH-2)).U  // 40000000
    }.otherwise {
      io.posit(i) := Mux(frac_hide(i) === 1.U, result, 0.U)
    }
  }
}


