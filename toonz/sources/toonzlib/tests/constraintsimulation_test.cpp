#include "toonz/constraintsimulation.h"

#include "tdoublekeyframe.h"
#include "tenv.h"
#include "tstream.h"
#include "toonz/stage.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/toonzfolders.h"
#include "toonz/txsheet.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ConstraintSimulation;
namespace {
using Constraint = TStageObject::Constraint;
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
void constant(TStageObject *o, TStageObject::Channel c, double value) {
  o->getParam(c)->setDefaultValue(value);
}
void key(TStageObject *o, TStageObject::Channel c, double frame, double value) {
  TDoubleKeyframe k(frame, value);
  k.m_type = k.m_prevType = TDoubleKeyframe::Linear;
  o->getParam(c)->setKeyframe(k);
}
TPointD origin(TStageObject *o, int frame) {
  return o->getPlacement(frame) * TPointD() / Stage::inch;
}
bool sameBasis(const TAffine &a, const TAffine &b) {
  return std::abs(a.a11 - b.a11) + std::abs(a.a12 - b.a12) +
             std::abs(a.a21 - b.a21) + std::abs(a.a22 - b.a22) <
         1e-5;
}
void mobile(TStageObject *o) {
  Constraint link;
  link.type = Constraint::Mobile;
  check(o->addConstraint(link), "targetless Mobile assignment");
}
void assignmentsAndPersistence() {
  TXsheet xsheet;
  auto *tree   = xsheet.getStageObjectTree();
  auto *o      = tree->getStageObject(TStageObjectId::ColumnId(0));
  auto *target = tree->getStageObject(TStageObjectId::ColumnId(1));
  check(o->getSimulationMotion() == Constraint::Fixed,
        "unassigned defaults to Fixed");
  Constraint aim;
  aim.target = target->getId();
  check(o->addConstraint(aim), "Aim At assignment");
  mobile(o);
  check(o->getConstraints().size() == 2 &&
            o->getSimulationMotion() == Constraint::Mobile,
        "motion assignment coexists with Aim At");
  auto before = o->getConstraints();
  Constraint fixed;
  fixed.type = Constraint::Fixed;
  check(o->addConstraint(fixed) && o->getConstraints().size() == 2 &&
            o->getSimulationMotion() == Constraint::Fixed,
        "Fixed replaces Mobile, retaining Aim At");
  o->setConstraints(before);
  check(o->getSimulationMotion() == Constraint::Mobile,
        "assignment snapshot Undo");
  auto *cloned = o->clone();
  check(cloned->getSimulationMotion() == Constraint::Mobile,
        "cloned objects retain assignment");
  check(!tree->getStageObject(TStageObjectId::TableId)
             ->addConstraint(before.back()),
        "table cannot be Mobile");

  QTemporaryDir temporary;
  check(temporary.isValid(), "temporary stage stream directory");
  TFilePath path((temporary.path() + "/constraints.xml").toStdWString());
  {
    TOStream os(path);
    os.openChild("object");
    o->saveData(os);
    os.closeChild();
  }
  TXsheet reopened;
  auto *restored = reopened.getStageObjectTree()->getStageObject(o->getId());
  for (int repeat = 0; repeat < 2; ++repeat) {
    TIStream is(path);
    is.setVersion(VersionNumber(1, 30));
    std::string tag;
    check(is.matchTag(tag) && tag == "object", "stage stream root");
    restored->loadData(is);
    is.matchEndTag();
    check(restored->getSimulationMotion() == Constraint::Mobile &&
              restored->getConstraints().size() == 2 &&
              restored->getConstraints().front().type == Constraint::AimAt,
          "assignment and Aim At survive repeated save/reopen without "
          "duplication");
  }
  o->clearConstraints();
  check(o->getSimulationMotion() == Constraint::Fixed,
        "clear returns to automatic Fixed");
}
void gravityTimeAndUndo() {
  TXsheet xsheet;
  auto *tree  = xsheet.getStageObjectTree();
  auto *o     = tree->getStageObject(TStageObjectId::ColumnId(0));
  auto *fixed = tree->getStageObject(TStageObjectId::ColumnId(1));
  constant(o, TStageObject::T_Y, 6);
  constant(o, TStageObject::T_Angle, 25);
  o->setCenter(0, TPointD(0.25, -0.5));
  key(o, TStageObject::T_X, 0, 3);
  key(o, TStageObject::T_X, 50, 4);
  key(o, TStageObject::T_X, 10.5, 3.5);
  key(fixed, TStageObject::T_X, 0, -1);
  key(fixed, TStageObject::T_X, 50, 1);
  mobile(o);
  TPointD start     = origin(o, 5);
  TAffine original  = o->getPlacement(5);
  auto *x           = o->getParam(TStageObject::T_X);
  int originalKeys  = x->getKeyframeCount();
  double fractional = x->getValue(10.5);
  Settings s;
  s.firstFrame = 5;
  s.lastFrame  = 29;
  Bake bake;
  std::string error;
  check(prepare(xsheet, s, bake, error), error.c_str());
  check(bake.mobileObjects == 1 && bake.compensatedObjects == 0,
        "only explicitly Mobile object participates");
  check(
      x->getKeyframeCount() == originalKeys && x->getValue(10.5) == fractional,
      "simulation preflight leaves the scene unchanged");
  for (int repeat = 0; repeat < 3; ++repeat) {
    apply(bake, true);
    check(norm(origin(o, 5) - start) < 1e-7,
          "first sample is authored starting pose");
    for (int f : {29, 5, 17, 10, 29}) {
      double t = (f - 5) / 24.0;
      check(norm(origin(o, f) -
                 (start + TPointD(0, -0.5 * s.gravity * t * t))) < 1e-6,
            "gravity output matches analytic free fall under arbitrary "
            "scrubbing");
      check(sameBasis(o->getPlacement(f), original),
            "authored orientation and scale retained");
    }
    check(!x->isKeyframe(10.5) && x->getKeyframeAt(0).m_value == 3 &&
              x->getKeyframeAt(50).m_value == 4,
          "range keys replaced; outside key data retained");
    check(fixed->getParam(TStageObject::T_X)->getKeyframeCount() == 2,
          "unrelated unassigned object's authored keys remain unchanged");
    apply(bake, false);
    check(x->getKeyframeCount() == originalKeys && x->isKeyframe(10.5) &&
              x->getValue(10.5) == fractional,
          "Undo restores complete fractional key state");
  }
  s.fps       = 48;
  s.lastFrame = 53;
  check(prepare(xsheet, s, bake, error), error.c_str());
  apply(bake, true);
  check(norm(origin(o, 53) - (start + TPointD(0, -s.gravity / 2))) < 1e-6,
        "scene frame rate preserves elapsed simulation time");
  QTemporaryDir temporary;
  TFilePath path((temporary.path() + "/baked.xml").toStdWString());
  {
    TOStream os(path);
    os.openChild("object");
    o->saveData(os);
    os.closeChild();
  }
  TXsheet reopened;
  auto *restored = reopened.getStageObjectTree()->getStageObject(o->getId());
  {
    TIStream is(path);
    is.setVersion(VersionNumber(1, 30));
    std::string tag;
    is.matchTag(tag);
    restored->loadData(is);
    is.matchEndTag();
  }
  check(norm(origin(o, 53) - origin(restored, 53)) < 1e-4 &&
            restored->getSimulationMotion() == Constraint::Mobile,
        "baked channels and assignment survive ordinary stage serialization");
}
void fixedChildrenAndAim() {
  TXsheet xsheet;
  auto *tree    = xsheet.getStageObjectTree();
  auto *root    = tree->getStageObject(TStageObjectId::PegbarId(1));
  auto *fixed   = tree->getStageObject(TStageObjectId::ColumnId(0));
  auto *falling = tree->getStageObject(TStageObjectId::ColumnId(1));
  fixed->setParent(root->getId());
  falling->setParent(root->getId());
  constant(root, TStageObject::T_X, 2);
  constant(root, TStageObject::T_Angle, 35);
  constant(root, TStageObject::T_Scale, 1.5);
  constant(fixed, TStageObject::T_X, 1);
  constant(falling, TStageObject::T_X, 3);
  mobile(root);
  mobile(falling);
  TPointD fixedStart = origin(fixed, 0), fallingStart = origin(falling, 0);
  TAffine fixedBasis = fixed->getPlacement(0);
  Settings s;
  s.lastFrame = 24;
  Bake bake;
  std::string error;
  check(prepare(xsheet, s, bake, error), error.c_str());
  check(bake.mobileObjects == 2 && bake.compensatedObjects == 1,
        "fixed child is automatically compensated beneath Mobile parent");
  apply(bake, true);
  check(norm(origin(fixed, 24) - fixedStart) < 1e-6 &&
            sameBasis(fixed->getPlacement(24), fixedBasis),
        "unassigned fixed child keeps authored world position and orientation");
  check(norm(origin(falling, 24) -
             (fallingStart + TPointD(0, -s.gravity / 2))) < 1e-6,
        "nested Mobile object receives gravity once, independently of parent");
  apply(bake, false);
  fixed->lock(true);
  check(!prepare(xsheet, s, bake, error) && bake.channels.empty(),
        "locked compensation target rejects the complete bake atomically");
  fixed->lock(false);

  TXsheet aiming;
  tree          = aiming.getStageObjectTree();
  auto *tracker = tree->getStageObject(TStageObjectId::PegbarId(1));
  auto *target  = tree->getStageObject(TStageObjectId::ColumnId(0));
  auto *child   = tree->getStageObject(TStageObjectId::ColumnId(1));
  child->setParent(tracker->getId());
  constant(tracker, TStageObject::T_Angle, 17);
  constant(child, TStageObject::T_X, 1);
  constant(target, TStageObject::T_X, 4);
  constant(target, TStageObject::T_Y, 4);
  mobile(target);
  Constraint aim;
  aim.target      = target->getId();
  aim.influence   = 0.4;
  aim.angleOffset = 20;
  check(tracker->addConstraint(aim), "partial-influence Aim At");
  TPointD childStart = origin(child, 0);
  TAffine childBasis = child->getPlacement(0);
  check(prepare(aiming, s, bake, error), error.c_str());
  apply(bake, true);
  TPointD targetPosition = origin(target, 24);
  double desired =
      std::atan2(targetPosition.y, targetPosition.x) * 180 / 3.141592653589793 +
      20;
  double expected = 17 + std::remainder(desired - 17, 360.0) * 0.4;
  TAffine actual  = tracker->getPlacement(24);
  check(std::abs(std::atan2(actual.a21, actual.a11) * 180 / 3.141592653589793 -
                 expected) < 1e-5,
        "Aim At uses the simulated target position without applying influence "
        "twice");
  check(tracker->getParam(TStageObject::T_Angle)->getKeyframeCount() == 0,
        "Aim At remains live and its authored base angle stays editable");
  check(norm(origin(child, 24) - childStart) < 1e-6 &&
            sameBasis(child->getPlacement(24), childBasis),
        "fixed descendant is compensated when Aim At parent follows a Mobile "
        "target");
  check(tracker->getConstraints().front().enabled,
        "Aim At assignment remains enabled");
}
void rejection() {
  TXsheet xsheet;
  auto *o =
      xsheet.getStageObjectTree()->getStageObject(TStageObjectId::ColumnId(0));
  Settings s;
  s.lastFrame = 2;
  Bake bake;
  std::string error;
  auto reject = [&] {
    bake.mobileObjects = 99;
    check(!prepare(xsheet, s, bake, error) && bake.channels.empty() &&
              bake.mobileObjects == 0 && !error.empty(),
          "invalid simulation fails before mutation");
  };
  reject();
  mobile(o);
  o->lock(true);
  reject();
  o->lock(false);
  s.gravity = std::numeric_limits<double>::quiet_NaN();
  reject();
  s.gravity   = 9.81;
  s.lastFrame = 1200;
  reject();
  s.lastFrame = 2;
  constant(o, TStageObject::T_Scale, 0);
  reject();
  constant(o, TStageObject::T_Scale, 1);
  o->enableCycle(true);
  reject();
  o->enableCycle(false);
  auto *other =
      xsheet.getStageObjectTree()->getStageObject(TStageObjectId::ColumnId(1));
  Constraint buffer;
  buffer.type   = Constraint::Buffer;
  buffer.target = other->getId();
  check(o->addConstraint(buffer),
        "legacy Buffer remains loadable and removable");
  reject();
  o->removeConstraint(1);
  Constraint a, b;
  a.target = other->getId();
  b.target = o->getId();
  check(o->addConstraint(a), "acyclic Aim At accepted");
  check(!other->addConstraint(b),
        "UI cycle check ignores targetless motion assignments");
  other->setConstraints({b});
  reject();
}
}  // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    // TXsheet initializes Preferences. Give it an isolated minimal stuff tree,
    // rather than depending on installed rooms or modifying user settings.
    QTemporaryDir environment;
    check(environment.isValid(), "temporary preferences directory");
    TEnv::setStuffDir(TFilePath(environment.path().toStdWString()));
    TEnv::setArgPathValue("TOONZPROFILES",
                          (environment.path() + "/profiles").toStdString());
    TEnv::setArgPathValue("TOONZCONFIG",
                          (environment.path() + "/config").toStdString());
    QDir().mkpath(ToonzFolder::getRoomsDir().getQString() + "/Default");
    QDir().mkpath(ToonzFolder::getMyModuleDir().getQString());
    assignmentsAndPersistence();
    gravityTimeAndUndo();
    fixedChildrenAndAim();
    rejection();
    std::cout << "Constraint gravity tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
