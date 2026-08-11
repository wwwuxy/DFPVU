package pvu

import chisel3._
import chisel3.util._

/**
  * 输入：
  *   in: 浮点数数据，宽度 = 1 + EXP_WIDTH + FRAC_WIDTH，
  *       格式：[符号位][指数域][尾数域]
  *
  * 输出：
  *   sign     : 符号位（0为正，1为负）
  *   exp      : 实际指数（SInt类型，可为负）
  *   frac     : 有效尾数（对于规约数隐含1，对于非规约数隐含0，宽度 = FRAC_WIDTH+1）
  *   isNaN    : 表示输入为 NaN（指数全1且尾数非0）
  *   isInf    : 表示输入为无穷大（指数全1且尾数为0）
  *   isZero   : 表示输入为零（指数全0且尾数为0）
  */
class FloatDecode(val EXP_WIDTH: Int, val FRAC_WIDTH: Int, val VECTOR_SIZE: Int) extends Module {
  val TOTAL_WIDTH: Int = 1 + EXP_WIDTH + FRAC_WIDTH
  val BIAS: Int  = (1 << (EXP_WIDTH - 1)) - 1

  val io = IO(new Bundle {
    val float    = Input(Vec(VECTOR_SIZE, UInt(TOTAL_WIDTH.W)))
    
    val Sign     = Output(Vec(VECTOR_SIZE, Bool()))
    val Exp      = Output(Vec(VECTOR_SIZE, SInt((EXP_WIDTH + 1).W)))
    val Frac     = Output(Vec(VECTOR_SIZE, UInt((FRAC_WIDTH + 1).W)))
    val isNaN    = Output(Vec(VECTOR_SIZE, Bool()))
    val isInf    = Output(Vec(VECTOR_SIZE, Bool()))
    val isZero   = Output(Vec(VECTOR_SIZE, Bool()))
  })

// 对每个向量元素进行解码
  for(i <- 0 until VECTOR_SIZE) {
    val in       = io.float(i)
    // 位分解：符号位为最高位；指数域在中间；尾数域在低位
    val signBit  = in(TOTAL_WIDTH - 1)
    val exponent = in(FRAC_WIDTH + EXP_WIDTH - 1, FRAC_WIDTH)
    val fraction = in(FRAC_WIDTH - 1, 0)

    // 特殊值检测
    val isExpAllOnes  = exponent === ((BigInt(1) << EXP_WIDTH) - 1).U(EXP_WIDTH.W)
    val isExpAllZeros = exponent === 0.U

    io.isInf(i)  := isExpAllOnes && (fraction === 0.U)  // 无穷大：指数全1且尾数全0
    io.isNaN(i)  := isExpAllOnes && (fraction =/= 0.U)  // NaN：指数全1且尾数非0
    io.isZero(i) := isExpAllZeros && (fraction === 0.U)   // 零：指数全0且尾数全0

    // 有效指数和有效尾数计算
    // 对于非规约数（subnormal）：实际指数 = 1 - BIAS，尾数前隐含0
    // 对于规约数（normal）：实际指数 = exponent - BIAS，尾数前隐含1
    // 对于特殊值（NaN/Inf）：此处将指数置为0，尾数直接传递 fraction
    val effectiveExp      = Wire(SInt((EXP_WIDTH + 1).W))
    val effectiveMantissa = Wire(UInt((FRAC_WIDTH + 1).W))

    when (isExpAllZeros) {      // 非规约数（Subnormal）
      effectiveExp      := (1 - BIAS).S
      effectiveMantissa := Cat(0.U(1.W), fraction)
    } .elsewhen (!isExpAllOnes) {     // 规约数（Normal）
      effectiveExp      := (exponent.asSInt - BIAS.S)
      effectiveMantissa := Cat(1.U(1.W), fraction)
    } .otherwise {            // 特殊值（NaN/Inf）
      effectiveExp      := 0.S
      effectiveMantissa := fraction
    }

    io.Sign(i) := signBit
    io.Exp(i)  := effectiveExp
    io.Frac(i) := effectiveMantissa
  }
}

/* 示例：实例化不同格式的解码单元
   - 单精度：EXP_WIDTH = 8, FRAC_WIDTH = 23  => 总位宽 32
   - 双精度：EXP_WIDTH = 11, FRAC_WIDTH = 52 => 总位宽 64
   - 半精度：EXP_WIDTH = 5, FRAC_WIDTH = 10  => 总位宽 16

   例如：
   val singleDecoder = Module(new FloatDecode(8, 23, 4))
   val doubleDecoder = Module(new FloatDecode(11, 52, 4))
   val halfDecoder   = Module(new FloatDecode(5, 10, 4))
*/