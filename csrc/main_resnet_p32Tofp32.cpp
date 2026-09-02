#include "../config.h"

#if defined(CONFIG_RESNET_POSIT32_TO_FP4) || defined(CONFIG_RESNET_POSIT32_TO_FP8) || \
    defined(CONFIG_RESNET_POSIT32_TO_FP16) || defined(CONFIG_RESNET_POSIT32_TO_FP32)

#include <verilated.h>
#include <verilated_vcd_c.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>

#include "VPvuTop.h"
#include "pvu_float_reference.h"
#include "pvu_precision_report.h"
#include "pvu_protocol_driver.h"

namespace {

constexpr const char* kPositFile = "./test_src/posit32_data.bin";
constexpr size_t kSampleCount = 1000;
constexpr size_t kLanes = 4;

struct FormatConfig {
  uint8_t mode;
  const char* name;
};

#if defined(CONFIG_RESNET_POSIT32_TO_FP4)
constexpr FormatConfig kFormat{0, "FP4"};
#elif defined(CONFIG_RESNET_POSIT32_TO_FP8)
constexpr FormatConfig kFormat{1, "FP8"};
#elif defined(CONFIG_RESNET_POSIT32_TO_FP16)
constexpr FormatConfig kFormat{2, "FP16"};
#else
constexpr FormatConfig kFormat{3, "FP32"};
#endif

uint64_t format_mask() {
  const auto format = pvu::kFpFormats.at(kFormat.mode);
  return pvu::width_mask(1 + format.exponent_bits + format.fraction_bits);
}

bool is_nan(uint64_t raw) {
  const auto format = pvu::kFpFormats.at(kFormat.mode);
  const uint64_t exponent = (raw >> format.fraction_bits) & pvu::width_mask(format.exponent_bits);
  return exponent == pvu::width_mask(format.exponent_bits) &&
         (raw & pvu::width_mask(format.fraction_bits)) != 0;
}

bool is_infinity(uint64_t raw) {
  const auto format = pvu::kFpFormats.at(kFormat.mode);
  const uint64_t exponent = (raw >> format.fraction_bits) & pvu::width_mask(format.exponent_bits);
  return exponent == pvu::width_mask(format.exponent_bits) &&
         (raw & pvu::width_mask(format.fraction_bits)) == 0;
}

bool is_subnormal(uint64_t raw) {
  const auto format = pvu::kFpFormats.at(kFormat.mode);
  const uint64_t exponent = (raw >> format.fraction_bits) & pvu::width_mask(format.exponent_bits);
  return exponent == 0 && (raw & pvu::width_mask(format.fraction_bits)) != 0;
}

uint64_t ordered(uint64_t raw) {
  const auto format = pvu::kFpFormats.at(kFormat.mode);
  const unsigned width = 1 + format.exponent_bits + format.fraction_bits;
  const uint64_t mask = pvu::width_mask(width);
  const uint64_t sign_bit = uint64_t{1} << (width - 1);
  raw &= mask;
  return (raw & sign_bit) ? ((~raw) & mask) : (raw | sign_bit);
}

uint64_t ulp_distance(uint64_t actual, uint64_t expected) {
  if (is_nan(actual) && is_nan(expected)) return 0;
  if (is_nan(actual) || is_nan(expected)) return std::numeric_limits<uint64_t>::max();
  const uint64_t ordered_actual = ordered(actual);
  const uint64_t ordered_expected = ordered(expected);
  return ordered_actual >= ordered_expected ? ordered_actual - ordered_expected
                                            : ordered_expected - ordered_actual;
}

std::array<uint32_t, kSampleCount> load_inputs() {
  std::array<uint32_t, kSampleCount> inputs{};
  std::ifstream file(kPositFile, std::ios::binary);
  if (!file) {
    std::cerr << "无法打开 Posit32 输入数据: " << kPositFile << "\n";
    std::exit(EXIT_FAILURE);
  }
  file.read(reinterpret_cast<char*>(inputs.data()), sizeof(inputs));
  if (file.gcount() != static_cast<std::streamsize>(sizeof(inputs))) {
    std::cerr << "Posit32 输入数据长度不足，期望 " << kSampleCount << " 个元素\n";
    std::exit(EXIT_FAILURE);
  }
  return inputs;
}

void print_report(const pvu::UlpHistogram& ulps, const pvu::PrecisionLossStats& loss,
                  size_t nan_inputs, size_t underflow_to_zero, size_t subnormal_outputs,
                  size_t overflow_to_infinity, size_t mismatches) {
  std::cout << "\n精度损失报告\n==========\n"
            << "转换: Posit<32,2> -> " << kFormat.name << "\n"
            << "样本数: " << kSampleCount << "\n"
            << "硬件-vs-golden ULP 分布: 0=" << ulps.zero()
            << ", 1=" << ulps.one() << ", 2-4=" << ulps.two_to_four()
            << ", >=5=" << ulps.five_or_more() << "\n"
            << "硬件-golden 非零 ULP 样本: " << mismatches << "\n"
            << "有限非零输入数: " << loss.finite_nonzero() << "\n"
            << std::scientific << std::setprecision(8)
            << "最大相对误差: " << static_cast<double>(loss.max_relative_error()) << "\n"
            << "平均相对误差: " << static_cast<double>(loss.mean_relative_error()) << "\n"
            << "最大绝对误差: " << static_cast<double>(loss.max_absolute_error()) << "\n"
            << std::defaultfloat
            << "特殊样本: Posit NaR=" << nan_inputs
            << ", 下溢到零=" << underflow_to_zero
            << ", FP 次正规=" << subnormal_outputs
            << ", 溢出到无穷=" << overflow_to_infinity << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Verilated::traceEverOn(true);
  VPvuTop top;
  VerilatedVcdC trace;
  trace.open("pvu_precision_batch.vcd");
  const auto inputs = load_inputs();
  const uint64_t mask = format_mask();

  top.clock = 0;
  top.reset = 1;
  top.eval();
  for (int cycle = 0; cycle < 2; ++cycle) {
    top.clock ^= 1;
    top.eval();
    trace.dump(cycle);
  }
  top.reset = 0;
  top.io_in_valid = 0;
  top.io_out_ready = 1;

  pvu::UlpHistogram ulps;
  pvu::PrecisionLossStats loss;
  size_t nan_inputs = 0;
  size_t underflow_to_zero = 0;
  size_t subnormal_outputs = 0;
  size_t overflow_to_infinity = 0;
  size_t mismatches = 0;

  for (size_t base = 0; base < kSampleCount; base += kLanes) {
    uint32_t inputs_lane[kLanes]{};
    uint64_t expected[kLanes]{};
    for (size_t lane = 0; lane < kLanes; ++lane) {
      inputs_lane[lane] = inputs[base + lane];
      expected[lane] = pvu::ref_posit_to_float(inputs_lane[lane], kFormat.mode) & mask;
    }
    top.io_posit_i1_0 = inputs_lane[0]; top.io_posit_i1_1 = inputs_lane[1];
    top.io_posit_i1_2 = inputs_lane[2]; top.io_posit_i1_3 = inputs_lane[3];
    top.io_posit_i2_0 = 0; top.io_posit_i2_1 = 0; top.io_posit_i2_2 = 0; top.io_posit_i2_3 = 0;
    top.io_float_i_0 = 0; top.io_float_i_1 = 0; top.io_float_i_2 = 0; top.io_float_i_3 = 0;
    top.io_float_i2_0 = 0; top.io_float_i2_1 = 0; top.io_float_i2_2 = 0; top.io_float_i2_3 = 0;
    top.io_op = 7; top.io_Isposit = true; top.io_Outposit = false;
    top.io_float_mode = kFormat.mode; top.io_float_posit = false;
    top.io_src_posit_width = 32; top.io_dst_posit_width = 32; top.io_vector_size = kLanes;
    top.io_in_tag = base / kLanes;
    top.io_in_valid = 1;
    pvu_wait_until_request_ready(&top);
    top.clock = 1; top.eval(); trace.dump(base * 2 + 1);
    top.clock = 0; top.eval(); trace.dump(base * 2 + 2);
    top.io_in_valid = 0;
    pvu_wait_until_response_valid(&top);
    const uint64_t actual[kLanes] = {top.io_float_o_0 & mask, top.io_float_o_1 & mask,
                                     top.io_float_o_2 & mask, top.io_float_o_3 & mask};
    for (size_t lane = 0; lane < kLanes; ++lane) {
      const uint64_t distance = ulp_distance(actual[lane], expected[lane]);
      ulps.add(distance);
      if (distance != 0) ++mismatches;
      const long double source = pvu::posit_to_long_double(inputs_lane[lane]);
      if (std::isnan(source)) {
        ++nan_inputs;
      } else if (source == 0.0L) {
        loss.add_zero();
      } else if (is_infinity(expected[lane])) {
        ++overflow_to_infinity;
        loss.add_special();
      } else {
        const long double converted = pvu::decode_fp(expected[lane], pvu::kFpFormats.at(kFormat.mode));
        if (converted == 0.0L) ++underflow_to_zero;
        if (is_subnormal(expected[lane])) ++subnormal_outputs;
        loss.add_finite(source, converted);
      }
    }
  }
  trace.close();
  top.final();
  print_report(ulps, loss, nan_inputs, underflow_to_zero, subnormal_outputs, overflow_to_infinity, mismatches);
  return mismatches == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#endif
