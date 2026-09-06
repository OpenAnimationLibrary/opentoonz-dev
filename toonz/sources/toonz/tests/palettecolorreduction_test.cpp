// Standalone regression test using the production reducer and CM32 pixel type.
// From the repository root on Linux:
// g++ -std=c++14 -DLINUX -DTNZ_LITTLE_ENDIAN=1 -I toonz/sources/include \
//   -fsanitize=address,undefined -fno-omit-frame-pointer \
//   toonz/sources/toonz/tests/palettecolorreduction_test.cpp \
//   toonz/sources/common/tcolor/tpixel.cpp -o /tmp/reduce-test
// ASAN_OPTIONS=detect_leaks=0 /tmp/reduce-test

#include "../palettecolorreduction.h"

#include <cassert>
#include <iostream>
#include <random>
#include <set>

using namespace PaletteColorReduction;

namespace {

void validate(const Plan &plan, const std::vector<Color> &colors,
              const Used &used) {
  assert(plan.styles[0] == 0);
  std::map<int, TPixel32> palette;
  for (const Color &c : colors) palette.emplace(c.id, c.rgba);
  std::set<int> survivors;
  for (int id = 1; id < 4096; ++id) {
    int to = plan.styles[id];
    assert(to > 0 && to < 4096);
    assert(plan.styles[to] == to);
    if (!palette.count(id)) {
      assert(to == id);
      continue;
    }
    assert(palette.count(to));
    assert(palette.at(to).m == palette.at(id).m);
    if (used[id]) survivors.insert(to);
  }
  assert(int(survivors.size()) == plan.after);
}

std::array<int, 4> render(const TPixelCM32 &p,
                          const std::array<TPixel32, 4096> &palette) {
  const TPixel32 a = palette[p.getInk()], b = palette[p.getPaint()];
  int ink = 255 - p.getTone(), paint = p.getTone();
  return {
      {a.r * a.m * ink + b.r * b.m * paint, a.g * a.m * ink + b.g * b.m * paint,
       a.b * a.m * ink + b.b * b.m * paint, 255 * (a.m * ink + b.m * paint)}};
}

void duplicatesAndPixels() {
  std::vector<Color> colors{{1, TPixel32(20, 40, 60, 128)},
                            {9, TPixel32(20, 40, 60, 128)},
                            {4095, TPixel32(20, 40, 60, 128)},
                            {12, TPixel32(20, 40, 60, 255)},
                            {7, TPixel32(220, 200, 90, 255)}};
  Usage usage{};
  Used used{};
  for (int id : {1, 7, 9, 12, 4095}) {
    used[id]  = true;
    usage[id] = 10;
  }
  usage[9] = 1000;  // Keep the most-used duplicate, not the first palette chip.
  Plan plan = makePlan(colors, usage, used, 0);
  validate(plan, colors, used);
  assert(plan.before == 5 && plan.after == 3 && plan.minimum == 2);
  assert(plan.styles[1] == 9 && plan.styles[4095] == 9);
  assert(plan.styles[12] == 12);
  std::array<TPixel32, 4096> palette{};
  palette[0] = TPixel32(0, 0, 0, 0);
  for (const Color &c : colors) palette[c.id] = c.rgba;
  for (int ink : {0, 1, 7, 9, 12, 4095})
    for (int paint : {0, 1, 7, 9, 12, 4095})
      for (int tone = 0; tone < 256; ++tone) {
        const TPixelCM32 before(ink, paint, tone);
        TPixelCM32 after = before;
        remap(after, plan.styles);
        assert(after.getTone() == tone);
        assert(render(before, palette) == render(after, palette));
        assert(after.getInk() == plan.styles[ink]);
        assert(after.getPaint() == plan.styles[paint]);
        assert(!remap(after, plan.styles));
      }
  // A style present only in the hidden paint slot is still redirected.
  used.fill(false);
  usage.fill(0);
  count(TPixelCM32(9, 4095, 0), usage, used);
  plan = makePlan(colors, usage, used, 0);
  assert(usage[4095] == 0 && used[4095]);
  assert(plan.styles[4095] == 9 && plan.before == 2 && plan.after == 1);
  // Empty drawings never manufacture usage for unused palette chips.
  used.fill(false);
  usage.fill(0);
  plan = makePlan(colors, usage, used, 1);
  assert(plan.before == 0 && plan.after == 0);
}

void weightsAndOpacity() {
  std::vector<Color> colors{{3, TPixel32(0, 0, 0)},
                            {5, TPixel32(255, 255, 255)},
                            {8, TPixel32(255, 0, 0, 128)}};
  Usage usage{};
  Used used{};
  usage[3] = 10000;
  usage[5] = 1;
  used[3] = used[5] = true;
  Plan p            = makePlan(colors, usage, used, 1);
  validate(p, colors, used);
  assert(p.after == 1 && p.styles[5] == 3);
  usage[3] = 1;
  usage[5] = 10000;
  p        = makePlan(colors, usage, used, 1);
  assert(p.styles[3] == 5);
  used[8]  = true;
  usage[8] = 100;
  p        = makePlan(colors, usage, used, 1);
  assert(p.minimum ==
         2);  // Caller rejects this impossible opacity-safe target.
  p = makePlan(colors, usage, used, 2);
  validate(p, colors, used);
  assert(p.after == 2 && p.styles[8] == 8);
  Usage coverage{};
  Used references{};
  count(TPixelCM32(3, 5, 200), coverage, references);
  assert(coverage[3] == 55 && coverage[5] == 200);
}

void cleanupUsage() {
  Used used{};
  Usage coverage{};
  count(TPixelCM32(9, 4095, 0), coverage, used);
  count(TPixelCM32(3, 0, 255), coverage, used);
  Plan plan;
  plan.styles[4095] = 9;
  Used remaining    = remappedUsage(used, plan.styles);
  assert(remaining[9] && remaining[3] && remaining[0]);
  assert(!remaining[4095] && !remaining[7]);
  // Another level still references the source ID. Accumulate its original
  // usage without applying this level's map or clearing earlier evidence.
  Used other{};
  other[4095] = true;
  for (size_t id = 0; id < other.size(); ++id) remaining[id] |= other[id];
  assert(remaining[4095] && remaining[9] && remaining[3]);
  // Cleanup is also meaningful when the reducer itself makes no changes.
  Plan unchanged;
  assert(remappedUsage(used, unchanged.styles) == used);
}

void randomized() {
  std::mt19937 random(20260906);
  for (int trial = 0; trial < 200; ++trial) {
    Usage usage{};
    Used used{};
    std::vector<Color> colors;
    int n = 2 + random() % 150;
    for (int id = 1; id <= n; ++id) {
      int alpha = 64 + 63 * (random() % 4);
      TPixel32 color(random() % 256, random() % 256, random() % 256, alpha);
      if (id > 1 && id % 5 == 0) color = colors.back().rgba;
      colors.push_back({id, color});
      usage[id] = random() % 100000;
      used[id]  = random() % 5 != 0;
      if (!used[id]) usage[id] = 0;
    }
    Plan exact = makePlan(colors, usage, used, 0);
    validate(exact, colors, used);
    for (int target :
         {exact.minimum, std::max(exact.minimum, exact.after / 2), n}) {
      Plan p = makePlan(colors, usage, used, target);
      validate(p, colors, used);
      assert(p.after == std::min(target, exact.after));
      std::reverse(colors.begin(), colors.end());
      assert(makePlan(colors, usage, used, target).styles == p.styles);
    }
  }
  // Exercise the entire 12-bit style range, plus cancellation between splits.
  Usage usage{};
  Used used{};
  std::vector<Color> colors;
  for (int id = 1; id < 4096; ++id) {
    colors.push_back({id, TPixel32(id % 256, id / 256, 120)});
    usage[id] = id;
    used[id]  = true;
  }
  Plan p = makePlan(colors, usage, used, 32);
  validate(p, colors, used);
  assert(p.after == 32);
  int calls = 0;
  p         = makePlan(colors, usage, used, 64, [&]() { return ++calls == 3; });
  assert(p.canceled && calls == 3);
}

}  // namespace

int main() {
  duplicatesAndPixels();
  weightsAndOpacity();
  cleanupUsage();
  randomized();
  std::cout
      << "Palette reduction: duplicate rendering, hidden slots, coverage, "
         "opacity, targets, determinism, 12-bit IDs and cancellation passed.\n";
}
