#include "glbpose.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace otglb {
namespace {

constexpr float Epsilon = 1.0e-8f;

void warn(PoseResult &result, const std::string &message) {
  if (std::find(result.warnings.begin(), result.warnings.end(), message) ==
      result.warnings.end())
    result.warnings.push_back(message);
}

bool finiteMatrix(const Matrix &m) {
  return std::all_of(m.begin(), m.end(), [](float v) { return std::isfinite(v); });
}

Matrix multiply(const Matrix &a, const Matrix &b) {
  Matrix out{};
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row)
      for (int k = 0; k < 4; ++k)
        out[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
  return out;
}

std::array<float, 4> normalized(std::array<float, 4> q) {
  double length2 = 0.0;
  for (float v : q) length2 += double(v) * v;
  if (!(length2 > 0.0) || !std::isfinite(length2)) return {{0, 0, 0, 1}};
  const float inv = float(1.0 / std::sqrt(length2));
  for (auto &v : q) v *= inv;
  return q;
}

Matrix compose(const std::array<float, 3> &t, const std::array<float, 4> &rotation,
               const std::array<float, 3> &s) {
  const auto q = normalized(rotation);
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  const float xx = x * x, yy = y * y, zz = z * z;
  const float xy = x * y, xz = x * z, yz = y * z;
  const float wx = w * x, wy = w * y, wz = w * z;
  Matrix m{{
      (1 - 2 * (yy + zz)) * s[0], (2 * (xy + wz)) * s[0],
      (2 * (xz - wy)) * s[0], 0,
      (2 * (xy - wz)) * s[1], (1 - 2 * (xx + zz)) * s[1],
      (2 * (yz + wx)) * s[1], 0,
      (2 * (xz + wy)) * s[2], (2 * (yz - wx)) * s[2],
      (1 - 2 * (xx + yy)) * s[2], 0,
      t[0], t[1], t[2], 1}};
  return m;
}

bool inverseAffine(const Matrix &m, Matrix &out) {
  // Invert the upper-left 3x3, then the translation. glTF node matrices are affine.
  const double a00 = m[0], a01 = m[4], a02 = m[8];
  const double a10 = m[1], a11 = m[5], a12 = m[9];
  const double a20 = m[2], a21 = m[6], a22 = m[10];
  const double c00 = a11 * a22 - a12 * a21;
  const double c01 = a02 * a21 - a01 * a22;
  const double c02 = a01 * a12 - a02 * a11;
  const double c10 = a12 * a20 - a10 * a22;
  const double c11 = a00 * a22 - a02 * a20;
  const double c12 = a02 * a10 - a00 * a12;
  const double c20 = a10 * a21 - a11 * a20;
  const double c21 = a01 * a20 - a00 * a21;
  const double c22 = a00 * a11 - a01 * a10;
  const double det = a00 * c00 + a01 * c10 + a02 * c20;
  if (!std::isfinite(det) || std::abs(det) <= 1.0e-12) return false;
  const double inv = 1.0 / det;
  out = {{float(c00 * inv), float(c10 * inv), float(c20 * inv), 0,
          float(c01 * inv), float(c11 * inv), float(c21 * inv), 0,
          float(c02 * inv), float(c12 * inv), float(c22 * inv), 0,
          0, 0, 0, 1}};
  const double tx = m[12], ty = m[13], tz = m[14];
  out[12] = float(-(out[0] * tx + out[4] * ty + out[8] * tz));
  out[13] = float(-(out[1] * tx + out[5] * ty + out[9] * tz));
  out[14] = float(-(out[2] * tx + out[6] * ty + out[10] * tz));
  return finiteMatrix(out);
}

std::size_t valueOffset(std::size_t key, bool cubic, std::size_t components,
                        int slot) {
  return (key * (cubic ? 3 : 1) + (cubic ? slot : 0)) * components;
}

std::vector<float> sample(const AnimationSampler &sampler, std::size_t components,
                          double seconds, bool rotation) {
  const bool cubic = sampler.interpolation == AnimationInterpolation::CubicSpline;
  std::vector<float> out(components);
  auto copyKey = [&](std::size_t key) {
    const std::size_t offset = valueOffset(key, cubic, components, cubic ? 1 : 0);
    std::copy_n(sampler.values.data() + offset, components, out.data());
  };
  if (seconds <= sampler.times.front()) {
    copyKey(0);
  } else if (seconds >= sampler.times.back()) {
    copyKey(sampler.times.size() - 1);
  } else {
    const auto upper = std::upper_bound(sampler.times.begin(), sampler.times.end(),
                                        float(seconds));
    const std::size_t next = std::size_t(upper - sampler.times.begin());
    const std::size_t previous = next - 1;
    const double t0 = sampler.times[previous], t1 = sampler.times[next];
    const float u = float((seconds - t0) / (t1 - t0));
    if (sampler.interpolation == AnimationInterpolation::Step) {
      copyKey(previous);
    } else if (cubic) {
      const float dt = float(t1 - t0);
      const float u2 = u * u, u3 = u2 * u;
      const float h00 = 2 * u3 - 3 * u2 + 1;
      const float h10 = u3 - 2 * u2 + u;
      const float h01 = -2 * u3 + 3 * u2;
      const float h11 = u3 - u2;
      const auto p0 = valueOffset(previous, true, components, 1);
      const auto m0 = valueOffset(previous, true, components, 2);
      const auto p1 = valueOffset(next, true, components, 1);
      const auto m1 = valueOffset(next, true, components, 0);
      for (std::size_t i = 0; i < components; ++i)
        out[i] = h00 * sampler.values[p0 + i] + h10 * dt * sampler.values[m0 + i] +
                 h01 * sampler.values[p1 + i] + h11 * dt * sampler.values[m1 + i];
    } else if (rotation) {
      const auto aOffset = valueOffset(previous, false, components, 0);
      const auto bOffset = valueOffset(next, false, components, 0);
      std::array<float, 4> a{}, b{};
      std::copy_n(sampler.values.data() + aOffset, 4, a.begin());
      std::copy_n(sampler.values.data() + bOffset, 4, b.begin());
      a = normalized(a); b = normalized(b);
      float dot = 0;
      for (int i = 0; i < 4; ++i) dot += a[i] * b[i];
      if (dot < 0) {
        dot = -dot;
        for (auto &v : b) v = -v;
      }
      std::array<float, 4> q{};
      if (dot > 0.9995f) {
        for (int i = 0; i < 4; ++i) q[i] = a[i] + u * (b[i] - a[i]);
      } else {
        dot = std::max(-1.0f, std::min(1.0f, dot));
        const float theta = std::acos(dot);
        const float sinTheta = std::sin(theta);
        const float wa = std::sin((1 - u) * theta) / sinTheta;
        const float wb = std::sin(u * theta) / sinTheta;
        for (int i = 0; i < 4; ++i) q[i] = wa * a[i] + wb * b[i];
      }
      q = normalized(q);
      std::copy(q.begin(), q.end(), out.begin());
    } else {
      const auto a = valueOffset(previous, false, components, 0);
      const auto b = valueOffset(next, false, components, 0);
      for (std::size_t i = 0; i < components; ++i)
        out[i] = sampler.values[a + i] + u * (sampler.values[b + i] - sampler.values[a + i]);
    }
  }
  if (rotation) {
    std::array<float, 4> q{};
    std::copy_n(out.begin(), 4, q.begin());
    q = normalized(q);
    std::copy(q.begin(), q.end(), out.begin());
  }
  return out;
}

}  // namespace

