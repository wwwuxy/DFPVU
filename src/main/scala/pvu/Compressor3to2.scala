package pvu
import chisel3._
import chisel3.util._

/** 1bit 全加器 **/
class FullAdder extends Module {
  val io = IO(new Bundle {
    val x     = Input(Bool())
    val y     = Input(Bool())
    val z     = Input(Bool())
    val sum   = Output(Bool())
    val carry = Output(Bool())
  })

  // 全加器逻辑
  io.sum   := (io.x ^ io.y) ^ io.z
  io.carry := ((io.x ^ io.y) & io.z) | (io.x & io.y)
}

/** 3:2 压缩器，内部实例化多个 fulladder */
class Compressor3to2(val WIDTH_I: Int, val WIDTH_O: Int) extends Module {
  val io = IO(new Bundle {
    val operands_i = Input(Vec(3, UInt(WIDTH_I.W)))

    val sum_o   = Output(UInt(WIDTH_O.W))
    val carry_o = Output(UInt(WIDTH_O.W))
  })

  // 声明线网，用来存放逐位 fulladder 的计算结果
  val sumBits   = Wire(Vec(WIDTH_I, Bool()))
  val carryBits = Wire(Vec(WIDTH_I, Bool()))

  // 逐位实例化 fulladder
  for(i <- 0 until WIDTH_I) {
    val fa = Module(new FullAdder)
    fa.io.x := io.operands_i(0)(i)
    fa.io.y := io.operands_i(1)(i)
    fa.io.z := io.operands_i(2)(i)

    sumBits(i)   := fa.io.sum
    carryBits(i) := fa.io.carry
  }

  // sum_o 直接拼成 UInt
  io.sum_o := sumBits.asUInt
  // carry_o 首尾各补一位 0
  io.carry_o := Cat(0.U(1.W), carryBits.asUInt, 0.U(1.W))
}
