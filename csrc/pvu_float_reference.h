#ifndef PVU_FLOAT_REFERENCE_H
#define PVU_FLOAT_REFERENCE_H

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include "softposit.h"

namespace pvu {

struct FpFormat {
  uint8_t exponent_bits;
  uint8_t fraction_bits;
};

constexpr std::array<FpFormat, 5> kFpFormats{{{1, 2}, {4, 3}, {5, 10}, {8, 23}, {11, 52}}};

inline uint64_t width_mask(unsigned width) {
  return width == 64 ? std::numeric_limits<uint64_t>::max() : ((uint64_t{1} << width) - 1);
}

inline bool is_posit_nar(uint32_t raw) { return raw == 0x80000000u; }

inline long double decode_fp(uint64_t raw, FpFormat format) {
  const unsigned width = 1 + format.exponent_bits + format.fraction_bits;
  raw &= width_mask(width);
  const uint64_t fraction = raw & width_mask(format.fraction_bits);
  const uint64_t exponent = (raw >> format.fraction_bits) & width_mask(format.exponent_bits);
  const bool sign = (raw >> (width - 1)) != 0;
  const uint64_t exponent_max = width_mask(format.exponent_bits);
  const int bias = (1 << (format.exponent_bits - 1)) - 1;
  if (exponent == exponent_max) {
    if (fraction != 0) return std::numeric_limits<long double>::quiet_NaN();
    return sign ? -std::numeric_limits<long double>::infinity() : std::numeric_limits<long double>::infinity();
  }
  const long double value = exponent == 0
    ? std::ldexp(static_cast<long double>(fraction), 1 - bias - format.fraction_bits)
    : std::ldexp(1.0L + std::ldexp(static_cast<long double>(fraction), -format.fraction_bits),
                 static_cast<int>(exponent) - bias);
  return sign ? -value : value;
}

inline uint64_t round_even(long double value) {
  const long double floor_value = std::floor(value);
  const long double fraction = value - floor_value;
  if (fraction > 0.5L) return static_cast<uint64_t>(floor_value + 1.0L);
  if (fraction < 0.5L) return static_cast<uint64_t>(floor_value);
  const uint64_t lower = static_cast<uint64_t>(floor_value);
  return (lower & 1u) == 0 ? lower : lower + 1;
}

inline uint64_t encode_fp(long double value, FpFormat format) {
  const unsigned width = 1 + format.exponent_bits + format.fraction_bits;
  const uint64_t sign = std::signbit(value) ? 1 : 0;
  const uint64_t exponent_max = width_mask(format.exponent_bits);
  const uint64_t fraction_mask = width_mask(format.fraction_bits);
  if (std::isnan(value)) return (sign << (width - 1)) | (exponent_max << format.fraction_bits) | 1;
  if (std::isinf(value)) return (sign << (width - 1)) | (exponent_max << format.fraction_bits);
  const long double magnitude = std::fabs(value);
  if (magnitude == 0.0L) return sign << (width - 1);

  const int bias = (1 << (format.exponent_bits - 1)) - 1;
  const int minimum_normal_exponent = 1 - bias;
  const int maximum_normal_exponent = static_cast<int>(exponent_max - 1) - bias;
  int exponent = static_cast<int>(std::floor(std::log2(magnitude)));
  uint64_t fraction = 0;
  uint64_t exponent_field = 0;
  if (exponent < minimum_normal_exponent) {
    fraction = round_even(std::ldexp(magnitude, format.fraction_bits - minimum_normal_exponent));
    if (fraction == 0) return sign << (width - 1);
    if (fraction > fraction_mask) {
      exponent_field = 1;
      fraction = 0;
    }
  } else {
    if (exponent > maximum_normal_exponent) {
      return (sign << (width - 1)) | (exponent_max << format.fraction_bits);
    }
    fraction = round_even(std::ldexp(std::ldexp(magnitude, -exponent) - 1.0L, format.fraction_bits));
    if (fraction > fraction_mask) {
      fraction = 0;
      ++exponent;
      if (exponent > maximum_normal_exponent) {
        return (sign << (width - 1)) | (exponent_max << format.fraction_bits);
      }
    }
    exponent_field = static_cast<uint64_t>(exponent + bias);
  }
  return (sign << (width - 1)) | (exponent_field << format.fraction_bits) | fraction;
}

inline long double posit_to_long_double(uint32_t raw) {
  if (is_posit_nar(raw)) return std::numeric_limits<long double>::quiet_NaN();
  posit32_t posit{};
  posit.v = raw;
  return static_cast<long double>(convertP32ToDouble(posit));
}

inline uint64_t ref_posit_to_float(uint32_t raw, uint8_t mode) {
  return encode_fp(posit_to_long_double(raw), kFpFormats.at(mode));
}

}  // namespace pvu

#endif  // PVU_FLOAT_REFERENCE_H
