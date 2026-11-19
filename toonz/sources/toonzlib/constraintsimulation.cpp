#include "toonz/constraintsimulation.h"

#include "tdoublekeyframe.h"
#include "toonz/stage.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/txshcolumn.h"
#include "toonz/txsheet.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace ConstraintSimulation {
namespace {
using Constraint    = TStageObject::Constraint;
constexpr double pi = 3.141592653589793;
bool finite(const TAffine &a) {
  for (double v : {a.a11, a.a12, a.a13, a.a21, a.a22, a.a23})
    if (!std::isfinite(v) || std::abs(v) > 1e10) return false;
  return std::abs(a.det()) > 1e-10;
}
bool nearlyEqual(double a, double b) {
  return std::abs(a - b) <= 1e-7 * std::max({1.0, std::abs(a), std::abs(b)});
}
bool sameLinear(const TAffine &a, const TAffine &b) {
  return nearlyEqual(a.a11, b.a11) && nearlyEqual(a.a12, b.a12) &&
         nearlyEqual(a.a21, b.a21) && nearlyEqual(a.a22, b.a22);
}
struct Object {
  TStageObject *stage;
  int parent  = -1;
  bool mobile = false;
  TPointD start;
  std::vector<Constraint> aims;
  std::array<std::vector<double>, 3> values;
  std::array<bool, 3> changed{{false, false, false}};
  double previousDelta = 0;
};
}  // namespace

