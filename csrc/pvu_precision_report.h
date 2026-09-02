#ifndef PVU_PRECISION_REPORT_H
#define PVU_PRECISION_REPORT_H

#include <cstddef>
#include <cstdint>
#include <cmath>

namespace pvu {

class UlpHistogram {
 public:
  void add(uint64_t distance) {
    if (distance == 0) {
      ++zero_;
    } else if (distance == 1) {
      ++one_;
    } else if (distance <= 4) {
      ++two_to_four_;
    } else {
      ++five_or_more_;
    }
  }

  size_t zero() const { return zero_; }
  size_t one() const { return one_; }
  size_t two_to_four() const { return two_to_four_; }
  size_t five_or_more() const { return five_or_more_; }

 private:
  size_t zero_ = 0;
  size_t one_ = 0;
  size_t two_to_four_ = 0;
  size_t five_or_more_ = 0;
};

class PrecisionLossStats {
 public:
  void add_finite(long double source, long double converted) {
    const long double absolute_error = std::fabs(source - converted);
    const long double relative_error = absolute_error / std::fabs(source);
    ++finite_nonzero_;
    relative_error_sum_ += relative_error;
    if (relative_error > max_relative_error_) max_relative_error_ = relative_error;
    if (absolute_error > max_absolute_error_) max_absolute_error_ = absolute_error;
  }

  void add_zero() { ++zero_inputs_; }
  void add_special() { ++special_inputs_; }

  size_t finite_nonzero() const { return finite_nonzero_; }
  size_t zero_inputs() const { return zero_inputs_; }
  size_t special_inputs() const { return special_inputs_; }
  long double max_relative_error() const { return max_relative_error_; }
  long double mean_relative_error() const {
    return finite_nonzero_ == 0 ? 0.0L : relative_error_sum_ / finite_nonzero_;
  }
  long double max_absolute_error() const { return max_absolute_error_; }

 private:
  size_t finite_nonzero_ = 0;
  size_t zero_inputs_ = 0;
  size_t special_inputs_ = 0;
  long double relative_error_sum_ = 0.0L;
  long double max_relative_error_ = 0.0L;
  long double max_absolute_error_ = 0.0L;
};

}  // namespace pvu

#endif  // PVU_PRECISION_REPORT_H
