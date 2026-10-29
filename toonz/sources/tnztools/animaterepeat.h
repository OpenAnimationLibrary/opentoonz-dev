#pragma once

#include <cmath>

namespace AnimateRepeat {

enum class Mode { Offset, Factor, Absolute };

struct Value {
  Mode mode;
  double amount;

  static Value capture(double before, double after, bool scale, bool absolute) {
    if (absolute || (scale && std::abs(before) < 1e-8))
      return {Mode::Absolute, after};
    return scale ? Value{Mode::Factor, after / before}
                 : Value{Mode::Offset, after - before};
  }

  bool apply(double current, double &result) const {
    if (!std::isfinite(current) || !std::isfinite(amount)) return false;
    switch (mode) {
    case Mode::Offset:
      result = current + amount;
      break;
    case Mode::Factor:
      result = current * amount;
      break;
    case Mode::Absolute:
      result = amount;
      break;
    }
    return std::isfinite(result);
  }
};

}  // namespace AnimateRepeat
