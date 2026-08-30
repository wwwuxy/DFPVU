package pvu

import chisel3._
import chisel3.util._

/** Exact same-width Posit32/es=2 addition and subtraction.
  *
  * Raw encodings preserve signs and specials while decoded PIR fields provide
  * the exact scale and significand. The smaller significand is right-shifted
  * with jam, the fixed-point result is normalized, and the raw Posit payload is
  * rounded once with round-to-nearest, ties-to-even.
  */
class PositAddSub(
  val VECTOR_SIZE: Int,
  val EXP_WIDTH: Int
) extends Module {
  private val POSIT_WIDTH = 32
  private val ES = 2
  private val PIR_FRAC_WIDTH = POSIT_WIDTH - ES - 2
  private val WORK_WIDTH = 64
  private val SCALE_WIDTH = 10
  private val STREAM_WIDTH = 128

  require(EXP_WIDTH >= 8, "Posit32 add/sub requires the full es=2 scale range")

  val io = IO(new Bundle {
    val posit1_i = Input(Vec(VECTOR_SIZE, UInt(POSIT_WIDTH.W)))
    val posit2_i = Input(Vec(VECTOR_SIZE, UInt(POSIT_WIDTH.W)))
    val pir_exp1_i = Input(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_exp2_i = Input(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_frac1_i = Input(Vec(VECTOR_SIZE, UInt(PIR_FRAC_WIDTH.W)))
    val pir_frac2_i = Input(Vec(VECTOR_SIZE, UInt(PIR_FRAC_WIDTH.W)))
    val subtract_i = Input(Bool())

    val posit_o = Output(Vec(VECTOR_SIZE, UInt(POSIT_WIDTH.W)))
  })

  private val RawNaR = (BigInt(1) << (POSIT_WIDTH - 1)).U(POSIT_WIDTH.W)
  private val MaxPos = ((BigInt(1) << (POSIT_WIDTH - 1)) - 1).U(POSIT_WIDTH.W)
  private val MaxPosSignificand = (BigInt(1) << (PIR_FRAC_WIDTH - 1)).U(PIR_FRAC_WIDTH.W)
  private val StreamOnes = ((BigInt(1) << STREAM_WIDTH) - 1).U(STREAM_WIDTH.W)

  for (i <- 0 until VECTOR_SIZE) {
    val negatedB = (~io.posit2_i(i)).asUInt + 1.U(POSIT_WIDTH.W)
    val effectiveB = Mux(io.subtract_i, negatedB, io.posit2_i(i))

    val signA = io.posit1_i(i)(POSIT_WIDTH - 1)
    val signB = effectiveB(POSIT_WIDTH - 1)
    val magnitudeA = Mux(signA, (~io.posit1_i(i)).asUInt + 1.U(POSIT_WIDTH.W), io.posit1_i(i))
    val magnitudeB = Mux(signB, (~effectiveB).asUInt + 1.U(POSIT_WIDTH.W), effectiveB)

    val scaleA = Mux(magnitudeA === MaxPos, 120.S(SCALE_WIDTH.W),
      io.pir_exp1_i(i).pad(SCALE_WIDTH))
    val scaleB = Mux(magnitudeB === MaxPos, 120.S(SCALE_WIDTH.W),
      io.pir_exp2_i(i).pad(SCALE_WIDTH))
    val fractionA = Mux(magnitudeA === MaxPos, MaxPosSignificand, io.pir_frac1_i(i))
    val fractionB = Mux(magnitudeB === MaxPos, MaxPosSignificand, io.pir_frac2_i(i))

    // Bit 62 is the common binary point; bit 63 remains available for carry.
    val fixedA = Cat(0.U(1.W), fractionA, 0.U((WORK_WIDTH - PIR_FRAC_WIDTH - 1).W))
    val fixedB = Cat(0.U(1.W), fractionB, 0.U((WORK_WIDTH - PIR_FRAC_WIDTH - 1).W))
    val aIsLarger = magnitudeA >= magnitudeB
    val largerFixed = Mux(aIsLarger, fixedA, fixedB)
    val smallerFixed = Mux(aIsLarger, fixedB, fixedA)
    val largerScale = Mux(aIsLarger, scaleA, scaleB)
    val smallerScale = Mux(aIsLarger, scaleB, scaleA)
    val resultSign = Mux(aIsLarger, signA, signB)

    val shiftDistance = (largerScale - smallerScale).asUInt
    val cappedShift = Mux(shiftDistance >= WORK_WIDTH.U, WORK_WIDTH.U, shiftDistance)
    val shiftedSmaller = smallerFixed >> cappedShift
    val discarded = (0 until WORK_WIDTH).map { bit =>
      (shiftDistance > bit.U) && smallerFixed(bit)
    }.reduce(_ || _)
    val alignedSmaller = Cat(shiftedSmaller(WORK_WIDTH - 1, 1), shiftedSmaller(0) | discarded)

    val sameSign = signA === signB
    val fixedSum = largerFixed +& alignedSmaller
    val fixedDifference = largerFixed - alignedSmaller
    val fixedMagnitude = Mux(sameSign, fixedSum(WORK_WIDTH - 1, 0), fixedDifference)

    val leadingZeros = PriorityEncoder(Reverse(fixedMagnitude))
    val normalizedWide = fixedMagnitude << leadingZeros
    val normalized = normalizedWide(WORK_WIDTH - 1, 0)
    val normalizedScale = Wire(SInt(SCALE_WIDTH.W))
    normalizedScale := largerScale + 1.S - leadingZeros.zext

    val regimeK = normalizedScale >> ES
    val positiveRegime = regimeK >= 0.S
    val positiveRun = regimeK.asUInt + 1.U
    val negativeRun = (-regimeK).asUInt
    val regimeLength = Mux(positiveRegime, positiveRun + 1.U, negativeRun + 1.U)

    // Stream layout is regime, exponent, fraction. The normalized hidden bit
    // is omitted; every remaining bit stays live through the sole RNE point.
    val scaleBits = normalizedScale.asUInt
    val exponent = scaleBits(ES - 1, 0)
    val tail = Cat(exponent, normalized(WORK_WIDTH - 2, 0),
      0.U((STREAM_WIDTH - ES - (WORK_WIDTH - 1)).W))
    val shiftedTail = tail >> regimeLength
    val positiveRegimeBits = ~(StreamOnes >> positiveRun)
    val negativeRegimeWide = 1.U(STREAM_WIDTH.W) << ((STREAM_WIDTH - 1).U - negativeRun)
    val negativeRegimeBits = negativeRegimeWide(STREAM_WIDTH - 1, 0)
    val stream = shiftedTail | Mux(positiveRegime, positiveRegimeBits, negativeRegimeBits)

    val retained = stream(STREAM_WIDTH - 1, STREAM_WIDTH - (POSIT_WIDTH - 1))
    val guard = stream(STREAM_WIDTH - POSIT_WIDTH)
    val sticky = stream(STREAM_WIDTH - POSIT_WIDTH - 1, 0).orR
    val roundUp = guard && (sticky || retained(0))
    val rounded = Cat(0.U(1.W), retained) + roundUp.asUInt
    val positivePacked = Mux(normalizedScale >= 120.S || rounded(POSIT_WIDTH - 1),
      MaxPos, rounded(POSIT_WIDTH - 2, 0))
    val signedPacked = Mux(resultSign,
      (~positivePacked).asUInt + 1.U(POSIT_WIDTH.W), positivePacked)

    when(io.posit1_i(i) === RawNaR || io.posit2_i(i) === RawNaR) {
      io.posit_o(i) := RawNaR
    }.elsewhen(io.posit1_i(i) === 0.U) {
      io.posit_o(i) := effectiveB
    }.elsewhen(io.posit2_i(i) === 0.U) {
      io.posit_o(i) := io.posit1_i(i)
    }.elsewhen(fixedMagnitude === 0.U) {
      io.posit_o(i) := 0.U
    }.otherwise {
      io.posit_o(i) := signedPacked
    }
  }
}
