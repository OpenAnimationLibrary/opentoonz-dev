// Standalone regression checks using the production geometry helper.
// g++ -std=c++17 -DLINUX -I../../include vectorassistants_test.cpp -o
// /tmp/vectorassistants_test
#include "tools/vectorassistants.h"

#include <cstdlib>
#include <iostream>

using namespace VectorAssistants;

namespace {

void check(bool condition, const char *message) {
  if (condition) return;
  std::cerr << message << '\n';
  std::exit(1);
}

void near(double a, double b, double tolerance = 1e-9) {
  check(std::abs(a - b) <= tolerance, "Unexpected coordinate");
}

Guide polyline(std::initializer_list<TPointD> points) {
  Guide guide;
  for (const TPointD &p : points) guide.add(p);
  return guide;
}

void captureAndSmoothing() {
  Guide horizontal = polyline({{-200, 0}, {200, 0}});
  Guide vertical   = polyline({{0, -200}, {0, 200}});
  near(chooseGuide({vertical, horizontal}, {0, 0}, {10, 0}, 32), 1);
  near(chooseGuide({horizontal, vertical}, {0, 0}, {0, -10}, 32), 1);
  near(chooseGuide({horizontal}, {0, 40}, {10, 0}, 32), -1);
  State state;
  double rawError = 0.0, guidedError = 0.0;
  for (int i = 0; i <= 150; ++i) {
    TPointD raw(i, 2 + 2 * std::sin(i * 0.7));
    TPointD guided = follow(horizontal, state, raw);
    near(guided.x, raw.x);
    if (i > 30) {
      rawError += raw.y * raw.y;
      guidedError += guided.y * guided.y;
    }
  }
  check(guidedError < rawError * 0.04, "Guide did not suppress lateral wobble");
  TPointD stopped = follow(horizontal, state, state.previous);
  for (int i = 0; i < 20; ++i)
    near(length(follow(horizontal, state, state.previous) - stopped), 0.0);
}

void intersectionsAndRelease() {
  Guide crossing = polyline({{-60, -60}, {60, 60}, {-60, 60}, {60, -60}});
  State state;
  TPointD previousRaw(-40, -39), previousGuided;
  for (int x = -40; x <= 40; ++x) {
    TPointD raw(x, x + 1.5 * std::cos(x));
    TPointD guided = follow(crossing, state, raw);
    if (x > -40) {
      double ds = length(raw - previousRaw);
      double dc = length((guided - raw) - (previousGuided - previousRaw));
      check(dc <= 2.0 * ds + 1e-8, "Correction jumped at crossing");
    }
    check(state.arc < crossing.lengths[1], "Jumped to another curve branch");
    previousRaw    = raw;
    previousGuided = guided;
  }

  Guide line = polyline({{0, 0}, {300, 0}});
  state      = State();
  for (int x = 0; x < 200; ++x) {
    double y       = x < 40 ? 3.0 : x < 100 ? x - 37.0 : 3.0;
    TPointD result = follow(line, state, {static_cast<double>(x), y});
    if (x == 99)
      check(state.released, "Did not release outside attraction range");
    if (x == 199) {
      check(state.released, "Recaptured a guide without a pen lift");
      near(result.y, y, 0.01);
    }
  }
}

void closedCurvesAndEndpoints() {
  Guide eight;
  eight.closed = true;
  for (int i = 0; i <= 800; ++i) {
    double t = 2 * M_PI * i / 800;
    eight.add({60 * std::sin(t), 30 * std::sin(2 * t)});
  }
  State state;
  double previousArc = 0.0;
  for (int i = 0; i <= 1600; ++i) {
    double t = M_PI_2 + 4 * M_PI * i / 1600;
    TPointD raw(60 * std::sin(t), 30 * std::sin(2 * t) + 0.5);
    TPointD result = follow(eight, state, raw);
    check(std::isfinite(result.x) && std::isfinite(result.y),
          "Invalid loop position");
    if (i)
      check(state.arc > previousArc - 2.0, "Loop progress jumped backward");
    previousArc = state.arc;
  }
  check(!state.released && state.arc > 2 * eight.totalLength(),
        "Could not follow repeated laps through the seam and crossing");
  // The same curve must also work in reverse, without forcing forward motion.
  state = State();
  for (int i = 0; i <= 800; ++i) {
    double t = M_PI_2 - 2 * M_PI * i / 800;
    follow(eight, state, {60 * std::sin(t), 30 * std::sin(2 * t) + 0.5});
  }
  check(!state.released && state.arc < 0.0,
        "Could not reverse through loop seam");

  Guide line = polyline({{0, 0}, {40, 0}});
  state      = State();
  for (int x = 0; x <= 100; ++x) {
    TPointD result = follow(line, state, {static_cast<double>(x), 4});
    near(result.x, x);  // No clamping/pileup at the vector's end.
    if (x == 100) near(result.y, 4.0, 0.5);
  }
}

void replayAndSampling() {
  Guide line = polyline({{0, 0}, {300, 0}});
  State dense, sparse;
  std::vector<TPointD> results;
  for (int x = 0; x <= 100; ++x)
    results.push_back(follow(line, dense, {static_cast<double>(x), 4}));
  for (int x = 0; x <= 100; x += 10)
    near(length(follow(line, sparse, {static_cast<double>(x), 4}) - results[x]),
         0.0);

  State prefix;
  for (int x = 0; x <= 50; ++x)
    follow(line, prefix, {static_cast<double>(x), 2 * std::sin(x * 0.2)});
  State first = prefix, replay = prefix;
  // Out-of-order interpolation must use copies, leaving the saved state intact.
  State query = prefix;
  TPointD a   = follow(line, query, {50.75, 1});
  query       = prefix;
  follow(line, query, {50.25, 2});
  query = prefix;
  near(length(follow(line, query, {50.75, 1}) - a), 0.0);
  for (int x = 51; x <= 100; ++x) {
    TPointD raw(x, 5 * std::cos(x * 0.3));
    near(length(follow(line, first, raw) - follow(line, replay, raw)), 0.0);
  }
  near(prefix.previous.x, 50);
}

void curvesAndDegenerates() {
  Guide curve;
  curve.add({0, 0});
  check(appendQuadratic(curve, {0, 0}, {50, 100}, {100, 0}),
        "Could not flatten curve");
  for (int i = 0; i <= 100; ++i) {
    double t = i / 100.0;
    Projection q =
        project(curve, {100 * t, 200 * t * (1 - t)}, 0, curve.totalLength());
    check(q.distance <= 0.251,
          "Guide approximation exceeded quarter pixel error");
  }
  Guide empty, dot;
  dot.add({1, 1});
  dot.add({1, 1});
  State state;
  near(length(follow(empty, state, {9, 8}) - TPointD(9, 8)), 0);
  near(length(follow(dot, state, {9, 8}) - TPointD(9, 8)), 0);
  near(length(follow(curve, state, {9, 8}, 0) - TPointD(9, 8)), 0);
  near(length(follow(curve, state, {9, 8}, 32, 0) - TPointD(9, 8)), 0);
  follow(curve, state, {0, 0});
  TPointD jumped = follow(curve, state, {100000, 100000});
  check(std::isfinite(jumped.x) && std::isfinite(jumped.y),
        "Invalid large-jump result");
}

}  // namespace

int main() {
  captureAndSmoothing();
  intersectionsAndRelease();
  closedCurvesAndEndpoints();
  replayAndSampling();
  curvesAndDegenerates();
  std::cout << "Vector level assistant regression checks passed\n";
}