PoseResult evaluatePose(const Asset &asset, int animation, double sourceSeconds) {
  PoseResult result;
  result.pose.animation = animation;
  result.pose.sourceSeconds = sourceSeconds;
  if (!std::isfinite(sourceSeconds)) {
    result.error = "GLB pose time must be finite.";
    return result;
  }
  if (animation != NoIndex &&
      (animation < 0 || std::size_t(animation) >= asset.animations.size())) {
    result.error = "GLB pose animation index is out of range.";
    return result;
  }

  result.pose.nodes.resize(asset.nodes.size());
  for (std::size_t i = 0; i < asset.nodes.size(); ++i) {
    const auto &source = asset.nodes[i];
    auto &node = result.pose.nodes[i];
    node.translation = source.translation;
    node.rotation = source.rotation;
    node.scale = source.scale;
    node.local = source.local;
  }

  if (animation != NoIndex) {
    const auto &clip = asset.animations[animation];
    for (const auto &channel : clip.channels) {
      if (channel.node == NoIndex || channel.path == AnimationPath::Unknown) continue;
      if (channel.path == AnimationPath::Weights) {
        warn(result, "Morph weight animation is retained but not evaluated in the pose layer.");
        continue;
      }
      if (channel.sampler < 0 || std::size_t(channel.sampler) >= clip.samplers.size() ||
          channel.node < 0 || std::size_t(channel.node) >= result.pose.nodes.size()) {
        result.error = "GLB pose contains an invalid channel reference.";
        return result;
      }
      const auto &sampler = clip.samplers[channel.sampler];
      const bool rotation = channel.path == AnimationPath::Rotation;
      const auto value = sample(sampler, channel.components, sourceSeconds, rotation);
      auto &node = result.pose.nodes[channel.node];
      if (channel.path == AnimationPath::Translation)
        std::copy_n(value.begin(), 3, node.translation.begin());
      else if (channel.path == AnimationPath::Scale)
        std::copy_n(value.begin(), 3, node.scale.begin());
      else if (rotation)
        std::copy_n(value.begin(), 4, node.rotation.begin());
    }
  }

  // Compose animated TRS nodes, preserve matrix-authored nodes, then rebuild the
  // hierarchy in parent-before-child order. Loader validation guarantees a DAG.
  for (std::size_t i = 0; i < asset.nodes.size(); ++i)
    if (!asset.nodes[i].hasMatrix)
      result.pose.nodes[i].local = compose(result.pose.nodes[i].translation,
                                           result.pose.nodes[i].rotation,
                                           result.pose.nodes[i].scale);
  std::vector<int> order;
  order.reserve(asset.nodes.size());
  for (std::size_t i = 0; i < asset.nodes.size(); ++i)
    if (asset.nodes[i].parent == NoIndex) order.push_back(int(i));
  for (std::size_t at = 0; at < order.size(); ++at) {
    const int index = order[at];
    const int parent = asset.nodes[index].parent;
    result.pose.nodes[index].world = parent == NoIndex ? result.pose.nodes[index].local :
        multiply(result.pose.nodes[parent].world, result.pose.nodes[index].local);
    if (!finiteMatrix(result.pose.nodes[index].world)) {
      result.error = "GLB pose evaluation produced a non-finite transform.";
      return result;
    }
    for (int child : asset.nodes[index].children) order.push_back(child);
  }
  if (order.size() != asset.nodes.size()) {
    result.error = "GLB pose hierarchy is incomplete or cyclic.";
    return result;
  }

  std::size_t bindings = 0;
  for (const auto &node : asset.nodes) bindings += node.hasSkin;
  result.pose.skins.reserve(bindings);
  for (std::size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex) {
    const auto &node = asset.nodes[nodeIndex];
    if (!node.hasSkin) continue;
    if (node.skin < 0 || std::size_t(node.skin) >= asset.skins.size()) {
      result.error = "GLB pose contains an invalid skin reference.";
      return result;
    }
    Matrix inverseMesh{};
    if (!inverseAffine(result.pose.nodes[nodeIndex].world, inverseMesh)) {
      result.error = "GLB skinned mesh transform is singular at the requested pose.";
      return result;
    }
    const auto &skin = asset.skins[node.skin];
    SkinPose binding;
    binding.node = int(nodeIndex);
    binding.skin = node.skin;
    binding.jointMatrices.resize(skin.joints.size());
    for (std::size_t j = 0; j < skin.joints.size(); ++j) {
      const int joint = skin.joints[j];
      if (joint < 0 || std::size_t(joint) >= result.pose.nodes.size() ||
          j >= skin.inverseBindMatrices.size()) {
        result.error = "GLB pose contains incomplete skin joint data.";
        return result;
      }
      binding.jointMatrices[j] = multiply(
          multiply(inverseMesh, result.pose.nodes[joint].world), skin.inverseBindMatrices[j]);
      if (!finiteMatrix(binding.jointMatrices[j])) {
        result.error = "GLB skin evaluation produced a non-finite joint matrix.";
        return result;
      }
    }
    result.pose.skins.push_back(std::move(binding));
  }
  return result;
}

}  // namespace otglb
