#include "bulletstage.h"

#include "tdoublekeyframe.h"
#include "toonz/stage.h"
#include "toonz/tstageobjecttree.h"

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
bool nearlyEqual(double a, double b) {
  return std::abs(a - b) <= 1e-6 * std::max({1.0, std::abs(a), std::abs(b)});
}
bool sameLinear(const TAffine &a, const TAffine &b) {
  return nearlyEqual(a.a11, b.a11) && nearlyEqual(a.a12, b.a12) &&
         nearlyEqual(a.a21, b.a21) && nearlyEqual(a.a22, b.a22);
}
bool same(const TAffine &a, const TAffine &b) {
  return sameLinear(a, b) && nearlyEqual(a.a13, b.a13) &&
         nearlyEqual(a.a23, b.a23);
}
TDoubleParamP clone(TDoubleParam *parameter) {
  return TDoubleParamP(new TDoubleParam(*parameter));
}
bool hierarchyUnchanged(const Binding &binding) {
  auto *object = binding.object.getPointer();
  for (const auto &ancestor : binding.ancestors) {
    if (!object || !ancestor || object->getParent() != ancestor->getId())
      return false;
    object = ancestor.getPointer();
  }
  return object && object->getParent() == TStageObjectId::NoneId;
}
bool checkOwnership(const std::vector<Binding> &bindings,
                    const std::vector<Body> &bodies, std::string &error) {
  std::set<TStageObjectId> dynamic;
  for (size_t i = 0; i < bindings.size(); ++i)
    if (bodies[i].motion == Body::Dynamic)
      dynamic.insert(bindings[i].object->getId());
  for (size_t i = 0; i < bindings.size(); ++i)
    if (bodies[i].motion != Body::Dynamic)
      for (const auto &ancestor : bindings[i].ancestors)
        if (dynamic.count(ancestor->getId())) {
          error =
              "Pinned and kinematic columns cannot descend from a selected "
              "dynamic column. Move controllers above the driven rig.";
          return false;
        }
  return true;
}
}  // namespace

