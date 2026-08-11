/**
  * The GenProds module generates multiple partial products based on Radix-4 Booth encoding.
  *
  * Parameter:
  * - WIDTH_A: Bit width of multiplicand A
  * - WIDTH_B: Bit width of multiplier B
  * - COUNT: The generated partial product quantity, the calculation formula is (WIDTH_B + 2) / 2
  * - WIDTH_O: Output the bit width of the partial product, the calculation formula is WIDTH_A + WIDTH_B
  *
  * 输入:
  * - operand_a: Multiplicand A
  * - operand_b: Multiplier B
  *
  * 输出:
  * - partial_prods: Generated partial sum
 **/
package pvu

import chisel3._
import chisel3.util._


class GenProds(val WIDTH_A: Int,val WIDTH_B: Int) extends Module {
  var COUNT: Int   = (WIDTH_B + 2) / 2
  var WIDTH_O: Int = WIDTH_A + WIDTH_B
  
  val io = IO(new Bundle {
    val operand_a     = Input(UInt(WIDTH_A.W))               // Multiplicand A
    val operand_b     = Input(UInt(WIDTH_B.W))               // Multiplier B
    val partial_prods = Output(Vec(COUNT, UInt(WIDTH_O.W)))  // Generated partial sum
  })

// Define internal signals and perform Booth encoding on multiplier b.
  val multiplier  = Wire(UInt((WIDTH_B + 3).W))
      multiplier := Cat(0.U(2.W), io.operand_b, 0.U(1.W))  // {2'b0, operand_b, 1'b0} --> Must be an odd width

  val codes      = Wire(Vec(COUNT, UInt(3.W)))
  val temp_prods = Wire(Vec(COUNT, UInt((WIDTH_A + 1).W)))  // Partial product bit-width is WIDTH_A + 1
  val signs      = Wire(Vec(COUNT, Bool()))

  //The first code takes the lower 3 bits of the multiplier
  codes(0) := multiplier(2, 0)

  // Instantiate a GenProduct and use codes(0) to generate the first partial product.
  val genProdFirst = Module(new GenProduct(WIDTH = WIDTH_A))
  genProdFirst.io.multiplicand := io.operand_a
  genProdFirst.io.code         := codes(0)
  temp_prods(0)                := genProdFirst.io.partial_prod
  signs(0)                     := genProdFirst.io.sign

  // Perform sign extension, concatenated { ~signs(0), signs(0), signs(0), temp_prods(0) }
  io.partial_prods(0) := Cat(~signs(0), signs(0), signs(0), temp_prods(0))


  // Generate partial products for the middle (i = 1 to COUNT - 2)
  for (i <- 1 until COUNT - 1) {
    // Intercept multiplier[(2*i+2):(2*i)]，As codes(i)
    codes(i) := multiplier((2 * i + 2), (2 * i))
    

    val genProd = Module(new GenProduct(WIDTH = WIDTH_A))
    genProd.io.multiplicand := io.operand_a
    genProd.io.code         := codes(i)
    temp_prods(i)           := genProd.io.partial_prod
    signs(i)                := genProd.io.sign

    // Concatenate partial product:{1'b1, ~signs(i), temp_prods(i), 1'b0, signs(i - 1)}
    // Then shift left by (2*i - 2) bits, and finally truncate to WIDTH_O width
    val concatenated = Cat(
      1.U(1.W),
      ~signs(i),
      temp_prods(i),
      0.U(1.W),
      signs(i - 1)
    )
    io.partial_prods(i) := (concatenated << (2 * (i - 1))) // left shift

  }

  // The last part
  codes(COUNT - 1) := multiplier((2 * COUNT), (2 * COUNT - 2 ))
  
  val genProdLast = Module(new GenProduct(WIDTH = WIDTH_A))
  genProdLast.io.multiplicand := io.operand_a
  genProdLast.io.code         := codes(COUNT - 1)
  temp_prods(COUNT - 1)       := genProdLast.io.partial_prod
  signs(COUNT - 1)            := genProdLast.io.sign

  val concatenatedLast = Cat(
    temp_prods(COUNT - 1),
    0.U(1.W),
    signs(COUNT - 2)
  )

  io.partial_prods(COUNT - 1) := (concatenatedLast << (2 * (COUNT - 2)))
}