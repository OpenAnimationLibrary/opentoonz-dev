#include "cpitool.h"
#include "tools/tool.h"
#include "tools/toolhandle.h"
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
  CpiTool cpi;
  Tool() : TTool("T_CpiTest"), m_axis("Active Axis"), cpi(this, this) {
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
  tool.cpi.openChannels();
  events();
  QDialog *dialog = nullptr;
  for (auto w : QApplication::topLevelWidgets())
    if (w->windowTitle().contains("Control Point Interpolation"))
      dialog = qobject_cast<QDialog *>(w);
  check(dialog, "channels window");
  TMouseEvent e;
  tool.cpi.down(TPointD(-20, -20), e);
  tool.cpi.drag(TPointD(80, 70), e);
  tool.cpi.up(TPointD(80, 70), e);
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
  tool.cpi.keyDown(&clear);
  tool.cpi.down(TPointD(30, 50), e);
  tool.cpi.drag(TPointD(40, 70), e);
  auto during = col->getCpi();
  checkNear(during->group(id)->evaluate(72).offset(Cpi::pointId(0, 1)).x, 10,
            "live drag preview");
  tool.cpi.up(TPointD(40, 70), e);
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
  tool.cpi.down(TPointD(35, 60), e);
  tool.cpi.drag(TPointD(45, 65), e);
  tool.cpi.up(TPointD(45, 65), e);
  events();
  check(col->getCpi()->group(id)->pairs[0].keys.count(36),
        "interior drag creates common offset key");
  checkNear(col->getCpi()->group(id)->evaluate(36).offset(1).x, 15,
            "interior drag position");
  auto committed = col->getCpi();
  tool.cpi.down(TPointD(45, 65), e);
  tool.cpi.drag(TPointD(55, 75), e);
  tool.cpi.keyDown(&clear);
  check(col->getCpi() == committed, "Escape discards preview");
  tool.cpi.down(TPointD(45, 65), e);
  tool.cpi.drag(TPointD(55, 75), e);
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
  tool.cpi.down(TPointD(45, 65), e);
  tool.cpi.drag(TPointD(60, 75), e);
  tool.cpi.up(TPointD(60, 75), e);
  events();
  checkNear(col->getCpi()->group(id)->evaluate(36).translation.x, 15,
            "entire-group drag");
  auto saved = col->getCpi();
  col->lock(true);
  tool.cpi.down(TPointD(60, 75), e);
  tool.cpi.drag(TPointD(90, 100), e);
  tool.cpi.up(TPointD(90, 100), e);
  check(col->getCpi() == saved, "locked column unchanged");
  col->lock(false);
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
  dialog->grab().save(directory + "/cpi-channels.png");
  tool.cpi.deactivate();
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
