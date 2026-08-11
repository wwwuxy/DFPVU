/**
  * BoothEncoder 模块实现了 Radix-4 Booth 编码逻辑。
  * 
  * 输入:
  * - code: 3 位操作码
  * 
  * 输出:
  * - neg: 是否为负操作 (-1*A 或 -2*A)
  * - zero: 是否为零操作 (0)
  * - one: 是否为 +1*A 或 -1*A
  * - two: 是否为 +2*A 或 -2*A
  */
package pvu

import chisel3._
import chisel3.util._
/*
The radix-4 booth encoding is as follows:
    code | value | operation
    000  |  +0   |     0
    001  |  +1   |    1*A
    010  |  +1   |    1*A
    011  |  +2   |    2*A
    100  |  -2   |   -2*A
    101  |  -1   |   -1*A
    110  |  -1   |   -1*A
    111  |   0   |     0
*/

class BoothEncoder extends Module {
  val io = IO(new Bundle {
    val code = Input(UInt(3.W))  // 3 位操作码
    val neg  = Output(Bool())    // 负操作指示
    val zero = Output(Bool())    // 零操作指示
    val one  = Output(Bool())    // +1*A 或 -1*A 指示
    val two  = Output(Bool())    // +2*A 或 -2*A 指示 左移一位表示乘2
  })

  // 先将所有输出信号初始化为 false
  io.neg  := false.B
  io.zero := false.B
  io.one  := false.B
  io.two  := false.B

  switch (io.code) {
    is("b000".U) {
      io.zero := true.B          // 0
    }
    is("b001".U) {
      io.one  := true.B          // +1
    }
    is("b010".U) {
      io.one  := true.B          // +1
    }
    is("b011".U) {
      io.two  := true.B          // +2
    }
    is("b100".U) {
      io.neg  := true.B          // 负
      io.two  := true.B          // -2
    }
    is("b101".U) {
      io.neg  := true.B          // 负
      io.one  := true.B          // -1
    }
    is("b110".U) {
      io.neg  := true.B          // 负
      io.one  := true.B          // -1
    }
    is("b111".U) {
      io.zero := true.B          // 0
    }
  }
}
