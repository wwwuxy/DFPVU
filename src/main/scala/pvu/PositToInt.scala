package pvu

import chisel3._
import chisel3.util._

/** Convert raw Posit32 values to signed Int32 with round-to-nearest, ties-to-even. */
class PositToInt(
  val MAX_POSIT_WIDTH: Int,
  val MAX_VECTOR_SIZE: Int,
  val MAX_ALIGN_WIDTH: Int,
  val ES: Int,
  val INT_WIDTH: Int
) extends Module {
  require(MAX_POSIT_WIDTH == 32, "PositToInt implements Posit32 conversion")
  require(ES == 2, "PositToInt implements Posit32 es=2 conversion")
  require(INT_WIDTH == 32, "PositToInt implements Int32 conversion")

  val io = IO(new Bundle {
    val posit_i = Input(Vec(MAX_VECTOR_SIZE, UInt(MAX_POSIT_WIDTH.W)))
    val int_o = Output(Vec(MAX_VECTOR_SIZE, SInt(INT_WIDTH.W)))
  })

  val nar = "h80000000".U(32.W)
  val half = "h38000000".U(32.W)
  val threeHalves = "h44000000".U(32.W)
  val fiveHalves = "h4a000000".U(32.W)
  val maxUnsaturated = "h7fafffff".U(32.W)

  for (i <- 0 until MAX_VECTOR_SIZE) {
    val raw = io.posit_i(i)
    val sign = raw(31)
    val magnitude = Mux(sign, (~raw).asUInt + 1.U(32.W), raw)

    // The general path is only selected above 2.5, where the regime begins
    // with one bits. Count them, remove regime/termination, then extract es=2.
    val regimeOnes = PriorityEncoder(Reverse(~magnitude(30, 0)))
    val payload = (magnitude(30, 0) << (regimeOnes + 1.U))(30, 0)
    val exponent = payload(30, 29)
    val scale = Wire(UInt(5.W))
    scale := (((regimeOnes - 1.U) << 2) + exponent)(4, 0)

    // significand has 29 fractional places. Scaling it in 61 bits retains
    // every encoded fraction bit needed for the integer guard/sticky test.
    val significand = Cat(1.U(1.W), payload(28, 0))
    val scaledSignificand = significand << scale
    val integerPart = scaledSignificand(60, 29)
    val guard = scaledSignificand(28)
    val sticky = scaledSignificand(27, 0).orR
    val roundUp = guard && (sticky || integerPart(0))
    val decodedRoundedMagnitude = (integerPart +& roundUp)(31, 0)

    val finiteMagnitude = Wire(UInt(32.W))
    when (magnitude <= half) {
      finiteMagnitude := 0.U
    }.elsewhen (magnitude < threeHalves) {
      finiteMagnitude := 1.U
    }.elsewhen (magnitude <= fiveHalves) {
      finiteMagnitude := 2.U
    }.otherwise {
      finiteMagnitude := decodedRoundedMagnitude
    }

    val unsignedSignedMagnitude = Cat(0.U(1.W), finiteMagnitude).asSInt
    val finiteResult = Mux(sign, -unsignedSignedMagnitude, unsignedSignedMagnitude)

    when (raw === nar) {
      io.int_o(i) := 0.S
    }.elsewhen (magnitude > maxUnsaturated) {
      io.int_o(i) := Mux(sign, (-2147483648L).S(32.W), 2147483647.S(32.W))
    }.otherwise {
      io.int_o(i) := finiteResult(31, 0).asSInt
    }
  }
}
