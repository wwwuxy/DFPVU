package pvu

import chisel3._
import chisel3.util._

/** Convert a statically selected binary FP format to raw Posit32 es=2. */
class FloatToPosit(
  val FLOAT_EXP_WIDTH: Int,
  val FLOAT_FRAC_WIDTH: Int,
  val POSIT_WIDTH: Int,
  val ES: Int,
  val VECTOR_SIZE: Int
) extends Module {
  require(POSIT_WIDTH == 32, "FloatToPosit implements Posit32 conversion")
  require(ES == 2, "FloatToPosit implements Posit32 es=2 conversion")
  require(FLOAT_EXP_WIDTH >= 1, "floating-point exponent must be non-empty")
  require(FLOAT_FRAC_WIDTH >= 1, "floating-point fraction must be non-empty")

  private val FloatWidth = 1 + FLOAT_EXP_WIDTH + FLOAT_FRAC_WIDTH
  private val FloatBias = (1 << (FLOAT_EXP_WIDTH - 1)) - 1
  private val PositPayloadBits = POSIT_WIDTH - 1
  private val TailBits = ES + FLOAT_FRAC_WIDTH
  private val TailWorkBits = Math.max(PositPayloadBits, TailBits)
  private val ScaleWidth = 14
  private val RegimeCountWidth = 6
  private val TailDropWidth = log2Ceil(TailBits + 1)

  val io = IO(new Bundle {
    val float_in = Input(Vec(VECTOR_SIZE, UInt(FloatWidth.W)))
    val posit_out = Output(Vec(VECTOR_SIZE, UInt(POSIT_WIDTH.W)))
  })

  private val exponentAllOnes = ((BigInt(1) << FLOAT_EXP_WIDTH) - 1).U(FLOAT_EXP_WIDTH.W)
  private val positNaR = "h80000000".U(POSIT_WIDTH.W)
  private val maxpos = "h7fffffff".U(POSIT_WIDTH.W)
  private val minpos = 1.U(POSIT_WIDTH.W)
  private val payloadOnes = ((BigInt(1) << PositPayloadBits) - 1).U(PositPayloadBits.W)

  for (lane <- 0 until VECTOR_SIZE) {
    val raw = io.float_in(lane)
    val sign = raw(FloatWidth - 1)
    val exponentField = raw(FLOAT_FRAC_WIDTH + FLOAT_EXP_WIDTH - 1, FLOAT_FRAC_WIDTH)
    val fractionField = raw(FLOAT_FRAC_WIDTH - 1, 0)
    val exponentIsZero = exponentField === 0.U
    val exponentIsAllOnes = exponentField === exponentAllOnes
    val isZero = exponentIsZero && fractionField === 0.U
    val isSpecial = exponentIsAllOnes

    // IEEE subnormals carry no hidden bit. Normalize their first one before
    // deriving the binary scale so the Posit regime sees the true exponent.
    val fractionLzc = Module(new LZC(
      FLOAT_FRAC_WIDTH, true, log2Ceil(FLOAT_FRAC_WIDTH)))
    fractionLzc.io.in_i := fractionField
    val normalizationShift = fractionLzc.io.cnt_o +& 1.U
    val subnormalSignificand = Cat(0.U(1.W), fractionField) << normalizationShift
    val significand = Mux(exponentIsZero, subnormalSignificand,
      Cat(1.U(1.W), fractionField))

    val scale = Wire(SInt(ScaleWidth.W))
    when(exponentIsZero) {
      scale := (1 - FloatBias).S(ScaleWidth.W) - normalizationShift.zext
    }.otherwise {
      scale := exponentField.zext - FloatBias.S(ScaleWidth.W)
    }

    // Arithmetic right shift implements floor(scale / 4); the low two bits
    // are the non-negative exponent remainder required by Posit es=2.
    val regime = scale >> ES
    val positExponent = scale.asUInt(ES - 1, 0)
    val regimeIsNonnegative = !regime(regime.getWidth - 1)
    val positiveRun = regime.asUInt(RegimeCountWidth - 1, 0) +& 1.U
    val negativeRun = (-regime).asUInt(RegimeCountWidth - 1, 0)
    val regimeLength = Wire(UInt(RegimeCountWidth.W))
    regimeLength := Mux(regimeIsNonnegative,
      positiveRun +& 1.U, negativeRun +& 1.U)

    val positiveRegime = payloadOnes - (payloadOnes >> positiveRun)
    val negativeRegime = "h40000000".U(PositPayloadBits.W) >> negativeRun
    val regimePattern = Mux(regimeIsNonnegative, positiveRegime, negativeRegime)

    // The exponent and every source fraction bit remain exact in this tail.
    // Only the final 31-bit Posit payload boundary performs RNE-even.
    val tail = Cat(positExponent, significand(FLOAT_FRAC_WIDTH - 1, 0))
    val availablePayloadBits = PositPayloadBits.U(RegimeCountWidth.W) - regimeLength
    val tailFits = availablePayloadBits >= TailBits.U
    val tailLeftShift = Mux(tailFits, availablePayloadBits - TailBits.U, 0.U)
    val tailRightShift = Mux(tailFits, 0.U, TailBits.U - availablePayloadBits)
    val tailWork = tail.pad(TailWorkBits)
    val alignedTail = Mux(tailFits,
      tailWork << tailLeftShift, tailWork >> tailRightShift)
    val unroundedMagnitude = regimePattern | alignedTail(PositPayloadBits - 1, 0)

    val dropsTailBits = !tailFits
    val drop = Wire(UInt(TailDropWidth.W))
    drop := Mux(dropsTailBits, TailBits.U - availablePayloadBits, 0.U)
    val guard = dropsTailBits && ((tail >> (drop - 1.U))(0)).asBool
    val stickyTerms = (0 until TailBits - 1).map { bit =>
      (drop > (bit + 1).U) && tail(bit)
    }
    val sticky = stickyTerms.reduce(_ || _)
    val roundUp = guard && (sticky || unroundedMagnitude(0))
    val roundedMagnitude = Cat(0.U(1.W), unroundedMagnitude) + roundUp.asUInt
    val finiteMagnitude = Mux(roundedMagnitude(POSIT_WIDTH - 1),
      maxpos(PositPayloadBits - 1, 0), roundedMagnitude(PositPayloadBits - 1, 0))
    val positivePacked = Cat(0.U(1.W), finiteMagnitude)
    val signedPacked = Mux(sign.asBool, (~positivePacked).asUInt + 1.U, positivePacked)

    val finitePacked = Wire(UInt(POSIT_WIDTH.W))
    when(scale >= 120.S) {
      finitePacked := Mux(sign.asBool, "h80000001".U(POSIT_WIDTH.W), maxpos)
    }.elsewhen(scale <= -120.S) {
      finitePacked := Mux(sign.asBool, "hffffffff".U(POSIT_WIDTH.W), minpos)
    }.otherwise {
      finitePacked := signedPacked
    }

    io.posit_out(lane) := Mux(isSpecial, positNaR,
      Mux(isZero, 0.U(POSIT_WIDTH.W), finitePacked))
  }
}
