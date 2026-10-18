#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/trigserialization.h"
#include "toonz/stage.h"
#include "tstream.h"
#include "tfilepath.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void point(const TPointD &a, const TPointD &b, const char *message) {
  check(std::isfinite(a.x) && std::isfinite(a.y) && norm(a - b) < 1e-6,
        message);
}
TPointD pivot(TStageObject *object, double frame) {
  return object->getPlacement(frame) * (object->getCenter(frame) * Stage::inch);
}
TRigBone follower(TStageObject *target) {
  TRigBone bone;
  bone.enabled = true;
  TRigConstraint constraint;
  constraint.type     = TRigConstraint::TranslateTo;
  constraint.targetId = target->getRigId();
  bone.constraints.push_back(constraint);
  return bone;
}
void constant(TStageObject *object, TStageObject::Channel channel,
              double value) {
  object->getParam(channel)->setDefaultValue(value);
}
void writeObjects(const QString &path, TStageObjectTree &tree, int count) {
  TOStream os(TFilePath(path.toStdWString()));
  os.openChild("objects");
  for (int i = 0; i < count; ++i) {
    os.openChild("object");
    tree.getStageObject(TStageObjectId::ColumnId(i))->saveData(os);
    os.closeChild();
  }
  os.closeChild();
}
void readObjects(const QString &path, TStageObjectTree &tree, int count) {
  TIStream is(TFilePath(path.toStdWString()));
  is.setVersion(VersionNumber(1, 30));
  std::string tag;
  check(is.matchTag(tag) && tag == "objects", "fixture root");
  for (int i = 0; i < count; ++i) {
    check(is.matchTag(tag) && tag == "object", "fixture object");
    tree.getStageObject(TStageObjectId::ColumnId(i))->loadData(is);
    is.matchEndTag();
  }
  is.matchEndTag();
}

void legacyAndLiveCache() {
  TStageObjectTree tree;
  auto *parent = tree.getStageObject(TStageObjectId::ColumnId(0));
  auto *child  = tree.getStageObject(TStageObjectId::ColumnId(1));
  child->setParent(parent->getId());
  constant(parent, TStageObject::T_X, 2);
  constant(parent, TStageObject::T_Angle, 90);
  constant(child, TStageObject::T_X, 1);
  point(pivot(child, 0), TPointD(2, 1) * Stage::inch,
        "legacy parenting unchanged");

  auto *controller = tree.getStageObject(TStageObjectId::ColumnId(2));
  constant(controller, TStageObject::T_X, 7);
  TRigBone bone              = follower(controller);
  bone.constraints[0].offset = TPointD(0.25, -0.5);
  check(child->setRigBone(bone), "assign independent controller");
  point(pivot(child, 0), TPointD(7.25, -0.5) * Stage::inch,
        "world-space constraint after parenting");
  constant(controller, TStageObject::T_X, 9);
  point(pivot(child, 0), TPointD(9.25, -0.5) * Stage::inch,
        "same-frame target change invalidates cached follower");
  child->getParam(TStageObject::T_X)->setValue(0, 1);
  child->getParam(TStageObject::T_X)->setValue(10, 123);
  for (int frame : {10, 0, 5, 10, 0})
    point(pivot(child, frame), TPointD(9.25, -0.5) * Stage::inch,
          "native scrub order independent");
  check(child->getParam(TStageObject::T_X)->getValue(10) == 123,
        "authored channel survives constraints");

  TRigBone none;
  check(child->setRigBone(none), "remove final constraint");
  point(pivot(child, 0), TPointD(2, 1) * Stage::inch,
        "removal restores authored transform");
}

