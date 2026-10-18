#include "bulletworld.h"

#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/ConstraintSolver/btFixedConstraint.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>

namespace OTBullet {
namespace {
constexpr double pi = 3.141592653589793;
bool finite(double value, double limit = 10000) {
  return std::isfinite(value) && std::abs(value) <= limit;
}
bool valid(Point point) { return finite(point.x) && finite(point.y); }
btVector3 vector(Point point) { return btVector3(point.x, point.y, 0); }
btTransform transform(const Body &body) {
  btTransform value(btQuaternion(btVector3(0, 0, 1), body.pose.angle),
                    vector(body.pose.pivot));
  value.setOrigin(value * btVector3(0, body.length / 2, 0));
  return value;
}

// Explicit teardown order also covers early exits and exceptions.
struct World {
  btDefaultCollisionConfiguration configuration;
  btCollisionDispatcher dispatcher{&configuration};
  btDbvtBroadphase broadphase;
  btSequentialImpulseConstraintSolver solver;
  btDiscreteDynamicsWorld world{&dispatcher, &broadphase, &solver,
                                &configuration};
  std::vector<std::unique_ptr<btCollisionShape>> shapes;
  std::vector<std::unique_ptr<btRigidBody>> bodies;
  std::vector<std::unique_ptr<btTypedConstraint>> joints;

