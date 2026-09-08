#include "toonz/cpi.h"
#include "toonz/txshleveltypes.h"
#include "tstroke.h"
#include "tstream.h"
#include "tthread.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Cpi {
namespace {
std::string uuid() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}
Vec3 lerp(const Vec3 &a, const Vec3 &b, double t) {
  return a * (1 - t) + b * t;
}
void require(bool ok) {
  if (!ok) throw TException("Invalid control point interpolation data");
}
QString qs(const std::string &s) { return QString::fromStdString(s); }
QJsonArray vec(const Vec3 &v) { return {v.x, v.y, v.z}; }
double number(const QJsonValue &v) {
  require(v.isDouble() && std::isfinite(v.toDouble()));
  return v.toDouble();
}
int integer(const QJsonValue &v) {
  double n = number(v);
  require(n >= 0 && n <= (std::numeric_limits<int>::max)() &&
          n == std::floor(n));
  return int(n);
}
Vec3 readVec(const QJsonValue &v) {
  require(v.isArray() && v.toArray().size() == 3);
  auto a = v.toArray();
  return Vec3(number(a[0]), number(a[1]), number(a[2]));
}
PointId readId(const QJsonValue &v) {
  require(v.isString());
  bool ok = false;
  auto id = v.toString().toULongLong(&ok, 16);
  require(ok);
  return PointId(id);
}
QString idString(PointId id) { return QString::number(qulonglong(id), 16); }
QJsonObject poseJson(const Pose &p) {
  QJsonArray offsets;
  for (const auto &o : p.offsets)
    offsets.append(QJsonArray{idString(o.first), vec(o.second)});
  return {{"translation", vec(p.translation)},
          {"rotation",
           QJsonArray{p.rotation.w, p.rotation.x, p.rotation.y, p.rotation.z}},
          {"offsets", offsets}};
}
Pose readPose(const QJsonValue &v) {
  require(v.isObject());
  auto o = v.toObject();
  Pose p;
  p.translation = readVec(o["translation"]);
  require(o["rotation"].isArray() && o["rotation"].toArray().size() == 4);
  auto q       = o["rotation"].toArray();
  p.rotation.w = number(q[0]);
  p.rotation.x = number(q[1]);
  p.rotation.y = number(q[2]);
  p.rotation.z = number(q[3]);
  p.rotation   = p.rotation.normalized();
  require(o["offsets"].isArray());
  for (const auto &value : o["offsets"].toArray()) {
    auto a = value.toArray();
    require(a.size() == 2);
    require(p.offsets.emplace(readId(a[0]), readVec(a[1])).second);
  }
  return p;
}
QJsonObject groupJson(const Group &g) {
  QJsonArray points, pairs;
  for (auto p : g.points) points.append(idString(p));
  for (const auto &pair : g.pairs) {
    QJsonArray keys;
    for (const auto &key : pair.keys)
      keys.append(QJsonArray{key.first, poseJson(key.second)});
    pairs.append(QJsonObject{{"first", pair.first},
                             {"last", pair.last},
                             {"start", poseJson(pair.start)},
                             {"end", poseJson(pair.end)},
                             {"keys", keys}});
  }
  return {{"id", qs(g.id)},
          {"name", qs(g.name)},
          {"binding", qs(g.bindingId)},
          {"pivot", vec(g.pivot)},
          {"points", points},
          {"pairs", pairs}};
}
Group readGroup(const QJsonValue &v) {
  require(v.isObject());
  auto o = v.toObject();
  Group g;
  g.id        = o["id"].toString().toStdString();
  g.name      = o["name"].toString().toStdString();
  g.bindingId = o["binding"].toString().toStdString();
  g.pivot     = readVec(o["pivot"]);
  require(o["points"].isArray() && o["pairs"].isArray());
  for (const auto &p : o["points"].toArray())
    require(g.points.insert(readId(p)).second);
  for (const auto &value : o["pairs"].toArray()) {
    require(value.isObject());
    auto p = value.toObject();
    ExtremePair pair;
    pair.first = integer(p["first"]);
    pair.last  = integer(p["last"]);
    pair.start = readPose(p["start"]);
    pair.end   = readPose(p["end"]);
    require(p["keys"].isArray());
    for (const auto &value : p["keys"].toArray()) {
      auto a = value.toArray();
      require(a.size() == 2);
      require(pair.keys.emplace(integer(a[0]), readPose(a[1])).second);
    }
    g.pairs.push_back(std::move(pair));
  }
  return g;
}
QJsonDocument readJson(const std::string &text) {
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(text), &error);
  require(error.error == QJsonParseError::NoError);
  return doc;
}
bool validPose(const Pose &p, const Group &g, const Binding &b) {
  if (!p.translation.finite()) return false;
  double n = p.rotation.w * p.rotation.w + p.rotation.x * p.rotation.x +
             p.rotation.y * p.rotation.y + p.rotation.z * p.rotation.z;
  if (!std::isfinite(n) || std::abs(n - 1) > 1e-8) return false;
  for (const auto &v : p.offsets) {
    if (!g.points.count(v.first) || !v.second.finite()) return false;
    unsigned stroke = strokeIndex(v.first), point = pointIndex(v.first);
    unsigned last = unsigned(b.strokes[stroke].size() - 1);
    if (b.loops[stroke] && (point == 0 || point == last)) {
      Vec3 other = p.offset(pointId(stroke, point == 0 ? last : 0));
      if (other.x != v.second.x || other.y != v.second.y ||
          other.z != v.second.z)
        return false;
    }
  }
  return true;
}
}  // namespace

