#include "cpitool.h"
#include "tools/tool.h"
#include "tools/toolhandle.h"
#include "tools/tooloptions.h"
#include "tools/toolcommandids.h"
#include "toonz/tapplication.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/tobjecthandle.h"
#include "toonz/tscenehandle.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/txsheet.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/toonzscene.h"
#include "toonzqt/tselectionhandle.h"
#include "tpalette.h"
#include "tstroke.h"
#include "tproperty.h"
#include "tthread.h"
#include "tundo.h"
#include "tenv.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QKeyEvent>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <stdexcept>

class App final : public TApplication {
public:
  mutable TFrameHandle frame;
  mutable TXshLevelHandle level;
  mutable TXsheetHandle xsheet;
  mutable TColumnHandle column;
  mutable TSceneHandle scene;
  mutable TObjectHandle object;
  mutable ToolHandle tool;
  mutable TSelectionHandle selection;
  TFrameHandle *getCurrentFrame() const override { return &frame; }
  TXshLevelHandle *getCurrentLevel() const override { return &level; }
  TXsheetHandle *getCurrentXsheet() const override { return &xsheet; }
  TObjectHandle *getCurrentObject() const override { return &object; }
  TColumnHandle *getCurrentColumn() const override { return &column; }
  TSceneHandle *getCurrentScene() const override { return &scene; }
  ToolHandle *getCurrentTool() const override { return &tool; }
  TSelectionHandle *getCurrentSelection() const override { return &selection; }
  TOnionSkinMaskHandle *getCurrentOnionSkin() const override { return nullptr; }
  TPaletteHandle *getCurrentPalette() const override { return nullptr; }
  TFxHandle *getCurrentFx() const override { return nullptr; }
  PaletteController *getPaletteController() const override { return nullptr; }
  TColorStyle *getCurrentLevelStyle() const override { return nullptr; }
  int getCurrentLevelStyleIndex() const override { return 0; }
  void setCurrentLevelStyleIndex(int, bool) override {}
};
class Tool final : public QObject, public TTool {
  TEnumProperty m_axis;
  TPropertyGroup m_properties;

public:
  Tool() : TTool("T_CpiTest"), m_axis("Active Axis") {
    bind(AllTargets);
    m_axis.addValue(L"CPI");
    m_axis.setValue(L"CPI");
    m_properties.bind(m_axis);
  }
  ToolType getToolType() const override { return ColumnTool; }
  TPropertyGroup *getProperties(int) override { return &m_properties; }
};
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
void checkNear(double a, double b, const char *message) {
  check(std::abs(a - b) < 1e-7, message);
}
QPushButton *button(QDialog *dialog, const QString &text) {
  for (auto b : dialog->findChildren<QPushButton *>())
    if (b->text() == text) return b;
  throw std::runtime_error("missing button");
}
void events() { QCoreApplication::processEvents(); }
void extraInteractions(App &app, CpiTool &cpi, ToolOptionsBox *bar,
                       TXshLevelColumn *col, const std::string &id,
                       TVectorImageP source) {
  auto saved    = col->getCpi();
  auto selected = cpi.selectedPoints();
  auto key      = app.frame.getFrame();
  auto point    = [&](int p) {
    return TPointD(cpi.image()->getStroke(0)->getControlPoint(p));
  };
  auto gesture = [&](TPointD from, TPointD to, const TMouseEvent &e) {
    app.tool.getTool()->leftButtonDown(from, e);
    app.tool.getTool()->leftButtonDrag(to, e);
    app.tool.getTool()->leftButtonUp(to, e);
  };
  TMouseEvent e;
  for (const char *name : {T_Magnet, T_Iron, T_Edit, T_Selection}) {
    app.tool.setTool(name);
    check(app.tool.isCpiMode() &&
              app.tool.getTool() == CpiTool::interactionTool(),
          "tools share persistent CPI session");
    check(cpi.selectedPoints() == selected, "selection survives tool switch");
  }
  TSelection::setCurrent(nullptr);
  check(cpi.selectedPoints() == selected,
        "Xsheet selection does not discard CPI selection");
  cpi.activate();
  app.tool.setTool(T_HandView);
  check(app.tool.getTool() != CpiTool::interactionTool(),
        "navigation uses ordinary tool");
  check(cpi.selectedPoints() == selected, "navigation retains CP selection");
  app.tool.setTool(T_Selection);
  auto box    = cpi.selectionBounds();
  auto start0 = point(0), start2 = point(2);
  TPointD handle(box.x1, (box.y0 + box.y1) * 0.5);
  int history = TUndoManager::manager()->getCurrentHistoryIndex();
  gesture(handle, handle + TPointD(25, 0), e);
  checkNear(point(2).x - point(0).x,
            (start2.x - start0.x) * (box.getLx() + 25) / box.getLx(),
            "ordinary side handle scales CPI points");
  check(TUndoManager::manager()->getCurrentHistoryIndex() == history + 1,
        "one transform is one undo");
  check(col->getCpi()->group(id)->pairs[0].keys.count(key),
        "handle records common offset key");
  TUndoManager::manager()->undo();
  check(col->getCpi() == saved, "scale undo restores snapshot");
  box            = cpi.selectionBounds();
  TPointD center = box.getP00() + TPointD(box.getLx(), box.getLy()) * 0.5;
  handle         = box.getP11() + TPointD(15, 15);
  auto oldPose   = saved->group(id)->evaluate(key);
  gesture(handle, center + TRotation(45) * (handle - center), e);
  checkNear(norm(point(2) - point(0)), norm(start2 - start0),
            "rotation handle preserves group distances");
  auto newPose = col->getCpi()->group(id)->evaluate(key);
  check(std::abs(newPose.rotation.z - oldPose.rotation.z) > 0.1,
        "group rotation uses quaternion channel");
  check(newPose.offsets.size() == oldPose.offsets.size(),
        "group rotation does not expand point offsets");
  TUndoManager::manager()->undo();
  check(col->getCpi() == saved, "rotation undo");
  auto radius   = bar->findChild<QDoubleSpinBox *>("cpiRadius");
  auto strength = bar->findChild<QDoubleSpinBox *>("cpiStrength");
  radius->setValue(20);
  strength->setValue(100);
  app.tool.setTool(T_Magnet);
  TPointD tip = point(1);
  start0      = point(0);
  start2      = point(2);
  gesture(tip, tip + TPointD(12, 7), e);
  checkNear(point(1).x, tip.x + 12,
            "Magnet applies full influence at brush center");
  checkNear(point(0).x, start0.x,
            "Magnet leaves points outside radius unchanged");
  checkNear(point(2).x, start2.x, "Magnet preserves distant endpoint");
  check(source->getStroke(0)->getControlPointCount() == 3,
        "brushes preserve source topology");
  TUndoManager::manager()->undo();
  check(col->getCpi() == saved, "Magnet undo");
  app.tool.getTool()->leftButtonDown(tip, e);
  app.tool.getTool()->leftButtonDrag(tip + TPointD(10, 0), e);
  check(col->getCpiPreview() && col->getCpi() == saved,
        "Magnet preview leaves saved channels unchanged");
  app.tool.setTool(T_Iron);
  check(!col->getCpiPreview() && col->getCpi() == saved,
        "switching brushes cancels unfinished edit");
  strength->setValue(25);
  tip = point(1);
  gesture(tip, tip, e);
  check(point(1).y < tip.y, "Smooth relaxes the control point");
  checkNear(point(0).y, start0.y, "Smooth pins first open endpoint");
  checkNear(point(2).y, start2.y, "Smooth pins last open endpoint");
  TUndoManager::manager()->undo();
  check(col->getCpi() == saved, "Smooth undo");
  app.tool.setTool(T_Eraser);
  check(!cpi.editable(), "source erasure is unavailable in CPI");
  gesture(point(1), point(1) + TPointD(100, 100), e);
  check(col->getCpi() == saved && source->getStrokeCount() == 1,
        "unsupported tool cannot alter drawing");
  app.tool.setTool(T_Selection);
  check(cpi.selectedPoints() == selected,
        "selection survives unsupported tool and return");
  check(saved->bindings[0].matches(source),
        "all shared tools preserve the binding");
}

