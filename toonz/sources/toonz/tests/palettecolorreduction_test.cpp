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

void validateSimilarity(const Plan &plan, const std::vector<Color> &colors,
                        const Usage &usage, const Used &used) {
  validate(plan, colors, used);
  const Plan exact = makePlan(colors, usage, used, 0);
  const int goal   = std::max(exact.minimum, (exact.after + 4) / 5);
  assert(int(plan.protectedStyles.size()) == goal);
  assert(plan.after >= goal && plan.after <= exact.after);
  std::map<int, detail::Lab> labs;
  std::set<int> protectedIds(plan.protectedStyles.begin(),
                             plan.protectedStyles.end());
  for (const Color &c : colors) labs[c.id] = detail::toLab(c.rgba);
  for (int id : protectedIds) assert(plan.styles[id] == id && used[id]);
  for (const Color &c : colors) {
    int to         = plan.styles[c.id];
    double squared = 0;
    for (int d = 0; d < 3; ++d) {
      double delta = labs[c.id][d] - labs[to][d];
      squared += delta * delta;
    }
    assert(std::sqrt(squared) <= plan.tolerance + 1e-12);
    // Every non-identical merge goes directly to a protected survivor.
    if (squared > 0) assert(protectedIds.count(to));
  }
}

void similarity() {
  Usage usage{};
  Used used{};
  std::vector<Color> colors;
  // A frequently used gray, eight close shades and one small red accent.
  for (int id = 1; id <= 10; ++id) {
    const int gray = 120 + id;
    colors.push_back(
        {id, id == 10 ? TPixel32(230, 10, 10) : TPixel32(gray, gray, gray)});
    used[id]  = true;
    usage[id] = id == 1 ? 100000 : 100;
  }
  Plan automatic = makeSimilarityPlan(colors, usage, used);
  validateSimilarity(automatic, colors, usage, used);
  assert(automatic.after == 2);
  assert(automatic.styles[1] == 1 && automatic.styles[10] == 10);
  Plan strict = makeSimilarityPlan(colors, usage, used, 0);
  validateSimilarity(strict, colors, usage, used);
  assert(strict.after == 10);
  Plan medium =
      makeSimilarityPlan(colors, usage, used, automatic.tolerance / 2);
  validateSimilarity(medium, colors, usage, used);
  assert(medium.after > automatic.after && medium.after <= strict.after);
  assert(medium.protectedStyles == automatic.protectedStyles);
  assert(makeSimilarityPlan(colors, usage, used, 2).styles == automatic.styles);

  // Duplicates, unused chips and hidden-only references do not inflate the
  // distinct-color budget. A small palette rounds up to one survivor.
  colors = {{2, TPixel32(100, 100, 100)},
            {3, TPixel32(101, 101, 101)},
            {4, TPixel32(102, 102, 102)},
            {5, TPixel32(100, 100, 100)},
            {6, TPixel32(0, 255, 0)}};
  usage.fill(0);
  used.fill(false);
  for (int id : {2, 3, 4, 5}) used[id] = true;
  usage[2]  = 100;
  automatic = makeSimilarityPlan(colors, usage, used);
  validateSimilarity(automatic, colors, usage, used);
  assert(automatic.before == 4 && automatic.after == 1);
  strict = makeSimilarityPlan(colors, usage, used, 0);
  assert(strict.after == 3 && strict.styles[5] == 2 && strict.styles[6] == 6);
  colors.push_back({7, TPixel32(100, 100, 100, 128)});
  used[7]   = true;
  automatic = makeSimilarityPlan(colors, usage, used);
  validateSimilarity(automatic, colors, usage, used);
  assert(automatic.after == 2 && automatic.styles[7] == 7);
  used.fill(false);
  automatic = makeSimilarityPlan(colors, usage, used);
  assert(automatic.after == 0 && automatic.protectedStyles.empty());
}

