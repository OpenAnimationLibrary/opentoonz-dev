#include "bulletworld.h"

#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/ConstraintSolver/btFixedConstraint.h>
#include <BulletDynamics/ConstraintSolver/btGeneric6DofSpring2Constraint.h>

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
btTransform transform(const Pose &pose, double length) {
  btTransform value(btQuaternion(btVector3(0, 0, 1), pose.angle),
                    vector(pose.pivot));
  value.setOrigin(value * btVector3(0, length / 2, 0));
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
               double mass, double damping, bool kinematic = false) {
    btVector3 inertia(0, 0, 0);
    if (mass > 0) shape->calculateLocalInertia(mass, inertia);
    shapes.push_back(std::move(shape));
    btRigidBody::btRigidBodyConstructionInfo info(mass, nullptr,
                                                  shapes.back().get(), inertia);
    bodies.emplace_back(new btRigidBody(info));
    auto &body = *bodies.back();
    body.setWorldTransform(placement);
    body.setInterpolationWorldTransform(placement);
    if (kinematic)
      body.setCollisionFlags(
          (body.getCollisionFlags() & ~btCollisionObject::CF_STATIC_OBJECT) |
          btCollisionObject::CF_KINEMATIC_OBJECT);
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
  for (const Body &b : bodies) {
    if (!valid(b.pose.pivot) || !finite(b.pose.angle, 100000) ||
        !finite(b.length, 1000) || b.length < 0.01 || !finite(b.width, 1000) ||
        b.width < 0.01 || !finite(b.mass, 1000) || b.mass <= 0 ||
        b.motion < Body::Dynamic || b.motion > Body::Kinematic) {
      error = "Invalid body pose, size or mass.";
      return false;
    }
    const size_t samples = 1 + (s.frames - 1) * s.substeps;
    if ((b.motion == Body::Kinematic && b.animation.size() != samples) ||
        (b.motion != Body::Kinematic && !b.animation.empty())) {
      error = "Kinematic controllers need one authored pose per physics step.";
      return false;
    }
    for (size_t i = 0; i < b.animation.size(); ++i) {
      const Pose &p = b.animation[i];
      if (!valid(p.pivot) || !finite(p.angle, 100000) ||
          (i && std::abs(p.angle - b.animation[i - 1].angle) >= pi) ||
          (!i && (std::abs(p.pivot.x - b.pose.pivot.x) > 1e-9 ||
                  std::abs(p.pivot.y - b.pose.pivot.y) > 1e-9 ||
                  std::abs(p.angle - b.pose.angle) > 1e-9))) {
        error =
            "Invalid controller pose or rotation too fast for the substeps.";
        return false;
      }
    }
  }
  std::set<std::pair<int, int>> pairs;
  for (const Connection &c : connections) {
    if (c.body < 0 || c.body >= int(bodies.size()) || c.target < -1 ||
        c.target >= int(bodies.size()) || c.body == c.target ||
        c.type < Connection::Hinge || c.type > Connection::SpringHinge ||
        !valid(c.anchor) || !finite(c.lowerAngle, pi) ||
        !finite(c.upperAngle, pi) || c.lowerAngle > c.upperAngle ||
        !finite(c.stiffness, 1000) || c.stiffness < 0 ||
        !finite(c.damping, 100) || c.damping < 0) {
      error = "Invalid connection endpoint, type or angular limits.";
      return false;
    }
    auto pair = std::minmax(c.body, c.target);
    if (!pairs.insert(pair).second ||
        (bodies[c.body].motion != Body::Dynamic &&
         (c.target == -1 || bodies[c.target].motion != Body::Dynamic))) {
      error = "Duplicate connection or connection without a dynamic endpoint.";
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
      state.addBody(std::move(shape), transform(b.pose, b.length),
                    b.motion == Body::Dynamic ? b.mass : 0, s.damping,
                    b.motion == Body::Kinematic);
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
      else if (c.type == Connection::SpringHinge) {
        auto *spring = new btGeneric6DofSpring2Constraint(a, b, localA, localB);
        spring->setLinearLowerLimit(btVector3(0, 0, 0));
        spring->setLinearUpperLimit(btVector3(0, 0, 0));
        spring->setAngularLowerLimit(btVector3(0, 0, c.lowerAngle));
        spring->setAngularUpperLimit(btVector3(0, 0, c.upperAngle));
        spring->enableSpring(5, true);
        spring->setStiffness(5, c.stiffness, true);
        spring->setDamping(5, c.damping, true);
        spring->setEquilibriumPoint(5, 0);
        joint.reset(spring);
      } else {
        // Positive limits follow the body's counterclockwise planar rotation,
        // matching the spring hinge's Euler-Z convention.
        auto *hinge = new btHingeConstraint(a, b, localA, localB, false);
        hinge->setLimit(c.lowerAngle, c.upperAngle);
        joint.reset(hinge);
      }
      state.joints.push_back(std::move(joint));
      // Adjacent connected proxies may overlap at their joint.
      state.world.addConstraint(state.joints.back().get(), true);
    }
    Clip clip;
    Frame initial;
    std::vector<double> angles;
    for (const Body &b : bodies) angles.push_back(b.pose.angle);
    for (const Body &b : bodies) initial.push_back(b.pose);
    clip.push_back(std::move(initial));
    const btScalar dt = btScalar(1.0 / (s.fps * s.substeps));
    for (int frame = 1; frame < s.frames; ++frame) {
      for (int step = 1; step <= s.substeps; ++step) {
        const size_t sample = (frame - 1) * s.substeps + step;
        for (size_t i = 0; i < bodies.size(); ++i)
          if (bodies[i].motion == Body::Kinematic)
            state.bodies[i]->setWorldTransform(
                transform(bodies[i].animation[sample], bodies[i].length));
        state.world.stepSimulation(dt, 0);  // Exactly one explicit fixed step.
        for (size_t i = 0; i < bodies.size(); ++i)
          if (bodies[i].motion == Body::Dynamic) {
            const auto &basis = state.bodies[i]->getWorldTransform().getBasis();
            double angle = std::atan2(double(basis[1][0]), double(basis[0][0]));
            // Unwrap every physics step, including multiple turns per frame.
            angles[i] += std::remainder(angle - angles[i], 2 * pi);
          }
      }
      Frame poses;
      for (size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].motion != Body::Dynamic) {
          poses.push_back(bodies[i].motion == Body::Pinned
                              ? bodies[i].pose
                              : bodies[i].animation[frame * s.substeps]);
          continue;
        }
        const auto &t   = state.bodies[i]->getWorldTransform();
        btVector3 pivot = t * btVector3(0, -bodies[i].length / 2, 0);
        Pose pose{{double(pivot.x()), double(pivot.y())}, angles[i]};
        if (!valid(pose.pivot) || !finite(pose.angle, 100000) ||
            std::abs(double(pivot.z())) > 1e-3) {
          error = "Simulation diverged or left the drawing plane.";
          return false;
        }
        poses.push_back(pose);
      }
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
