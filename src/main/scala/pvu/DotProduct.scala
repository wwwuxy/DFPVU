//Posit Dot_Product Unit
package pvu

import chisel3._
import chisel3.util._

class DotProduct(val POSIT_WIDTH: Int, val VECTOR_SIZE: Int, val ALIGN_WIDTH: Int,  val ES :Int) extends Module {
  var nd: Int         = log2Ceil(POSIT_WIDTH - 1)
  var EXP_WIDTH: Int  = nd + ES + 1
  var FRAC_WIDTH: Int = POSIT_WIDTH - ES - 3
  var MUL_WIDTH: Int  = 2 * (FRAC_WIDTH + 1)
  val SUM_WIDTH: Int  = MUL_WIDTH + log2Ceil(VECTOR_SIZE)

  val io = IO(new Bundle {
    val pir_sign1_i = Input(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_sign2_i = Input(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_exp1_i  = Input(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_exp2_i  = Input(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_frac1_i = Input(Vec(VECTOR_SIZE, UInt((FRAC_WIDTH+1).W)))
    val pir_frac2_i = Input(Vec(VECTOR_SIZE, UInt((FRAC_WIDTH+1).W)))

    val pir_sign_o = Output(UInt(1.W))
    val pir_exp_o  = Output(SInt(EXP_WIDTH.W))
    val pir_frac_o = Output(UInt((SUM_WIDTH+1).W))
  })

// Perform cumulative multiplication using mul.
  val pir_sign_mul = Wire(Vec(VECTOR_SIZE, UInt(1.W)))
  val pir_exp_mul  = Wire(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
  val pir_frac_mul = Wire(Vec(VECTOR_SIZE, UInt(MUL_WIDTH.W)))

  val mul = Module(new Mul(POSIT_WIDTH, VECTOR_SIZE, ALIGN_WIDTH, ES))
  mul.io.pir_sign1_i := io.pir_sign1_i
  mul.io.pir_sign2_i := io.pir_sign2_i
  mul.io.pir_exp1_i  := io.pir_exp1_i
  mul.io.pir_exp2_i  := io.pir_exp2_i
  mul.io.pir_frac1_i := io.pir_frac1_i
  mul.io.pir_frac2_i := io.pir_frac2_i

  pir_sign_mul := mul.io.pir_sign_o
  pir_exp_mul  := mul.io.pir_exp_o
  pir_frac_mul := mul.io.pir_frac_o

  for(i <- 0 until VECTOR_SIZE){
    when((io.pir_exp1_i(i) === 0.S && io.pir_frac1_i(i) === 0.U) || (io.pir_exp2_i(i) === 0.S && io.pir_frac2_i(i) === 0.U)) {
      pir_frac_mul(i) := 0.U
      pir_exp_mul(i) := 0.S
    }
  }

// Align the mantissa.
  val pir_exp_cmp  = Wire(SInt(EXP_WIDTH.W))
  val pir_frac_cmp = Wire(Vec(VECTOR_SIZE, UInt(MUL_WIDTH.W)))

  val frac_compare            = Module(new FractionAlignment_DotProduct(POSIT_WIDTH, VECTOR_SIZE, ALIGN_WIDTH, ES))
  frac_compare.io.pir_exp_i  := pir_exp_mul
  frac_compare.io.pir_frac_i := pir_frac_mul
  pir_exp_cmp                := frac_compare.io.pir_max_exp
  pir_frac_cmp               := frac_compare.io.pir_frac_align

// Convert aligned negative mantissas to explicitly sized two's-complement operands.
  val pir_frac_cmp_signed = Wire(Vec(VECTOR_SIZE, SInt(SUM_WIDTH.W)))
  for (i <- 0 until VECTOR_SIZE) {
    val alignedMagnitude = pir_frac_cmp(i).pad(SUM_WIDTH).asSInt
    pir_frac_cmp_signed(i) := Mux(pir_sign_mul(i) === 1.U, -alignedMagnitude, alignedMagnitude)
  }

// Accumulation through the CSA tree
  val sum          = Wire(UInt(SUM_WIDTH.W))
  val carry        = Wire(UInt(SUM_WIDTH.W))
  val reducedSum   = Wire(SInt(SUM_WIDTH.W))
  val sumMagnitude = Wire(UInt(SUM_WIDTH.W))

  val csaTree = Module(new CsaTree(VECTOR_SIZE, SUM_WIDTH, SUM_WIDTH))
  csaTree.io.operands_i := VecInit(pir_frac_cmp_signed.map(_.asUInt))
  sum                   := csaTree.io.sum_o
  carry                 := csaTree.io.carry_o
  // Convert the reduced two's-complement value back to sign and magnitude.
  reducedSum   := (carry + sum).asSInt
  sumMagnitude := Mux(reducedSum < 0.S, -reducedSum, reducedSum).asUInt

// Output result
  io.pir_sign_o := reducedSum < 0.S
  io.pir_exp_o  := Mux(sumMagnitude === 0.U, 0.S, pir_exp_cmp)
  io.pir_frac_o := sumMagnitude.pad(SUM_WIDTH + 1)
}
