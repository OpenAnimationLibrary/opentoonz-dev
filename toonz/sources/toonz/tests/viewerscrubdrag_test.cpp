
#include "../viewerscrubdrag.h"
#include <cstdlib>
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Failed: " #condition << " at line " << __LINE__ << "\n";   \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)
#include <iostream>
int main() {
  ViewerScrubDrag d;
  // Event frequency must not change distance-based results.
  d.begin(0);
  int frame = 0;
  for (int x = 1; x <= 100; ++x) frame = d.move(x, 20, frame, 0, 1000, false);
  CHECK(frame == 5);
  d.begin(0);
  CHECK(d.move(100, 20, 0, 0, 1000, false) == 5);
  // Preserve tiny moves, signed reversal and fractional sensitivity.
  d.begin(0);
  CHECK(d.move(.2, .5, 10, 0, 100, false) == 10);
  CHECK(d.move(.5, .5, 10, 0, 100, false) == 11);
  CHECK(d.move(0, .5, 11, 0, 100, false) == 10);
  // Clamp large overshoot, then reverse immediately.
  d.begin(0);
  CHECK(d.move(2000, 2, 5, 0, 10, false) == 10);
  CHECK(d.move(1998, 2, 10, 0, 10, false) == 9);
  // Inclusive range wrapping in either direction, including multiple laps.
  d.begin(0);
  CHECK(d.move(2, 2, 20, 10, 20, true) == 10);
  CHECK(d.move(0, 2, 10, 10, 20, true) == 20);
  CHECK(d.move(-46, 2, 20, 10, 20, true) == 19);
  // A single-frame range and an invalid range remain safe.
  d.begin(0);
  CHECK(d.move(100, 2, 7, 7, 7, true) == 7);
  CHECK(d.move(200, 2, 7, 7, 6, true) == 7);
  // Strip defaults move ten times as many frames as the viewer default.
  d.begin(0);
  CHECK(d.move(40, 20, 0, 0, 100, false) == 2);
  d.begin(0);
  CHECK(d.move(40, 2, 0, 0, 100, false) == 20);
  std::cout << "Viewer scrub movement tests passed\n";
}