void cyclesAndIdentity() {
  TStageObjectTree tree;
  TStageObjectP target = tree.getStageObject(TStageObjectId::ColumnId(0));
  TStageObjectP source = tree.getStageObject(TStageObjectId::ColumnId(1));
  constant(target.getPointer(), TStageObject::T_X, 6);
  TRigBone bone = follower(target.getPointer());
  check(source->setRigBone(bone), "assign target");
  std::string error;
  check(!target->setRigBone(follower(source.getPointer()), &error) &&
            !error.empty(),
        "mutual cycle rejected");
  target->setParent(source->getId());
  check(target->getParent() == TStageObjectId::TableId,
        "reparenting cannot introduce cycle");
  std::string identity = target->getRigId();
  tree.insertColumn(0);
  tree.swapColumns(1, 2);
  check(tree.getRigObject(identity) == target.getPointer(),
        "insert and reorder preserve target identity");
  point(pivot(source.getPointer(), 0), TPointD(6, 0) * Stage::inch,
        "reordered target still drives source");

  std::unique_ptr<TStageObjectParams> snapshot(target->getParams());
  int oldColumn = target->getId().getIndex();
  tree.removeColumn(oldColumn);
  check(!tree.getRigObject(identity), "deleted target unresolved");
  auto *replacement = tree.getStageObject(TStageObjectId::ColumnId(3));
  constant(replacement, TStageObject::T_X, 100);
  check(!source->rigConstraintProblem(source->getRigBone().constraints[0])
             .empty(),
        "unresolved link reported");
  point(pivot(source.getPointer(), 0), TPointD(),
        "deleted target never redirects to another column");
  replacement->assignParams(snapshot.get());
  check(tree.getRigObject(identity) == replacement,
        "deleted object identity can be restored for undo");
  point(pivot(source.getPointer(), 0), TPointD(6, 0) * Stage::inch,
        "restored target reactivates follower");

  auto *clone = replacement->clone();
  check(clone->getRigId() != identity, "clone gets fresh identity");
  constant(clone, TStageObject::T_X, 55);
  point(pivot(source.getPointer(), 0), TPointD(6, 0) * Stage::inch,
        "clone does not steal target links");

  // A malformed loaded cycle is preserved for repair and ignored consistently.
  replacement->assignRigBone(follower(source.getPointer()));
  check(!source->rigConstraintProblem(source->getRigBone().constraints[0])
             .empty(),
        "loaded cycle reported");
  check(rigFinite(source->getPlacement(3)), "loaded cycle does not recurse");
  replacement->assignRigBone(TRigBone());
  replacement->setStatus(TStageObject::IK);
  check(!source->rigConstraintProblem(source->getRigBone().constraints[0])
             .empty(),
        "legacy IK conflict explicit");
}

void persistence(const QString &directory) {
  TStageObjectTree tree;
  auto *target = tree.getStageObject(TStageObjectId::ColumnId(0));
  auto *source = tree.getStageObject(TStageObjectId::ColumnId(1));
  constant(target, TStageObject::T_X, 8);
  QString legacy = directory + "/legacy.xml";
  writeObjects(legacy, tree, 2);
  QFile oldFile(legacy);
  check(oldFile.open(QIODevice::ReadOnly), "read legacy serialization");
  check(!oldFile.readAll().contains("<rig"),
        "unconfigured scenes emit no rig extension");

  TRigBone bone                       = follower(target);
  bone.tip                            = TPointD(2, 0.5);
  bone.constraints[0].enforcement     = 0;
  bone.constraints[0].enforcementKeys = {{0, 0.25}, {12, 1}, {24, 0}};
  bone.constraints[0].firstFrame      = 0;
  bone.constraints[0].lastFrame       = 35;
  bone.constraints[0].offset          = TPointD(1, 0);
  check(source->setRigBone(bone), "persistent constraint assignment");
  source->setName("forearm");
  QString path = directory + "/rig.xml";
  writeObjects(path, tree, 2);
  TStageObjectTree restored;
  readObjects(path, restored, 2);
  auto *copy = restored.getStageObject(TStageObjectId::ColumnId(1));
  check(copy->getName() == "forearm", "bone name roundtrip");
  check(copy->getRigId() == source->getRigId(), "identity roundtrip");
  point(copy->getRigBone().tip, bone.tip, "tip roundtrip");
  check(copy->getRigBone().constraints[0].enforcementKeys.size() == 3,
        "enforcement keys roundtrip");
  for (int frame : {0, 12, 30, 6, 36, 12})
    point(pivot(copy, frame), pivot(source, frame),
          "reopen matches original at arbitrary frames");
}
}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    QTemporaryDir directory;
    check(directory.isValid(), "temporary fixture directory");
    legacyAndLiveCache();
    cyclesAndIdentity();
    persistence(directory.path());
    std::cout << "Skeleton stage-object tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "Unexpected stage-object test exception\n";
    return 1;
  }
  return 0;
}
