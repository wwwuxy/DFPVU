//Posit Vector Mul Unit
package pvu

import chisel3._
import chisel3.util._

class Mul(val POSIT_WIDTH: Int, val VECTOR_SIZE: Int, val ALIGN_WIDTH: Int, val ES: Int) extends Module {
  var nd: Int         = log2Ceil(POSIT_WIDTH - 1)
  var EXP_WIDTH: Int  = nd + ES + 1 
  var FRAC_WIDTH: Int = POSIT_WIDTH - ES - 3
  var MUL_WIDTH: Int  = 2 * (FRAC_WIDTH + 1)

  val io = IO(new Bundle {
    val pir_sign1_i = Input(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_sign2_i = Input(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_exp1_i  = Input(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_exp2_i  = Input(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_frac1_i = Input(Vec(VECTOR_SIZE, UInt((FRAC_WIDTH+1).W)))
    val pir_frac2_i = Input(Vec(VECTOR_SIZE, UInt((FRAC_WIDTH+1).W)))

    val pir_sign_o = Output(Vec(VECTOR_SIZE, UInt(1.W)))
    val pir_exp_o  = Output(Vec(VECTOR_SIZE, SInt(EXP_WIDTH.W)))
    val pir_frac_o = Output(Vec(VECTOR_SIZE, UInt(MUL_WIDTH.W)))
  })

  // XOR operator
  for (i <- 0 until VECTOR_SIZE) {
      io.pir_sign_o(i) := io.pir_sign1_i(i) ^ io.pir_sign2_i(i)
  }

  // Calculate the remainder
  val sum_frac = Wire(Vec(VECTOR_SIZE, UInt(MUL_WIDTH.W)))
  val carry    = Wire(Vec(VECTOR_SIZE, UInt(MUL_WIDTH.W)))
  val frac     = Wire(Vec(VECTOR_SIZE, UInt(MUL_WIDTH.W)))

  for (i <- 0 until VECTOR_SIZE) {
    val radix4BoothMultiplier = Module(new Radix4BoothMultiplier(FRAC_WIDTH+1, FRAC_WIDTH+1))
    radix4BoothMultiplier.io.operand_a := io.pir_frac1_i(i)
    radix4BoothMultiplier.io.operand_b := io.pir_frac2_i(i)
    sum_frac(i)                        := radix4BoothMultiplier.io.sum_o
    carry(i)                           := radix4BoothMultiplier.io.carry_o
    frac(i)                            := (sum_frac(i) + carry(i))
  }

  // Calculate exponents in one extra signed bit. `sum(EXP_WIDTH)` is the
  // sign bit of that widened SInt, not an unsigned carry: treating it as a
  // carry maps every negative product scale to +max and drops its fraction.
  val maxExp = ((1.U << (EXP_WIDTH - 1)) - 1.U).asSInt
  val minExp = (-(1 << (EXP_WIDTH - 1))).S(EXP_WIDTH.W)

  for (i <- 0 until VECTOR_SIZE) {
    val sum = io.pir_exp1_i(i) +& io.pir_exp2_i(i)
    io.pir_exp_o(i) := Mux(sum > maxExp, maxExp,
      Mux(sum < minExp, minExp, sum(EXP_WIDTH - 1, 0).asSInt))
    io.pir_frac_o(i) := frac(i)
  }
}

/** Exact raw Posit32/es2 multiplication packer. Keeps the full 56-bit product
  * until final RNE-even packing, avoiding the generic PIR sticky double-round.
  */
class ExactP32Mul(val VECTOR_SIZE: Int) extends Module {
  private val Width = 32
  private val Es = 2
  private val FracWidth = Width - Es - 3
  private val PirFracWidth = FracWidth + 1
  private val ProductWidth = 2 * PirFracWidth
  private val PayloadWidth = Width - 1
  private val TailBits = Es + 29
  private val RegimeWidth = 7
  val io = IO(new Bundle {
    val sign1_i = Input(Vec(VECTOR_SIZE, UInt(1.W)))
    val sign2_i = Input(Vec(VECTOR_SIZE, UInt(1.W)))
    val exp1_i = Input(Vec(VECTOR_SIZE, SInt(8.W)))
    val exp2_i = Input(Vec(VECTOR_SIZE, SInt(8.W)))
    val frac1_i = Input(Vec(VECTOR_SIZE, UInt(PirFracWidth.W)))
    val frac2_i = Input(Vec(VECTOR_SIZE, UInt(PirFracWidth.W)))
    val posit_o = Output(Vec(VECTOR_SIZE, UInt(Width.W)))
  })
  private val maxpos = "h7fffffff".U(Width.W)
  private val minpos = 1.U(Width.W)
  private val negMaxpos = "h80000001".U(Width.W)
  private val negMinpos = "hffffffff".U(Width.W)
  private val payloadOnes = ((BigInt(1) << PayloadWidth) - 1).U(PayloadWidth.W)
  for (lane <- 0 until VECTOR_SIZE) {
    val product = io.frac1_i(lane) * io.frac2_i(lane)
    val zero = product === 0.U
    val normalize = product(ProductWidth - 1)
    val normalized = Mux(normalize, Cat(0.U(1.W), product(ProductWidth - 1, 2), product(1) | product(0)), product)
    val quotient = (normalized << 1)(ProductWidth - 1, 0)
    val scale = (io.exp1_i(lane) +& io.exp2_i(lane)) + normalize.asUInt.zext
    val sign = io.sign1_i(lane) ^ io.sign2_i(lane)
    val tail = Cat(scale.asUInt(Es - 1, 0), quotient(ProductWidth - 2, ProductWidth - 30))
    val lowerSticky = quotient(ProductWidth - 31, 0).orR || (normalize && product(0))
    val regime = scale >> Es
    val regimeNonnegative = !regime(regime.getWidth - 1)
    val positiveRun = regime.asUInt(RegimeWidth - 1, 0) +& 1.U
    val negativeRun = (-regime).asUInt(RegimeWidth - 1, 0)
    val regimeLength = Mux(regimeNonnegative, positiveRun +& 1.U, negativeRun +& 1.U)
    val positiveRegime = payloadOnes - (payloadOnes >> positiveRun)
    val negativeRegime = "h40000000".U(PayloadWidth.W) >> negativeRun
    val regimePattern = Mux(regimeNonnegative, positiveRegime, negativeRegime)
    val regimeFits = regimeLength <= PayloadWidth.U
    val availableBits = Mux(regimeFits, PayloadWidth.U(RegimeWidth.W) - regimeLength, 0.U)
    val tailDrop = TailBits.U(RegimeWidth.W) - availableBits
    val alignedTail = tail >> tailDrop
    val magnitude = regimePattern | alignedTail(PayloadWidth - 1, 0)
    val guard = Mux(tailDrop === 0.U, false.B, ((tail >> (tailDrop - 1.U))(0)).asBool)
    val sticky = (0 until TailBits - 1).map(bit => (tailDrop > (bit + 1).U) && tail(bit)).reduce(_ || _) || lowerSticky
    val rounded = Cat(0.U(1.W), magnitude) + (guard && (sticky || magnitude(0))).asUInt
    val finiteMagnitude = Mux(rounded(Width - 1), maxpos(PayloadWidth - 1, 0), rounded(PayloadWidth - 1, 0))
    val positivePacked = Cat(0.U(1.W), finiteMagnitude)
    val signedPacked = Mux(sign.asBool, (~positivePacked).asUInt + 1.U, positivePacked)
    val packed = Wire(UInt(Width.W))
    when(scale >= 120.S) { packed := Mux(sign.asBool, negMaxpos, maxpos)
    }.elsewhen(scale <= -120.S) { packed := Mux(sign.asBool, negMinpos, minpos)
    }.otherwise { packed := signedPacked }
    io.posit_o(lane) := Mux(zero, 0.U, packed)
  }
}

/** Exact combinational Posit32/es2 fused multiply-add with one final RNE. */
class Posit32MulAdd(val EXP_WIDTH: Int) extends Module {
  private val Width = 32
  private val Es = 2
  private val PirFracWidth = Width - Es - 2
  private val ProductWidth = 2 * PirFracWidth
  private val WorkWidth = 128
  private val ScaleWidth = 10
  private val StreamWidth = 192
  require(EXP_WIDTH >= 8, "Posit32 FMA requires the full es=2 scale range")
  val io = IO(new Bundle {
    val multiplicand_i = Input(UInt(Width.W))
    val multiplier_i = Input(UInt(Width.W))
    val accumulator_i = Input(UInt(Width.W))
    val posit_o = Output(UInt(Width.W))
  })
  private val RawNaR = "h80000000".U(Width.W)
  private val MaxPos = "h7fffffff".U(Width.W)
  private val NegMaxPos = "h80000001".U(Width.W)
  private val MinPos = 1.U(Width.W)
  private val NegMinPos = "hffffffff".U(Width.W)
  private val MaxPosSignificand = (BigInt(1) << (PirFracWidth - 1)).U(PirFracWidth.W)
  private val StreamOnes = ((BigInt(1) << StreamWidth) - 1).U(StreamWidth.W)
  val decodeA = Module(new PositDecode(Width, 1, Es))
  val decodeB = Module(new PositDecode(Width, 1, Es))
  val decodeC = Module(new PositDecode(Width, 1, Es))
  decodeA.io.posit(0) := io.multiplicand_i
  decodeB.io.posit(0) := io.multiplier_i
  decodeC.io.posit(0) := io.accumulator_i
  val signA = decodeA.io.Sign(0)
  val signB = decodeB.io.Sign(0)
  val signC = decodeC.io.Sign(0)
  val magnitudeA = Mux(signA.asBool, (~io.multiplicand_i).asUInt + 1.U, io.multiplicand_i)
  val magnitudeB = Mux(signB.asBool, (~io.multiplier_i).asUInt + 1.U, io.multiplier_i)
  val magnitudeC = Mux(signC.asBool, (~io.accumulator_i).asUInt + 1.U, io.accumulator_i)
  val scaleA = Mux(magnitudeA === MaxPos, 120.S(ScaleWidth.W), decodeA.io.Exp(0).pad(ScaleWidth))
  val scaleB = Mux(magnitudeB === MaxPos, 120.S(ScaleWidth.W), decodeB.io.Exp(0).pad(ScaleWidth))
  val scaleC = Mux(magnitudeC === MaxPos, 120.S(ScaleWidth.W), decodeC.io.Exp(0).pad(ScaleWidth))
  val fractionA = Mux(magnitudeA === MaxPos, MaxPosSignificand, decodeA.io.Frac(0))
  val fractionB = Mux(magnitudeB === MaxPos, MaxPosSignificand, decodeB.io.Frac(0))
  val fractionC = Mux(magnitudeC === MaxPos, MaxPosSignificand, decodeC.io.Frac(0))
  val rawProduct = fractionA * fractionB
  val productNormalize = rawProduct(ProductWidth - 1)
  val normalizedProduct = Mux(productNormalize, Cat(0.U(1.W), rawProduct(ProductWidth - 1, 2), rawProduct(1) | rawProduct(0)), rawProduct)
  val productScale = scaleA +& scaleB + productNormalize.asUInt.zext
  val productSign = signA ^ signB
  val productFixed = Cat(0.U(1.W), normalizedProduct(ProductWidth - 2, 0),
    0.U((WorkWidth - ProductWidth).W))
  val accumulatorFixed = Cat(0.U(1.W), fractionC,
    0.U((WorkWidth - PirFracWidth - 1).W))
  val accumulatorIsZero = io.accumulator_i === 0.U
  val productIsLarger = accumulatorIsZero || productScale > scaleC ||
    (productScale === scaleC && productFixed >= accumulatorFixed)
  val largerFixed = Mux(productIsLarger, productFixed, accumulatorFixed)
  val smallerFixed = Mux(productIsLarger, accumulatorFixed, productFixed)
  val largerScale = Mux(productIsLarger, productScale, scaleC)
  val smallerScale = Mux(productIsLarger, scaleC, productScale)
  val largerSign = Mux(productIsLarger, productSign, signC)
  val smallerSign = Mux(productIsLarger, signC, productSign)
  val shiftDistance = (largerScale - smallerScale).asUInt
  val cappedShift = Mux(shiftDistance >= WorkWidth.U, WorkWidth.U, shiftDistance)
  val shiftedSmaller = smallerFixed >> cappedShift
  val discarded = (0 until WorkWidth).map { bit =>
    (shiftDistance > bit.U) && smallerFixed(bit)
  }.reduce(_ || _)
  val alignedSmaller = Cat(shiftedSmaller(WorkWidth - 1, 1), shiftedSmaller(0) | discarded)
  val fixedSum = largerFixed +& alignedSmaller
  val fixedDifference = largerFixed - alignedSmaller
  val fixedMagnitude = Mux(largerSign === smallerSign,
    fixedSum(WorkWidth - 1, 0), fixedDifference)
  val leadingZeros = PriorityEncoder(Reverse(fixedMagnitude))
  val normalizedWide = fixedMagnitude << leadingZeros
  val normalized = normalizedWide(WorkWidth - 1, 0)
  val normalizedScale = Wire(SInt(ScaleWidth.W))
  normalizedScale := largerScale + 1.S - leadingZeros.zext
  val regimeK = normalizedScale >> Es
  val positiveRegime = regimeK >= 0.S
  val positiveRun = regimeK.asUInt + 1.U
  val negativeRun = (-regimeK).asUInt
  val regimeLength = Mux(positiveRegime, positiveRun + 1.U, negativeRun + 1.U)
  val exponent = normalizedScale.asUInt(Es - 1, 0)
  val tail = Cat(exponent, normalized(WorkWidth - 2, 0),
    0.U((StreamWidth - Es - (WorkWidth - 1)).W))
  val shiftedTail = tail >> regimeLength
  val positiveRegimeBits = ~(StreamOnes >> positiveRun)
  val negativeRegimeWide = 1.U(StreamWidth.W) << ((StreamWidth - 1).U - negativeRun)
  val negativeRegimeBits = negativeRegimeWide(StreamWidth - 1, 0)
  val stream = shiftedTail | Mux(positiveRegime, positiveRegimeBits, negativeRegimeBits)
  val retained = stream(StreamWidth - 1, StreamWidth - (Width - 1))
  val guard = stream(StreamWidth - Width)
  val sticky = stream(StreamWidth - Width - 1, 0).orR
  val roundUp = guard && (sticky || retained(0))
  val rounded = Cat(0.U(1.W), retained) + roundUp.asUInt
  val finiteMagnitude = Mux(rounded(Width - 1),
    MaxPos(Width - 2, 0), rounded(Width - 2, 0))
  val positivePacked = Cat(0.U(1.W), finiteMagnitude)
  val signedPacked = Mux(largerSign.asBool,
    (~positivePacked).asUInt + 1.U, positivePacked)
  val saturated = Mux(normalizedScale >= 120.S,
    Mux(largerSign.asBool, NegMaxPos, MaxPos),
    Mux(normalizedScale <= -120.S,
      Mux(largerSign.asBool, NegMinPos, MinPos), signedPacked))
  val productIsZero = io.multiplicand_i === 0.U || io.multiplier_i === 0.U
  when(io.multiplicand_i === RawNaR || io.multiplier_i === RawNaR ||
    io.accumulator_i === RawNaR) {
    io.posit_o := RawNaR
  }.elsewhen(productIsZero) {
    io.posit_o := io.accumulator_i
  }.elsewhen(fixedMagnitude === 0.U) {
    io.posit_o := 0.U
  }.otherwise {
    io.posit_o := saturated
  }
}
