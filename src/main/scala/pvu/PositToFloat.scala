package pvu

import chisel3._
import chisel3.util._

/** Convert raw Posit32 es=2 values to a statically selected binary FP format. */
class PositToFloat(
  val POSIT_WIDTH: Int,
  val ES: Int,
  val FLOAT_EXP_WIDTH: Int,
  val FLOAT_FRAC_WIDTH: Int,
  val VECTOR_SIZE: Int
) extends Module {
  require(POSIT_WIDTH == 32, "PositToFloat implements Posit32 conversion")
  require(ES == 2, "PositToFloat implements Posit32 es=2 conversion")
  require(FLOAT_EXP_WIDTH >= 1, "floating-point exponent must be non-empty")
  require(FLOAT_FRAC_WIDTH >= 1, "floating-point fraction must be non-empty")

  private val FloatWidth = 1 + FLOAT_EXP_WIDTH + FLOAT_FRAC_WIDTH
  private val FloatBias = (1 << (FLOAT_EXP_WIDTH - 1)) - 1
  private val MinimumNormalExponent = 1 - FloatBias
  private val MaximumNormalExponent = ((1 << FLOAT_EXP_WIDTH) - 2) - FloatBias
  private val FixedFractionBits = 29
  private val RegimeCountWidth = log2Ceil(POSIT_WIDTH - 1)

  val io = IO(new Bundle {
    val posit_in = Input(Vec(VECTOR_SIZE, UInt(POSIT_WIDTH.W)))
    val float_out = Output(Vec(VECTOR_SIZE, UInt(FloatWidth.W)))
  })

  private val exponentAllOnes = ((BigInt(1) << FLOAT_EXP_WIDTH) - 1).U(FLOAT_EXP_WIDTH.W)
  private val canonicalNaN = Cat(0.U(1.W), exponentAllOnes,
    1.U(FLOAT_FRAC_WIDTH.W))

  for (lane <- 0 until VECTOR_SIZE) {
    val raw = io.posit_in(lane)
    val sign = raw(POSIT_WIDTH - 1)
    val isZero = raw === 0.U
    val isNaR = raw === "h80000000".U(POSIT_WIDTH.W)

    // Posit negatives are the two's complement of their positive encoding.
    val magnitude = Mux(sign.asBool, (~raw).asUInt + 1.U, raw)
    val regimePolarity = magnitude(POSIT_WIDTH - 2)
    val regimeDifference = Mux(regimePolarity.asBool,
      ~magnitude(POSIT_WIDTH - 2, 0), magnitude(POSIT_WIDTH - 2, 0))
    val regimeLzc = Module(new LZC(POSIT_WIDTH - 1, true, RegimeCountWidth))
    regimeLzc.io.in_i := regimeDifference

    // An all-ones maxpos regime has no terminator and therefore consumes all 31 payload bits.
    val regimeRunLength = Mux(regimeLzc.io.empty_o, (POSIT_WIDTH - 1).U(6.W),
      regimeLzc.io.cnt_o.pad(6))
    val regime = Wire(SInt(7.W))
    regime := Mux(regimePolarity.asBool,
      regimeRunLength.asSInt - 1.S(7.W), -regimeRunLength.asSInt)

    val payloadShift = Module(new BarrelShifter(POSIT_WIDTH - 1, 6, false))
    payloadShift.io.operand_i := magnitude(POSIT_WIDTH - 2, 0)
    payloadShift.io.shift_amount := regimeRunLength + 1.U
    val alignedPayload = payloadShift.io.result_o
    val positExponent = alignedPayload(POSIT_WIDTH - 2, POSIT_WIDTH - 1 - ES)
    val scale = Cat(regime.asUInt, positExponent).asSInt

    // The raw P32 fraction is aligned to a fixed Q1.29 significand. Low padding
    // bits remain explicit so normal and subnormal rounding share one binary point.
    val significand = Cat(1.U(1.W), alignedPayload(FixedFractionBits - 1, 0))
    val finitePacked = WireDefault(0.U(FloatWidth.W))

    when(scale > MaximumNormalExponent.S) {
      finitePacked := Cat(sign, exponentAllOnes, 0.U(FLOAT_FRAC_WIDTH.W))
    }.elsewhen(scale >= MinimumNormalExponent.S) {
      val roundedSignificand = Wire(UInt((FLOAT_FRAC_WIDTH + 2).W))
      if (FLOAT_FRAC_WIDTH < FixedFractionBits) {
        val droppedBits = FixedFractionBits - FLOAT_FRAC_WIDTH
        val retained = significand(FixedFractionBits, droppedBits)
        val guard = significand(droppedBits - 1)
        val sticky = if (droppedBits > 1) significand(droppedBits - 2, 0).orR else false.B
        val roundUp = guard && (sticky || retained(0))
        roundedSignificand := Cat(0.U(1.W), retained) + roundUp.asUInt
      } else {
        roundedSignificand := Cat(0.U(1.W), significand,
          0.U((FLOAT_FRAC_WIDTH - FixedFractionBits).W))
      }

      val significandCarry = roundedSignificand(FLOAT_FRAC_WIDTH + 1)
      val roundedScale = scale + Mux(significandCarry.asBool, 1.S(9.W), 0.S(9.W))
      when(roundedScale > MaximumNormalExponent.S) {
        finitePacked := Cat(sign, exponentAllOnes, 0.U(FLOAT_FRAC_WIDTH.W))
      }.otherwise {
        val exponentField = (roundedScale + FloatBias.S).asUInt
        val fractionField = Mux(significandCarry.asBool, 0.U(FLOAT_FRAC_WIDTH.W),
          roundedSignificand(FLOAT_FRAC_WIDTH - 1, 0))
        finitePacked := Cat(sign, exponentField(FLOAT_EXP_WIDTH - 1, 0), fractionField)
      }
    }.otherwise {
      if (FLOAT_FRAC_WIDTH < FixedFractionBits) {
        val dropSigned = (FixedFractionBits - FLOAT_FRAC_WIDTH + MinimumNormalExponent).S(10.W) -
          scale.pad(10)
        val drop = dropSigned.asUInt
        val shifted = significand >> drop
        val retained = shifted(FLOAT_FRAC_WIDTH, 0)
        val guard = drop > 0.U && drop <= significand.getWidth.U &&
          ((significand >> (drop - 1.U))(0)).asBool
        val stickyTerms = (0 until FixedFractionBits).map { bit =>
          (drop > (bit + 1).U) && significand(bit)
        }
        val sticky = stickyTerms.reduce(_ || _)
        val roundUp = guard && (sticky || retained(0))
        val rounded = Cat(0.U(1.W), retained) + roundUp.asUInt
        val roundsToMinimumNormal = rounded(FLOAT_FRAC_WIDTH)
        val exponentField = Mux(roundsToMinimumNormal.asBool,
          1.U(FLOAT_EXP_WIDTH.W), 0.U(FLOAT_EXP_WIDTH.W))
        val fractionField = Mux(roundsToMinimumNormal.asBool,
          0.U(FLOAT_FRAC_WIDTH.W), rounded(FLOAT_FRAC_WIDTH - 1, 0))
        finitePacked := Cat(sign, exponentField, fractionField)
      } else {
        // P32's minimum scale is -120, so FP64 cannot reach its subnormal range.
        finitePacked := Cat(sign, 0.U((FloatWidth - 1).W))
      }
    }

    io.float_out(lane) := Mux(isNaR, canonicalNaN, Mux(isZero, 0.U(FloatWidth.W), finitePacked))
  }
}
