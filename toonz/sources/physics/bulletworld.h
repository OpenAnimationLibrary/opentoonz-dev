#pragma once

#include <string>
#include <vector>

// CPU-only planar exploration. No Bullet types escape this boundary.
namespace OTBullet {
struct Point {
  double x = 0, y = 0;
};
struct Pose {
  Point pivot;       // World-space attachment/pivot, in meters.
  double angle = 0;  // Radians; the body extends along its local +Y axis.
};
struct Body {
  Pose pose;
  double length = 1, width = 0.2, mass = 1;
  bool pinned = false;
};
struct Connection {
  enum Type { Hinge, PointJoint, Fixed };
  int body = -1, target = -1;  // -1 target means the static world.
  Type type = Hinge;
  Point anchor;  // Shared world-space anchor at the initial pose.
  double lowerAngle = -3.141592653589793;
  double upperAngle = 3.141592653589793;
};
struct Settings {
  double fps = 24;
  int frames = 72, substeps = 5;
  double gravity = -9.81, damping = 0.05;
  bool floor    = false;
  double floorY = -4;
};
using Frame = std::vector<Pose>;
using Clip  = std::vector<Frame>;

// Starts a fresh world for every run. Frame zero is the authored starting pose;
// each subsequent frame advances exactly substeps steps of 1/(fps*substeps).
// Failure leaves output empty and supplies a diagnostic. No scene is mutated.
bool simulate(const std::vector<Body> &bodies,
              const std::vector<Connection> &connections,
              const Settings &settings, Clip &output, std::string &error);
}  // namespace OTBullet
