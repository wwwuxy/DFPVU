package pvu

import chisel3._
import chisel3.util._

/**
  *
  * 输入：
  *   Sign     : Vec(VECTOR_SIZE, Bool())，符号位（0为正，1为负）
  *   Exp      : Vec(VECTOR_SIZE, SInt((EXP_WIDTH+1).W))，有效指数（规约数为 exponent - BIAS，非规约数为 1 - BIAS）
  *   Frac     : Vec(VECTOR_SIZE, UInt((FRAC_WIDTH+1).W))，有效尾数（规约数隐含1，对于非规约数隐含0）
  *   isNaN    : Vec(VECTOR_SIZE, Bool())，表示输入为 NaN（指数全1且尾数非0）
  *   isInf    : Vec(VECTOR_SIZE, Bool())，表示输入为无穷大（指数全1且尾数为0）
  *   isZero   : Vec(VECTOR_SIZE, Bool())，表示输入为零（指数全0且尾数为0）
  *
  * 输出：
  *   float: Vec(VECTOR_SIZE, UInt(TOTAL_WIDTH.W))，组合后的 IEEE-754 浮点数
  *
  * 编码逻辑说明：
  *   1. 对于零、无穷大、NaN等特殊情况，直接生成对应的编码：
  *      - 零：指数全0，尾数全0；
  *      - 无穷大：指数全1，尾数全0；
  *      - NaN：指数全1，尾数设置为非0（这里将输入的有效尾数低 FRAC_WIDTH 位传递，如全0则置1）。
  *   2. 对于正常数字（包括规约数和非规约数）：
  *      - 如果为非规约数（判断条件为有效指数等于 1-BIAS 且隐含位为0），则编码时指数域置0，尾数直接为有效尾数低 FRAC_WIDTH 位。
  *      - 否则为规约数，编码时先计算 exponent field = effective exponent + BIAS，再去掉有效尾数中的隐含1（即只保留低 FRAC_WIDTH 位）。
  *   3. 最终将符号位、指数域和尾数域拼接成 TOTAL_WIDTH 位的浮点数。
  */
class FloatEncode(val EXP_WIDTH: Int, val FRAC_WIDTH: Int, val VECTOR_SIZE: Int) extends Module {
  // 添加参数限制
  private val LIMITED_VECTOR_SIZE = Math.min(VECTOR_SIZE, 16)  // 限制最大向量大小为16
  private val LIMITED_EXP_WIDTH = Math.min(EXP_WIDTH, 11)     // 限制最大指数宽度为11（FP64标准）
  private val LIMITED_FRAC_WIDTH = Math.min(FRAC_WIDTH, 52)   // 限制最大尾数宽度为52（FP64标准）
  
  val TOTAL_WIDTH = 1 + LIMITED_EXP_WIDTH + LIMITED_FRAC_WIDTH
  val BIAS = (1 << (LIMITED_EXP_WIDTH - 1)) - 1

  val io = IO(new Bundle {
    val Sign   = Input(Vec(LIMITED_VECTOR_SIZE, Bool()))
    val Exp    = Input(Vec(LIMITED_VECTOR_SIZE, SInt((LIMITED_EXP_WIDTH + 1).W)))
    val Frac   = Input(Vec(LIMITED_VECTOR_SIZE, UInt((LIMITED_FRAC_WIDTH + 1).W)))
    val isNaN  = Input(Vec(LIMITED_VECTOR_SIZE, Bool()))
    val isInf  = Input(Vec(LIMITED_VECTOR_SIZE, Bool()))
    val isZero = Input(Vec(LIMITED_VECTOR_SIZE, Bool()))
    
    val float  = Output(Vec(LIMITED_VECTOR_SIZE, UInt(TOTAL_WIDTH.W)))
  })

  // 对向量中的每个元素进行编码
  for(i <- 0 until LIMITED_VECTOR_SIZE) {
    val signBit = io.Sign(i)
    val exponentField = Wire(UInt(LIMITED_EXP_WIDTH.W))
    val fractionField = Wire(UInt(LIMITED_FRAC_WIDTH.W))

    // 特殊情况处理
    when(io.isZero(i)) {
      // 零：指数全0，尾数全0
      exponentField := 0.U
      fractionField := 0.U
    } .elsewhen(io.isInf(i)) {
      // 无穷大：指数全1，尾数全0
      exponentField := ((BigInt(1) << LIMITED_EXP_WIDTH) - 1).U(LIMITED_EXP_WIDTH.W)
      fractionField := 0.U
    } .elsewhen(io.isNaN(i)) {
      // NaN：指数全1，尾数非0
      exponentField := ((BigInt(1) << LIMITED_EXP_WIDTH) - 1).U(LIMITED_EXP_WIDTH.W)
      fractionField := Mux(io.Frac(i)(LIMITED_FRAC_WIDTH - 1, 0) === 0.U, 1.U, io.Frac(i)(LIMITED_FRAC_WIDTH - 1, 0))
    } .otherwise {
      // 正常数字（规约数或非规约数）
      val isSubnormal = (io.Exp(i) === (1 - BIAS).S) && (io.Frac(i)(LIMITED_FRAC_WIDTH) === 0.U)
      
      when(isSubnormal) {       // 非规约数
        exponentField := 0.U
        fractionField := io.Frac(i)(LIMITED_FRAC_WIDTH - 1, 0)
      } .otherwise {        // 规约数
        exponentField := (io.Exp(i) + BIAS.S).asUInt
        fractionField := io.Frac(i)(LIMITED_FRAC_WIDTH - 1, 0)
      }
    }

    io.float(i) := Cat(signBit, exponentField, fractionField)
  }
}

/* 示例：
   // 单精度向量编码（32位）
   val singleEncoder = Module(new FloatEncode(8, 23, 4))
   // 双精度向量编码（64位）
   val doubleEncoder = Module(new FloatEncode(11, 52, 4))
   // 半精度向量编码（16位）
   val halfEncoder   = Module(new FloatEncode(5, 10, 4))
*/

