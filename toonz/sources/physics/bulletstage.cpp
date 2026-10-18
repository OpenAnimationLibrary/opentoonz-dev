#include "bulletstage.h"

#include "tdoublekeyframe.h"
#include "toonz/stage.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace OTBullet {
namespace {
constexpr double pi = 3.141592653589793;
bool finite(const TAffine &a) {
  for (double v : {a.a11, a.a12, a.a13, a.a21, a.a22, a.a23})
    if (!std::isfinite(v) || std::abs(v) > 1e10) return false;
  return std::abs(a.det()) > 1e-10;
}
bool near(double a, double b) {
  return std::abs(a - b) <= 1e-6 * std::max({1.0, std::abs(a), std::abs(b)});
}
bool sameLinear(const TAffine &a, const TAffine &b) {
  return near(a.a11, b.a11) && near(a.a12, b.a12) && near(a.a21, b.a21) &&
         near(a.a22, b.a22);
}
bool same(const TAffine &a, const TAffine &b) {
  return sameLinear(a, b) && near(a.a13, b.a13) && near(a.a23, b.a23);
}
TDoubleParamP clone(TDoubleParam *parameter) {
  return TDoubleParamP(new TDoubleParam(*parameter));
}
}  // namespace

bool capture(const std::vector<TStageObject *> &objects, int firstFrame,
             int frames, std::vector<Binding> &bindings, std::string &error) {
  bindings.clear();
  error.clear();
  if (objects.empty() || objects.size() > 64 || firstFrame < 0 ||
      firstFrame > 1000000 || frames < 1 || frames > 1200) {
    error = "Invalid capture range or object count.";
    return false;
  }
  std::set<TStageObject *> unique;
  std::vector<Binding> result;
  for (auto *object : objects) {
    if (!object || !unique.insert(object).second || object->isLocked() ||
        object->getStatus() != TStageObject::XY || object->isCycleEnabled() ||
        object->getHandle().find('H') == 0 ||
        object->getParentHandle().find('H') == 0) {
      error =
          "Use distinct unlocked XY columns without cycling, paths, IK or "
          "drawing-hook pivots.";
      return false;
    }
    Binding binding;
    binding.object   = object;
    binding.parentId = object->getParent();
    for (int f = firstFrame; f < firstFrame + frames; ++f) {
      SourceFrame source;
      source.world  = object->getPlacement(f);
      source.parent = object->getParentPlacement(f);
      source.center = object->getCenter(f) * Stage::inch;
      source.x      = object->getParam(TStageObject::T_X, f);
      source.y      = object->getParam(TStageObject::T_Y, f);
      source.angle  = object->getParam(TStageObject::T_Angle, f);
      if (!finite(source.world) || !finite(source.parent) ||
          !std::isfinite(source.center.x) || !std::isfinite(source.center.y) ||
          !std::isfinite(source.x) || !std::isfinite(source.y) ||
          !std::isfinite(source.angle)) {
        error = "A captured transform is singular or non-finite.";
        return false;
      }
      binding.source.push_back(source);
    }
    result.push_back(std::move(binding));
  }
  bindings.swap(result);
  return true;
}