Vec3 Vec3::operator+(const Vec3 &v) const {
  return Vec3(x + v.x, y + v.y, z + v.z);
}
Vec3 Vec3::operator-(const Vec3 &v) const {
  return Vec3(x - v.x, y - v.y, z - v.z);
}
Vec3 Vec3::operator*(double t) const { return Vec3(x * t, y * t, z * t); }
bool Vec3::finite() const {
  return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}
Rotation Rotation::normalized() const {
  double n = std::sqrt(w * w + x * x + y * y + z * z);
  require(std::isfinite(n) && n > 1e-12);
  Rotation q;
  q.w = w / n;
  q.x = x / n;
  q.y = y / n;
  q.z = z / n;
  return q;
}
Rotation Rotation::inverse() const {
  Rotation q = normalized();
  q.x        = -q.x;
  q.y        = -q.y;
  q.z        = -q.z;
  return q;
}
Rotation Rotation::operator*(const Rotation &q) const {
  Rotation r;
  r.w = w * q.w - x * q.x - y * q.y - z * q.z;
  r.x = w * q.x + x * q.w + y * q.z - z * q.y;
  r.y = w * q.y - x * q.z + y * q.w + z * q.x;
  r.z = w * q.z + x * q.y - y * q.x + z * q.w;
  return r.normalized();
}
Vec3 Rotation::apply(const Vec3 &v) const {
  auto q = normalized();
  Vec3 u(q.x, q.y, q.z);
  double dot = u.x * v.x + u.y * v.y + u.z * v.z;
  Vec3 cross(u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z,
             u.x * v.y - u.y * v.x);
  return u * (2 * dot) + v * (q.w * q.w - q.x * q.x - q.y * q.y - q.z * q.z) +
         cross * (2 * q.w);
}
Rotation Rotation::axisAngle(const Vec3 &axis, double degrees) {
  double n = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
  require(n > 1e-12 && std::isfinite(n) && std::isfinite(degrees));
  double a = degrees * (3.14159265358979323846 / 360.0), s = std::sin(a) / n;
  Rotation q;
  q.w = std::cos(a);
  q.x = axis.x * s;
  q.y = axis.y * s;
  q.z = axis.z * s;
  return q;
}
Rotation Rotation::interpolate(Rotation a, Rotation b, double t) {
  a          = a.normalized();
  b          = b.normalized();
  double dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
  if (dot < 0) {
    b.w = -b.w;
    b.x = -b.x;
    b.y = -b.y;
    b.z = -b.z;
    dot = -dot;
  }
  double s = 1 - t, u = t;
  if (dot < 0.9995) {
    double angle = std::acos(std::min(1.0, dot));
    s            = std::sin((1 - t) * angle) / std::sin(angle);
    u            = std::sin(t * angle) / std::sin(angle);
  }
  Rotation q;
  q.w = s * a.w + u * b.w;
  q.x = s * a.x + u * b.x;
  q.y = s * a.y + u * b.y;
  q.z = s * a.z + u * b.z;
  return q.normalized();
}
Vec3 Pose::offset(PointId id) const {
  auto i = offsets.find(id);
  return i == offsets.end() ? Vec3() : i->second;
}
Pose Pose::interpolate(const Pose &a, const Pose &b, double t) {
  Pose p;
  p.translation = lerp(a.translation, b.translation, t);
  p.rotation    = Rotation::interpolate(a.rotation, b.rotation, t);
  for (const auto &o : a.offsets)
    p.offsets[o.first] = lerp(o.second, b.offset(o.first), t);
  for (const auto &o : b.offsets)
    if (!a.offsets.count(o.first)) p.offsets[o.first] = o.second * t;
  return p;
}
Pose ExtremePair::baseline(double frame) const {
  return Pose::interpolate(
      start, end,
      std::max(0.0, std::min(1.0, (frame - first) / double(last - first))));
}
Pose ExtremePair::evaluate(double frame) const {
  Pose base = baseline(frame);
  if (frame <= first || frame >= last || keys.empty()) return base;
  auto hi = keys.upper_bound(int(std::floor(frame)));
  Pose low, high;
  int a = first, b = last;
  if (hi != keys.end()) {
    b    = hi->first;
    high = hi->second;
  }
  if (hi != keys.begin()) {
    auto lo = std::prev(hi);
    a       = lo->first;
    low     = lo->second;
  }
  Pose delta       = Pose::interpolate(low, high, (frame - a) / double(b - a));
  base.translation = base.translation + delta.translation;
  base.rotation    = base.rotation * delta.rotation;
  for (const auto &o : delta.offsets)
    base.offsets[o.first] = base.offset(o.first) + o.second;
  return base;
}
void ExtremePair::setPose(int frame, const Pose &pose) {
  require(frame >= first && frame <= last);
  if (frame == first) {
    start = pose;
    return;
  }
  if (frame == last) {
    end = pose;
    return;
  }
  Pose base         = baseline(frame), delta;
  delta.translation = pose.translation - base.translation;
  delta.rotation    = base.rotation.inverse() * pose.rotation;
  for (const auto &o : base.offsets)
    delta.offsets[o.first] = pose.offset(o.first) - o.second;
  for (const auto &o : pose.offsets)
    delta.offsets[o.first] = o.second - base.offset(o.first);
  for (auto it = delta.offsets.begin(); it != delta.offsets.end();) {
    const auto &v = it->second;
    if (v.x == 0 && v.y == 0 && v.z == 0)
      it = delta.offsets.erase(it);
    else
      ++it;
  }
  keys[frame] = std::move(delta);
}
Binding Binding::capture(TXshLevel *level, const TFrameId &fid,
                         const TVectorImageP &image) {
  require(level && image);
  Binding b;
  b.id    = uuid();
  b.level = level;
  b.fid   = fid;
  TThread::MutexLocker lock(image->getMutex());
  for (unsigned s = 0; s < image->getStrokeCount(); ++s) {
    auto stroke = image->getStroke(s);
    std::vector<Vec3> points;
    for (int p = 0; p < stroke->getControlPointCount(); ++p) {
      auto v = stroke->getControlPoint(p);
      points.emplace_back(v.x, v.y);
    }
    b.strokes.push_back(std::move(points));
    b.loops.push_back(stroke->isSelfLoop());
  }
  return b;
}
bool Binding::contains(PointId id) const {
  auto s = strokeIndex(id);
  return s < strokes.size() && pointIndex(id) < strokes[s].size();
}
Vec3 Binding::rest(PointId id) const {
  return strokes.at(strokeIndex(id)).at(pointIndex(id));
}
bool Binding::matches(const TVectorImageP &image) const {
  if (!image || strokes.size() != image->getStrokeCount()) return false;
  // PLI loading also normalizes geometry at 1/128 pixel, including files
  // written with finer delta precision. Compare deltas so the tolerance
  // does not grow with stroke length.
  const double tolerance = 1.1 / 128.0;
  for (unsigned s = 0; s < strokes.size(); ++s) {
    auto stroke = image->getStroke(s);
    if (strokes[s].size() != size_t(stroke->getControlPointCount()) ||
        loops[s] != stroke->isSelfLoop())
      return false;
    Vec3 previous, previousRest;
    for (unsigned p = 0; p < strokes[s].size(); ++p) {
      auto cp = stroke->getControlPoint(p);
      Vec3 point(cp.x, cp.y), rest = strokes[s][p];
      auto d = (point - previous) - (rest - previousRest);
      if (!point.finite() || std::abs(d.x) > tolerance ||
          std::abs(d.y) > tolerance)
        return false;
      previous     = point;
      previousRest = rest;
    }
  }
  return true;
}
const ExtremePair *Group::pairAt(int frame) const {
  for (const auto &p : pairs)
    if (frame >= p.first && frame <= p.last) return &p;
  return nullptr;
}
ExtremePair *Group::pairAt(int frame) {
  return const_cast<ExtremePair *>(
      static_cast<const Group *>(this)->pairAt(frame));
}
bool Group::createPair(int first, int distance) {
  if (first < 0 || distance < 1 ||
      first > (std::numeric_limits<int>::max)() - 1 - distance)
    return false;
  int last = first + distance;
  for (const auto &p : pairs)
    if (first <= p.last && last >= p.first) return false;
  ExtremePair pair;
  pair.first = first;
  pair.last  = last;
  pair.start = pair.end = evaluate(first);
  pairs.push_back(std::move(pair));
  std::sort(pairs.begin(), pairs.end(),
            [](const ExtremePair &a, const ExtremePair &b) {
              return a.first < b.first;
            });
  return true;
}
Pose Group::evaluate(double frame) const {
  if (pairs.empty()) return Pose();
  for (const auto &p : pairs) {
    if (frame < p.first)
      return (&p == &pairs.front()) ? p.start : std::prev(&p)->end;
    if (frame <= p.last) return p.evaluate(frame);
  }
  return pairs.back().end;
}
Vec3 Group::position(const Binding &b, PointId id, const Pose &pose) const {
  return pivot + pose.translation +
         pose.rotation.apply(b.rest(id) - pivot + pose.offset(id));
}
bool Group::localDelta(const Pose &pose, const TPointD &delta,
                       Vec3 &result) const {
  auto x = pose.rotation.apply(Vec3(1, 0)), y = pose.rotation.apply(Vec3(0, 1));
  double det = x.x * y.y - y.x * x.y;
  if (std::abs(det) < 1e-6) return false;
  result = Vec3((y.y * delta.x - y.x * delta.y) / det,
                (-x.y * delta.x + x.x * delta.y) / det);
  return result.finite();
}
const Binding *Data::binding(const std::string &id) const {
  for (const auto &b : bindings)
    if (b.id == id) return &b;
  return nullptr;
}
const Binding *Data::binding(TXshLevel *level, const TFrameId &fid) const {
  for (const auto &b : bindings)
    if (b.level.getPointer() == level && b.fid == fid) return &b;
  return nullptr;
}
const Group *Data::group(const std::string &id) const {
  for (const auto &g : groups)
    if (g.id == id) return &g;
  return nullptr;
}
Group *Data::group(const std::string &id) {
  return const_cast<Group *>(static_cast<const Data *>(this)->group(id));
}
bool Data::addGroup(const Binding &b, const std::string &name,
                    const std::set<PointId> &points, std::string &newId) {
  if (points.empty() || qs(name).trimmed().isEmpty()) return false;
  for (auto p : points)
    if (!b.contains(p)) return false;
  auto members = points;
  for (auto p : points) {
    unsigned s = strokeIndex(p), last = unsigned(b.strokes[s].size() - 1);
    if (b.loops[s] && (pointIndex(p) == 0 || pointIndex(p) == last)) {
      members.insert(pointId(s, 0));
      members.insert(pointId(s, last));
    }
  }
  for (const auto &g : groups) {
    if (qs(g.name).compare(qs(name).trimmed(), Qt::CaseInsensitive) == 0)
      return false;
    if (g.bindingId == b.id)
      for (auto p : members)
        if (g.points.count(p)) return false;
  }
  Group g;
  g.id        = uuid();
  g.bindingId = b.id;
  g.name      = qs(name).trimmed().toStdString();
  g.points    = members;
  for (auto p : members) g.pivot = g.pivot + b.rest(p) * (1.0 / members.size());
  if (!binding(b.id)) bindings.push_back(b);
  newId = g.id;
  groups.push_back(std::move(g));
  return true;
}
bool Data::valid() const {
  std::set<std::string> ids;
  std::set<QString> names;
  std::map<TXshLevel *, std::set<TFrameId>> sources;
  for (const auto &b : bindings) {
    if (QUuid(qs(b.id)).isNull() || !ids.insert(b.id).second || !b.level ||
        b.level->getType() != PLI_XSHLEVEL ||
        !sources[b.level.getPointer()].insert(b.fid).second ||
        b.strokes.size() != b.loops.size())
      return false;
    for (const auto &s : b.strokes) {
      if (s.empty()) return false;
      for (const auto &p : s)
        if (!p.finite()) return false;
    }
  }
  std::set<std::pair<std::string, PointId>> used;
  for (const auto &g : groups) {
    auto b = binding(g.bindingId);
    if (!b || QUuid(qs(g.id)).isNull() || !ids.insert(g.id).second ||
        qs(g.name).trimmed().isEmpty() ||
        !names.insert(qs(g.name).toCaseFolded()).second || g.points.empty() ||
        !g.pivot.finite())
      return false;
    for (auto p : g.points) {
      if (!b->contains(p) || !used.emplace(g.bindingId, p).second) return false;
      unsigned stroke = strokeIndex(p), point = pointIndex(p);
      unsigned last = unsigned(b->strokes[stroke].size() - 1);
      if (b->loops[stroke] && (point == 0 || point == last) &&
          !g.points.count(pointId(stroke, point == 0 ? last : 0)))
        return false;
    }
    int previous = -1;
    for (const auto &p : g.pairs) {
      if (p.first <= previous || p.first < 0 || p.last <= p.first ||
          p.last == (std::numeric_limits<int>::max)() ||
          !validPose(p.start, g, *b) || !validPose(p.end, g, *b))
        return false;
      previous = p.last;
      for (const auto &k : p.keys)
        if (k.first <= p.first || k.first >= p.last ||
            !validPose(k.second, g, *b))
          return false;
    }
  }
  return true;
}
size_t Data::memorySize() const {
  size_t bytes = sizeof(*this) + bindings.capacity() * sizeof(Binding) +
                 groups.capacity() * sizeof(Group);
  for (const auto &b : bindings) {
    bytes += b.id.capacity() +
             b.strokes.capacity() * sizeof(std::vector<Vec3>) +
             b.loops.capacity() / 8;
    for (const auto &s : b.strokes) bytes += s.capacity() * sizeof(Vec3);
  }
  auto poseBytes = [](const Pose &p) {
    return p.offsets.size() *
           (sizeof(std::pair<const PointId, Vec3>) + 4 * sizeof(void *));
  };
  for (const auto &g : groups) {
    bytes += g.id.capacity() + g.name.capacity() + g.bindingId.capacity() +
             g.points.size() * (sizeof(PointId) + 4 * sizeof(void *)) +
             g.pairs.capacity() * sizeof(ExtremePair);
    for (const auto &p : g.pairs) {
      bytes += poseBytes(p.start) + poseBytes(p.end) +
               p.keys.size() *
                   (sizeof(std::pair<const int, Pose>) + 4 * sizeof(void *));
      for (const auto &k : p.keys) bytes += poseBytes(k.second);
    }
  }
  return bytes;
}
TVectorImageP Data::deform(TXshLevel *level, const TFrameId &fid, double frame,
                           const TVectorImageP &source) const {
  const auto b = binding(level, fid);
  if (!b || !source) return source;
  TThread::MutexLocker lock(source->getMutex());
  if (!b->matches(source)) return source;
  TVectorImageP result;
  std::map<int, std::unique_ptr<TStroke>> old;
  for (const auto &g : groups) {
    if (g.bindingId != b->id || g.pairs.empty()) continue;
    if (!result) result = source->clone();
    Pose pose = g.evaluate(frame);
    for (auto id : g.points) {
      int s = int(strokeIndex(id)), p = int(pointIndex(id));
      auto stroke = result->getStroke(s);
      if (!old.count(s)) old[s].reset(new TStroke(*stroke));
      Vec3 v = g.position(*b, id, pose);
      stroke->setControlPoint(p, TPointD(v.x, v.y));
    }
  }
  if (!result) return source;
  std::vector<int> indices;
  std::vector<TStroke *> originals;
  for (const auto &s : old) {
    indices.push_back(s.first);
    originals.push_back(s.second.get());
  }
  result->notifyChangedStrokes(indices, originals);
  return result;
}
std::string Data::alias(TXshLevel *level, const TFrameId &fid,
                        double frame) const {
  auto b = binding(level, fid);
  if (!b) return {};
  QJsonArray data;
  data.append(qs(b->id));
  for (const auto &g : groups)
    if (g.bindingId == b->id && !g.pairs.empty())
      data.append(
          QJsonArray{qs(g.id), vec(g.pivot), poseJson(g.evaluate(frame))});
  return "cpi:" + QCryptographicHash::hash(
                      QJsonDocument(data).toJson(QJsonDocument::Compact),
                      QCryptographicHash::Sha256)
                      .toHex()
                      .toStdString();
}
void Data::saveData(TOStream &os) const {
  require(valid());
  os.child("version") << 1;
  for (const auto &b : bindings) {
    os.openChild("binding");
    os << b.id << b.level.getPointer() << b.fid.getNumber()
       << b.fid.getLetter();
    QJsonArray strokes;
    for (size_t s = 0; s < b.strokes.size(); ++s) {
      QJsonArray points;
      for (const auto &p : b.strokes[s]) points.append(vec(p));
      strokes.append(QJsonObject{{"loop", b.loops[s]}, {"points", points}});
    }
    os << QJsonDocument(strokes).toJson(QJsonDocument::Compact).toStdString();
    os.closeChild();
  }
  QJsonArray groupsJson;
  for (const auto &g : groups) groupsJson.append(groupJson(g));
  os.child("groups")
      << QJsonDocument(groupsJson).toJson(QJsonDocument::Compact).toStdString();
}
void Data::loadData(TIStream &is) {
  Data data;
  std::string tag;
  bool version = false, groupsSeen = false;
  while (is.openChild(tag)) {
    if (tag == "version") {
      int v = 0;
      is >> v;
      require(!version && v == 1);
      version = true;
    } else if (tag == "binding") {
      require(version);
      Binding b;
      TPersist *p = nullptr;
      int frame;
      QString letter;
      std::string text;
      is >> b.id >> p >> frame >> letter >> text;
      b.level  = dynamic_cast<TXshLevel *>(p);
      b.fid    = TFrameId(frame, letter);
      auto doc = readJson(text);
      require(doc.isArray());
      for (const auto &v : doc.array()) {
        require(v.isObject());
        auto o = v.toObject();
        require(o["loop"].isBool() && o["points"].isArray());
        b.loops.push_back(o["loop"].toBool());
        std::vector<Vec3> points;
        for (const auto &point : o["points"].toArray())
          points.push_back(readVec(point));
        b.strokes.push_back(std::move(points));
      }
      data.bindings.push_back(std::move(b));
    } else if (tag == "groups") {
      require(version && !groupsSeen);
      groupsSeen = true;
      std::string text;
      is >> text;
      auto doc = readJson(text);
      require(doc.isArray());
      for (const auto &g : doc.array()) data.groups.push_back(readGroup(g));
    } else
      require(false);
    is.closeChild();
  }
  require(version && groupsSeen && data.valid());
  *this = std::move(data);
}
}  // namespace Cpi
