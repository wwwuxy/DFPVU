package pvu

import chisel3._
import chisel3.util._
import chisel3.experimental._

class LZC(val WIDTH: Int, val MODE: Boolean, val nd: Int) extends BlackBox(Map(
  "WIDTH" -> WIDTH,
  "MODE"  -> (if (MODE) 1 else 0)
)) with HasBlackBoxResource {
  val io = IO(new Bundle {
    val in_i    = Input(UInt(WIDTH.W))
    val cnt_o   = Output(UInt(nd.W))
    val empty_o = Output(Bool())
  })
  addResource("/pvu/lzc.sv")
}
