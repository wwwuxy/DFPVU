/**
  * The GenProduct module generates partial products based on Radix-4 Booth encoding.
  *
  * Input:
  * - multiplicand: Multiplicand A
  * - code: 3-bit Booth encoding
  *
  * Output:
  * - partial_prod: Generated partial sum
  * - sign: Whether it is a negative partial product
  */
package pvu

import chisel3._
import chisel3.util._

class GenProduct(val WIDTH: Int) extends Module {
  val io = IO(new Bundle {
    val multiplicand = Input(UInt(WIDTH.W))  // Multiplican A
    val code         = Input(UInt(3.W))      // 3-bit Booth encoding
    
    val partial_prod = Output(UInt((WIDTH + 1).W))  // Generated partial sum
    val sign         = Output(Bool())               // Whether it is a negative partial product
  })

  // Instantiation BoothEncoder
  val boothEncoder = Module(new BoothEncoder())
  boothEncoder.io.code := io.code

  // Define temporary partial product
  val tempProd = Wire(UInt((WIDTH + 1).W))
  

  // Generate `tempProd` according to Booth encoding
  when (boothEncoder.io.one) {
    tempProd := io.multiplicand
  } .elsewhen (boothEncoder.io.two) {
    tempProd := io.multiplicand << 1
  } .otherwise {
    tempProd := 0.U
  }


  // Generate the final partial product, no need to add +1 here because +1 is included in the partial product of the next line.
  io.partial_prod := Mux(boothEncoder.io.neg, ~tempProd, tempProd)

  // Output symbol
  io.sign := boothEncoder.io.neg
}
