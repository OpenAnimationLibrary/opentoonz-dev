#include "tnztools/trailcyclestate.h"

#include <stdexcept>
#include <limits>

namespace {

using TrailCycle::Mode;
using TrailCycle::State;
using TrailCycle::StyleKey;

const StyleKey first{nullptr, 1, "raster:first"};
const StyleKey replacement{nullptr, 1, "raster:replacement"};

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

int stamp(State &state, Mode mode, const StyleKey &style = first,
          int count = 3) {
  const auto selection = state.begin(mode, style, count);
  state.commit(selection, true);
  return selection.offset;
}

void checkModes() {
  State forward, backward, repeat;
  for (int expected : {0, 1, 2, 0})
    require(stamp(forward, Mode::Forward) == expected,
            "Forward sequence failed");
  for (int expected : {2, 1, 0, 2})
    require(stamp(backward, Mode::Backward) == expected,
            "Backward sequence failed");
  for (int i = 0; i < 4; ++i)
    require(stamp(repeat, Mode::Repeat) == 0, "Initial Repeat sequence failed");
  State shared;
  stamp(shared, Mode::Forward);
  stamp(shared, Mode::Forward);
  stamp(shared, Mode::Forward);
  require(stamp(shared, Mode::Backward) == 1, "Backward did not backtrack");
  require(stamp(shared, Mode::Repeat) == 1, "Repeat did not hold");
  require(stamp(shared, Mode::Repeat) == 1, "Repeat moved the cursor");
  require(stamp(shared, Mode::Forward) == 2, "Forward did not resume");
}

void checkCancellationAndReplacement() {
  State state;
  stamp(state, Mode::Forward);
  const auto canceled = state.begin(Mode::Forward, first, 3);
  state.commit(canceled, false);
  require(stamp(state, Mode::Forward) == 1, "Canceled gesture advanced");
  state.commit(state.begin(Mode::Forward, replacement, 3), false);
  require(stamp(state, Mode::Repeat) == 1,
          "Canceled replacement erased cursor");
  require(stamp(state, Mode::Forward, replacement) == 0,
          "Same-slot replacement did not reset");
  require(stamp(state, Mode::Backward, first) == 2,
          "Returning from another Trail did not reset");
  require(stamp(state, Mode::Forward, first, 2) == 0,
          "Frame-count change did not reset");
}

void checkInactiveAndReplicas() {
  State state;
  stamp(state, Mode::Forward);
  state.commit(state.begin(Mode::Off, replacement, 3), true);
  state.commit(state.begin(Mode::Forward, replacement, 1), true);
  state.commit(state.begin(Mode::Forward, replacement, 0), true);
  require(stamp(state, Mode::Repeat) == 0, "Inactive style erased cursor");
  const auto gesture = state.begin(Mode::Forward, first, 3);
  for (int replica = 0; replica < 4; ++replica) {
    require(gesture.offset == 1 && gesture.step == 1,
            "Replicas are out of phase");
    state.commit(gesture, true);
  }
  require(stamp(state, Mode::Forward) == 2, "Replicas advanced more than once");
}

void checkExplicitStarts() {
  auto draw = [](State &state, Mode mode, int start) {
    auto selection = state.begin(mode, first, 4, start);
    state.commit(selection, true);
    return selection.offset;
  };
  State forward, backward, repeat;
  for (int expected : {1, 2, 3, 0, 1})
    require(draw(forward, Mode::Forward, 1) == expected,
            "Explicit Forward start failed");
  for (int expected : {1, 0, 3, 2})
    require(draw(backward, Mode::Backward, 1) == expected,
            "Explicit Backward start failed");
  for (int i = 0; i < 4; ++i)
    require(draw(repeat, Mode::Repeat, 2) == 2, "Explicit Repeat moved");
  require(draw(forward, Mode::Repeat, 1) == 1,
          "Repeat did not use the selected drawing");
  require(draw(forward, Mode::Forward, 3) == 3,
          "Changing start did not restart");
  require(draw(forward, Mode::Forward, 3) == 0,
          "New start was reapplied on every gesture");
  auto canceled = forward.begin(Mode::Forward, first, 4, 2);
  forward.commit(canceled, false);
  require(draw(forward, Mode::Forward, 3) == 1,
          "Canceled offset change replaced cursor");
  for (int missing : {-1, -2, 4, (std::numeric_limits<int>::max)()}) {
    State state, control;
    draw(state, Mode::Forward, 1);
    draw(control, Mode::Forward, 1);
    require(
        draw(state, Mode::Forward, missing) == draw(control, Mode::Forward, -1),
        "Invalid start changed the established cycle");
    require(draw(state, Mode::Repeat, missing) == 2,
            "Invalid Repeat moved the cursor");
    require(draw(state, Mode::Forward, 1) == 3,
            "Returning from an invalid start reset the cursor");
  }
  State state;
  require(draw(state, Mode::Forward, 2) == 2, "Setup failed");
  auto off = state.begin(Mode::Off, replacement, 4, 1);
  require(off.active && !off.updatesCursor && off.offset == 1 && off.step == 1,
          "Off cannot select a starting frame without cycling");
  state.commit(off, true);
  require(draw(state, Mode::Repeat, -1) == 2,
          "Off selection replaced the retained palette/cursor");
  auto single = state.begin(Mode::Repeat, replacement, 1, 0);
  require(single.active && !single.updatesCursor && single.offset == 0,
          "Single-frame explicit selection failed");
  state.commit(single, true);
  require(draw(state, Mode::Repeat, -1) == 2,
          "Single-frame selection replaced the cycle");
  require(!state.begin(Mode::Repeat, first, 0, 0).active,
          "Empty source became active");
  const int maximum = (std::numeric_limits<int>::max)();
  State limit;
  auto last = limit.begin(Mode::Forward, first, maximum, maximum - 1);
  limit.commit(last, true);
  require(limit.begin(Mode::Forward, first, maximum, maximum - 1).offset == 0,
          "Large start overflowed during advancement");
}

}  // namespace

int main() {
  for (int value : {-1, 4, std::numeric_limits<int>::min(),
                    std::numeric_limits<int>::max()})
    require(TrailCycle::modeFromValue(value) == Mode::Off,
            "Invalid mode was not defaulted");
  for (int value = 0; value < 4; ++value)
    require(int(TrailCycle::modeFromValue(value)) == value,
            "Persisted mode changed");
  State identity;
  int paletteA = 0, paletteB = 0;
  require(stamp(identity, Mode::Forward, {&paletteA, 1, "trail"}) == 0,
          "First palette failed");
  require(stamp(identity, Mode::Forward, {&paletteA, 1, "trail"}) == 1,
          "Cursor failed");
  require(stamp(identity, Mode::Forward, {&paletteB, 1, "trail"}) == 0,
          "Palette switch did not reset");
  require(stamp(identity, Mode::Forward, {&paletteB, 2, "trail"}) == 0,
          "Style switch did not reset");
  checkExplicitStarts();
  checkModes();
  checkCancellationAndReplacement();
  checkInactiveAndReplicas();
}
