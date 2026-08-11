// src/pvu/CompTree.scala
package pvu

import chisel3._
import chisel3.util._

/**
  * 递归比较器树模块，用于在任意大小的输入中找到最大值。
  *
  * @param N      输入的数量
  * @param WIDTH 每个输入的位宽
  */
class CompTree(val N: Int, val WIDTH: Int) extends Module {
  // 添加参数限制
  private val LIMITED_N = Math.min(N, 16)      // 限制最大输入数量为16
  private val LIMITED_WIDTH = Math.min(WIDTH, 64) // 限制最大位宽为64
  
  val io = IO(new Bundle {
    val operands_i = Input(Vec(LIMITED_N, SInt(LIMITED_WIDTH.W))) // N 个有符号输入
    val result_o   = Output(SInt(LIMITED_WIDTH.W))        // 最大值的输出
  })

  if (LIMITED_N == 1) {
    // 基本情况：只有一个输入，直接赋值给输出
    io.result_o := io.operands_i(0)
  } else if (LIMITED_N == 2) {
    // 基本情况：两个输入，使用 Comparator 模块比较
    val comparator = Module(new Comparator(LIMITED_WIDTH))
    comparator.io.operand_a := io.operands_i(0)
    comparator.io.operand_b := io.operands_i(1)
    io.result_o := comparator.io.result_o
  } else {
    // 递归情况：将输入分成两部分，分别递归比较，然后再比较两部分的最大值
    val N_A = LIMITED_N / 2
    val N_B = LIMITED_N - N_A

    // 分割输入为两部分
    val operands_i_A = io.operands_i.slice(0, N_A)
    val operands_i_B = io.operands_i.slice(N_A, LIMITED_N)

    // 递归实例化 CompTree 模块
    val compTreeA = Module(new CompTree(N_A, LIMITED_WIDTH))
    compTreeA.io.operands_i := operands_i_A

    val compTreeB = Module(new CompTree(N_B, LIMITED_WIDTH))
    compTreeB.io.operands_i := operands_i_B

    // 比较两部分的最大值
    val comparator = Module(new Comparator(LIMITED_WIDTH))
    comparator.io.operand_a := compTreeA.io.result_o
    comparator.io.operand_b := compTreeB.io.result_o

    io.result_o := comparator.io.result_o
  }
}

