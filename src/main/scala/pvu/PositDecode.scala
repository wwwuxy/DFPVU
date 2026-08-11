package pvu

import chisel3._
import chisel3.util._
import chisel3.experimental._

class PositDecode(val POSIT_WIDTH: Int, val VECTOR_SIZE: Int, val ES: Int) extends Module {
  var nd: Int         = log2Ceil(POSIT_WIDTH - 1)
  var EXP_WIDTH: Int  = nd + ES + 1 
  var FRAC_WIDTH: Int = POSIT_WIDTH - ES - 3

  val io = IO(new Bundle {
    val posit = Input(Vec(VECTOR_SIZE, UInt(POSIT_WIDTH.W)))
    
    val Sign = Output(Vec(VECTOR_SIZE, UInt(1.W)))
    val Exp  = Output(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val Frac = Output(Vec(VECTOR_SIZE, UInt((FRAC_WIDTH+1).W)))
  })

  // 检测NaR (Not a Real) - 80000000 (最高位为1，其余位为0)
  val isNaR = Wire(Vec(VECTOR_SIZE, Bool()))
  for (i <- 0 until VECTOR_SIZE) {
    isNaR(i) := (io.posit(i)(POSIT_WIDTH-1) === 1.U) && (io.posit(i)(POSIT_WIDTH-2, 0) === 0.U)
  }

  // Handling symbols and operand 将负数转换为补码
  val operand = Wire(Vec(VECTOR_SIZE, UInt((POSIT_WIDTH - 1).W))) // Remove the sign bit
  for (i <- 0 until VECTOR_SIZE) {
    io.Sign(i) := io.posit(i)(POSIT_WIDTH - 1)
    operand(i) := Mux(io.Sign(i) === 1.U, (~io.posit(i)(POSIT_WIDTH - 2, 0) + 1.U), io.posit(i)(POSIT_WIDTH - 2, 0))
  }

  // Count leading zeros
  val R0          = Wire(Vec(VECTOR_SIZE, UInt(1.W)))
  val lzc_operand = Wire(Vec(VECTOR_SIZE, UInt((POSIT_WIDTH - 1).W)))
  val lzc         = Wire(Vec(VECTOR_SIZE, UInt(nd.W)))
  val lzc_zeros   = Wire(Vec(VECTOR_SIZE, Bool()))

  for (i <- 0 until VECTOR_SIZE) {
    R0(i)          := operand(i)(POSIT_WIDTH - 2)
    lzc_operand(i) := Mux(R0(i) === 1.U, ~operand(i), operand(i))
  }

  // Instantiate LZC module for each vector element
  for (i <- 0 until VECTOR_SIZE) {
    val lzcModule = Module(new LZC(POSIT_WIDTH - 1, true, nd))
    lzcModule.io.in_i := lzc_operand(i)
    lzc(i)            := lzcModule.io.cnt_o
    lzc_zeros(i)      := lzcModule.io.empty_o
  }

  // printf("lzc[0] = %d, lzc_zeros[0] = %d\n", lzc(0), lzc_zeros(0))

  // Get the regime value
  val same_length = Wire(Vec(VECTOR_SIZE, UInt(nd.W)))
  val regime_r    = Wire(Vec(VECTOR_SIZE, SInt((nd + 1).W)))

  for (i <- 0 until VECTOR_SIZE) {
    same_length(i) := lzc(i)
    when(lzc_zeros(i) === 1.U) {
      regime_r(i)    := 0.S
    }.otherwise {
      regime_r(i)    := (Mux(R0(i) === 1.U, Cat(0.U, lzc(i) - 1.U), Cat(1.U, ~lzc(i) + 1.U))).asSInt
    }
    // printf("lzc_zeros[%d] = %d, lzc[%d] = %d, same_length[%d] = %d, regime_r[%d] = %d\n", i.U, lzc_zeros(i), i.U, lzc(i), i.U, same_length(i), i.U, regime_r(i))
  }

  // Left shift the regime
  val operand_after_shift = Wire(Vec(VECTOR_SIZE, UInt((POSIT_WIDTH - 1).W)))

  for (i <- 0 until VECTOR_SIZE) {
    val barrel_shifter = Module(new BarrelShifter(POSIT_WIDTH - 1, nd, false))
    barrel_shifter.io.operand_i    := operand(i)
    barrel_shifter.io.shift_amount := same_length(i) + 1.U
    operand_after_shift(i)         := barrel_shifter.io.result_o
  }

  // Get the exponent value
  val es_value = Wire(Vec(VECTOR_SIZE, UInt(2.W)))
  for (i <- 0 until VECTOR_SIZE) {
    when(lzc_zeros(i) === true.B) {
      es_value(i) := 0.U
    }.otherwise {
      es_value(i) := operand_after_shift(i)(POSIT_WIDTH - 2, POSIT_WIDTH - 3)
    }
    
    // 根据是否为NaR设置指数值
    when(isNaR(i)) {
      io.Exp(i) := 0.S  // NaR的指数为0
    }.otherwise {
      io.Exp(i) := Cat(regime_r(i), es_value(i)).asSInt
    }
    // printf("es_value[%d] = %b, regime_r[%d] = %d, io.Exp[%d] = %b\n", i.U, es_value(i), i.U, regime_r(i), i.U, io.Exp(i))
  }

  // printf("es_value[0] = %b, io.Exp[0] = %b\n", es_value(0), io.Exp(0))

  // Extract the fraction
  val implicit_bits = Wire(Vec(VECTOR_SIZE, UInt(1.W)))
  for (i <- 0 until VECTOR_SIZE) {
    implicit_bits(i) := operand(i)(POSIT_WIDTH - 2, 0).orR // |operand_after_shift(i)(POSIT_WIDTH - 2 - es, 2)
    
    // 根据是否为NaR设置尾数值
    when(isNaR(i)) {
      io.Frac(i) := 0.U  // NaR的尾数为0
    }.otherwise {
      io.Frac(i) := Cat(implicit_bits(i), operand_after_shift(i)(POSIT_WIDTH - 2 - ES, 2))
    }
  }

  // printf("implicit_bits(0) = %d, io.Frac(0) = %b\n", implicit_bits(0), io.Frac(0));
}
