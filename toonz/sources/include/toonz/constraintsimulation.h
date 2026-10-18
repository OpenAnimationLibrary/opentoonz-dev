#pragma once

#include "tdoubleparam.h"

#include <string>
#include <vector>

#undef DVAPI
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#else
#define DVAPI DV_IMPORT_API
#endif

class TXsheet;

// Gravity-only position simulation. Aim At remains an ordinary live constraint;
// the bake solves its input channels, so it is never applied a second time.
namespace ConstraintSimulation {
struct Settings {
  int firstFrame = 0, lastFrame = 71;  // Inclusive, zero-based scene frames.
  double fps = 24, gravity = 9.81;     // Downward acceleration, OT inches/s^2.
};
struct ChannelEdit {
  TDoubleParamP channel, before, after;
};
struct Bake {
  std::vector<ChannelEdit> channels;
  int mobileObjects = 0, compensatedObjects = 0;
};

// Preflights the whole result without changing scene channels or assignments.
// Mobile starts from rest. Other objects retain authored world motion; local
// compensation is baked only when a simulated ancestor would otherwise move it.
// Failure leaves the output empty and supplies a diagnostic.
DVAPI bool prepare(TXsheet &xsheet, const Settings &settings, Bake &bake,
                   std::string &error);
DVAPI void apply(const Bake &bake, bool after);
}  // namespace ConstraintSimulation