  ~World() {
    for (auto &joint : joints) world.removeConstraint(joint.get());
    for (auto &body : bodies) world.removeRigidBody(body.get());
  }
  void addBody(std::unique_ptr<btCollisionShape> shape, btTransform placement,
               double mass, double damping) {
    btVector3 inertia(0, 0, 0);
    if (mass > 0) shape->calculateLocalInertia(mass, inertia);
    shapes.push_back(std::move(shape));
    btRigidBody::btRigidBodyConstructionInfo info(mass, nullptr,
                                                  shapes.back().get(), inertia);
    bodies.emplace_back(new btRigidBody(info));
    auto &body = *bodies.back();
    body.setWorldTransform(placement);
    body.setLinearFactor(btVector3(1, 1, 0));
    body.setAngularFactor(btVector3(0, 0, 1));
    body.setDamping(damping, damping);
    body.setActivationState(DISABLE_DEACTIVATION);
    world.addRigidBody(&body);
  }
};

bool validate(const std::vector<Body> &bodies,
              const std::vector<Connection> &connections, const Settings &s,
              std::string &error) {
  if (bodies.empty() || bodies.size() > 64 || connections.size() > 128) {
    error = "Use 1 to 64 bodies and at most 128 connections.";
    return false;
  }
  if (!finite(s.fps, 240) || s.fps < 1 || s.frames < 1 || s.frames > 1200 ||
      s.substeps < 1 || s.substeps > 32 || !finite(s.gravity, 100) ||
      !finite(s.floorY) || !finite(s.damping, 1) || s.damping < 0) {
    error = "Invalid simulation settings.";
    return false;
  }
  for (const Body &b : bodies)
    if (!valid(b.pose.pivot) || !finite(b.pose.angle, 100000) ||
        !finite(b.length, 1000) || b.length < 0.01 || !finite(b.width, 1000) ||
        b.width < 0.01 || !finite(b.mass, 1000) || b.mass <= 0) {
      error = "Invalid body pose, size or mass.";
      return false;
    }
  std::set<std::pair<int, int>> pairs;
  for (const Connection &c : connections) {
    if (c.body < 0 || c.body >= int(bodies.size()) || c.target < -1 ||
        c.target >= int(bodies.size()) || c.body == c.target ||
        c.type < Connection::Hinge || c.type > Connection::Fixed ||
        !valid(c.anchor) || !finite(c.lowerAngle, pi) ||
        !finite(c.upperAngle, pi) || c.lowerAngle > c.upperAngle) {
      error = "Invalid connection endpoint, type or angular limits.";
      return false;
    }
    auto pair = std::minmax(c.body, c.target);
    if (!pairs.insert(pair).second ||
        (bodies[c.body].pinned &&
         (c.target == -1 || bodies[c.target].pinned))) {
      error = "Duplicate connection or connection between pinned endpoints.";
      return false;
    }
  }
  return true;
}
}  // namespace

bool simulate(const std::vector<Body> &bodies,
              const std::vector<Connection> &connections, const Settings &s,
              Clip &output, std::string &error) {
  output.clear();
  error.clear();
  if (!validate(bodies, connections, s, error)) return false;
  try {
    World state;
    state.world.setGravity(btVector3(0, s.gravity, 0));
    state.world.getSolverInfo().m_numIterations = 40;
    // Stable single-threaded insertion/solve order; no cross-version guarantee.
    state.world.getSolverInfo().m_solverMode &= ~SOLVER_RANDMIZE_ORDER;
    for (const Body &b : bodies) {
      std::unique_ptr<btBoxShape> shape(
          new btBoxShape(btVector3(b.width / 2, b.length / 2, b.width / 2)));
      shape->setMargin(btScalar(std::min(b.width, b.length) * 0.02));
      state.addBody(std::move(shape), transform(b), b.pinned ? 0 : b.mass,
                    s.damping);
    }
    // A private static body is used for explicit world connections.
    const int worldIndex = int(state.bodies.size());
    state.addBody(std::unique_ptr<btCollisionShape>(new btEmptyShape),
                  btTransform::getIdentity(), 0, 0);
    if (s.floor)
      state.addBody(
          std::unique_ptr<btCollisionShape>(
              new btStaticPlaneShape(btVector3(0, 1, 0), 0)),
          btTransform(btQuaternion::getIdentity(), btVector3(0, s.floorY, 0)),
          0, 0);
    for (const Connection &c : connections) {
      auto &a = *state.bodies[c.body];
      auto &b = *state.bodies[c.target < 0 ? worldIndex : c.target];
      btTransform anchor(btQuaternion::getIdentity(), vector(c.anchor));
      btTransform localA = a.getWorldTransform().inverse() * anchor;
      btTransform localB = b.getWorldTransform().inverse() * anchor;
      std::unique_ptr<btTypedConstraint> joint;
      if (c.type == Connection::PointJoint)
        joint.reset(new btPoint2PointConstraint(a, b, localA.getOrigin(),
                                                localB.getOrigin()));
      else if (c.type == Connection::Fixed)
        joint.reset(new btFixedConstraint(a, b, localA, localB));
      else {
        auto *hinge = new btHingeConstraint(a, b, localA, localB, true);
        hinge->setLimit(c.lowerAngle, c.upperAngle);
        joint.reset(hinge);
      }
      state.joints.push_back(std::move(joint));
      // Adjacent connected proxies may overlap at their joint.
      state.world.addConstraint(state.joints.back().get(), true);
    }
    Clip clip;
    Frame previous;
    for (const Body &b : bodies) previous.push_back(b.pose);
    clip.push_back(previous);
    const btScalar dt = btScalar(1.0 / (s.fps * s.substeps));
    for (int frame = 1; frame < s.frames; ++frame) {
      for (int step = 0; step < s.substeps; ++step)
        state.world.stepSimulation(dt, 0);  // Exactly one explicit fixed step.
      Frame poses;
      for (size_t i = 0; i < bodies.size(); ++i) {
        const auto &t            = state.bodies[i]->getWorldTransform();
        btVector3 pivot          = t * btVector3(0, -bodies[i].length / 2, 0);
        const btMatrix3x3 &basis = t.getBasis();
        double angle = std::atan2(double(basis[1][0]), double(basis[0][0]));
        // Unwrap before baking, preserving turns through +/- pi.
        angle = previous[i].angle +
                std::remainder(angle - previous[i].angle, 2 * pi);
        Pose pose{{double(pivot.x()), double(pivot.y())}, angle};
        if (!valid(pose.pivot) || !finite(pose.angle, 100000) ||
            std::abs(double(pivot.z())) > 1e-3) {
          error = "Simulation diverged or left the drawing plane.";
          return false;
        }
        poses.push_back(pose);
      }
      previous = poses;
      clip.push_back(std::move(poses));
    }
    output.swap(clip);
    return true;
  } catch (const std::exception &e) {
    error = std::string("Bullet simulation failed: ") + e.what();
    return false;
  }
}
}  // namespace OTBullet
