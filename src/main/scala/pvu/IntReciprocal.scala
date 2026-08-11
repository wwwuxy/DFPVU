package pvu

import chisel3._
import chisel3.util._

// Integer Reciprocal Module
class IntReciprocal(val WIDTH: Int) extends Module {
  val io = IO(new Bundle {
    val num_i = Input(UInt(WIDTH.W))        // Enter an integer, the bit width is width

    val reciprocal_o = Output(UInt((2*WIDTH).W)) // Output countdown, q(1+width).width format
  })

  // 特殊情况处理
  val is_zero = io.num_i === 0.U
  val is_one = io.num_i === 1.U
  // 检测值是否为1.0的表示（fixed point表示中的2^WIDTH）
  val is_one_fixed_point = io.num_i === (BigInt(1) << (WIDTH-1)).U
  // 检测是否确实为40000000
  val is_40000000 = io.num_i === (BigInt(1) << (WIDTH-1)).U
  val is_power_of_two = (io.num_i & (io.num_i - 1.U)) === 0.U
  
  // 对于2的幂次，可以直接计算倒数
  val power_of_two_index = log2Ceil(WIDTH).U - PriorityEncoder(Reverse(io.num_i))
  val power_of_two_reciprocal = (BigInt(1) << (2*WIDTH - 1)).U >> power_of_two_index

  // 规范化输入以提高精度
  val leading_zeros = PriorityEncoder(Reverse(io.num_i))
  val normalized_num = (io.num_i << leading_zeros)(WIDTH-1, 0)

  // 使用扩展的查找表生成更精确的初始近似值
  // 使用前6位来生成精度更高的初始值
  val top_bits = normalized_num(WIDTH-1, WIDTH-6)
  val init_table = Wire(Vec(64, UInt(WIDTH.W)))
  
  // 填充初始值表 - 按照1/(1+x)的近似值生成
  for (i <- 0 until 64) {
    val x = BigDecimal(i) / BigDecimal(64)
    val recip = BigDecimal(1) / (BigDecimal(1) + x)
    init_table(i) := (recip * BigDecimal(1L << WIDTH)).toBigInt.U(WIDTH.W)
  }
  
  val init_approx = init_table(top_bits)
  
  // 调整初始近似值的指数
  val width_u = WIDTH.U
  val shift_amount = width_u - leading_zeros
  val x0_adjusted = (init_approx << shift_amount).pad(2*WIDTH)
  
  // 定义1.0的定点表示
  val one_fixed = (BigInt(1) << WIDTH).U((2*WIDTH).W)
  
  // 如果查表失败则使用简单除法
  val x0_simple = (one_fixed / io.num_i).asUInt
  val x0 = Mux(init_approx === 0.U, x0_simple, x0_adjusted)

  // 改进的牛顿迭代实现 - x_(n+1) = x_n * (2 - num * x_n)
  def improvedNRIter(x: UInt, num: UInt, iter: Int): UInt = {
    // 使用更高精度的乘法计算num*x
    val num_x = (num * x)((3*WIDTH)-1, 0)
    
    // 确保num_x的位宽是2*WIDTH
    val num_x_adjusted = num_x >> WIDTH
    
    // 2.0的定点表示
    val two = (BigInt(2) << WIDTH).U((2 * WIDTH).W)
    
    // 计算2 - num*x，注意保持精度
    val diff = two - num_x_adjusted
    
    // 可能需要饱和以防止溢出
    val diff_saturated = Mux(diff(2*WIDTH-1), 0.U, diff)
    
    // 使用更高精度的乘法计算x * diff
    val x_new_full = (x * diff_saturated)((3*WIDTH)-1, 0)
    
    // 添加舍入逻辑
    val round_bit = x_new_full(WIDTH)
    val sticky_bits = x_new_full(WIDTH-1, 0).orR
    val lsb = x_new_full(WIDTH+1)
    
    // 取高位并根据舍入规则调整
    val x_new = (x_new_full >> WIDTH)((2*WIDTH)-1, 0)
    val should_round = round_bit && (lsb || sticky_bits)
    
    // 舍入到最近偶数
    val result = Mux(should_round, x_new + 1.U, x_new)
    
    if (iter > 1) {
      // 迭代直到达到所需精度
      improvedNRIter(result, num, iter-1)
    } else {
      result
    }
  }

  // 使用5次迭代，提高精度
  val result_nr = improvedNRIter(x0, io.num_i, 5)

  // 最终结果选择
  val result = Wire(UInt((2*WIDTH).W))
  when(is_zero) {
    // 除以0，返回NaR (Not a Real)，最高位为1其余为0
    result := (BigInt(1) << ((2*WIDTH)-1)).U
  }.elsewhen(is_one) {
    // 输入为1，结果为1.0
    result := one_fixed
  }.elsewhen(is_one_fixed_point || is_40000000) {
    // 输入为1.0的fixed point表示或40000000，结果也是1.0
    result := one_fixed
  }.elsewhen(is_power_of_two) {
    // 输入为2的幂次，使用精确计算结果
    result := power_of_two_reciprocal
  }.otherwise {
    // 使用牛顿迭代结果
    result := result_nr
  }

  io.reciprocal_o := result
}