bool makeBake(const std::vector<Binding> &bindings,
              const std::vector<Body> &bodies, const Clip &clip, int firstFrame,
              double metersPerInch, Bake &bake, std::string &error) {
  bake.clear();
  error.clear();
  if (bindings.empty() || bindings.size() != bodies.size() || clip.empty() ||
      clip.size() > 1200 || firstFrame < 0 || firstFrame > 1000000 ||
      !std::isfinite(metersPerInch) || metersPerInch < 0.01 ||
      metersPerInch > 10) {
    error = "Invalid bake dimensions or unit conversion.";
    return false;
  }
  std::map<TStageObjectId, size_t> indices;
  for (size_t i = 0; i < bindings.size(); ++i) {
    const Binding &b = bindings[i];
    if (!b.object || b.source.size() != clip.size() || b.object->isLocked() ||
        b.object->getParent() != b.parentId ||
        !indices.emplace(b.object->getId(), i).second) {
      error = "The captured rig or frame range changed; simulate again.";
      return false;
    }
  }
  for (const Frame &f : clip)
    if (f.size() != bindings.size()) {
      error = "A simulated frame has missing bodies.";
      return false;
    }
  std::vector<std::vector<TAffine>> desired(clip.size());
  for (size_t f = 0; f < clip.size(); ++f)
    for (size_t i = 0; i < bindings.size(); ++i) {
      const SourceFrame &start = bindings[i].source.front();
      const Pose &pose         = clip[f][i];
      if (!std::isfinite(pose.pivot.x) || !std::isfinite(pose.pivot.y) ||
          !std::isfinite(pose.angle)) {
        error = "Non-finite simulated pose.";
        return false;
      }
      if (bodies[i].pinned && !same(start.world, bindings[i].source[f].world)) {
        error = "Pinned bodies must be stationary over the whole bake range.";
        return false;
      }
      TPointD initialPivot = start.world * start.center;
      TPointD newPivot(pose.pivot.x, pose.pivot.y);
      newPivot = newPivot * (Stage::inch / metersPerInch);
      TAffine world =
          TTranslation(newPivot) *
          TRotation((pose.angle - bodies[i].pose.angle) * 180 / pi) *
          TTranslation(-initialPivot) * start.world;
      if (!finite(world)) {
        error = "Invalid simulated placement.";
        return false;
      }
      desired[f].push_back(world);
    }
  const TStageObject::Channel channels[] = {
      TStageObject::T_X, TStageObject::T_Y, TStageObject::T_Angle};
  Bake result;
  for (size_t i = 0; i < bindings.size(); ++i) {
    if (bodies[i].pinned) continue;
    const Binding &binding = bindings[i];
    std::array<ChannelEdit, 3> edits;
    for (int c = 0; c < 3; ++c) {
      auto *parameter = binding.object->getParam(channels[c]);
      edits[c] = {TDoubleParamP(parameter), clone(parameter), clone(parameter)};
      std::set<double> keys;
      edits[c].after->getKeyframes(keys);
      for (double key : keys)
        if (key >= firstFrame && key <= firstFrame + int(clip.size()) - 1)
          edits[c].after->deleteKeyframe(key);
    }
    double previousAngle = binding.source.front().angle;
    for (size_t f = 0; f < clip.size(); ++f) {
      const SourceFrame &source = binding.source[f];
      auto parent               = indices.find(binding.parentId);
      TAffine parentWorld =
          parent == indices.end() ? source.parent : desired[f][parent->second];
      TAffine oldLocal = source.parent.inv() * source.world;
      TAffine local    = parentWorld.inv() * desired[f][i];
      double delta     = std::atan2(local.a21, local.a11) -
                     std::atan2(oldLocal.a21, oldLocal.a11);
      if (!finite(local) ||
          !sameLinear(local, TRotation(delta * 180 / pi) * oldLocal) ||
          norm2(source.center - binding.source.front().center) > 1e-12) {
        error =
            "This pose needs scale/shear or pivot changes. Use fixed body "
            "scales and rigid, uniformly scaled parents.";
        return false;
      }
      TPointD shift =
          (local * source.center - oldLocal * source.center) / Stage::inch;
      double angle = source.angle + delta * 180 / pi;
      // World and local parents may each have crossed a full turn.
      angle = previousAngle + std::remainder(angle - previousAngle, 360.0);
      previousAngle         = angle;
      const double values[] = {source.x + shift.x, source.y + shift.y, angle};
      for (int c = 0; c < 3; ++c) {
        if (!std::isfinite(values[c])) {
          error = "Non-finite baked channel.";
          return false;
        }
        TDoubleKeyframe key(firstFrame + int(f), values[c]);
        key.m_type = key.m_prevType = TDoubleKeyframe::Linear;
        edits[c].after->setKeyframe(key);
      }
    }
    result.insert(result.end(), edits.begin(), edits.end());
  }
  bake.swap(result);
  return true;
}

void applyBake(const Bake &bake, bool after) {
  for (const ChannelEdit &edit : bake)
    edit.channel->copy((after ? edit.after : edit.before).getPointer());
}
}  // namespace OTBullet