void renumbering() {
  Used removed{};
  removed[40] = true;
  std::vector<std::vector<int>> pages{{0, 1, 90, 40, 7}, {85, 3}};
  StyleMap map = makeRenumberMap(pages, 100, removed), inverse;
  assert(map[0] == 0 && map[1] == 1);
  assert(map[90] == 2 && map[7] == 3 && map[85] == 4 && map[3] == 5);
  std::set<int> indices(map.begin(), map.end());
  assert(indices.size() == map.size());
  std::array<TPixel32, 4096> before{}, after{};
  for (int id = 0; id < 4096; ++id) {
    before[id] = TPixel32(id % 256, (id / 3) % 256, (id / 9) % 256, id % 256);
    after[map[id]]   = before[id];
    inverse[map[id]] = id;
    if (id >= 100) assert(map[id] == id);
  }
  // Includes unpaged references (such as 99), hidden ink/paint, and all tones.
  for (int ink : {0, 1, 3, 7, 40, 85, 90, 99, 4095})
    for (int paint : {0, 1, 3, 7, 40, 85, 90, 99, 4095})
      for (int tone = 0; tone < 256; ++tone) {
        TPixelCM32 original(ink, paint, tone), pixel = original;
        remap(pixel, map);
        assert(render(original, before) == render(pixel, after));
        assert(pixel.getTone() == tone);
        remap(pixel, inverse);
        assert(pixel.getInk() == ink && pixel.getPaint() == paint);
      }
  // Compose reduction and renumbering once for the current level. Other
  // shared levels use just the permutation, retaining their original colors.
  StyleMap reduction;
  std::iota(reduction.begin(), reduction.end(), 0);
  reduction[40]     = 90;
  StyleMap combined = reduction;
  for (int &id : combined) id = map[id];
  TPixelCM32 current(40, 7, 0), shared = current;
  remap(current, combined);
  remap(shared, map);
  assert(current.getInk() == 2 && current.getPaint() == 3);
  assert(after[shared.getInk()] == before[40]);
  assert(after[current.getInk()] == before[90]);
  // No cleanup: keep every page entry, and compact the entire 12-bit range.
  removed.fill(false);
  map = makeRenumberMap({{0, 1, 4095, 12}}, 4096, removed);
  assert(map[4095] == 2 && map[12] == 3);
  assert(std::set<int>(map.begin(), map.end()).size() == 4096);
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
    Plan automatic = makeSimilarityPlan(colors, usage, used);
    validateSimilarity(automatic, colors, usage, used);
    assert(automatic.after == std::max(exact.minimum, (exact.after + 4) / 5));
    int previous = exact.after;
    for (double tolerance :
         {0.0, automatic.tolerance / 2, automatic.tolerance * 1.01}) {
      Plan p = makeSimilarityPlan(colors, usage, used, tolerance);
      validateSimilarity(p, colors, usage, used);
      assert(p.after <= previous);
      previous = p.after;
    }
    std::reverse(colors.begin(), colors.end());
    Plan reversed = makeSimilarityPlan(colors, usage, used);
    assert(reversed.styles == automatic.styles);
    assert(reversed.protectedStyles == automatic.protectedStyles);
    assert(reversed.tolerance == automatic.tolerance);
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
  p = makeSimilarityPlan(colors, usage, used);
  validateSimilarity(p, colors, usage, used);
  assert(p.after == 819);
  calls = 0;
  p     = makeSimilarityPlan(colors, usage, used, -1,
                             [&]() { return ++calls == 3; });
  assert(p.canceled && calls == 3);
}

}  // namespace

int main() {
  duplicatesAndPixels();
  weightsAndOpacity();
  cleanupUsage();
  similarity();
  renumbering();
  randomized();
  std::cout
      << "Palette reduction: duplicate rendering, hidden slots, coverage, "
         "opacity, targets, 80/20, tolerance, renumbering, determinism, "
         "12-bit IDs and cancellation passed.\n";
}
