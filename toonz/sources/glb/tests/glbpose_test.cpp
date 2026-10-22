#include "glbpose.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}
void near(float actual, float expected, float epsilon = 0.0002f) {
  check(std::abs(actual - expected) <= epsilon,
        "numeric mismatch: " + std::to_string(actual) + " != " + std::to_string(expected));
}
otglb::Matrix identity() {
  otglb::Matrix m{};
  m[0] = m[5] = m[10] = m[15] = 1;
  return m;
}
otglb::Node node(int parent = otglb::NoIndex) {
  otglb::Node n;
  n.parent = parent;
  n.local = n.world = identity();
  return n;
}
otglb::AnimationSampler sampler(std::initializer_list<float> times,
                                  std::initializer_list<float> values,
                                  int components,
                                  otglb::AnimationInterpolation interpolation =
                                      otglb::AnimationInterpolation::Linear) {
  otglb::AnimationSampler s;
  s.times = times;
  s.values = values;
  s.outputComponents = components;
  s.interpolation = interpolation;
  return s;
}
otglb::AnimationChannel channel(int samplerIndex, int nodeIndex,
                                  otglb::AnimationPath path,
                                  std::size_t components) {
  otglb::AnimationChannel c;
  c.sampler = samplerIndex;
  c.node = nodeIndex;
  c.path = path;
  c.components = components;
  return c;
}
otglb::Asset oneNode(const otglb::AnimationSampler &s,
                       otglb::AnimationPath path,
                       std::size_t components) {
  otglb::Asset asset;
  asset.nodes.push_back(node());
  otglb::Animation a;
  a.samplers.push_back(s);
  a.channels.push_back(channel(0, 0, path, components));
  asset.animations.push_back(a);
  asset.animationCount = 1;
  return asset;
}

}  // namespace

