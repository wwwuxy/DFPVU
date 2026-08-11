// CsaTree 
package pvu

import chisel3._
import chisel3.util._

class CsaTree(val N: Int, val WIDTH_I: Int, val WIDTH_O: Int) extends Module {
  val io = IO(new Bundle {
    val operands_i = Input(Vec(N, UInt(WIDTH_I.W)))

    val sum_o   = Output(UInt(WIDTH_O.W))
    val carry_o = Output(UInt(WIDTH_O.W))
  })

  // Calculate the split point
  var N_A = N / 2
  var N_B = N - N_A

  if (N == 1) {
    // N == 1，Direct output
    io.sum_o   := io.operands_i(0)
    io.carry_o := 0.U
  }
  else if (N == 2) {
    // N == 2，sum_o = operands_i[0], carry_o = operands_i[1]
    io.sum_o   := io.operands_i(0)
    io.carry_o := io.operands_i(1)
  }
  else if (N == 3) {
    // N == 3，use Compressor3to2
    val compressor = Module(new Compressor3to2(WIDTH_I, WIDTH_O))
    compressor.io.operands_i := io.operands_i
    io.sum_o                 := compressor.io.sum_o
    io.carry_o               := compressor.io.carry_o
  }
  else if (N == 4) {
    // N == 4，use Compressor4to2
    val compressor = Module(new Compressor4to2(WIDTH_I, WIDTH_O))
    compressor.io.operands_i := io.operands_i
    io.sum_o                 := compressor.io.sum_o
    io.carry_o               := compressor.io.carry_o
  }
  else {
    // N > 4，Recursive Partitioning and Compression
    val operands_i_A = Wire(Vec(N_A, UInt(WIDTH_I.W)))
    val operands_i_B = Wire(Vec(N_B, UInt(WIDTH_I.W)))
    val sum_o_A      = Wire(UInt(WIDTH_O.W))
    val carry_o_A    = Wire(UInt(WIDTH_O.W))
    val sum_o_B      = Wire(UInt(WIDTH_O.W))
    val carry_o_B    = Wire(UInt(WIDTH_O.W))

    for (i <- 0 until N_A) {
      operands_i_A(i) := io.operands_i(i)
    }

    for (i <- 0 until N_B) {
      operands_i_B(i) := io.operands_i(i + N_A)
    }

    // Create two sub-CSA trees
    val csa_tree_A = Module(new CsaTree(N_A, WIDTH_I, WIDTH_O))
    csa_tree_A.io.operands_i := operands_i_A
    sum_o_A                  := csa_tree_A.io.sum_o
    carry_o_A                := csa_tree_A.io.carry_o

    val csa_tree_B = Module(new CsaTree(N_B, WIDTH_I, WIDTH_O))
    csa_tree_B.io.operands_i := operands_i_B
    sum_o_B                  := csa_tree_B.io.sum_o
    carry_o_B                := csa_tree_B.io.carry_o

    // Use the outputs of two CSA trees as inputs to Compressor4to2
    val operands_i_C = Wire(Vec(4, UInt(WIDTH_O.W)))
    operands_i_C(0) := sum_o_A
    operands_i_C(1) := carry_o_A
    operands_i_C(2) := sum_o_B
    operands_i_C(3) := carry_o_B

    // use Compressor4to2 
    val compressor = Module(new Compressor4to2(WIDTH_O, WIDTH_O)) //The output of the previous round is the input of this round.
    compressor.io.operands_i := operands_i_C
    io.sum_o                 := compressor.io.sum_o
    io.carry_o               := compressor.io.carry_o
  }
}

