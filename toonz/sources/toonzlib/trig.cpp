#include "toonz/trig.h"

#include <cmath>

namespace {
bool finitePoint(const TPointD &p) {
  return std::isfinite(p.x) && std::isfinite(p.y);
}

TPointD vectorOf(const TAffine &a, const TPointD &v) {
  return TPointD(a.a11 * v.x + a.a12 * v.y, a.a21 * v.x + a.a22 * v.y);
}

TAffine rotateAt(const TAffine &a, const TPointD &pivot, double degrees) {
  return TTranslation(pivot) * TRotation(degrees) * TTranslation(-pivot) * a;
}

double turn(double from, double to, double weight) {
  return std::remainder(to - from, 360.0) * weight;
}
}  // namespace

double TRigConstraint::weight(double frame) const {
  if (!enabled || !std::isfinite(frame) ||
      (firstFrame >= 0 && frame < firstFrame) ||
      (lastFrame >= 0 && frame >= double(lastFrame) + 1.0))
    return 0.0;
  auto key = enforcementKeys.upper_bound(
      (int)std::floor(tcrop(frame, double(std::numeric_limits<int>::min()),
                            double(std::numeric_limits<int>::max()))));
  return key == enforcementKeys.begin() ? enforcement : std::prev(key)->second;
}

bool TRigConstraint::isValid() const {
  if (type < TranslateTo || type >= TypeCount || endpoint < Pivot ||
      endpoint >= EndpointCount || !std::isfinite(enforcement) ||
      enforcement < 0 || enforcement > 1 || !finitePoint(offset) ||
      !std::isfinite(angleOffset) || !finitePoint(minimum) ||
      !finitePoint(maximum) || minimum.x > maximum.x || minimum.y > maximum.y ||
      !std::isfinite(minValue) || !std::isfinite(maxValue) ||
      minValue > maxValue || firstFrame < -1 || lastFrame < -1 ||
      (firstFrame >= 0 && lastFrame >= 0 && firstFrame > lastFrame) ||
      enforcementKeys.size() > 100000 || targetId.size() > 128)
    return false;
  if (type == Distance && minValue < 0) return false;
  if (type == LimitScale && minValue <= 0) return false;
  if (type == LimitRotation && (minValue < -180 || maxValue > 180))
    return false;
  for (const auto &key : enforcementKeys)
    if (key.first < 0 || !std::isfinite(key.second) || key.second < 0 ||
        key.second > 1)
      return false;
  return true;
}

bool TRigBone::isValid() const {
  if (!finitePoint(tip) || norm2(tip) < 1e-12 || norm2(tip) > 1e12 ||
      constraints.size() > 128)
    return false;
  for (const auto &constraint : constraints)
    if (!constraint.isValid()) return false;
  return true;
}

bool rigDependsOn(const TRigDependencies &graph, const std::string &from,
                  const std::string &on) {
  std::vector<std::string> pending{from};
  std::set<std::string> visited;
  while (!pending.empty()) {
    std::string id = pending.back();
    pending.pop_back();
    if (id == on) return true;
    if (!visited.insert(id).second) continue;
    auto node = graph.find(id);
    if (node != graph.end())
      pending.insert(pending.end(), node->second.begin(), node->second.end());
  }
  return false;
}

bool rigFinite(const TAffine &a) {
  return std::isfinite(a.a11) && std::isfinite(a.a12) && std::isfinite(a.a13) &&
         std::isfinite(a.a21) && std::isfinite(a.a22) && std::isfinite(a.a23);
}

double rigAngle(const TPointD &direction) {
  return std::atan2(direction.y, direction.x) * M_180_PI;
}

TAffine applyRigConstraint(const TAffine &placement, const TRigConstraint &c,
                           const TRigContext &ctx, double frame) {
  double w = c.weight(frame);
  if (w <= 0 || !c.isValid() || !rigFinite(placement) ||
      !rigFinite(ctx.parentPlacement) || !finitePoint(ctx.pivot) ||
      !finitePoint(ctx.axis) || !finitePoint(ctx.targetPosition) ||
      !finitePoint(ctx.targetAxis) || !std::isfinite(ctx.unitsPerInch) ||
      ctx.unitsPerInch <= 0)
    return placement;
  TAffine result = placement;
  TPointD pivot  = placement * ctx.pivot;
  TPointD axis   = vectorOf(placement, ctx.axis);
  if (norm2(axis) < 1e-16) return placement;

  if (c.type == TRigConstraint::TranslateTo) {
    TPointD desired = ctx.targetPosition + c.offset * ctx.unitsPerInch;
    result          = TTranslation((desired - pivot) * w) * placement;
  } else if (c.type == TRigConstraint::AimAt ||
             c.type == TRigConstraint::OrientLike ||
             c.type == TRigConstraint::ParentLike) {
    TPointD direction = c.type == TRigConstraint::AimAt
                            ? ctx.targetPosition - pivot
                            : ctx.targetAxis;
    if (norm2(direction) < 1e-16) return placement;
    result =
        rotateAt(placement, pivot,
                 turn(rigAngle(axis), rigAngle(direction) + c.angleOffset, w));
    if (c.type == TRigConstraint::ParentLike) {
      TPointD desired =
          ctx.targetPosition +
          TRotation(rigAngle(ctx.targetAxis)) * (c.offset * ctx.unitsPerInch);
      result = TTranslation((desired - pivot) * w) * result;
    }
  } else if (c.type == TRigConstraint::Distance) {
    TPointD delta     = pivot - ctx.targetPosition;
    double distance   = norm(delta);
    TPointD direction = distance > 1e-8 ? delta / distance : normalize(axis);
    double desired    = tcrop(distance, c.minValue * ctx.unitsPerInch,
                              c.maxValue * ctx.unitsPerInch);
    result = TTranslation(direction * ((desired - distance) * w)) * placement;
  } else {
    if (std::abs(ctx.parentPlacement.det()) < 1e-10) return placement;
    TAffine local      = ctx.parentPlacement.inv() * placement;
    TPointD localPivot = local * ctx.pivot;
    if (c.type == TRigConstraint::LimitPosition) {
      TPointD desired(tcrop(localPivot.x, c.minimum.x * ctx.unitsPerInch,
                            c.maximum.x * ctx.unitsPerInch),
                      tcrop(localPivot.y, c.minimum.y * ctx.unitsPerInch,
                            c.maximum.y * ctx.unitsPerInch));
      local = TTranslation((desired - localPivot) * w) * local;
    } else if (c.type == TRigConstraint::LimitRotation) {
      TPointD localAxis = vectorOf(local, ctx.axis);
      double angle      = rigAngle(localAxis);
      local             = rotateAt(local, localPivot,
                                   turn(angle, tcrop(angle, c.minValue, c.maxValue), w));
    } else if (c.type == TRigConstraint::LimitScale) {
      double sx = std::hypot(local.a11, local.a21);
      double sy = std::hypot(local.a12, local.a22);
      if (sx < 1e-8 || sy < 1e-8) return placement;
      double x = 1 + w * (tcrop(sx, c.minValue, c.maxValue) / sx - 1);
      double y = 1 + w * (tcrop(sy, c.minValue, c.maxValue) / sy - 1);
      local    = local * TTranslation(ctx.pivot) * TScale(x, y) *
              TTranslation(-ctx.pivot);
    }
    result = ctx.parentPlacement * local;
  }
  return rigFinite(result) ? result : placement;
}
