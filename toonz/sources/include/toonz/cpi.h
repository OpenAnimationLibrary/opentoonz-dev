#pragma once

#ifndef TOONZ_CPI_INCLUDED
#define TOONZ_CPI_INCLUDED

#include "toonz/txshlevel.h"
#include "tvectorimage.h"
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#undef DVAPI
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#else
#define DVAPI DV_IMPORT_API
#endif

namespace Cpi {

// Point addresses are local to an immutable binding UUID. Hashes accelerate
// lookup; equality uses the complete address, never a hash alone.
using PointId = std::uint64_t;
inline PointId pointId(unsigned stroke, unsigned point) {
  return (PointId(stroke) << 32) | PointId(point);
}
inline unsigned strokeIndex(PointId id) { return unsigned(id >> 32); }
inline unsigned pointIndex(PointId id) { return unsigned(id & 0xffffffffu); }

struct DVAPI Vec3 {
  double x = 0, y = 0, z = 0;
  Vec3() = default;
  Vec3(double x, double y, double z = 0) : x(x), y(y), z(z) {}
  Vec3 operator+(const Vec3 &v) const;
  Vec3 operator-(const Vec3 &v) const;
  Vec3 operator*(double t) const;
  bool finite() const;
};

struct DVAPI Rotation {
  double w = 1, x = 0, y = 0, z = 0;
  Rotation normalized() const;
  Rotation inverse() const;
  Rotation operator*(const Rotation &q) const;
  Vec3 apply(const Vec3 &v) const;
  static Rotation axisAngle(const Vec3 &axis, double degrees);
  static Rotation interpolate(Rotation a, Rotation b, double t);
};

struct DVAPI Pose {
  Vec3 translation;
  Rotation rotation;
  std::map<PointId, Vec3> offsets;
  Vec3 offset(PointId id) const;
  static Pose interpolate(const Pose &a, const Pose &b, double t);
};

struct DVAPI ExtremePair {
  int first = 0, last = 72;  // Scene rows, zero based; inclusive endpoints.
  Pose start, end;
  std::map<int, Pose> keys;  // Deltas from the interpolated extremes.
  Pose baseline(double frame) const;
  Pose evaluate(double frame) const;
  void setPose(int frame, const Pose &pose);
};

struct DVAPI Binding {
  std::string id;
  TXshLevelP level;
  TFrameId fid;
  std::vector<std::vector<Vec3>> strokes;
  std::vector<bool> loops;
  static Binding capture(TXshLevel *level, const TFrameId &fid,
                         const TVectorImageP &image);
  bool matches(const TVectorImageP &image) const;
  bool contains(PointId point) const;
  Vec3 rest(PointId point) const;
};

struct DVAPI Group {
  std::string id, name, bindingId;
  std::set<PointId> points;
  Vec3 pivot;
  std::vector<ExtremePair> pairs;
  const ExtremePair *pairAt(int frame) const;
  ExtremePair *pairAt(int frame);
  bool createPair(int first, int distance);
  Pose evaluate(double frame) const;
  Vec3 position(const Binding &binding, PointId point, const Pose &pose) const;
  // Map a screen-plane displacement back to the rotated local XY plane.
  // Edge-on planes have no unique inverse and are deliberately rejected.
  bool localDelta(const Pose &pose, const TPointD &delta, Vec3 &result) const;
};

class DVAPI Data {
public:
  std::vector<Binding> bindings;
  std::vector<Group> groups;

  const Binding *binding(const std::string &id) const;
  const Binding *binding(TXshLevel *level, const TFrameId &fid) const;
  const Group *group(const std::string &id) const;
  Group *group(const std::string &id);
  bool addGroup(const Binding &binding, const std::string &name,
                const std::set<PointId> &points, std::string &newId);
  bool valid() const;
  size_t memorySize() const;
  bool empty() const { return groups.empty(); }
  TVectorImageP deform(TXshLevel *level, const TFrameId &fid, double frame,
                       const TVectorImageP &source) const;
  std::string alias(TXshLevel *level, const TFrameId &fid, double frame) const;
  void saveData(TOStream &os) const;
  void loadData(TIStream &is);
};

using Snapshot = std::shared_ptr<const Data>;
}  // namespace Cpi

#endif
