#include "toonz/cpi.h"
#include "toonz/txshlevelcolumn.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/toonzscene.h"
#include "toonz/txsheet.h"
#include "toonz/stageplayer.h"
#include "toonz/tcolumnfx.h"
#include "toonz/txshcell.h"
#include "tstroke.h"
#include "tpalette.h"
#include "tstream.h"
#include "tthread.h"
#include "tlevel_io.h"
#include "timage_io.h"
#include "tregion.h"
#include "tenv.h"
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <cmath>
#include <iostream>
#include <stdexcept>

extern void initImageIo(bool);

namespace {
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
void checkNear(double a, double b, const char *message) {
  check(std::abs(a - b) < 1e-7, message);
}
void vec(const Cpi::Vec3 &a, const Cpi::Vec3 &b, const char *message) {
  checkNear(a.x, b.x, message);
  checkNear(a.y, b.y, message);
  checkNear(a.z, b.z, message);
}
TVectorImageP makeImage(int strokes = 1, int points = 3) {
  TVectorImageP image = new TVectorImage;
  image->setPalette(new TPalette);
  for (int s = 0; s < strokes; ++s) {
    std::vector<TThickPoint> p;
    for (int i = 0; i < points; ++i)
      p.emplace_back(i * 3.0, s * 20.0 + ((i % 2) ? 8.0 : 0.0), 1.0);
    auto stroke = new TStroke(p);
    stroke->setStyle(1);
    image->addStroke(stroke);
  }
  return image;
}
void model() {
  Cpi::Group g;
  check(g.createPair(0, 72), "create initial pair");
  check(g.pairs.size() == 1, "atomic pair");
  check(!g.createPair(30, 72), "reject overlapping pair");
  check(!g.createPair(-1, 72), "reject negative frame");
  check(!g.createPair(100, 0), "reject zero spacing");
  check(!g.createPair(2147483640, 72), "reject frame overflow");
  auto &p = g.pairs.front();
  Cpi::Pose end;
  end.translation                 = Cpi::Vec3(72, 144, 36);
  end.offsets[Cpi::pointId(0, 1)] = Cpi::Vec3(10, 20, 30);
  p.setPose(72, end);
  vec(g.evaluate(36).translation, Cpi::Vec3(36, 72, 18),
      "midpoint translation");
  vec(g.evaluate(36).offset(Cpi::pointId(0, 1)), Cpi::Vec3(5, 10, 15),
      "sparse point midpoint");
  auto offset                        = g.evaluate(36);
  offset.translation                 = offset.translation + Cpi::Vec3(12, 0, 0);
  offset.offsets[Cpi::pointId(0, 0)] = Cpi::Vec3(0, 20, 0);
  p.setPose(36, offset);
  vec(p.keys.at(36).translation, Cpi::Vec3(12, 0, 0), "common key is delta");
  checkNear(g.evaluate(18).translation.x, 24,
            "offset ramps from neutral start");
  checkNear(g.evaluate(54).translation.x, 60, "offset returns to neutral end");
  auto newEnd          = end;
  newEnd.translation.x = 144;
  p.setPose(72, newEnd);
  checkNear(g.evaluate(36).translation.x, 84,
            "common key follows changed extremes");
  checkNear(g.evaluate(0).translation.x, 0, "start unaffected by common key");
  checkNear(g.evaluate(72).translation.x, 144, "end unaffected by common key");
  checkNear(g.evaluate(-10).translation.x, 0, "hold before pair");
  checkNear(g.evaluate(90).translation.x, 144, "hold after pair");
  check(g.createPair(100, 24), "second pair");
  checkNear(g.evaluate(112).translation.x, 144, "new pair captures held pose");
  Cpi::Rotation q = Cpi::Rotation::axisAngle(Cpi::Vec3(0, 0, 1), 90);
  vec(q.apply(Cpi::Vec3(1, 0)), Cpi::Vec3(0, 1), "z rotation");
  vec(q.inverse().apply(q.apply(Cpi::Vec3(2, 3, 4))), Cpi::Vec3(2, 3, 4),
      "quaternion inverse");
  auto half = Cpi::Rotation::interpolate(Cpi::Rotation(), q, 0.5);
  checkNear(half.apply(Cpi::Vec3(1, 0)).x, std::sqrt(0.5), "slerp midpoint");
  auto neg = q;
  neg.w    = -q.w;
  neg.x    = -q.x;
  neg.y    = -q.y;
  neg.z    = -q.z;
  vec(Cpi::Rotation::interpolate(q, neg, 0.5).apply(Cpi::Vec3(1, 0)),
      Cpi::Vec3(0, 1), "antipodal quaternions same rotation");
  Cpi::Pose tilted;
  tilted.rotation = Cpi::Rotation::axisAngle(Cpi::Vec3(0, 1, 0), 60) * q;
  Cpi::Vec3 delta;
  check(g.localDelta(tilted, TPointD(5, 7), delta), "inverse tilted plane");
  auto projected = tilted.rotation.apply(delta);
  checkNear(projected.x, 5, "tilted plane x");
  checkNear(projected.y, 7, "tilted plane y");
  tilted.rotation = Cpi::Rotation::axisAngle(Cpi::Vec3(1, 0, 0), 90);
  check(!g.localDelta(tilted, TPointD(5, 7), delta), "reject edge-on inverse");
  check(Cpi::pointId(0, 1) != Cpi::pointId(1, 0),
        "point identity distinguishes strokes");
}
void integration(const QString &directory) {
  ToonzScene scene;
  TXshSimpleLevelP level = new TXshSimpleLevel(L"CPI test");
  level->setType(PLI_XSHLEVEL);
  level->setScene(&scene);
  level->setPath(TFilePath(directory + "/source.pli"));
  auto image = makeImage(2);
  level->setPalette(image->getPalette());
  level->setFrame(TFrameId(1), image);
  Cpi::Data data;
  auto binding = Cpi::Binding::capture(level.getPointer(), TFrameId(1), image);
  std::string id;
  check(data.addGroup(
            binding, "手 / Arm",
            {Cpi::pointId(0, 0), Cpi::pointId(0, 1), Cpi::pointId(0, 2)}, id),
        "named group");
  std::string other;
  check(!data.addGroup(binding, "overlap", {Cpi::pointId(0, 1)}, other),
        "reject ambiguous membership");
  check(!data.addGroup(binding, "手 / Arm", {Cpi::pointId(1, 0)}, other),
        "reject duplicate name");
  check(!data.addGroup(binding, "invalid", {Cpi::pointId(7, 0)}, other),
        "reject missing point");
  auto g = data.group(id);
  check(g->createPair(0, 72), "paired extremes");
  auto pose                        = g->evaluate(72);
  pose.translation                 = Cpi::Vec3(100, 50, 0);
  pose.offsets[Cpi::pointId(0, 1)] = Cpi::Vec3(0, 20);
  g->pairAt(72)->setPose(72, pose);
  check(data.valid(), "valid data");
  auto halfway = data.deform(level.getPointer(), TFrameId(1), 36, image);
  check(halfway.getPointer() != image.getPointer(), "deform copies source");
  checkNear(halfway->getStroke(0)->getControlPoint(0).x, 50,
            "deformed position");
  checkNear(halfway->getStroke(0)->getControlPoint(1).y, 43,
            "deformed individual offset");
  checkNear(image->getStroke(0)->getControlPoint(0).x, 0, "source preserved");
  checkNear(halfway->getStroke(1)->getControlPoint(0).x, 0,
            "unselected stroke preserved");
  checkNear(halfway->getStroke(0)->getControlPoint(0).thick, 1,
            "thickness preserved");
  check(data.alias(level.getPointer(), TFrameId(1), 0) !=
            data.alias(level.getPointer(), TFrameId(1), 72),
        "frame-sensitive cache alias");
  auto changed = TVectorImageP(image->clone());
  changed->getStroke(0)->setControlPoint(1, TPointD(100, 100));
  check(!binding.matches(changed), "detect geometry edits");
  check(
      data.deform(level.getPointer(), TFrameId(1), 36, changed).getPointer() ==
          changed.getPointer(),
      "mismatch safely leaves source");
  auto reordered = TVectorImageP(image->clone());
  reordered->moveStrokes(0, 1, 2);
  check(!binding.matches(reordered), "detect reordered equal-count strokes");
  auto twins = makeImage(2);
  for (int p = 0; p < 3; ++p)
    twins->getStroke(1)->setControlPoint(
        p, twins->getStroke(0)->getControlPoint(p));
  twins->getStroke(1)->setStyle(2);
  auto twinBinding =
      Cpi::Binding::capture(level.getPointer(), TFrameId(5), twins);
  twins->moveStrokes(0, 1, 2);
  check(!twinBinding.matches(twins),
        "identical geometry with different styles cannot be reassigned");
  auto widths = makeImage(2);
  for (int p = 0; p < 3; ++p) {
    auto point  = widths->getStroke(0)->getControlPoint(p);
    point.thick = 10;
    widths->getStroke(1)->setControlPoint(p, point);
  }
  auto widthBinding =
      Cpi::Binding::capture(level.getPointer(), TFrameId(6), widths);
  widths->moveStrokes(0, 1, 2);
  check(!widthBinding.matches(widths),
        "identical geometry with different widths cannot be reassigned");
  auto regrouped = TVectorImageP(image->clone());
  regrouped->group(0, 2);
  check(!binding.matches(regrouped), "native regrouping requires rebinding");
  auto wrong                        = data;
  wrong.group(id)->pairs[0].keys[0] = Cpi::Pose();
  check(!wrong.valid(), "common key cannot replace endpoint");
  auto xsh = scene.getXsheet();
  xsh->setCell(0, 0, TXshCell(level.getPointer(), TFrameId(1)));
  xsh->setCell(36, 0, TXshCell(level.getPointer(), TFrameId(1)));
  auto col = xsh->getColumn(0)->getLevelColumn();
  col->setCpi(std::make_shared<Cpi::Data>(data));
  Stage::Player player;
  player.m_sl          = level.getPointer();
  player.m_fid         = TFrameId(1);
  player.m_xsh         = xsh;
  player.m_column      = 0;
  player.m_frame       = 36;
  TVectorImageP viewed = player.image();
  check(bool(viewed), "viewer image");
  checkNear(viewed->getStroke(0)->getControlPoint(0).x, 50,
            "viewer evaluates CPI");
  TRectD renderBounds;
  check(col->getLevelColumnFx()->getBBox(36, renderBounds, TRenderSettings()),
        "render bounds available");
  check(renderBounds.x1 > image->getBBox().x1 + 40,
        "render bounds include displaced CPs");
  auto renderAlias = col->getLevelColumnFx()->getAlias(36, TRenderSettings());
  check(renderAlias.find("cpi:") != std::string::npos,
        "render cache includes CPI data");
  auto preview     = std::make_shared<Cpi::Preview>();
  preview->base    = col->getCpi();
  preview->groupId = id;
  preview->frame   = 36;
  preview->pose    = preview->base->group(id)->evaluate(36);
  preview->pose.translation.x += 100;
  col->setCpiPreview(preview);
  TVectorImageP live = player.image();
  checkNear(live->getStroke(0)->getControlPoint(0).x, 150,
            "viewer consumes transient pose preview");
  check(col->getLevelColumnFx()->getAlias(36, TRenderSettings()) != renderAlias,
        "render alias includes preview pose");
  check(col->getLevelColumnFx()->getBBox(36, renderBounds, TRenderSettings()) &&
            renderBounds.x1 > live->getStroke(0)->getControlPoint(0).x,
        "render bounds consume preview pose");
  checkNear(col->getCpi()->group(id)->evaluate(36).translation.x, 50,
            "preview does not change saved channels");
  checkNear(TVectorImageP(col->applyCpi(image, col->getCell(0), 0))
                ->getStroke(0)
                ->getControlPoint(0)
                .x,
            0, "preview is limited to its scene frame");
  TXshColumnP previewClone = col->clone();
  check(!previewClone->getLevelColumn()->getCpiPreview(),
        "column copies omit transient preview");
  col->setCpiPreview({});
  check(col->getLevelColumnFx()->getAlias(36, TRenderSettings()) == renderAlias,
        "cancelling preview restores render cache identity");
  auto fractional = makeImage(1, 501);
  for (int p = 0; p < 501; ++p)
    fractional->getStroke(0)->setControlPoint(
        p,
        TPointD(0.1234567 + p * 3.01234567, 0.9876543 + (p % 2) * 8.07654321));
  auto fractionalBinding =
      Cpi::Binding::capture(level.getPointer(), TFrameId(3), fractional);
  TFilePath pliPath(directory + "/roundtrip.pli");
  {
    TLevelWriterP writer(pliPath);
    writer->setPalette(fractional->getPalette());
    writer->getFrameWriter(TFrameId(1))->save(fractional);
  }
  {
    TLevelReaderP reader(pliPath);
    reader->loadInfo();
    TVectorImageP reopened = reader->getFrameReader(TFrameId(1))->load();
    check(fractionalBinding.matches(reopened),
          "real PLI quantization keeps binding");
  }
  auto closed                   = makeImage();
  std::vector<TThickPoint> loop = {{0, 0, 1},   {10, 0, 1},  {20, 0, 1},
                                   {20, 10, 1}, {20, 20, 1}, {10, 20, 1},
                                   {0, 20, 1},  {0, 10, 1},  {0, 0, 1}};
  closed->deleteStroke(0);
  auto outline = new TStroke(loop);
  outline->setSelfLoop(true);
  outline->setStyle(1);
  closed->addStroke(outline);
  closed->findRegions();
  check(closed->getRegionCount() == 1, "closed region fixture");
  closed->getRegion(0)->setStyle(1);
  auto closedBinding =
      Cpi::Binding::capture(level.getPointer(), TFrameId(4), closed);
  Cpi::Data closedData;
  std::string closedId;
  check(closedData.addGroup(closedBinding, "loop", {0, 1, 2, 3, 4, 5, 6, 7},
                            closedId),
        "closed group");
  check(closedData.group(closedId)->points.count(8),
        "closed endpoint alias included");
  auto closedGroup = closedData.group(closedId);
  closedGroup->createPair(0, 72);
  Cpi::Pose shifted;
  shifted.translation = Cpi::Vec3(30, 40);
  closedGroup->pairAt(72)->setPose(72, shifted);
  auto closedResult =
      closedData.deform(level.getPointer(), TFrameId(4), 72, closed);
  check(closedResult->getRegionCount() == 1 &&
            closedResult->getRegion(0)->getStyle() == 1,
        "fill preserved after deformation");
  TXshColumnP clone = col->clone();
  check(clone->getLevelColumn()->getCpi()->group(id) != nullptr,
        "column clone retains CPI");
  auto edited = std::make_shared<Cpi::Data>(*clone->getLevelColumn()->getCpi());
  edited->group(id)->name = "clone";
  clone->getLevelColumn()->setCpi(edited);
  check(col->getCpi()->group(id)->name == "手 / Arm",
        "clone edits independent");
  col->setCpiPreview(preview);
  TFilePath path(directory + "/cpi-column.xml");
  {
    TOStream os(path);
    os << static_cast<TPersist *>(col);
  }
  TXshColumnP loaded;
  {
    TIStream is(path);
    TPersist *p = nullptr;
    is >> p;
    loaded = dynamic_cast<TXshColumn *>(p);
  }
  check(bool(loaded), "column reload");
  check(!loaded->getLevelColumn()->getCpiPreview(),
        "scene serialization omits transient preview");
  col->setCpiPreview({});
  auto saved = loaded->getLevelColumn()->getCpi();
  check(saved && saved->valid(), "saved data valid");
  auto savedGroup = saved->group(id);
  check(savedGroup && savedGroup->name == "手 / Arm",
        "identity and Unicode name persist");
  vec(savedGroup->evaluate(36).translation, Cpi::Vec3(50, 25),
      "saved channels evaluate");
  check(saved->binding(savedGroup->bindingId)->matches(image),
        "binding persists");
  TFilePath invalidPath(directory + "/invalid-cpi.xml");
  {
    TOStream os(invalidPath);
    os.child("version") << 2;
  }
  auto retained = data;
  bool rejected = false;
  try {
    TIStream is(invalidPath);
    retained.loadData(is);
  } catch (const TException &) {
    rejected = true;
  }
  check(rejected && retained.group(id),
        "unsupported version leaves prior data intact");
  rejected              = false;
  auto originalSnapshot = col->getCpi();
  try {
    col->setCpi(std::make_shared<Cpi::Data>(wrong));
  } catch (const TException &) {
    rejected = true;
  }
  check(rejected && col->getCpi() == originalSnapshot,
        "invalid snapshot cannot replace column channels");
  // More than ten thousand CPs without a fixed channel-count limit.
  auto large = makeImage(100, 101);
  auto bigBinding =
      Cpi::Binding::capture(level.getPointer(), TFrameId(2), large);
  Cpi::Data big;
  std::set<Cpi::PointId> points;
  for (unsigned s = 0; s < 100; ++s)
    for (unsigned p = 0; p < 101; ++p) points.insert(Cpi::pointId(s, p));
  check(big.addGroup(bigBinding, "10000+ points", points, other),
        "large group");
  check(big.group(other)->createPair(0, 72), "large pair");
  auto largeEnd        = big.group(other)->evaluate(72);
  largeEnd.translation = Cpi::Vec3(12, 24, 36);
  big.group(other)->pairAt(72)->setPose(72, largeEnd);
  QElapsedTimer timer;
  timer.start();
  auto result = big.deform(level.getPointer(), TFrameId(2), 36, large);
  checkNear(result->getStroke(99)->getControlPoint(100).x, 306,
            "large group last point");
  std::cout << "10,100-point deformation: " << timer.elapsed() << " ms\n";
}
}  // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  TThread::init();
  QTemporaryDir temporary;
  TEnv::setStuffDir(TFilePath(temporary.path()));
  TEnv::setApplicationFileName("OpenToonz");
  initImageIo(false);
  try {
    model();
    integration(temporary.path());
    std::cout
        << "PASS: paired extremes, sparse offsets, quaternion planes, "
           "bindings, copy-on-write, viewer, serialization and large groups\n";
    return 0;
  } catch (const TException &e) {
    std::cerr << QString::fromStdWString(e.getMessage()).toStdString() << "\n";
    return 1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
