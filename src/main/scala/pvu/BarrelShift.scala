package pvu

import chisel3._
import chisel3.util._

/**
  * Barrel Shifter 模块
  *
  * WIDTH        输入数据的位宽
  * SHIFT_WIDTH  移位量的位宽
  * MODE         移位模式，false 表示左移，true 表示右移
  */
class BarrelShifter(val WIDTH: Int, val SHIFT_WIDTH: Int, val MODE: Boolean) extends Module {
  val io = IO(new Bundle {
    val operand_i    = Input(UInt(WIDTH.W))         // 输入操作数
    val shift_amount = Input(UInt(SHIFT_WIDTH.W))   // 移位量

    val result_o = Output(UInt(WIDTH.W))  // 移位结果
  })

  // 初始化 temp_results 为一个包含 SHIFT_WIDTH 个 WIDTH 位的向量
  val temp_results = Wire(Vec(SHIFT_WIDTH, UInt(WIDTH.W)))

  // 设置最右侧的 temp_results 为输入操作数
  temp_results(SHIFT_WIDTH - 1) := io.operand_i

  // 根据 MODE 参数选择左移或右移
  for (i <- (1 to SHIFT_WIDTH - 1).reverse) {
    val shift_val = 1 << i
    if (!MODE) {
      // 左移逻辑
      temp_results(i - 1) := Mux(io.shift_amount(i), temp_results(i) << shift_val, temp_results(i))
    } else {
      // 右移逻辑
      temp_results(i - 1) := Mux(io.shift_amount(i), temp_results(i) >> shift_val, temp_results(i))
    }
  }

  // 最终的移位操作，处理最低位的移位量
  if (!MODE) {
    // 左移
    io.result_o := Mux(io.shift_amount(0), temp_results(0) << 1, temp_results(0))
  } else {
    // 右移
    io.result_o := Mux(io.shift_amount(0), temp_results(0) >> 1, temp_results(0))
  }
}

