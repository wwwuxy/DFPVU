package pvu

import chisel3._
import chisel3.util._

/** Exact combinational Posit32/es2 vector division.
  *
  * The decoded PIR significands are divided at fixed point without rounding.
  * The raw Posit result is then packed directly from the normalized scale and
  * quotient so discarded quotient bits and the integer remainder participate
  * in one final RNE-even decision.
  */
class Div(val POSIT_WIDTH: Int, val VECTOR_SIZE: Int, val ALIGN_WIDTH: Int, val ES: Int) extends Module {
  require(POSIT_WIDTH == 32, "Div implements Posit32 division")
  require(ES == 2, "Div implements Posit32 es=2 division")

  private val Nd = log2Ceil(POSIT_WIDTH - 1)
  private val ExpWidth = Nd + ES + 1
  private val FracWidth = POSIT_WIDTH - ES - 3
  private val PirFracWidth = 2 * (FracWidth + 1)
  private val QuotientFractionBits = PirFracWidth - 1
  private val RawFractionBits = POSIT_WIDTH - ES - 3
  private val PositPayloadBits = POSIT_WIDTH - 1
  private val TailBits = ES + RawFractionBits + 2
  private val ScaleWidth = 10
  private val RegimeCountWidth = 7

  val io = IO(new Bundle {
    val posit1_i = Input(Vec(VECTOR_SIZE, UInt(POSIT_WIDTH.W)))
    val posit2_i = Input(Vec(VECTOR_SIZE, UInt(POSIT_WIDTH.W)))
    val pir_exp1_i = Input(Vec(VECTOR_SIZE, SInt(ExpWidth.W)))
    val pir_exp2_i = Input(Vec(VECTOR_SIZE, SInt(ExpWidth.W)))
    val pir_frac1_i = Input(Vec(VECTOR_SIZE, UInt((FracWidth + 1).W)))
    val pir_frac2_i = Input(Vec(VECTOR_SIZE, UInt((FracWidth + 1).W)))
    val pir_sign_o = Output(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_exp_o = Output(Vec(VECTOR_SIZE, SInt(ExpWidth.W)))
    val pir_frac_o = Output(Vec(VECTOR_SIZE, UInt(PirFracWidth.W)))
    val posit_o = Output(Vec(VECTOR_SIZE, UInt(POSIT_WIDTH.W)))
  })

  private val positNaR = "h80000000".U(POSIT_WIDTH.W)
  private val maxpos = "h7fffffff".U(POSIT_WIDTH.W)
  private val minpos = 1.U(POSIT_WIDTH.W)
  private val negativeMaxpos = "h80000001".U(POSIT_WIDTH.W)
  private val negativeMinpos = "hffffffff".U(POSIT_WIDTH.W)
  private val payloadOnes = ((BigInt(1) << PositPayloadBits) - 1).U(PositPayloadBits.W)


  for (lane <- 0 until VECTOR_SIZE) {
    val rawA = io.posit1_i(lane)
    val rawB = io.posit2_i(lane)
    val fracA = io.pir_frac1_i(lane)
    val fracB = io.pir_frac2_i(lane)
    val magnitudeA = Mux(rawA(POSIT_WIDTH - 1).asBool, (~rawA).asUInt + 1.U, rawA)
    val magnitudeB = Mux(rawB(POSIT_WIDTH - 1).asBool, (~rawB).asUInt + 1.U, rawB)
    val scaleA = Wire(SInt(ScaleWidth.W))
    val scaleB = Wire(SInt(ScaleWidth.W))
    scaleA := Mux(magnitudeA === maxpos, 120.S, io.pir_exp1_i(lane).pad(ScaleWidth))
    scaleB := Mux(magnitudeB === maxpos, 120.S, io.pir_exp2_i(lane).pad(ScaleWidth))
    val isInvalid = rawA === positNaR || rawB === positNaR || rawB === 0.U
    val isZero = rawA === 0.U
    val sign = rawA(POSIT_WIDTH - 1) ^ rawB(POSIT_WIDTH - 1)

    // A/B is in [0.5, 2). Shift A once when needed so the quotient is
    // normalized to [1, 2), and compensate that normalization in the scale.
    val normalizeLeft = fracA < fracB
    val normalizedNumerator = Mux(normalizeLeft,
      Cat(fracA, 0.U(1.W)), Cat(0.U(1.W), fracA))
    val scale = Wire(SInt(ScaleWidth.W))
    scale := scaleA - scaleB - normalizeLeft.asUInt.zext

    // Keep a hidden bit plus 55 quotient fraction bits. The remainder is not
    // discarded: it is folded into sticky for both PIR and final raw packing.
    val divisionNumerator = normalizedNumerator << QuotientFractionBits
    val safeDivisor = Mux(fracB === 0.U, 1.U, fracB).pad(divisionNumerator.getWidth)
    val quotientFull = divisionNumerator / safeDivisor
    val remainder = divisionNumerator % safeDivisor
    val quotient = quotientFull(PirFracWidth - 1, 0)
    val quotientHasMore = remainder.orR
    val pirSticky = quotient(0) || quotientHasMore
    val pirQuotient = Cat(quotient(PirFracWidth - 1, 1), pirSticky)

    io.pir_sign_o(lane) := Mux(isInvalid, 1.U, Mux(isZero, 0.U, sign))
    io.pir_exp_o(lane) := Mux(isInvalid || isZero, 0.S, scale)
    io.pir_frac_o(lane) := Mux(isInvalid || isZero, 0.U, pirQuotient)

    // Quotient bits 54:26 are the first 29 bits after the hidden bit. They
    // cover the maximum 27-bit Posit32 fraction plus guard and sticky inputs.
    val quotientTail = quotient(PirFracWidth - 2, PirFracWidth - RawFractionBits - 3)
    val lowerQuotientSticky = quotient(PirFracWidth - RawFractionBits - 4, 0).orR || quotientHasMore
    val positExponent = scale.asUInt(ES - 1, 0)
    val tail = Cat(positExponent, quotientTail)

    // Arithmetic right shift gives floor(scale / 4), including negative
    // scales. Build the regime and align the exact exponent/fraction tail.
    val regime = scale >> ES
    val regimeIsNonnegative = !regime(regime.getWidth - 1)
    val positiveRun = regime.asUInt(RegimeCountWidth - 1, 0) +& 1.U
    val negativeRun = (-regime).asUInt(RegimeCountWidth - 1, 0)
    val regimeLength = Wire(UInt(RegimeCountWidth.W))
    regimeLength := Mux(regimeIsNonnegative, positiveRun +& 1.U, negativeRun +& 1.U)

    val positiveRegime = payloadOnes - (payloadOnes >> positiveRun)
    val negativeRegime = "h40000000".U(PositPayloadBits.W) >> negativeRun
    val regimePattern = Mux(regimeIsNonnegative, positiveRegime, negativeRegime)

    val regimeFits = regimeLength <= PositPayloadBits.U
    val availablePayloadBits = Mux(regimeFits,
      PositPayloadBits.U(RegimeCountWidth.W) - regimeLength, 0.U)
    val tailDrop = TailBits.U(RegimeCountWidth.W) - availablePayloadBits
    val alignedTail = tail >> tailDrop
    val unroundedMagnitude = regimePattern | alignedTail(PositPayloadBits - 1, 0)

    val guard = ((tail >> (tailDrop - 1.U))(0)).asBool
    val stickyTerms = (0 until TailBits - 1).map { bit =>
      (tailDrop > (bit + 1).U) && tail(bit)
    }
    val sticky = stickyTerms.reduce(_ || _) || lowerQuotientSticky
    val roundUp = guard && (sticky || unroundedMagnitude(0))
    val roundedMagnitude = Cat(0.U(1.W), unroundedMagnitude) + roundUp.asUInt
    val finiteMagnitude = Mux(roundedMagnitude(POSIT_WIDTH - 1),
      maxpos(PositPayloadBits - 1, 0), roundedMagnitude(PositPayloadBits - 1, 0))
    val positivePacked = Cat(0.U(1.W), finiteMagnitude)
    val signedPacked = Mux(sign.asBool, (~positivePacked).asUInt + 1.U, positivePacked)

    val finitePacked = Wire(UInt(POSIT_WIDTH.W))
    when(scale >= 120.S) {
      finitePacked := Mux(sign.asBool, negativeMaxpos, maxpos)
    }.elsewhen(scale <= -120.S) {
      finitePacked := Mux(sign.asBool, negativeMinpos, minpos)
    }.otherwise {
      finitePacked := signedPacked
    }

    io.posit_o(lane) := Mux(isInvalid, positNaR,
      Mux(isZero, 0.U(POSIT_WIDTH.W), finitePacked))
  }
}
