#include "bulletworld.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace OTBullet;
namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
double distance(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }
Clip run(const std::vector<Body> &bodies, const std::vector<Connection> &links,
         Settings s = Settings()) {
  Clip clip;
  std::string error;
  check(simulate(bodies, links, s, clip, error), error.c_str());
  check(clip.size() == size_t(s.frames), "inclusive frame count");
  return clip;
}
void freeFallAndTime() {
  Body body;
  body.pose.pivot = {0, 5};
  Settings s;
  s.frames  = 25;
  s.damping = 0;
  auto clip = run({body}, {}, s);
  check(distance(clip.front()[0].pivot, body.pose.pivot) == 0,
        "frame zero must not advance");
  check(std::abs(clip.back()[0].pivot.y - (5 - 9.81 / 2)) < 0.05,
        "one second of explicit fixed-step free fall");
  s.fps      = 48;
  s.frames   = 49;
  auto twice = run({body}, {}, s);
  check(std::abs(twice.back()[0].pivot.y - clip.back()[0].pivot.y) < 0.025,
        "scene fps changes must preserve elapsed time");
  s.frames = 1;
  check(run({body}, {}, s)[0][0].pivot.y == 5, "one-frame clip");
}
void joints() {
  Body body;
  body.pose.angle = 1.5707963267948966;
  Connection link;
  link.body   = 0;
  link.anchor = body.pose.pivot;
  for (auto type : {Connection::Hinge, Connection::PointJoint}) {
    link.type = type;
    auto clip = run({body}, {link});
    check(std::abs(clip.back()[0].angle - body.pose.angle) > 0.2,
          "joint permits gravity-driven rotation");
    for (const auto &frame : clip)
      check(distance(frame[0].pivot, link.anchor) < 0.025,
            "world joint anchor remains attached");
  }
  link.type  = Connection::Fixed;
  auto fixed = run({body}, {link});
  check(std::abs(fixed.back()[0].angle - body.pose.angle) < 0.001,
        "fixed joint preserves relative orientation");
  check(distance(fixed.back()[0].pivot, body.pose.pivot) < 0.001,
        "fixed joint preserves position");
  link.type       = Connection::Hinge;
  link.lowerAngle = -0.2;
  link.upperAngle = 0.2;
  auto limited    = run({body}, {link});
  for (const auto &frame : limited)
    check(std::abs(frame[0].angle - body.pose.angle) < 0.25,
          "hinge limits are relative to the starting connection");
}
void chainAndRepeat() {
  Body root, child, leaf;
  root.pinned      = true;
  child.pose.pivot = {0, 1};
  child.pose.angle = 1.5707963267948966;
  leaf.pose.pivot  = {-1, 1};
  Connection a, b;
  a.body     = 1;
  a.target   = 0;
  a.anchor   = child.pose.pivot;
  b.body     = 2;
  b.target   = 1;
  b.anchor   = leaf.pose.pivot;
  auto clip  = run({root, child, leaf}, {a, b});
  auto again = run({root, child, leaf}, {a, b});
  for (size_t f = 0; f < clip.size(); ++f) {
    check(distance(clip[f][0].pivot, root.pose.pivot) < 1e-6,
          "pinned root stays fixed");
    check(distance(clip[f][1].pivot, child.pose.pivot) < 0.025,
          "child attaches to pinned root");
    Point expected{
        clip[f][1].pivot.x - std::cos(clip[f][1].angle - child.pose.angle),
        clip[f][1].pivot.y - std::sin(clip[f][1].angle - child.pose.angle)};
    check(distance(clip[f][2].pivot, expected) < 0.04,
          "second connection follows the moving first body");
    for (size_t i = 0; i < 3; ++i) {
      check(distance(clip[f][i].pivot, again[f][i].pivot) == 0 &&
                clip[f][i].angle == again[f][i].angle,
            "fresh-world replay is repeatable in the same binary");
      if (f)
        check(std::abs(clip[f][i].angle - clip[f - 1][i].angle) < 3.1416,
              "angle continuity");
    }
  }
}
void floorAndValidation() {
  Body body;
  body.pose.pivot = {0, 3};
  Settings s;
  s.floor          = true;
  s.floorY         = 0;
  s.frames         = 97;
  auto clip        = run({body}, {}, s);
  const auto &rest = clip.back()[0];
  double bottom    = rest.pivot.y +
                  std::min(0.0, std::cos(rest.angle) * body.length) -
                  std::abs(std::sin(rest.angle)) * body.width / 2;
  check(std::abs(bottom) < 0.03,
        "floor supports the lowest corner after settling");
  std::string error;
  auto reject = [&](std::vector<Body> bodies, std::vector<Connection> links,
                    Settings settings) {
    clip = {{body.pose}};
    check(!simulate(bodies, links, settings, clip, error) && clip.empty() &&
              !error.empty(),
          "invalid input must fail atomically");
  };
  reject({}, {}, s);
  s.frames = 1201;
  reject({body}, {}, s);
  s.frames = 3;
  s.fps    = std::numeric_limits<double>::quiet_NaN();
  reject({body}, {}, s);
  s.fps     = 24;
  body.mass = 0;
  reject({body}, {}, s);
  body.mass = 1;
  Connection link;
  link.body   = 0;
  link.target = 0;
  reject({body}, {link}, s);
  link.target = 8;
  reject({body}, {link}, s);
  link.target = -1;
  reject({body}, {link, link}, s);
  body.pinned = true;
  reject({body}, {link}, s);
}
}  // namespace
int main() {
  try {
    freeFallAndTime();
    joints();
    chainAndRepeat();
    floorAndValidation();
    std::cout << "Bullet world tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
