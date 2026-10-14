// Standalone tests: c++ -std=c++14 -Wall -Wextra -Werror
//   toonz/sources/tnztools/tests/rasterautofill_test.cpp -o
//   /tmp/rasterautofill_test /tmp/rasterautofill_test
#include "../rasterautofill.h"
#include <cstdlib>
#include <iostream>

using namespace RasterAutoFill;

struct Pixel {
  int ink = 0, paint = 0, tone = 255;
  int getInk() const { return ink; }
  int getPaint() const { return paint; }
  int getTone() const { return tone; }
};
struct Image {
  int w, h;
  std::vector<Pixel> pixels;
  Image(int width, int height) : w(width), h(height), pixels(width * height) {}
  Pixel &at(int x, int y) { return pixels[y * w + x]; }
  const Pixel &operator()(int x, int y) const { return pixels[y * w + x]; }
  void ink(int x, int y, int style = 1, int tone = 0) {
    at(x, y).ink  = style;
    at(x, y).tone = tone;
  }
  void box(int x0, int y0, int x1, int y1, int style = 1, int tone = 0) {
    for (int x = x0; x <= x1; ++x) {
      ink(x, y0, style, tone);
      ink(x, y1, style, tone);
    }
    for (int y = y0; y <= y1; ++y) {
      ink(x0, y, style, tone);
      ink(x1, y, style, tone);
    }
  }
};
static int checks = 0;
#define CHECK(expr)                                                            \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(expr)) {                                                             \
      std::cerr << "FAIL at line " << __LINE__ << ": " << #expr << '\n';       \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

int main() {
  Image before(40, 30), after = before;
  StrokeEnd end;
  end.first = Point(5, 15);
  end.last  = Point(20, 15);
  after.ink(5, 15);
  after.ink(20, 15);
  for (int x = 16; x < 20; ++x) after.ink(x, 15);  // new tail is not a target
  CHECK(!findClosure(40, 30, before, after, end, 1, 6).found);

  before.ink(24, 15);
  after.ink(24, 15);
  Closure c = findClosure(40, 30, before, after, end, 1, 20);
  CHECK(c.found && c.hasGap && c.to == Point(24, 15));
  before.ink(20, 17, 2);
  after.ink(20, 17, 2);  // nearer, wrong style
  c = findClosure(40, 30, before, after, end, 1, 20);
  CHECK(c.to == Point(24, 15));
  before.ink(21, 15, 1, 255);
  after.ink(21, 15, 1, 255);  // latent ink ID
  CHECK(findClosure(40, 30, before, after, end, 1, 6).to == Point(24, 15));
  before.ink(23, 15, 1, 128);
  after.ink(23, 15, 1, 128);  // visible AA target
  CHECK(findClosure(40, 30, before, after, end, 1, 6).to == Point(23, 15));
  CHECK(!findClosure(40, 30, before, after, end, 1, 1).found);
  CHECK(!findClosure(40, 30, before, after, end, 1, 0).found);
  CHECK(!findClosure(40, 30, before, after, end, 0, 6).found);

  // The start competes with an adjacent line, rather than always winning.
  end.first = Point(20, 12);
  after.ink(20, 12);
  before.at(23, 15) = Pixel();
  after.at(23, 15)  = Pixel();
  CHECK(findClosure(40, 30, before, after, end, 1, 6).to == Point(20, 12));
  before.ink(20, 15);  // already touching old same-style ink
  c = findClosure(40, 30, before, after, end, 1, 6);
  CHECK(c.found && !c.hasGap);

  Image b2(20, 20), a2 = b2;
  end.first = Point(2, 2);
  end.last  = Point(10, 10);
  a2.ink(2, 2);
  a2.ink(10, 10);
  b2.ink(14, 10);
  a2.ink(14, 10);
  b2.ink(12, 10, 2);
  a2.ink(12, 10, 2);  // cannot cross foreign ink
  CHECK(!findClosure(20, 20, b2, a2, end, 1, 5).found);
  end.last = Point(-1, 10);
  CHECK(!findClosure(20, 20, b2, a2, end, 1, 5).found);

  Image old(60, 40);
  old.box(2, 2, 12, 12);           // pre-existing empty enclosure
  old.box(20, 4, 55, 35, 3, 100);  // AA outline, one-pixel gap
  old.at(40, 4) = Pixel();
  Image now     = old;
  now.ink(40, 4, 3, 100);
  Regions r = newlyEnclosed(60, 40, old, now);
  CHECK(r.seeds.size() == 1);
  CHECK(r.interior[10 * 60 + 30] != 0);
  CHECK(r.interior[30 * 60 + 50] !=
        0);                             // much farther than closing-stroke bbox
  CHECK(r.interior[5 * 60 + 5] == 0);   // old enclosure stays empty
  CHECK(r.interior[1 * 60 + 30] == 0);  // outside stays empty
  CHECK(newlyEnclosed(60, 40, old, old).seeds.empty());
  CHECK(newlyEnclosed(60, 40, now, old).seeds.empty());  // erasing cannot close

  // Preserve previously painted pixels, including paint without an ink edge.
  now.at(30, 10).paint = 8;
  r                    = newlyEnclosed(60, 40, old, now);
  CHECK(r.interior[10 * 60 + 30] == 0);
  CHECK(r.interior[11 * 60 + 30] != 0);

  // A crop/canvas edge is NOT a missing outline segment.
  Image edgeBefore(12, 12), edgeAfter(12, 12);
  for (int y = 0; y < 12; ++y) edgeAfter.ink(6, y);
  CHECK(newlyEnclosed(12, 12, edgeBefore, edgeAfter).seeds.empty());

  // Same index with tone 255 must not create an invisible enclosure.
  Image invisible(12, 12), empty(12, 12);
  invisible.box(2, 2, 9, 9, 1, 255);
  CHECK(newlyEnclosed(12, 12, empty, invisible).seeds.empty());

  // Distinct new regions each get a seed; an old nested hole is protected.
  Image multiBefore(30, 20), multiAfter = multiBefore;
  multiBefore.box(5, 5, 8, 8);
  multiAfter = multiBefore;
  multiAfter.box(2, 2, 12, 12);
  multiAfter.box(18, 2, 27, 12);
  Regions multi = newlyEnclosed(30, 20, multiBefore, multiAfter);
  CHECK(multi.seeds.size() == 2);
  CHECK(multi.interior[6 * 30 + 6] == 0);

  // Scratch-fill validation accepts interior and native AA underpaint only.
  Image filled            = now;
  filled.at(31, 10).paint = 4;
  filled.at(40, 4).paint  = 4;
  CHECK(fillStayedInside(60, 40, now, filled, r.interior));
  filled.at(0, 0).paint = 4;
  CHECK(!fillStayedInside(60, 40, now, filled, r.interior));
  filled                = now;
  filled.at(5, 5).paint = 4;
  CHECK(!fillStayedInside(60, 40, now, filled, r.interior));
  filled                  = now;
  filled.at(30, 10).paint = 4;
  CHECK(!fillStayedInside(60, 40, now, filled, r.interior));
  CHECK(!fillStayedInside(60, 40, now, now, Mask()));
  CHECK(pixelCount(0, 40) == 0);
  bool overflow = false;
  try {
    pixelCount(std::numeric_limits<int>::max(), 2);
  } catch (const std::length_error &) {
    overflow = true;
  }
  CHECK(overflow);
  std::cout << checks << " raster autoclose/autofill checks passed\n";
}
