//Modified radix-4 booth wallace multiplier
package pvu

import chisel3._
import chisel3.util._

class Radix4BoothMultiplier(val WIDTH_A: Int,val WIDTH_B: Int) extends Module {
  // Calculate COUNT based on given parameters
  val WIDTH_O: Int = WIDTH_A +  WIDTH_B  // Default is WIDTH_A + WIDTH_B
  val COUNT: Int   = (WIDTH_B + 2) / 2

  val io = IO(new Bundle {
    val operand_a = Input(UInt(WIDTH_A.W))
    val operand_b = Input(UInt(WIDTH_B.W))

    val sum_o   = Output(UInt(WIDTH_O.W))
    val carry_o = Output(UInt(WIDTH_O.W))
  })

  // Generate partial products
  val genProds = Module(new GenProds(WIDTH_A, WIDTH_B))
  genProds.io.operand_a := io.operand_a
  genProds.io.operand_b := io.operand_b

  // Use csa_tree to perform parallel compression of multiple partial products
  val csaTree = Module(new CsaTree(COUNT, WIDTH_O, WIDTH_O))
  csaTree.io.operands_i := genProds.io.partial_prods

  // Output and carry
  io.sum_o   := csaTree.io.sum_o
  io.carry_o := csaTree.io.carry_o

}
