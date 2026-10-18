#include "bulletstage.h"

#include "tdoublekeyframe.h"
#include "tstream.h"
#include "toonz/stage.h"
#include "toonz/tstageobjecttree.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace OTBullet;
namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void constant(TStageObject *o, TStageObject::Channel c, double value) {
  o->getParam(c)->setDefaultValue(value);
}
TPointD pivot(TStageObject *o, int frame) {
  return o->getPlacement(frame) * (o->getCenter(frame) * Stage::inch);
}
std::vector<Body> bodies(const std::vector<Binding> &bindings) {
  std::vector<Body> result;
  for (const auto &binding : bindings) {
    auto p = binding.source[0].world * binding.source[0].center / Stage::inch;
    Body body;
    body.pose.pivot = {p.x, p.y};
    result.push_back(body);
  }
  return result;
}
void nativeBakeAndUndo() {
  TStageObjectTree tree;
  auto *parent = tree.getStageObject(TStageObjectId::ColumnId(0));
  auto *child  = tree.getStageObject(TStageObjectId::ColumnId(1));
  child->setParent(parent->getId());
  constant(parent, TStageObject::T_X, 2);
  constant(parent, TStageObject::T_Y, -1);
  constant(parent, TStageObject::T_Angle, 30);
  constant(parent, TStageObject::T_Scale, 1.5);
  constant(child, TStageObject::T_X, 1);
  constant(child, TStageObject::T_Y, 0.5);
  constant(child, TStageObject::T_Angle, -20);
  child->setCenter(0, TPointD(0.2, -0.1));
  parent->getParam(TStageObject::T_X)->setKeyframe(TDoubleKeyframe(0, 2));
  parent->getParam(TStageObject::T_X)->setKeyframe(TDoubleKeyframe(50, 3));
  parent->getParam(TStageObject::T_X)->setKeyframe(TDoubleKeyframe(5.5, 2.5));
  std::vector<Binding> bindings;
  std::string error;
  check(capture({parent, child}, 5, 4, bindings, error), error.c_str());
  auto inputs = bodies(bindings);
  Clip clip;
  std::vector<TPointD> expectedParent, expectedChild;
  for (int f = 0; f < 4; ++f) {
    Frame frame;
    TAffine delta = TTranslation(f * 0.1 * Stage::inch, f * 0.2 * Stage::inch) *
                    TRotation(f * 25);
    for (const auto &binding : bindings) {
      auto p = delta * (binding.source[0].world * binding.source[0].center);
      frame.push_back({{p.x / Stage::inch, p.y / Stage::inch},
                       f * 25 * 3.141592653589793 / 180});
    }
    expectedParent.push_back(delta * pivot(parent, 5));
    expectedChild.push_back(delta * pivot(child, 5));
    clip.push_back(frame);
  }
  Bake bake;
  check(makeBake(bindings, inputs, clip, 5, 1, bake, error), error.c_str());
  check(bake.size() == 6, "only three channels per dynamic column");
  auto *x                   = parent->getParam(TStageObject::T_X);
  int originalCount         = x->getKeyframeCount();
  double originalFractional = x->getValue(5.5);
  for (int repeat = 0; repeat < 3; ++repeat) {
    applyBake(bake, true);
    check(!x->isKeyframe(5.5), "fractional keys in bake interval are replaced");
    for (int f : {8, 5, 7, 6, 5, 8}) {
      check(
          norm(pivot(parent, f) - expectedParent[f - 5]) < 1e-5,
          "baked parent matches desired world pivot under arbitrary scrubbing");
      check(norm(pivot(child, f) - expectedChild[f - 5]) < 1e-5,
            "baked child accounts for simulated parent and nonzero "
            "center/offset");
    }
    check(x->getKeyframeAt(0).m_value == 2 && x->getKeyframeAt(50).m_value == 3,
          "outside key values retained");
    check(child->getParam(TStageObject::T_Scale, 6) == 1,
          "scale remains authored");
    applyBake(bake, false);
    check(x->getKeyframeCount() == originalCount && x->isKeyframe(5.5) &&
              x->getValue(5.5) == originalFractional,
          "Undo restores exact fractional and outside key state");
  }
}
void rejectionAndFreeFall() {
  TStageObjectTree tree;
  auto *o = tree.getStageObject(TStageObjectId::ColumnId(0));
  std::vector<Binding> bindings;
  std::string error;
  o->lock(true);
  check(!capture({o}, 0, 3, bindings, error), "locked object rejected");
  o->lock(false);
  o->enableCycle(true);
  check(!capture({o}, 0, 3, bindings, error), "cycle rejected");
  o->enableCycle(false);
  check(!capture({o, o}, 0, 3, bindings, error), "duplicate rejected");
  check(capture({o}, 0, 25, bindings, error), error.c_str());
  auto inputs = bodies(bindings);
  Settings settings;
  settings.frames = 25;
  Clip clip;
  check(simulate(inputs, {}, settings, clip, error), error.c_str());
  Bake bake;
  check(makeBake(bindings, inputs, clip, 0, 1, bake, error), error.c_str());
  applyBake(bake, true);
  check(std::abs(pivot(o, 24).y / Stage::inch - clip.back()[0].pivot.y) < 1e-6,
        "actual Bullet output bakes into native stage channels");
  QTemporaryDir temporary;
  check(temporary.isValid(), "temporary scene stream directory");
  TFilePath path((temporary.path() + "/baked-stage.xml").toStdWString());
  {
    TOStream os(path);
    os.openChild("object");
    o->saveData(os);
    os.closeChild();
  }
  TStageObjectTree reopened;
  auto *restored = reopened.getStageObject(o->getId());
  {
    TIStream is(path);
    is.setVersion(VersionNumber(1, 30));
    std::string tag;
    check(is.matchTag(tag) && tag == "object", "ordinary stage stream root");
    restored->loadData(is);
    is.matchEndTag();
  }
  // The existing scene stream writes six significant decimal digits.
  check(norm(pivot(o, 24) - pivot(restored, 24)) < 1e-5 * Stage::inch &&
            restored->getParam(TStageObject::T_Y)->getKeyframeCount() == 25,
        "baked native keys survive stage save/reopen without physics metadata");
  applyBake(bake, false);
  inputs[0].pinned = true;
  bindings[0].source[1].world.a13 += 1;
  check(!makeBake(bindings, inputs, clip, 0, 1, bake, error) && bake.empty(),
        "moving pinned body rejected before mutation");
  inputs[0].pinned = false;
  bindings[0].source[1].world.a11 *= 2;
  check(!makeBake(bindings, inputs, clip, 0, 1, bake, error) && bake.empty(),
        "unrepresentable scale change rejected before mutation");
}
}  // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    nativeBakeAndUndo();
    rejectionAndFreeFall();
    std::cout << "Bullet native stage tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
