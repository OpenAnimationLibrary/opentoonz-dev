#include "toonz/trig.h"

#include <iostream>
#include <random>
#include <stdexcept>

namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void near(double a, double b, const char *message, double tolerance = 1e-7) {
  check(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < tolerance,
        message);
}
void point(const TPointD &a, const TPointD &b, const char *message) {
  near(a.x, b.x, message);
  near(a.y, b.y, message);
}
void affine(const TAffine &a, const TAffine &b, const char *message) {
  near(a.a11, b.a11, message);
  near(a.a12, b.a12, message);
  near(a.a13, b.a13, message);
  near(a.a21, b.a21, message);
  near(a.a22, b.a22, message);
  near(a.a23, b.a23, message);
}
TPointD axisOf(const TAffine &a, TPointD axis = TPointD(0, 1)) {
  return a * axis - a * TPointD();
}

void transformConstraints() {
  TRigContext ctx;
  ctx.pivot          = TPointD(2, 3);
  ctx.targetPosition = TPointD(20, 30);
  ctx.targetAxis     = TPointD(1, 0);
  TAffine original   = TTranslation(7, -4) * TRotation(20) * TScale(-2, 3);
  TPointD pivot      = original * ctx.pivot;
  TRigConstraint c;
  c.type             = TRigConstraint::TranslateTo;
  c.offset           = TPointD(2, -2);
  c.enforcement      = 0.5;
  TAffine translated = applyRigConstraint(original, c, ctx, 0);
  point(translated * ctx.pivot, (pivot + ctx.targetPosition + c.offset) * 0.5,
        "weighted translation around pivot");
  near(translated.det(), original.det(),
       "translation preserves mirrored scale");

  c.type        = TRigConstraint::AimAt;
  c.enforcement = 1;
  TAffine aimed = applyRigConstraint(original, c, ctx, 0);
  point(aimed * ctx.pivot, pivot, "aim preserves pivot");
  point(normalize(axisOf(aimed)), normalize(ctx.targetPosition - pivot),
        "aim uses bone axis");
  near(aimed.det(), original.det(), "aim preserves determinant");

  c.type           = TRigConstraint::OrientLike;
  c.angleOffset    = 45;
  TAffine oriented = applyRigConstraint(original, c, ctx, 0);
  near(rigAngle(axisOf(oriented)), 45, "orient includes angular offset");
  point(oriented * ctx.pivot, pivot, "orient preserves pivot");

  c.type           = TRigConstraint::ParentLike;
  c.angleOffset    = 0;
  c.offset         = TPointD(1, 2);
  ctx.targetAxis   = TPointD(0, 1);
  TAffine parented = applyRigConstraint(original, c, ctx, 0);
  point(parented * ctx.pivot, ctx.targetPosition + TPointD(-2, 1),
        "parent offset rotates with target");
  point(normalize(axisOf(parented)), normalize(ctx.targetAxis),
        "parent orientation");

  c.type          = TRigConstraint::OrientLike;
  c.enforcement   = 0.5;
  ctx.pivot       = TPointD();
  ctx.targetAxis  = TRotation(-179) * TPointD(0, 1);
  TAffine halfway = applyRigConstraint(TRotation(179), c, ctx, 0);
  point(normalize(axisOf(halfway)), TPointD(0, -1), "shortest arc across wrap");
}

void limitsAndDegeneracies() {
  TRigContext ctx;
  ctx.parentPlacement = TTranslation(50, -10) * TRotation(35) * TScale(-2, 3);
  ctx.pivot           = TPointD(4, 5);
  TAffine local = TTranslation(10, -8) * TRotation(100) * TScale(-4, 0.2) *
                  TTranslation(-ctx.pivot);
  TAffine original = ctx.parentPlacement * local;
  TRigConstraint c;
  c.type         = TRigConstraint::LimitPosition;
  c.minimum      = TPointD(-2, -3);
  c.maximum      = TPointD(3, 4);
  TAffine placed = applyRigConstraint(original, c, ctx, 0);
  point(ctx.parentPlacement.inv() * placed * ctx.pivot, TPointD(3, -3),
        "position limits in parent space");

  c.type     = TRigConstraint::LimitRotation;
  c.minValue = -45;
  c.maxValue = 45;
  TAffine rotated =
      ctx.parentPlacement.inv() * applyRigConstraint(original, c, ctx, 0);
  near(rigAngle(axisOf(rotated)), -45,
       "rotation limits on parent-space bone direction");
  point(rotated * ctx.pivot, local * ctx.pivot,
        "rotation limit preserves pivot");

  c.type     = TRigConstraint::LimitScale;
  c.minValue = 0.5;
  c.maxValue = 2;
  TAffine scaled =
      ctx.parentPlacement.inv() * applyRigConstraint(original, c, ctx, 0);
  near(std::hypot(scaled.a11, scaled.a21), 2, "upper scale bound");
  near(std::hypot(scaled.a12, scaled.a22), 0.5, "lower scale bound");
  check(scaled.det() < 0, "scale limit preserves reflection");
  point(scaled * ctx.pivot, local * ctx.pivot, "scale limit preserves pivot");

  ctx.parentPlacement = TScale(0, 1);
  affine(applyRigConstraint(original, c, ctx, 0), original,
         "singular parent is safe");
  ctx.parentPlacement = TAffine();
  ctx.pivot           = TPointD();
  c.type              = TRigConstraint::AimAt;
  ctx.targetPosition  = TPointD();
  affine(applyRigConstraint(TAffine(), c, ctx, 0), TAffine(),
         "coincident aim is safe");
  affine(applyRigConstraint(TScale(0), c, ctx, 0), TScale(0),
         "zero scale is safe");
  c.type          = TRigConstraint::Distance;
  c.minValue      = 2;
  c.maxValue      = 4;
  TAffine overlap = applyRigConstraint(TAffine(), c, ctx, 0);
  point(overlap * TPointD(), TPointD(0, 2),
        "overlap distance has deterministic direction");
  TAffine outside = applyRigConstraint(TTranslation(10, 0), c, ctx, 0);
  point(outside * TPointD(), TPointD(4, 0), "outer distance bound");
  affine(applyRigConstraint(TTranslation(3, 0), c, ctx, 0), TTranslation(3, 0),
         "inside distance band unchanged");
}

