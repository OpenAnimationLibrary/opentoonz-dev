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
bool sameBasis(const TAffine &a, const TAffine &b) {
  return std::abs(a.a11 - b.a11) + std::abs(a.a12 - b.a12) +
             std::abs(a.a21 - b.a21) + std::abs(a.a22 - b.a22) <
         1e-5;
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
  check(capture(tree, {parent, child}, 5, 4, bindings, error), error.c_str());
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
      check(sameBasis(child->getPlacement(f),
                      TRotation((f - 5) * 25) * bindings[1].source[0].world),
            "baked drawing orientation and scale match simulated world motion");
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
void skippedAncestorAndOwnership() {
  TStageObjectTree tree;
  auto *root   = tree.getStageObject(TStageObjectId::ColumnId(0));
  auto *middle = tree.getStageObject(TStageObjectId::ColumnId(2));
  auto *child  = tree.getStageObject(TStageObjectId::ColumnId(1));
  middle->setParent(root->getId());
  child->setParent(middle->getId());
  constant(root, TStageObject::T_X, 2);
  constant(root, TStageObject::T_Angle, 25);
  constant(middle, TStageObject::T_X, 1);
  constant(middle, TStageObject::T_Angle, -15);
  constant(child, TStageObject::T_Y, 0.75);
  std::vector<Binding> bindings;
  std::string error;
  // Selection order is independent of the parenting order.
  check(capture(tree, {child, root}, 0, 4, bindings, error), error.c_str());
  auto inputs = bodies(bindings);
  Clip clip;
  std::vector<TPointD> expected;
  for (int f = 0; f < 4; ++f) {
    TAffine delta = TTranslation(f * 0.2 * Stage::inch, 0) * TRotation(f * 20);
    Frame frame;
    for (const auto &b : bindings) {
      auto p = delta * b.source.front().world * b.source.front().center;
      frame.push_back({{p.x / Stage::inch, p.y / Stage::inch},
                       f * 20 * 3.141592653589793 / 180});
    }
    expected.push_back(delta * pivot(child, 0));
    clip.push_back(frame);
  }
  Bake bake;
  check(makeBake(bindings, inputs, clip, 0, 1, bake, error), error.c_str());
  applyBake(bake, true);
  for (int f : {3, 0, 2, 1})
    check(
        norm(pivot(child, f) - expected[f]) < 1e-5,
        "baking accounts for a selected ancestor through an unselected column");
  check(middle->getParam(TStageObject::T_X)->getKeyframeCount() == 0,
        "unselected intermediate channels remain authored");
  applyBake(bake, false);
  auto fast        = clip;
  fast[1][0].angle = 4;
  check(!makeBake(bindings, inputs, fast, 0, 1, bake, error) && bake.empty(),
        "ambiguous fast relative turns fail instead of losing complete turns");
  Settings s;
  s.frames         = 4;
  inputs[0].motion = Body::Kinematic;
  check(!prepareControllers(bindings, s, 0, 1, inputs, error) &&
            inputs[0].animation.empty(),
        "controller below dynamic ancestor is rejected without mutation");
  check(!makeBake(bindings, inputs, clip, 0, 1, bake, error) && bake.empty(),
        "bake also rejects conflicting controller ownership");
  inputs[0].motion = Body::Pinned;
  check(!prepareControllers(bindings, s, 0, 1, inputs, error),
        "pinned descendant cannot inherit a baked dynamic ancestor");
}
void authoredControllers() {
  TStageObjectTree tree;
  auto *root  = tree.getStageObject(TStageObjectId::ColumnId(0));
  auto *child = tree.getStageObject(TStageObjectId::ColumnId(1));
  child->setParent(root->getId());
  constant(child, TStageObject::T_X, 1);
  auto key = [](TStageObject *o, TStageObject::Channel channel, double frame,
                double value, TDoubleKeyframe::Type type) {
    TDoubleKeyframe k(frame, value);
    k.m_type = k.m_prevType = type;
    o->getParam(channel)->setKeyframe(k);
  };
  key(root, TStageObject::T_X, 0, 2, TDoubleKeyframe::Constant);
  key(root, TStageObject::T_X, 6, 4, TDoubleKeyframe::Constant);
  key(root, TStageObject::T_X, 12, 4, TDoubleKeyframe::Constant);
  key(root, TStageObject::T_Angle, 0, 10, TDoubleKeyframe::Linear);
  key(root, TStageObject::T_Angle, 12, 130, TDoubleKeyframe::Linear);
  Settings s;
  s.frames   = 13;
  s.substeps = 8;
  s.gravity  = 0;
  std::vector<Binding> bindings;
  std::string error;
  check(capture(tree, {root, child}, 0, s.frames, bindings, error),
        error.c_str());
  auto inputs      = bodies(bindings);
  inputs[0].motion = Body::Kinematic;
  check(prepareControllers(bindings, s, 0, 1, inputs, error), error.c_str());
  check(inputs[0].animation.size() == size_t(1 + 12 * s.substeps),
        "controller samples every exact physics substep");
  for (size_t step = 0; step < inputs[0].animation.size(); ++step) {
    double time = double(step) / s.substeps;
    auto expected =
        root->getPlacement(time) * (root->getCenter(time) * Stage::inch);
    auto p = inputs[0].animation[step].pivot;
    check(norm(TPointD(p.x, p.y) * Stage::inch - expected) < 1e-7,
          "substep poses use authored interpolation rather than endpoint lerp");
  }
  check(inputs[0].animation[6 * s.substeps - 1].pivot.x == 2 &&
            inputs[0].animation[6 * s.substeps].pivot.x == 4,
        "constant interpolation holds until the keyed frame");
  Connection link;
  link.body   = 1;
  link.target = 0;
  link.anchor = inputs[1].pose.pivot;
  link.type   = Connection::Fixed;
  Clip clip;
  check(simulate(inputs, {link}, s, clip, error), error.c_str());
  Bake bake;
  check(makeBake(bindings, inputs, clip, 0, 1, bake, error), error.c_str());
  check(bake.size() == 3, "only the dynamic follower receives baked channels");
  applyBake(bake, true);
  check(root->getParam(TStageObject::T_X)->getKeyframeCount() == 3 &&
            root->getParam(TStageObject::T_X, 5.75) == 2 &&
            root->getParam(TStageObject::T_Angle, 12) == 130,
        "authored controller keys and interpolation survive baking");
  for (int f : {12, 6, 1, 11, 0}) {
    check(norm(pivot(child, f) / Stage::inch -
               TPointD(clip[f][1].pivot.x, clip[f][1].pivot.y)) < 1e-5,
          "baked follower matches physics under an animated retained parent");
    check(sameBasis(child->getPlacement(f),
                    TRotation(clip[f][1].angle * 180 / 3.141592653589793) *
                        bindings[1].source[0].world),
          "follower drawing orientation matches physics under its controller");
  }
  applyBake(bake, false);

  inputs[0].motion = Body::Pinned;
  check(!prepareControllers(bindings, s, 0, 1, inputs, error),
        "animated pinned source requires kinematic mode");
  inputs[0].motion = Body::Kinematic;
  key(root, TStageObject::T_Scale, 0, 1, TDoubleKeyframe::Linear);
  key(root, TStageObject::T_Scale, 12, 2, TDoubleKeyframe::Linear);
  check(!prepareControllers(bindings, s, 0, 1, inputs, error),
        "controller scale changes cannot drive a rigid proxy");
  root->getParam(TStageObject::T_Scale)->deleteKeyframe(0);
  root->getParam(TStageObject::T_Scale)->deleteKeyframe(12);
  key(root, TStageObject::T_Angle, 12, 2170, TDoubleKeyframe::Linear);
  s.substeps = 1;
  check(!prepareControllers(bindings, s, 0, 1, inputs, error),
        "undersampled complete rotations fail instead of aliasing");
}
void rejectionAndFreeFall() {
  TStageObjectTree tree;
  auto *o = tree.getStageObject(TStageObjectId::ColumnId(0));
  std::vector<Binding> bindings;
  std::string error;
  o->lock(true);
  check(!capture(tree, {o}, 0, 3, bindings, error), "locked object rejected");
  o->lock(false);
  o->enableCycle(true);
  check(!capture(tree, {o}, 0, 3, bindings, error), "cycle rejected");
  o->enableCycle(false);
  check(!capture(tree, {o, o}, 0, 3, bindings, error), "duplicate rejected");
  check(capture(tree, {o}, 0, 25, bindings, error), error.c_str());
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
  inputs[0].motion = Body::Pinned;
  bindings[0].source[1].world.a13 += 1;
  check(!makeBake(bindings, inputs, clip, 0, 1, bake, error) && bake.empty(),
        "moving pinned body rejected before mutation");
  inputs[0].motion = Body::Dynamic;
  bindings[0].source[1].world.a11 *= 2;
  check(!makeBake(bindings, inputs, clip, 0, 1, bake, error) && bake.empty(),
        "unrepresentable scale change rejected before mutation");
}
}  // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    nativeBakeAndUndo();
    skippedAncestorAndOwnership();
    authoredControllers();
    rejectionAndFreeFall();
    std::cout << "Bullet native stage tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
