// src/pvu/Comparator.scala

package pvu

import chisel3._
import chisel3.util._

/**
  * Comparator 模块，用于比较两个有符号数并输出较大的值。
  *
  * @param WIDTH 输入操作数的位宽
  */
class Comparator(val WIDTH: Int) extends Module {
  val io = IO(new Bundle {
    val operand_a = Input(SInt(WIDTH.W))  // 第一个操作数
    val operand_b = Input(SInt(WIDTH.W))  // 第二个操作数
    val result_o  = Output(SInt(WIDTH.W)) // 较大值的输出
  })

  // 使用 Mux 比较两个操作数
  io.result_o := Mux(io.operand_a > io.operand_b, io.operand_a, io.operand_b)
}