bool capture(TStageObjectTree &tree, const std::vector<TStageObject *> &objects,
             int firstFrame, int frames, std::vector<Binding> &bindings,
             std::string &error) {
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
    if (!object || tree.getStageObject(object->getId(), false) != object ||
        !unique.insert(object).second || object->isLocked()) {
      error = "Use distinct unlocked columns from the current stage.";
      return false;
    }
    Binding binding;
    binding.object   = object;
    binding.parentId = object->getParent();
    std::set<TStageObject *> visited;
    for (auto *ancestor = object; ancestor;) {
      if (!visited.insert(ancestor).second ||
          ancestor->getStatus() != TStageObject::XY ||
          ancestor->isCycleEnabled() || ancestor->getHandle().find('H') == 0 ||
          ancestor->getParentHandle().find('H') == 0) {
        error =
            "Use XY parenting without cycles, paths, IK or drawing-hook "
            "pivots throughout the selected hierarchy.";
        return false;
      }
      if (ancestor != object) binding.ancestors.push_back(ancestor);
      if (ancestor->getParent() == TStageObjectId::NoneId) break;
      ancestor = tree.getStageObject(ancestor->getParent(), false);
      if (!ancestor) {
        error = "The selected hierarchy has a missing parent.";
        return false;
      }
    }
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

bool prepareControllers(const std::vector<Binding> &bindings, const Settings &s,
                        int firstFrame, double metersPerInch,
                        std::vector<Body> &bodies, std::string &error) {
  error.clear();
  if (bindings.empty() || bindings.size() > 64 ||
      bindings.size() != bodies.size() || firstFrame < 0 ||
      firstFrame > 1000000 || s.frames < 1 || s.frames > 1200 ||
      s.substeps < 1 || s.substeps > 32 || !std::isfinite(metersPerInch) ||
      metersPerInch < 0.01 || metersPerInch > 10) {
    error = "Invalid controller sampling range or unit conversion.";
    return false;
  }
  for (const auto &b : bindings)
    if (!b.object || b.source.size() != size_t(s.frames) ||
        !hierarchyUnchanged(b)) {
      error = "The captured rig changed; simulate again.";
      return false;
    }
  if (!checkOwnership(bindings, bodies, error)) return false;
  auto result = bodies;
  for (size_t i = 0; i < bindings.size(); ++i) {
    const Binding &binding = bindings[i];
    Body &body             = result[i];
    body.animation.clear();
    if (body.motion == Body::Dynamic) continue;
    const auto &start = binding.source.front();
    TAffine previous  = start.world;
    double angle      = 0;
    for (int step = 0; step <= (s.frames - 1) * s.substeps; ++step) {
      double time    = firstFrame + double(step) / s.substeps;
      TAffine world  = binding.object->getPlacement(time);
      TPointD center = binding.object->getCenter(time) * Stage::inch;
      // Affine matrices wrap at a full turn. Check authored channel increments
      // too, so a fast controller cannot silently alias at this sampling rate.
      double turn = 0;
      if (step) {
        auto rotation = [&](TStageObject *o) {
          return std::abs(
              o->getParam(TStageObject::T_Angle, time) -
              o->getParam(TStageObject::T_Angle,
                          firstFrame + double(step - 1) / s.substeps));
        };
        turn = rotation(binding.object.getPointer());
        for (const auto &ancestor : binding.ancestors)
          turn += rotation(ancestor.getPointer());
      }
      if (!std::isfinite(turn) || turn >= 180) {
        error = "Controller rotation is too fast; increase Steps per Frame.";
        return false;
      }
      angle += std::remainder(std::atan2(world.a21, world.a11) -
                                  std::atan2(previous.a21, previous.a11),
                              2 * pi);
      if (!finite(world) || norm2(center - start.center) > 1e-12 ||
          !sameLinear(world, TRotation(angle * 180 / pi) * start.world) ||
          (body.motion == Body::Pinned && !same(world, start.world))) {
        error =
            "Controllers require fixed pivots and rigid world motion. "
            "Use Kinematic for animated columns instead of Pinned.";
        return false;
      }
      previous = world;
      if (body.motion == Body::Kinematic) {
        TPointD p = world * center / Stage::inch * metersPerInch;
        body.animation.push_back({{p.x, p.y}, body.pose.angle + angle});
      }
    }
  }
  bodies.swap(result);
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
        b.object->getParent() != b.parentId || !hierarchyUnchanged(b) ||
        !indices.emplace(b.object->getId(), i).second) {
      error = "The captured rig or frame range changed; simulate again.";
      return false;
    }
  }
  if (!checkOwnership(bindings, bodies, error)) return false;
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
      if (bodies[i].motion == Body::Pinned &&
          !same(start.world, bindings[i].source[f].world)) {
        error = "Pinned bodies must be stationary over the whole bake range.";
        return false;
      }
      if (bodies[i].motion != Body::Dynamic) {
        desired[f].push_back(bindings[i].source[f].world);
        continue;
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
    if (bodies[i].motion != Body::Dynamic) continue;
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
      TAffine parentWorld       = source.parent;
      double turn =
          f ? std::abs(clip[f][i].angle - clip[f - 1][i].angle) * 180 / pi : 0;
      // A selected ancestor can be separated by unselected pegs/columns. Keep
      // their authored relative transforms while replacing that ancestor.
      for (const auto &ancestor : binding.ancestors) {
        auto parent = indices.find(ancestor->getId());
        if (parent != indices.end()) {
          size_t p = parent->second;
          parentWorld =
              desired[f][p] * bindings[p].source[f].world.inv() * source.parent;
          if (f)
            turn +=
                std::abs(clip[f][p].angle - clip[f - 1][p].angle) * 180 / pi;
          break;
        }
        if (f)
          turn += std::abs(ancestor->getParam(TStageObject::T_Angle,
                                              firstFrame + double(f)) -
                           ancestor->getParam(TStageObject::T_Angle,
                                              firstFrame + double(f) - 1));
      }
      // Integer-frame affine samples cannot disambiguate arbitrarily fast
      // relative turns. Keep the preview, but never silently bake the wrong
      // turn.
      if (!std::isfinite(turn) || turn >= 180) {
        error =
            "Rotation is too fast for this integer-frame bake. Use a "
            "higher scene frame rate or slower controller motion.";
        return false;
      }
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