bool prepare(TXsheet &xsheet, const Settings &s, Bake &bake,
             std::string &error) {
  bake = Bake();
  error.clear();
  if (s.firstFrame < 0 || s.firstFrame > 1000000 ||
      s.lastFrame < s.firstFrame || s.lastFrame - s.firstFrame >= 1200 ||
      !std::isfinite(s.fps) || s.fps < 1 || s.fps > 240 ||
      !std::isfinite(s.gravity) || s.gravity < 0 || s.gravity > 1000) {
    error = "Use 1 to 1200 frames and a finite, nonnegative gravity strength.";
    return false;
  }
  auto *tree = xsheet.getStageObjectTree();
  if (tree->getStageObjectCount() > 512) {
    error = "This first simulation supports at most 512 stage objects.";
    return false;
  }
  std::vector<Object> objects;
  std::map<TStageObjectId, int> indices;
  int mobileCount = 0;
  for (int i = 0; i < tree->getStageObjectCount(); ++i) {
    auto *stage = tree->getStageObject(i);
    Object object;
    object.stage  = stage;
    object.mobile = stage->getSimulationMotion() == Constraint::Mobile;
    if (object.mobile &&
        (stage->getStatus() != TStageObject::XY || stage->isCycleEnabled())) {
      error = "Mobile objects require XY motion without paths, IK or cycling.";
      return false;
    }
    mobileCount += object.mobile;
    indices[stage->getId()] = int(objects.size());
    objects.push_back(std::move(object));
  }
  if (!mobileCount) {
    error = "Assign Mobile to at least one drawing column or pegbar first.";
    return false;
  }
  std::vector<std::vector<int>> dependencies(objects.size());
  for (size_t i = 0; i < objects.size(); ++i) {
    auto &o     = objects[i];
    auto parent = indices.find(o.stage->getParent());
    if (o.stage->getParent() != TStageObjectId::NoneId &&
        parent == indices.end()) {
      error = "A stage object has a missing parent.";
      return false;
    }
    if (parent != indices.end()) {
      o.parent = parent->second;
      dependencies[i].push_back(o.parent);
    }
    std::vector<Constraint> links;
    for (const auto &link : o.stage->getConstraints())
      if (link.scope == Constraint::Self) links.push_back(link);
    if (o.parent >= 0)
      for (const auto &link : objects[o.parent].stage->getConstraints())
        if (link.scope == Constraint::Children) links.push_back(link);
    for (const auto &link : links) {
      if (!link.enabled || link.isMotionAssignment()) continue;
      if (!std::isfinite(link.influence) || !std::isfinite(link.angleOffset)) {
        error = "A constraint has non-finite values.";
        return false;
      }
      if (link.influence <= 0) continue;
      if (link.type != Constraint::AimAt) {
        error =
            "Gravity simulation uses Fixed, Mobile and Aim At only. "
            "Remove older Buffer assignments before baking.";
        return false;
      }
      auto target = indices.find(link.target);
      if (target == indices.end()) continue;
      dependencies[i].push_back(target->second);
      o.aims.push_back(link);
    }
  }
  // Catch parenting/link cycles before requesting any recursive placements.
  std::vector<int> visited(objects.size());
  std::function<bool(int)> visit = [&](int i) {
    if (visited[i] == 1) return false;
    if (visited[i] == 2) return true;
    visited[i] = 1;
    for (int dependency : dependencies[i])
      if (!visit(dependency)) return false;
    visited[i] = 2;
    return true;
  };
  for (int i = 0; i < int(objects.size()); ++i)
    if (!visit(i)) {
      error = "A parenting or Aim At dependency cycle must be removed first.";
      return false;
    }
  for (const auto &o : objects) {
    if (!o.mobile) continue;
    for (const Object *ancestor = &o; ancestor;
         ancestor               = ancestor->parent < 0 ? nullptr
                                                       : &objects[ancestor->parent]) {
      if (ancestor->stage->getStatus() != TStageObject::XY ||
          ancestor->stage->isCycleEnabled() ||
          ancestor->stage->getHandle().find('H') == 0 ||
          ancestor->stage->getParentHandle().find('H') == 0) {
        error =
            "Mobile hierarchies require XY motion without paths, IK, "
            "cycling or drawing-hook pivots.";
        return false;
      }
    }
  }
  const TStageObject::Channel channels[] = {
      TStageObject::T_X, TStageObject::T_Y, TStageObject::T_Angle};
  for (int f = s.firstFrame; f <= s.lastFrame; ++f) {
    std::vector<TAffine> authoredLocal, beforeAim, afterAim;
    for (auto &o : objects) {
      // Native parent placement includes authored constraints. Keep the matrix
      // before this object's own Aim At, which remains live after the bake.
      TAffine local = o.stage->getLocalPlacement(f);
      TAffine world = o.stage->getParentPlacement(f) * local;
      if (!finite(local) || !finite(world)) {
        error = "A source transform is singular or non-finite.";
        return false;
      }
      if (f == s.firstFrame) o.start = world * TPointD();
      if (o.mobile) {
        double time = (f - s.firstFrame) / s.fps;
        TPointD position =
            o.start + TPointD(0, -0.5 * s.gravity * time * time * Stage::inch);
        world = TTranslation(position - world * TPointD()) * world;
      }
      authoredLocal.push_back(local);
      beforeAim.push_back(world);
    }
    for (size_t i = 0; i < objects.size(); ++i) {
      TAffine world  = beforeAim[i];
      TPointD source = world * TPointD();
      for (const auto &link : objects[i].aims) {
        TPointD offset =
            beforeAim[indices.at(link.target)] * TPointD() - source;
        if (norm2(offset) < 1e-16) continue;
        double desired =
            std::atan2(offset.y, offset.x) * 180 / pi + link.angleOffset;
        double current = std::atan2(world.a21, world.a11) * 180 / pi;
        double delta   = std::remainder(desired - current, 360.0) *
                       std::min(1.0, link.influence);
        world = TTranslation(source) * TRotation(delta) *
                TTranslation(-source) * world;
      }
      if (!finite(world)) {
        error = "Simulation produced an invalid placement.";
        return false;
      }
      afterAim.push_back(world);
    }
    for (size_t i = 0; i < objects.size(); ++i) {
      auto &o            = objects[i];
      TAffine parent     = o.parent < 0 ? TAffine() : afterAim[o.parent];
      TAffine local      = parent.inv() * beforeAim[i];
      const TAffine &old = authoredLocal[i];
      double delta       = std::remainder(
                (std::atan2(local.a21, local.a11) - std::atan2(old.a21, old.a11)) *
                    180 / pi,
                360.0);
      delta = o.previousDelta + std::remainder(delta - o.previousDelta, 360.0);
      o.previousDelta = delta;
      if (!finite(local) || !sameLinear(local, TRotation(delta) * old)) {
        error =
            "This hierarchy needs scale or shear changes. Use rigid or "
            "uniformly scaled parents for the gravity bake.";
        return false;
      }
      TPointD center        = o.stage->getCenter(f) * Stage::inch;
      TPointD shift         = (local * center - old * center) / Stage::inch;
      const double values[] = {o.stage->getParam(channels[0], f) + shift.x,
                               o.stage->getParam(channels[1], f) + shift.y,
                               o.stage->getParam(channels[2], f) + delta};
      for (int c = 0; c < 3; ++c) {
        if (!std::isfinite(values[c])) {
          error = "Simulation produced a non-finite channel value.";
          return false;
        }
        o.values[c].push_back(values[c]);
        o.changed[c] =
            o.changed[c] || (o.mobile && c < 2) ||
            !nearlyEqual(values[c], o.stage->getParam(channels[c], f));
      }
    }
  }
  Bake result;
  result.mobileObjects = mobileCount;
  for (const auto &o : objects) {
    if (!o.changed[0] && !o.changed[1] && !o.changed[2]) continue;
    auto *stage  = o.stage;
    auto id      = stage->getId();
    auto *column = id.isColumn() ? xsheet.getColumn(id.getIndex()) : nullptr;
    if (stage->isLocked() || (column && column->isLocked()) ||
        stage->getStatus() != TStageObject::XY || stage->isCycleEnabled() ||
        stage->getHandle().find('H') == 0 ||
        stage->getParentHandle().find('H') == 0) {
      error =
          "Unlock affected objects and use XY motion without paths, IK, "
          "cycling or drawing-hook pivots before baking.";
      return false;
    }
    if (!o.mobile) ++result.compensatedObjects;
    for (int c = 0; c < 3; ++c) {
      if (!o.changed[c]) continue;
      auto *parameter = stage->getParam(channels[c]);
      ChannelEdit edit{TDoubleParamP(parameter),
                       TDoubleParamP(new TDoubleParam(*parameter)),
                       TDoubleParamP(new TDoubleParam(*parameter))};
      std::set<double> keys;
      edit.after->getKeyframes(keys);
      for (double key : keys)
        if (key >= s.firstFrame && key <= s.lastFrame)
          edit.after->deleteKeyframe(key);
      for (int f = s.firstFrame; f <= s.lastFrame; ++f) {
        TDoubleKeyframe key(f, o.values[c][f - s.firstFrame]);
        key.m_type = key.m_prevType = TDoubleKeyframe::Linear;
        edit.after->setKeyframe(key);
      }
      result.channels.push_back(std::move(edit));
    }
  }
  bake = std::move(result);
  return true;
}

void apply(const Bake &bake, bool after) {
  for (const auto &edit : bake.channels)
    edit.channel->copy((after ? edit.after : edit.before).getPointer());
}
}  // namespace ConstraintSimulation
