// Regression for #6923: empty overwrite buffers must not replace mesh columns.
// Uses the real TXsheet, TStageObject, Plastic rig and persistence components.
#include "toonz/toonzscene.h"
#include "toonz/txsheet.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/txshleveltypes.h"
#include "toonz/tstageobject.h"
#include "ext/plasticskeleton.h"
#include "ext/plasticskeletondeformation.h"
#include "tstream.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

void exercise(bool mesh, bool partial, bool occupied, bool crossColumn) {
  ToonzScene scene;
  TXsheet* xsh           = scene.getXsheet();
  TXshSimpleLevelP level = new TXshSimpleLevel(L"empty-buffer-test");
  level->setType(mesh ? MESH_XSHLEVEL : TZP_XSHLEVEL);
  const TXshCell cell(level.getPointer(), TFrameId(1)), empty;
  check(xsh->setCells(0, 0, 1, &cell), "seed source");
  if (partial) xsh->setCells(20, 0, 1, &cell);
  const int dst = crossColumn ? 1 : 0;
  if (occupied) xsh->setCells(10, dst, 1, &cell);

  TXshColumn* column  = xsh->getColumn(0);
  TStageObject* obj   = xsh->getStageObject(TStageObjectId::ColumnId(0));
  TStageObject* child = xsh->getStageObject(TStageObjectId::PegbarId(0));
  child->setParent(obj->getId());
  obj->setCenterAndOffset(TPointD(2, 3), TPointD(4, 5));
  obj->getParam(TStageObject::T_X)->setValue(2, 20);
  obj->getParam(TStageObject::T_X)
      ->setValue(50, 500);  // unrelated to moved cells

  PlasticSkeletonDeformationP deformation;
  PlasticSkeletonP skeleton;
  if (mesh) {
    deformation    = new PlasticSkeletonDeformation;
    skeleton       = new PlasticSkeleton;
    const int root = skeleton->addVertex(PlasticSkeletonVertex(TPointD()), -1);
    skeleton->addVertex(PlasticSkeletonVertex(TPointD(10, 0)), root);
    deformation->attach(1, skeleton.getPointer());
    deformation->skeletonIdsParam()->setValue(2, 1);
    deformation->vertexDeformation(1, root)->m_params[SkVD::ANGLE]->setValue(
        2, 25);
    deformation->vertexDeformation(1, root)->m_params[SkVD::ANGLE]->setValue(
        50, 75);
    obj->setPlasticSkeletonDeformation(deformation);
  }

  auto verify = [&]() {
    check(xsh->getColumn(0) == column, "column identity changed");
    check(xsh->getStageObject(TStageObjectId::ColumnId(0)) == obj,
          "stage object identity changed");
    check(child->getParent() == obj->getId(), "child reparented");
    check(obj->getParam(TStageObject::T_X)->getValue(2) == 20 &&
              obj->getParam(TStageObject::T_X)->getValue(50) == 500,
          "ordinary or unrelated keys lost");
    TPointD center, offset;
    obj->getCenterAndOffset(center, offset);
    check(center == TPointD(2, 3) && offset == TPointD(4, 5),
          "center/offset changed");
    if (mesh) {
      check(obj->getPlasticSkeletonDeformation() == deformation,
            "rig replaced");
      check(deformation->skeleton(1) == skeleton, "topology replaced");
      check(deformation->vertexDeformationsCount() == 2, "vertices lost");
      check(deformation->isKeyframe(2) && deformation->isKeyframe(50),
            "Plastic keys lost");
    }
  };

  // Non-insert overwrite preview, then cell-buffer Undo/return, then Redo.
  // These calls exercise the actual storage boundary, not the GUI drag tool.
  xsh->clearCells(0, 0, 1);
  xsh->setCells(0, 0, 1, &empty);
  xsh->setCells(10, dst, 1, &cell);
  verify();
  xsh->clearCells(10, dst, 1);
  xsh->setCells(10, dst, 1, occupied ? &cell : &empty);
  xsh->setCells(0, 0, 1, &cell);
  verify();
  xsh->clearCells(0, 0, 1);
  xsh->setCells(10, dst, 1, &cell);
  verify();

  // Real stage-object stream round trip. This does not claim a .tnz GUI save.
  QTemporaryDir dir;
  check(dir.isValid(), "temporary directory");
  const TFilePath path(dir.filePath("stage.xml").toStdWString());
  {
    TOStream os(path);
    obj->saveData(os);
  }
  ToonzScene reopened;
  TStageObject* restored =
      reopened.getXsheet()->getStageObject(TStageObjectId::ColumnId(0));
  {
    TIStream is(path);
    is.setVersion(VersionNumber(71, 1));
    restored->loadData(is);
  }
  check(restored->getParam(TStageObject::T_X)->getValue(50) == 500,
        "serialized keys lost");
  if (mesh) {
    const auto& rig = restored->getPlasticSkeletonDeformation();
    check(rig && rig->skeletonsCount() == 1 &&
              rig->vertexDeformationsCount() == 2,
          "serialized rig/topology lost");
    check(rig->isKeyframe(2) && rig->isKeyframe(50),
          "serialized Plastic keys lost");
  }
}

void emptyAndLocked() {
  ToonzScene scene;
  TXsheet* xsh = scene.getXsheet();
  TXshCell empty;
  int count = xsh->getColumnCount();
  check(!xsh->setCells(0, count + 3, 1, &empty),
        "empty write created absent column");
  check(xsh->getColumnCount() == count, "column count changed");
  TXshSimpleLevelP mesh = new TXshSimpleLevel;
  mesh->setType(MESH_XSHLEVEL);
  TXshCell cell(mesh.getPointer(), TFrameId(1));
  xsh->setCells(0, 0, 1, &cell);
  TXshColumn* column = xsh->getColumn(0);
  column->lock(true);
  check(!xsh->setCells(0, 0, 1, &empty), "empty write changed locked column");
  check(xsh->getCell(0, 0) == cell, "locked cell cleared");
  column->lock(false);
  xsh->clearCells(0, 0, 1);
  TXshSimpleLevelP ordinary = new TXshSimpleLevel;
  ordinary->setType(TZP_XSHLEVEL);
  TXshCell drawing(ordinary.getPointer(), TFrameId(1));
  check(xsh->setCells(0, 0, 1, &drawing), "explicit type conversion failed");
  check(xsh->getColumn(0)->getColumnType() == TXshColumn::eLevelType,
        "explicit type conversion disabled");
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  try {
    for (bool mesh : {false, true})
      for (bool partial : {false, true})
        for (bool occupied : {false, true})
          for (bool crossColumn : {false, true})
            exercise(mesh, partial, occupied, crossColumn);
    emptyAndLocked();
    std::cout
        << "16 real xsheet/Plastic cases and stage stream round trips passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
