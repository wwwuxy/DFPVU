#ifndef PVU_QWEN3_METRICS_H
#define PVU_QWEN3_METRICS_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "softposit.h"

namespace pvu {

struct WorkloadMetrics {
  uint64_t cycles = 0;
  uint64_t requests = 0;
  uint64_t active_lanes = 0;
};

struct UlpBins {
  uint64_t zero = 0;
  uint64_t one = 0;
  uint64_t two_to_four = 0;
  uint64_t five_or_more = 0;

  void add(uint64_t distance) {
    if (distance == 0) {
      ++zero;
    } else if (distance == 1) {
      ++one;
    } else if (distance <= 4) {
      ++two_to_four;
    } else {
      ++five_or_more;
    }
  }
};

inline float p32_to_float(uint32_t raw) {
  posit32_t posit{};
  posit.v = raw;
  return static_cast<float>(convertP32ToDouble(posit));
}

inline uint32_t ordered_fp32_key(float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "float must be IEEE binary32");
  static_assert(std::numeric_limits<float>::is_iec559 &&
                    std::numeric_limits<float>::digits == 24,
                "float must be IEEE binary32");
  std::memcpy(&bits, &value, sizeof(bits));
  return (bits & UINT32_C(0x80000000)) != 0
             ? UINT32_C(0) - bits
             : bits | UINT32_C(0x80000000);
}

inline uint64_t fp32_ulp_distance(float lhs, float rhs) {
  const uint32_t lhs_key = ordered_fp32_key(lhs);
  const uint32_t rhs_key = ordered_fp32_key(rhs);
  return lhs_key >= rhs_key ? static_cast<uint64_t>(lhs_key - rhs_key)
                            : static_cast<uint64_t>(rhs_key - lhs_key);
}

struct ComparisonStats {
  uint64_t finite_references = 0;
  uint64_t zero_references = 0;
  uint64_t non_finite_references = 0;
  uint64_t non_finite_results = 0;
  uint64_t relative_error_samples = 0;
  UlpBins ulp;
  double relative_error_sum = 0.0;
  double max_relative_error = 0.0;
  double max_absolute_error = 0.0;

  void add(uint32_t raw_p32, float reference) {
    const float result = p32_to_float(raw_p32);
    const bool reference_is_finite = std::isfinite(reference);
    const bool result_is_finite = std::isfinite(result);

    if (reference_is_finite) {
      ++finite_references;
      if (reference == 0.0F) ++zero_references;
    } else {
      ++non_finite_references;
    }
    if (!result_is_finite) ++non_finite_results;
    if (!reference_is_finite || !result_is_finite) return;

    ulp.add(fp32_ulp_distance(result, reference));
    const double absolute_error =
        std::fabs(static_cast<double>(result) - static_cast<double>(reference));
    if (absolute_error > max_absolute_error) max_absolute_error = absolute_error;
    if (reference == 0.0F) return;

    const double relative_error = absolute_error / std::fabs(reference);
    ++relative_error_samples;
    relative_error_sum += relative_error;
    if (relative_error > max_relative_error) max_relative_error = relative_error;
  }

  uint64_t finite_nonzero_references() const {
    return finite_references - zero_references;
  }

  double mean_relative_error() const {
    return relative_error_samples == 0
               ? 0.0
               : relative_error_sum /
                     static_cast<double>(relative_error_samples);
  }
};

}  // namespace pvu

#endif  // PVU_QWEN3_METRICS_H
