#pragma once

#ifndef T_RIG_INCLUDED
#define T_RIG_INCLUDED

#include "tgeometry.h"
#include <map>
#include <set>
#include <string>
#include <vector>

#undef DVAPI
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#else
#define DVAPI DV_IMPORT_API
#endif

// Rig geometry belongs to a stage object. Animation remains in its existing
// channels; constraints are an ordered, non-destructive placement overlay.
struct DVAPI TRigConstraint {
  enum Type {
    TranslateTo,
    OrientLike,
    AimAt,
    ParentLike,
    Distance,
    LimitPosition,
    LimitRotation,
    LimitScale,
    TypeCount
  };
  enum Endpoint { Pivot, Tip, EndpointCount };

  Type type         = AimAt;
  Endpoint endpoint = Pivot;
  std::string targetId;  // Persistent identity, never a column number.
  bool enabled       = true;
  double enforcement = 1.0;
  // Held keys, matching the common constraint on/off workflow. Frames are
  // zero-based internally, including fractional render sample times.
  std::map<int, double> enforcementKeys;
  int firstFrame = -1, lastFrame = -1;
  TPointD offset;
  double angleOffset = 0.0;
  TPointD minimum    = TPointD(-10000, -10000);
  TPointD maximum    = TPointD(10000, 10000);
  double minValue = 0.0, maxValue = 1.0;

  bool hasTarget() const { return type <= Distance; }
  double weight(double frame) const;
  bool isValid() const;
};

struct DVAPI TRigBone {
  bool enabled = false;
  TPointD tip =
      TPointD(0, 1);  // Inches from the object's pivot, in local space.
  std::vector<TRigConstraint> constraints;

  bool isValid() const;
};

// The same graph check is used by assignment, reparenting and evaluation.
// Edges point from a dependent to the objects it reads. Include disabled and
// out-of-range links so enabling a constraint cannot introduce a latent cycle.
using TRigDependencies = std::map<std::string, std::vector<std::string>>;
DVAPI bool rigDependsOn(const TRigDependencies &graph, const std::string &from,
                        const std::string &on);

struct DVAPI TRigContext {
  TAffine parentPlacement;
  TPointD pivot;  // Local coordinates, in stage units.
  TPointD axis = TPointD(0, 1);
  TPointD targetPosition;
  TPointD targetAxis  = TPointD(0, 1);
  double unitsPerInch = 1.0;
};

DVAPI bool rigFinite(const TAffine &aff);
DVAPI double rigAngle(const TPointD &direction);
DVAPI TAffine applyRigConstraint(const TAffine &placement,
                                 const TRigConstraint &constraint,
                                 const TRigContext &context, double frame);

#endif