void run(App &app, Tool &tool, const QString &directory) {
  ToonzScene scene;
  auto xsh = scene.getXsheet();
  app.scene.setScene(&scene);
  app.xsheet.setXsheet(xsh);
  app.column.setColumnIndex(0);
  app.object.setObjectId(TStageObjectId::ColumnId(0));
  app.frame.setFrame(0);
  TXshSimpleLevelP level = new TXshSimpleLevel(L"Curve");
  level->setType(PLI_XSHLEVEL);
  level->setScene(&scene);
  TVectorImageP image = new TVectorImage;
  image->setPalette(new TPalette);
  level->setPalette(image->getPalette());
  std::vector<TThickPoint> points = {{0, 0, 1}, {30, 50, 1}, {60, 0, 1}};
  auto stroke                     = new TStroke(points);
  stroke->setStyle(1);
  image->addStroke(stroke);
  level->setFrame(TFrameId(1), image);
  xsh->setCell(0, 0, TXshCell(level.getPointer(), TFrameId(1)));
  auto col = xsh->getColumn(0)->getLevelColumn();
  app.tool.setCpiMode(true);
  app.tool.setTool(T_Selection);
  auto &cpi = *CpiTool::session();
  auto down = [&](const TPointD &p, const TMouseEvent &e) {
    app.tool.getTool()->leftButtonDown(p, e);
  };
  auto drag = [&](const TPointD &p, const TMouseEvent &e) {
    app.tool.getTool()->leftButtonDrag(p, e);
  };
  auto up = [&](const TPointD &p, const TMouseEvent &e) {
    app.tool.getTool()->leftButtonUp(p, e);
  };
  auto press = [&](QKeyEvent *e) { return app.tool.getTool()->keyDown(e); };
  std::unique_ptr<ToolOptionsBox> bar(app.tool.getTool()->createOptionsBox());
  bar->resize(1450, 30);
  bar->show();
  cpi.openChannels();
  events();
  QDialog *dialog = nullptr;
  for (auto w : QApplication::topLevelWidgets())
    if (w->windowTitle().contains("Control Point Interpolation"))
      dialog = qobject_cast<QDialog *>(w);
  check(dialog, "channels window");
  TMouseEvent e;
  down(TPointD(-20, -20), e);
  drag(TPointD(80, 70), e);
  up(TPointD(80, 70), e);
  check(cpi.selectedPoints().size() == 3, "rectangle selects control points");
  QKeyEvent deselect(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  press(&deselect);
  auto shape = bar->findChild<QComboBox *>("cpiShape");
  shape->setCurrentIndex(1);
  down(TPointD(-20, -20), e);
  drag(TPointD(80, -20), e);
  drag(TPointD(80, 70), e);
  drag(TPointD(-20, 70), e);
  up(TPointD(-20, -20), e);
  check(cpi.selectedPoints().size() == 3, "lasso shares the CP selection");
  TMouseEvent subtract;
  subtract.setModifiers(false, false, true);
  down(TPointD(30, 50), subtract);
  up(TPointD(30, 50), subtract);
  check(cpi.selectedPoints().size() == 2, "Ctrl-click subtracts a CP");
  TMouseEvent add;
  add.setModifiers(true, false, false);
  down(TPointD(30, 50), add);
  up(TPointD(30, 50), add);
  check(cpi.selectedPoints().size() == 3, "Shift-click adds a CP");
  shape->setCurrentIndex(0);
  QTimer::singleShot(0, [] {
    auto input =
        qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
    if (input) {
      input->setTextValue("Arm");
      input->accept();
    }
  });
  button(dialog, "New Group")->click();
  events();
  check(col->getCpi() && col->getCpi()->groups.size() == 1,
        "selection creates named group");
  auto id = col->getCpi()->groups[0].id;
  check(col->getCpi()->groups[0].points.size() == 3, "all CPs selected");
  button(dialog, "Set Key")->click();
  events();
  check(col->getCpi()->groups[0].pairs.size() == 1,
        "first Set Key creates extreme pair");
  check(col->getCell(72).m_level == level,
        "partner exposure at 72-frame distance");
  check(xsh->getFrameCount() == 73, "scene duration includes partner");
  TUndoManager::manager()->undo();
  check(col->getCpi()->groups[0].pairs.empty() && col->getCell(72).isEmpty(),
        "undo restores channels and exposures");
  TUndoManager::manager()->redo();
  check(
      col->getCpi()->groups[0].pairs.size() == 1 && !col->getCell(72).isEmpty(),
      "redo restores pair atomically");
  app.frame.setFrame(72);
  events();
  QKeyEvent clear(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  press(&clear);
  down(TPointD(30, 50), e);
  drag(TPointD(40, 70), e);
  auto during = col->getCpiPreview();
  check(during && during->base == col->getCpi(),
        "preview retains immutable channel snapshot");
  checkNear(during->pose.offset(Cpi::pointId(0, 1)).x, 10, "live drag preview");
  up(TPointD(40, 70), e);
  events();
  auto posed = col->getCpi();
  checkNear(posed->group(id)->evaluate(72).offset(Cpi::pointId(0, 1)).y, 20,
            "individual CP endpoint edit");
  checkNear(posed->group(id)->evaluate(72).offset(Cpi::pointId(0, 0)).x, 0,
            "individual edit preserves neighbors");
  checkNear(image->getStroke(0)->getControlPoint(1).x, 30,
            "source drawing unchanged");
  app.frame.setFrame(36);
  events();
  down(TPointD(35, 60), e);
  drag(TPointD(45, 65), e);
  up(TPointD(45, 65), e);
  events();
  check(col->getCpi()->group(id)->pairs[0].keys.count(36),
        "interior drag creates common offset key");
  checkNear(col->getCpi()->group(id)->evaluate(36).offset(1).x, 15,
            "interior drag position");
  auto committed = col->getCpi();
  down(TPointD(45, 65), e);
  drag(TPointD(55, 75), e);
  press(&clear);
  check(col->getCpi() == committed, "Escape discards preview");
  down(TPointD(45, 65), e);
  drag(TPointD(55, 75), e);
  app.frame.setFrame(35);
  events();
  check(col->getCpi() == committed, "frame switch discards preview");
  app.frame.setFrame(36);
  events();
  QComboBox *target = nullptr;
  for (auto combo : dialog->findChildren<QComboBox *>())
    if (combo->findText("Entire group") >= 0) target = combo;
  check(target, "group manipulation option");
  target->setCurrentIndex(1);
  down(TPointD(45, 65), e);
  drag(TPointD(60, 75), e);
  up(TPointD(60, 75), e);
  events();
  checkNear(col->getCpi()->group(id)->evaluate(36).translation.x, 15,
            "entire-group drag");
  auto saved = col->getCpi();
  col->lock(true);
  down(TPointD(60, 75), e);
  drag(TPointD(90, 100), e);
  up(TPointD(90, 100), e);
  check(col->getCpi() == saved, "locked column unchanged");
  col->lock(false);
  extraInteractions(app, cpi, bar.get(), col, id, image);
  button(dialog, "Remove Key / Pair")->click();
  check(col->getCpi()->group(id)->pairs[0].keys.empty(),
        "remove common offset key");
  TUndoManager::manager()->undo();
  check(col->getCpi()->group(id)->pairs[0].keys.count(36),
        "undo common key removal");
  app.frame.setFrame(72);
  events();
  button(dialog, "Remove Key / Pair")->click();
  check(col->getCpi()->group(id)->pairs.empty(),
        "extreme deletion removes pair");
  TUndoManager::manager()->undo();
  check(col->getCpi()->group(id)->pairs[0].keys.count(36),
        "undo pair deletion restores interior keys");
  app.frame.setFrame(36);
  events();
  QDir().mkpath(directory);
  check(dialog->grab().save(directory + "/cpi-channels.png"),
        "save channels preview");
  check(bar->grab().save(directory + "/cpi-toolbar.png"),
        "save toolbar preview");
  app.tool.setTool("T_CpiTest");
  check(!cpi.editable(), "unsupported tools cannot edit source in CPI");
  app.tool.setCpiMode(false);
  check(app.tool.getTool() == &tool,
        "exiting CPI restores requested ordinary tool");
  TUndoManager::manager()->reset();
  app.xsheet.setXsheet(nullptr);
  app.scene.setScene(nullptr);
  events();
}
int main(int argc, char **argv) {
  QApplication qapp(argc, argv);
  TThread::init();
  QTemporaryDir temp;
  TEnv::setStuffDir(TFilePath(temp.path()));
  TEnv::setApplicationFileName("OpenToonz");
  App app;
  TTool::setApplication(&app);
  Tool tool;
  app.tool.onImageChanged(TImage::VECTOR);
  app.tool.setTool("T_CpiTest");
  try {
    run(app, tool, argc > 1 ? QString::fromUtf8(argv[1]) : temp.path());
    std::cout << "PASS: CPI controls, selection, paired exposures, "
                 "individual/group drag, live preview, offset keys, "
                 "cancellation, locks and Undo/Redo\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
