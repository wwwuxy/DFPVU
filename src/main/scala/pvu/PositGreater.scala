package pvu

import chisel3._
import chisel3.util._

/**
 * PositGreater模块 - 比较两个解码后的posit向量，选择较大值并直接输出编码后的Posit
 * 
 * @param MAX_POSIT_WIDTH 最大posit位宽
 * @param MAX_VECTOR_SIZE 最大向量大小
 * @param MAX_ALIGN_WIDTH 最大对齐宽度
 * @param ES ES参数
 */
class PositGreater(
  val MAX_POSIT_WIDTH: Int,
  val MAX_VECTOR_SIZE: Int,
  val MAX_ALIGN_WIDTH: Int,
  val ES: Int
) extends Module {
  
  // 定义相关常量
  val SRC_ND_MAX: Int         = log2Ceil(MAX_POSIT_WIDTH - 1)
  val SRC_EXP_WIDTH_MAX: Int  = SRC_ND_MAX + ES + 1
  val SRC_FRAC_WIDTH_MAX: Int = MAX_POSIT_WIDTH + 1
  
  val io = IO(new Bundle {
    // 输入信号 - 解码后的PIR格式
    val pir_sign1_i = Input(Vec(MAX_VECTOR_SIZE, UInt(1.W)))
    val pir_sign2_i = Input(Vec(MAX_VECTOR_SIZE, UInt(1.W)))
    val pir_exp1_i  = Input(Vec(MAX_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
    val pir_exp2_i  = Input(Vec(MAX_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
    val pir_frac1_i = Input(Vec(MAX_VECTOR_SIZE, UInt(SRC_FRAC_WIDTH_MAX.W)))
    val pir_frac2_i = Input(Vec(MAX_VECTOR_SIZE, UInt(SRC_FRAC_WIDTH_MAX.W)))
    
    // 原始Posit输入 - 用于检测特殊值
    val posit_i1    = Input(Vec(MAX_VECTOR_SIZE, UInt(MAX_POSIT_WIDTH.W)))
    val posit_i2    = Input(Vec(MAX_VECTOR_SIZE, UInt(MAX_POSIT_WIDTH.W)))
    
    // 输出信号 - 直接输出编码后的Posit
    val posit_o = Output(Vec(MAX_VECTOR_SIZE, UInt(MAX_POSIT_WIDTH.W)))
    
    // 保留PIR格式输出用于调试
    val pir_sign_o = Output(Vec(MAX_VECTOR_SIZE, UInt(1.W)))
    val pir_exp_o  = Output(Vec(MAX_VECTOR_SIZE, SInt(SRC_EXP_WIDTH_MAX.W)))
    val pir_frac_o = Output(Vec(MAX_VECTOR_SIZE, UInt(SRC_FRAC_WIDTH_MAX.W)))
    
    // 目标位宽输入
    val dst_posit_width = Input(UInt(6.W))  // 目标Posit位宽，值为0表示与源相同
  })
  
  // 初始化输出
  for (i <- 0 until MAX_VECTOR_SIZE) {
    io.pir_sign_o(i) := 0.U
    io.pir_exp_o(i)  := 0.S
    io.pir_frac_o(i) := 0.U
    io.posit_o(i)    := 0.U
  }
  
  // 计算实际使用的目标参数
  val ACTUAL_DST_POSIT_WIDTH = Mux(io.dst_posit_width === 0.U, MAX_POSIT_WIDTH.U, io.dst_posit_width)
  
  // 定义NaR值的常量 (0x80000000)
  val NAR_VALUE = (1.U << (MAX_POSIT_WIDTH - 1))
  
  // 比较逻辑
  for (i <- 0 until MAX_VECTOR_SIZE) {
    // 检测特殊值 NaR (0x80000000)
    val is_nar1 = io.posit_i1(i) === NAR_VALUE
    val is_nar2 = io.posit_i2(i) === NAR_VALUE
    
    // 比较两个posit数 - 给默认值true.B，确保完全初始化
    val is_greater = WireDefault(true.B)
    
    // 检查值是否为0 (如果尾数为0)
    val is_zero1 = io.pir_frac1_i(i) === 0.U && !is_nar1
    val is_zero2 = io.pir_frac2_i(i) === 0.U && !is_nar2
    
    // 处理特殊情况：两个都是NaR (0x80000000)
    when(is_nar1 && is_nar2) {
      // 两者都是NaR，直接输出NaR
      io.posit_o(i) := NAR_VALUE
      // 设置PIR格式输出为NaR对应的值
      io.pir_sign_o(i) := 1.U  // 负号
      io.pir_exp_o(i)  := 0.S  // 指数为0
      io.pir_frac_o(i) := 0.U  // 尾数为0
    }.elsewhen(is_nar1) {
      // 只有第一个是NaR，选择第二个输入
      io.pir_sign_o(i) := io.pir_sign2_i(i)
      io.pir_exp_o(i)  := io.pir_exp2_i(i)
      io.pir_frac_o(i) := io.pir_frac2_i(i)
    }.elsewhen(is_nar2) {
      // 只有第二个是NaR，选择第一个输入
      io.pir_sign_o(i) := io.pir_sign1_i(i)
      io.pir_exp_o(i)  := io.pir_exp1_i(i)
      io.pir_frac_o(i) := io.pir_frac1_i(i)
    }.otherwise {
      // 正常比较逻辑
      // 处理特殊情况：0值比较
      when(is_zero1 && is_zero2) {
        // 两者都是0，选择第一个输入
        is_greater := true.B
      }.elsewhen(is_zero1) {
        // 只有第一个是0，第二个输入更大 (除非是负数)
        is_greater := io.pir_sign2_i(i) === 1.U
      }.elsewhen(is_zero2) {
        // 只有第二个是0，第一个输入更大 (除非是负数)
        is_greater := io.pir_sign1_i(i) === 0.U
      }.elsewhen(io.pir_sign1_i(i) === 0.U && io.pir_sign2_i(i) === 1.U) {
        // 第一个数是正数，第二个是负数，所以第一个更大
        is_greater := true.B
      }.elsewhen(io.pir_sign1_i(i) === 1.U && io.pir_sign2_i(i) === 0.U) {
        // 第一个数是负数，第二个是正数，所以第二个更大
        is_greater := false.B
      }.elsewhen(io.pir_sign1_i(i) === 0.U && io.pir_sign2_i(i) === 0.U) {
        // 两个都是正数，比较指数
        when(io.pir_exp1_i(i) > io.pir_exp2_i(i)) {
          is_greater := true.B
        }.elsewhen(io.pir_exp1_i(i) < io.pir_exp2_i(i)) {
          is_greater := false.B
        }.otherwise {
          // 指数相等，比较尾数
          is_greater := io.pir_frac1_i(i) >= io.pir_frac2_i(i)
        }
      }.otherwise {
        // 两个都是负数，反向比较（绝对值小的负数更大）
        when(io.pir_exp1_i(i) > io.pir_exp2_i(i)) {
          is_greater := false.B  // 第一个绝对值更大，所以第二个负数更大
        }.elsewhen(io.pir_exp1_i(i) < io.pir_exp2_i(i)) {
          is_greater := true.B   // 第二个绝对值更大，所以第一个负数更大
        }.otherwise {
          // 指数相等，比较尾数（对于负数，尾数小的值更大）
          is_greater := io.pir_frac1_i(i) <= io.pir_frac2_i(i)
        }
      }
      
      // 选择较大值的PIR格式
      when(is_greater) {
        io.pir_sign_o(i) := io.pir_sign1_i(i)
        io.pir_exp_o(i)  := io.pir_exp1_i(i)
        io.pir_frac_o(i) := io.pir_frac1_i(i)
      }.otherwise {
        io.pir_sign_o(i) := io.pir_sign2_i(i)
        io.pir_exp_o(i)  := io.pir_exp2_i(i)
        io.pir_frac_o(i) := io.pir_frac2_i(i)
      }
    }
  }
  
  // 编码逻辑：将PIR格式转换为Posit格式
  val encode = Module(new PositEncode(MAX_POSIT_WIDTH, MAX_VECTOR_SIZE, ES))
  
  // 连接输入
  for(i <- 0 until MAX_VECTOR_SIZE) {
    encode.io.pir_sign(i) := io.pir_sign_o(i)
    encode.io.pir_exp(i)  := io.pir_exp_o(i)
    encode.io.pir_frac(i) := io.pir_frac_o(i)
  }
  
  // 处理输出 - 考虑目标位宽和特殊值（NaR）
  for(i <- 0 until MAX_VECTOR_SIZE) {
    // 检测是否为NaR特殊值处理
    val is_nar1 = io.posit_i1(i) === NAR_VALUE
    val is_nar2 = io.posit_i2(i) === NAR_VALUE
    
    when(is_nar1 && is_nar2) {
      // 如果两个输入都是NaR，直接输出NaR
      io.posit_o(i) := NAR_VALUE
    }.otherwise {
      // 正常处理
      when(ACTUAL_DST_POSIT_WIDTH > MAX_POSIT_WIDTH.U) {
        // 如果目标位宽超过最大位宽，截断
        io.posit_o(i) := encode.io.posit(i)(MAX_POSIT_WIDTH-1, 0)
      }.elsewhen(ACTUAL_DST_POSIT_WIDTH < MAX_POSIT_WIDTH.U) {
        // 否则，在运行时调整位宽
        val valid_bits = ACTUAL_DST_POSIT_WIDTH - 1.U
        io.posit_o(i) := (encode.io.posit(i) >> (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)) << (MAX_POSIT_WIDTH.U - ACTUAL_DST_POSIT_WIDTH)
      }.otherwise {
        io.posit_o(i) := encode.io.posit(i)
      }
    }
  }
} 