int main() {
  int passed = 0, failed = 0;
  auto run = [&](const char *name, const std::function<void()> &test) {
    try {
      test(); ++passed; std::cout << "PASS " << name << '\n';
    } catch (const std::exception &e) {
      ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n';
    }
  };

  run("rest pose and hierarchy are evaluated without an animation", [&] {
    otglb::Asset asset;
    auto root = node(); root.translation = {{4, 0, 0}}; root.children = {1};
    auto child = node(0); child.translation = {{0, 3, 0}};
    asset.nodes = {root, child};
    auto r = otglb::evaluatePose(asset, otglb::NoIndex, 12.5);
    check(bool(r) && r.pose.animation == otglb::NoIndex, r.error);
    near(r.pose.nodes[0].world[12], 4); near(r.pose.nodes[1].world[12], 4);
    near(r.pose.nodes[1].world[13], 3);
  });

  run("LINEAR translation interpolates and clamps to sampler endpoints", [&] {
    auto asset = oneNode(sampler({1, 3}, {2,0,0, 6,0,0}, 3),
                         otglb::AnimationPath::Translation, 3);
    auto before = otglb::evaluatePose(asset, 0, -10);
    auto middle = otglb::evaluatePose(asset, 0, 2);
    auto after = otglb::evaluatePose(asset, 0, 99);
    check(before && middle && after, "linear pose failed");
    near(before.pose.nodes[0].translation[0], 2);
    near(middle.pose.nodes[0].translation[0], 4);
    near(after.pose.nodes[0].translation[0], 6);
  });

  run("STEP holds the preceding value", [&] {
    auto asset = oneNode(sampler({0, 1}, {3,0,0, 9,0,0}, 3,
                                 otglb::AnimationInterpolation::Step),
                         otglb::AnimationPath::Translation, 3);
    near(otglb::evaluatePose(asset, 0, 0.999).pose.nodes[0].translation[0], 3);
    near(otglb::evaluatePose(asset, 0, 1.0).pose.nodes[0].translation[0], 9);
  });

  run("LINEAR quaternion uses shortest-path spherical interpolation", [&] {
    auto asset = oneNode(sampler({0, 1}, {0,0,0,1, 0,0,-1,0}, 4),
                         otglb::AnimationPath::Rotation, 4);
    auto r = otglb::evaluatePose(asset, 0, 0.5);
    check(bool(r), r.error);
    // q=(0,0,-sqrt(.5),sqrt(.5)) is -90 degrees around Z; -q at the key is
    // the same final orientation and exercises the shortest-sign correction.
    near(r.pose.nodes[0].world[0], 0, 0.001f);
    near(r.pose.nodes[0].world[1], -1, 0.001f);
    near(r.pose.nodes[0].world[4], 1, 0.001f);
    near(r.pose.nodes[0].world[5], 0, 0.001f);
  });

  run("CUBICSPLINE applies interval-scaled tangents", [&] {
    // key0: in=(0), value=(0), out=(1); key1: in=(1), value=(1), out=(0)
    auto asset = oneNode(sampler({0, 1},
        {0,0,0, 0,0,0, 1,0,0, 1,0,0, 1,0,0, 0,0,0}, 3,
        otglb::AnimationInterpolation::CubicSpline),
        otglb::AnimationPath::Translation, 3);
    auto r = otglb::evaluatePose(asset, 0, 0.5);
    check(bool(r), r.error);
    near(r.pose.nodes[0].translation[0], 0.5f);
  });

  run("CUBICSPLINE quaternion result is normalized", [&] {
    auto asset = oneNode(sampler({0, 1},
        {0,0,0,0,  0,0,0,1,  0,0,0,0,
         0,0,0,0,  0,0,1,0,  0,0,0,0}, 4,
        otglb::AnimationInterpolation::CubicSpline),
        otglb::AnimationPath::Rotation, 4);
    auto r = otglb::evaluatePose(asset, 0, 0.5);
    check(bool(r), r.error);
    const auto &q = r.pose.nodes[0].rotation;
    near(std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]), 1);
    near(std::abs(q[2]), 0.7071067f, 0.001f);
    near(std::abs(q[3]), 0.7071067f, 0.001f);
  });

  run("partial animation preserves authored components", [&] {
    auto asset = oneNode(sampler({0, 1}, {0,0,0, 2,0,0}, 3),
                         otglb::AnimationPath::Translation, 3);
    asset.nodes[0].scale = {{2, 3, 4}};
    asset.nodes[0].rotation = {{0, 0, 0.70710678f, 0.70710678f}};
    auto r = otglb::evaluatePose(asset, 0, 0.5);
    check(bool(r), r.error);
    near(r.pose.nodes[0].translation[0], 1);
    near(r.pose.nodes[0].scale[2], 4);
    near(r.pose.nodes[0].rotation[2], 0.70710678f);
  });

  run("matrix-authored nodes remain unchanged", [&] {
    otglb::Asset asset;
    auto n = node(); n.hasMatrix = true; n.local[12] = 7; n.local[13] = -2;
    asset.nodes.push_back(n);
    auto r = otglb::evaluatePose(asset, otglb::NoIndex, 0);
    check(bool(r), r.error);
    near(r.pose.nodes[0].local[12], 7); near(r.pose.nodes[0].world[13], -2);
  });

  run("skin matrices are produced in mesh-local space", [&] {
    otglb::Asset asset;
    auto mesh = node(); mesh.translation = {{10, 0, 0}}; mesh.hasSkin = true; mesh.skin = 0;
    auto joint = node(); joint.translation = {{12, 0, 0}};
    asset.nodes = {mesh, joint};
    otglb::Skin skin; skin.joints = {1}; skin.inverseBindMatrices = {identity()};
    asset.skins.push_back(skin); asset.skinCount = 1;
    auto r = otglb::evaluatePose(asset, otglb::NoIndex, 0);
    check(bool(r) && r.pose.skins.size() == 1, r.error);
    near(r.pose.skins[0].jointMatrices[0][12], 2);
  });

  run("skin matrix includes inverse bind transform", [&] {
    otglb::Asset asset;
    auto mesh = node(); mesh.hasSkin = true; mesh.skin = 0;
    auto joint = node(); joint.translation = {{5, 0, 0}};
    asset.nodes = {mesh, joint};
    auto bind = identity(); bind[12] = -3;
    otglb::Skin skin; skin.joints = {1}; skin.inverseBindMatrices = {bind};
    asset.skins.push_back(skin); asset.skinCount = 1;
    auto r = otglb::evaluatePose(asset, otglb::NoIndex, 0);
    check(bool(r), r.error);
    near(r.pose.skins[0].jointMatrices[0][12], 2);
  });

  run("singular skinned mesh transform is rejected", [&] {
    otglb::Asset asset;
    auto mesh = node(); mesh.hasSkin = true; mesh.skin = 0; mesh.scale = {{0,1,1}};
    asset.nodes = {mesh, node()};
    otglb::Skin skin; skin.joints = {1}; skin.inverseBindMatrices = {identity()};
    asset.skins.push_back(skin);
    auto r = otglb::evaluatePose(asset, otglb::NoIndex, 0);
    check(!r && r.error.find("singular") != std::string::npos, "singular transform accepted");
  });

  run("random-access evaluation is independent of call order", [&] {
    auto asset = oneNode(sampler({0, 1}, {0,0,0, 10,0,0}, 3),
                         otglb::AnimationPath::Translation, 3);
    const auto late = otglb::evaluatePose(asset, 0, 0.8);
    const auto early = otglb::evaluatePose(asset, 0, 0.2);
    const auto lateAgain = otglb::evaluatePose(asset, 0, 0.8);
    check(late && early && lateAgain, "random access failed");
    near(late.pose.nodes[0].translation[0], 8);
    near(early.pose.nodes[0].translation[0], 2);
    near(lateAgain.pose.nodes[0].translation[0], 8);
    near(asset.nodes[0].translation[0], 0);
  });

  run("morph weights are diagnosed and not mistaken for bone transforms", [&] {
    auto asset = oneNode(sampler({0, 1}, {0,1, 1,0}, 1),
                         otglb::AnimationPath::Weights, 2);
    auto r = otglb::evaluatePose(asset, 0, 0.5);
    check(bool(r) && r.warnings.size() == 1, r.error);
    near(r.pose.nodes[0].world[12], 0); near(r.pose.nodes[0].world[13], 0);
  });

  run("invalid animation indices and non-finite time are rejected", [&] {
    otglb::Asset asset; asset.nodes.push_back(node());
    auto badIndex = otglb::evaluatePose(asset, 0, 0);
    auto badTime = otglb::evaluatePose(asset, otglb::NoIndex,
                                       std::numeric_limits<double>::quiet_NaN());
    check(!badIndex && !badTime, "invalid request accepted");
  });

  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