void enforcementAndOrder() {
  TRigConstraint c;
  c.type            = TRigConstraint::TranslateTo;
  c.enforcement     = 0.25;
  c.enforcementKeys = {{10, 1}, {20, 0.5}};
  c.firstFrame      = 5;
  c.lastFrame       = 25;
  near(c.weight(4.999), 0, "range start");
  near(c.weight(5), 0.25, "base before first key");
  near(c.weight(10), 1, "held key");
  near(c.weight(19.99), 1, "fractional held sample");
  near(c.weight(20), 0.5, "next held key");
  near(c.weight(25.99), 0.5, "inclusive final frame");
  near(c.weight(26), 0, "range end");
  near(c.weight(std::numeric_limits<double>::infinity()), 0, "nonfinite frame");
  TRigContext ctx;
  ctx.targetPosition = TPointD(10, 0);
  for (double frame : {20.0, 6.0, 10.0, 25.5, 0.0, 20.0, 6.0}) {
    TAffine a = applyRigConstraint(TAffine(), c, ctx, frame);
    near(a.a13, 10 * c.weight(frame), "scrub order independent");
  }
  c.enforcementKeys.clear();
  c.firstFrame = c.lastFrame = -1;
  c.enforcement              = 1;
  ctx.targetPosition         = TPointD(2, 0);
  TRigConstraint aim;
  aim.type = TRigConstraint::AimAt;
  TRigContext aimCtx;
  aimCtx.targetPosition = TPointD(2, 2);
  TAffine moveThenAim   = applyRigConstraint(
        applyRigConstraint(TAffine(), c, ctx, 0), aim, aimCtx, 0);
  TAffine aimThenMove = applyRigConstraint(
      applyRigConstraint(TAffine(), aim, aimCtx, 0), c, ctx, 0);
  near(rigAngle(axisOf(moveThenAim)), 90, "stack position then aim");
  near(rigAngle(axisOf(aimThenMove)), 45, "stack aim then position");
  c.enabled = false;
  affine(applyRigConstraint(TAffine(), c, ctx, 0), TAffine(),
         "disabled is identity");
  c.enabled     = true;
  c.enforcement = std::numeric_limits<double>::quiet_NaN();
  check(!c.isValid(), "nonfinite enforcement rejected");
  affine(applyRigConstraint(TAffine(), c, ctx, 0), TAffine(),
         "invalid input not propagated");
  TRigBone bone;
  bone.tip = TPointD();
  check(!bone.isValid(), "zero length rejected");
}

void dependencies() {
  TRigDependencies graph{{"arm", {"body", "controller"}},
                         {"body", {"table"}},
                         {"controller", {"table"}}};
  check(rigDependsOn(graph, "arm", "table"), "transitive hierarchy dependency");
  check(!rigDependsOn(graph, "controller", "arm"), "independent controller");
  graph["controller"].push_back("arm");
  check(rigDependsOn(graph, "controller", "arm"), "constraint cycle detected");
  check(rigDependsOn(graph, "arm", "arm"), "self target rejected");
  check(!rigDependsOn(graph, "missing", "arm"),
        "missing target remains unresolved");
  check(!rigDependsOn(graph, "arm", "unrelated"),
        "cyclic malformed graph terminates");
  for (int i = 0; i < 5000; ++i)
    graph[std::to_string(i)] = {std::to_string(i + 1)};
  check(rigDependsOn(graph, "0", "5000"),
        "deep dependency graph uses bounded stack");
}

void randomizedAim() {
  std::mt19937 generator(8391);
  std::uniform_real_distribution<double> random(-50, 50);
  TRigConstraint aim;
  aim.type = TRigConstraint::AimAt;
  for (int i = 0; i < 1000; ++i) {
    TRigContext ctx;
    ctx.pivot          = TPointD(random(generator), random(generator));
    ctx.targetPosition = TPointD(random(generator), random(generator));
    TAffine input      = TTranslation(random(generator), random(generator)) *
                    TRotation(random(generator) * 7) *
                    TAffine(1, 0.4, 0, 0, 1, 0) * TScale(i % 2 ? -2 : 2, 0.5);
    TPointD pivot  = input * ctx.pivot;
    TAffine output = applyRigConstraint(input, aim, ctx, i);
    check(rigFinite(output), "finite randomized aim");
    near(input.det(), output.det(), "randomized aim preserves affine shape");
    point(output * ctx.pivot, pivot, "randomized pivot invariant");
    point(normalize(axisOf(output)), normalize(ctx.targetPosition - pivot),
          "randomized target direction");
  }
}
}  // namespace

int main() {
  try {
    transformConstraints();
    limitsAndDegeneracies();
    enforcementAndOrder();
    dependencies();
    randomizedAim();
    std::cout << "Skeleton rig tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
