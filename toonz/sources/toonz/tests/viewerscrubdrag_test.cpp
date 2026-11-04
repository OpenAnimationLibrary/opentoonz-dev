
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
#include <utility>
int main() {
  using Direction = ViewerScrubDirection;
  CHECK(viewerScrubDirection(2, -2, 4) == Direction::Undecided);
  CHECK(viewerScrubDirection(100, -41, 4) == Direction::Horizontal);
  CHECK(viewerScrubDirection(100, -42, 4) == Direction::UpRight);
  CHECK(viewerScrubDirection(-41, 100, 4) == Direction::Vertical);
  CHECK(viewerScrubDirection(-42, 100, 4) == Direction::UpRight);
  CHECK(viewerScrubDirection(5, 5, 4) == Direction::DownRight);
  CHECK(viewerScrubDirection(-5, -5, 4) == Direction::DownRight);
  CHECK(viewerScrubDirection(0, 2, 2) == Direction::Vertical);
  CHECK(!viewerScrubUsesDrawings(Direction::Horizontal, false));
  CHECK(viewerScrubUsesDrawings(Direction::Vertical, true));
  CHECK(viewerScrubUsesDrawings(Direction::DownRight, false));
  CHECK(!viewerScrubUsesDrawings(Direction::UpRight, true));
  CHECK(viewerScrubPosition(Direction::Horizontal, 5, -3) == 5);
  CHECK(viewerScrubPosition(Direction::Vertical, 5, -3) == -3);
  CHECK(viewerScrubPosition(Direction::DownRight, 2, 2) > 2);
  CHECK(viewerScrubPosition(Direction::UpRight, -2, 2) < -2);
  CHECK(viewerScrubPosition(Direction::UpRight, 2, 2) == 0);
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
  // Same drawing on a different level must count as a different exposure.
  // A reused drawing and blank gap must each remain separate navigation stops.
  std::vector<std::pair<int, int>> cells(24, {0, 0});
  std::fill(cells.begin(), cells.begin() + 8, std::make_pair(1, 1));
  std::fill(cells.begin() + 8, cells.begin() + 12, std::make_pair(1, 2));
  std::fill(cells.begin() + 12, cells.begin() + 16, std::make_pair(1, 1));
  std::fill(cells.begin() + 18, cells.begin() + 20, std::make_pair(1, 1));
  std::fill(cells.begin() + 20, cells.end(), std::make_pair(2, 1));
  const auto cellAt = [&cells](int row) { return cells.at(row); };
  const auto stops  = viewerScrubDrawingStops(0, 23, cellAt);
  CHECK(stops == std::vector<int>({0, 8, 12, 16, 18, 20}));
  // At default strip sensitivity, a diagonal frame-mode drag skips the hold
  // while the same drag in drawing mode steps one frame.
  const auto diagonal = viewerScrubDirection(2, 2, 2);
  const auto position = viewerScrubPosition(diagonal, 2, 2);
  d.begin(0);
  CHECK(d.moveDrawing(position, 2, 0, 23, false, stops) == 8);
  d.begin(0);
  CHECK(d.move(position, 2, 0, 0, 23, false) == 1);
  d.begin(0);
  CHECK(d.moveDrawing(2, 2, 0, 23, false, stops) == 8);  // frame 1 -> 9
  CHECK(d.moveDrawing(0, 2, 8, 23, false, stops) == 0);  // frame 9 -> 1
  d.begin(0);
  CHECK(d.moveDrawing(0, 2, 3, 23, false, stops) == 3);
  CHECK(d.moveDrawing(1, 2, 3, 23, false, stops) == 3);
  CHECK(d.moveDrawing(2, 2, 3, 23, false, stops) == 8);
  // Large moves advance by exposure count, independent of hold lengths.
  d.begin(0);
  CHECK(d.moveDrawing(8, 2, 0, 23, false, stops) == 18);
  d.begin(0);
  CHECK(d.moveDrawing(-2, 2, 10, 23, false, stops) == 0);
  // Inclusive looping and clipping retain the beginning of a partial hold.
  d.begin(0);
  CHECK(d.moveDrawing(-2, 2, 0, 23, true, stops) == 20);
  CHECK(d.moveDrawing(0, 2, 20, 23, true, stops) == 0);
  const auto clipped = viewerScrubDrawingStops(4, 10, cellAt);
  CHECK(clipped == std::vector<int>({4, 8}));
  d.begin(0);
  CHECK(d.moveDrawing(2, 2, 3, 10, false, clipped) == 4);
  d.begin(0);
  CHECK(d.moveDrawing(-2, 2, 11, 10, false, clipped) == 8);
  const auto held = viewerScrubDrawingStops(0, 7, cellAt);
  CHECK(held == std::vector<int>({0}));
  d.begin(0);
  CHECK(d.moveDrawing(100, 2, 0, 7, true, held) == 0);
  CHECK(viewerScrubDrawingStops(8, 7, cellAt).empty());
  std::cout << "Viewer scrub frame and drawing movement tests passed\n";
}
