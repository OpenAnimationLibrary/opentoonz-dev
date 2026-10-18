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
  root.motion      = Body::Pinned;
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
void animatedControllers() {
  constexpr double pi = 3.141592653589793;
  Settings s;
  s.frames   = 97;
  s.substeps = 8;
  Body controller, child;
  controller.motion = Body::Kinematic;
  controller.pose   = {{2, 3}, 0.35};
  child.pose        = {{3, 3}, 0.35};
  child.length      = 0.5;
  for (int step = 0; step <= (s.frames - 1) * s.substeps; ++step) {
    double t = double(step) / ((s.frames - 1) * s.substeps);
    controller.animation.push_back(
        {{2 + t, 3 + 0.2 * std::sin(t * 2 * pi)}, 0.35 + t * 4 * pi});
  }
  Connection link;
  link.body   = 1;
  link.target = 0;
  link.anchor = child.pose.pivot;
  link.type   = Connection::Fixed;
  auto clip   = run({controller, child}, {link}, s);
  auto again  = run({controller, child}, {link}, s);
  for (size_t f = 0; f < clip.size(); ++f) {
    const auto &p = controller.animation[f * s.substeps];
    check(
        distance(clip[f][0].pivot, p.pivot) == 0 && clip[f][0].angle == p.angle,
        "controller output retains exact authored frame poses");
    Point tip{p.pivot.x + std::cos(p.angle - controller.pose.angle),
              p.pivot.y + std::sin(p.angle - controller.pose.angle)};
    check(
        distance(clip[f][1].pivot, tip) < 0.045 &&
            std::abs(clip[f][1].angle - p.angle) < 0.025,
        "fixed follower responds to controller translation and complete turns");
    check(distance(clip[f][1].pivot, again[f][1].pivot) == 0 &&
              clip[f][1].angle == again[f][1].angle,
          "animated controller replay is repeatable");
  }
  check(clip.back()[1].angle > 4 * pi,
        "dynamic rotation retains full turns across matrix wrapping");

  link.type       = Connection::SpringHinge;
  link.lowerAngle = -0.5;
  link.upperAngle = 0.5;
  auto sprung     = run({controller, child}, {link}, s);
  bool lagged     = false;
  for (size_t f = 0; f < sprung.size(); ++f) {
    const auto &p = sprung[f][0];
    Point anchor{p.pivot.x + std::cos(p.angle - controller.pose.angle),
                 p.pivot.y + std::sin(p.angle - controller.pose.angle)};
    check(distance(sprung[f][1].pivot, anchor) < 0.06,
          "spring attachment follows an animated controller");
    lagged = lagged || std::abs(sprung[f][1].angle - p.angle) > 0.03;
  }
  check(lagged,
        "spring follower permits secondary rotation under authored motion");

  s.frames        = 49;
  s.gravity       = 0;
  controller.pose = {{-1, 0}, 0};
  controller.animation.clear();
  for (int step = 0; step <= (s.frames - 1) * s.substeps; ++step)
    controller.animation.push_back(
        {{-1 + 2.0 * step / ((s.frames - 1) * s.substeps), 0}, 0});
  Body prop;
  auto pushed = run({controller, prop}, {}, s);
  check(pushed.back()[1].pivot.x > 0.7,
        "animated collider transfers velocity to an unconnected dynamic prop");
}
void springConnections() {
  Body body;
  body.pose.angle = 1.5707963267948966;
  Settings s;
  s.frames = 241;
  Connection link;
  link.body       = 0;
  link.type       = Connection::SpringHinge;
  link.lowerAngle = -0.4;
  link.upperAngle = 0.4;
  link.stiffness  = 40;
  link.damping    = 3;
  auto clip       = run({body}, {link}, s);
  for (const auto &frame : clip) {
    check(distance(frame[0].pivot, body.pose.pivot) < 0.025,
          "spring hinge keeps its shared anchor attached");
    check(std::abs(frame[0].angle - body.pose.angle) < 0.42,
          "spring hinge respects angular limits");
  }
  check(
      std::abs(clip.back()[0].angle - body.pose.angle) < 0.18 &&
          std::abs(clip.back()[0].angle - clip[clip.size() - 2][0].angle) <
              0.005,
      "spring torque restores the initial relative angle and damping settles");
  link.stiffness = 0;
  link.damping   = 0;
  auto loose     = run({body}, {link}, s);
  check(std::abs(loose.back()[0].angle - body.pose.angle) > 0.35,
        "zero stiffness permits gravity to reach the hinge stop");
  for (auto type : {Connection::Hinge, Connection::SpringHinge}) {
    link.type       = type;
    link.lowerAngle = 0;
    link.upperAngle = 0.3;
    auto positive   = run({body}, {link}, s);
    check(positive.back()[0].angle - body.pose.angle > 0.25 &&
              positive.back()[0].angle - body.pose.angle < 0.32,
          "both hinge types use positive counterclockwise angular limits");
    link.lowerAngle = -0.3;
    link.upperAngle = 0;
    auto negative   = run({body}, {link}, s);
    check(std::abs(negative.back()[0].angle - body.pose.angle) < 0.02,
          "asymmetric opposite limit blocks gravity-driven rotation");
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
  body.motion = Body::Pinned;
  reject({body}, {link}, s);
  body.motion = Body::Kinematic;
  reject({body}, {}, s);
  body.animation.assign(1 + (s.frames - 1) * s.substeps, body.pose);
  reject({body}, {link}, s);
  body.animation[1].angle = 4;
  reject({body}, {}, s);
  body.animation[1] = body.pose;
  body.animation[0].pivot.x += 1;
  reject({body}, {}, s);
  body.animation[0]         = body.pose;
  body.animation[1].pivot.x = std::numeric_limits<double>::infinity();
  reject({body}, {}, s);
  body.animation.clear();
  body.motion    = Body::Dynamic;
  link.type      = Connection::SpringHinge;
  link.stiffness = -1;
  reject({body}, {link}, s);
}
}  // namespace
int main() {
  try {
    freeFallAndTime();
    joints();
    chainAndRepeat();
    animatedControllers();
    springConnections();
    floorAndValidation();
    std::cout << "Bullet world tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
