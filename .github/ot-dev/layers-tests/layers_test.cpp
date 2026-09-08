// Integration tests use the same Qt and native libraries as the application.
#include "drawinglayers.h"
#include "tenv.h"
#include "texception.h"
#include "tlevel_io.h"
#include "tnzimage.h"
#include "tools/imagegrouping.h"
#include "tools/strokeselection.h"
#include "tools/tool.h"
#include "tools/toolhandle.h"
#include "toonz/tapplication.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/tobjecthandle.h"
#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
#include "toonz/txshcolumn.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"
#include "toonzqt/icongenerator.h"
#include "toonzqt/tselectionhandle.h"
#include "tpalette.h"
#include "tstroke.h"
#include "tundo.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#ifdef __linux__
#include <csignal>
#include <execinfo.h>
#endif
#include <stdexcept>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      throw std::runtime_error(std::string(__FILE__) + ":" +                   \
                               std::to_string(__LINE__) + ": " #condition);    \
  } while (false)

class App final : public TApplication {
public:
  mutable TFrameHandle frame;
  mutable TXshLevelHandle level;
  mutable TXsheetHandle xsheet;
  mutable TColumnHandle column;
  mutable TSceneHandle scene;
  mutable TObjectHandle object;
  mutable TSelectionHandle selection;
  mutable ToolHandle tool;
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
class TestTool final : public TTool {
public:
  TestTool() : TTool("LayersTestTool") { bind(VectorImage); }
  ToolType getToolType() const override { return LevelWriteTool; }
};
void events() {
  QCoreApplication::processEvents();
  QTest::qWait(30);
}
int indexOf(const TVectorImageP &image, int id) {
  for (UINT i = 0; i < image->getStrokeCount(); ++i)
    if (image->getStroke(i)->getId() == id)
      return i;
  throw std::runtime_error("Lost stroke");
}
TVectorImageP drawing() {
  TVectorImageP image = new TVectorImage;
  image->setPalette(new TPalette);
  for (int i = 0; i < 6; ++i) {
    std::vector<TThickPoint> points = {TThickPoint(i * 20, 0, 2),
                                       TThickPoint(i * 20 + 5, 10, 2),
                                       TThickPoint(i * 20 + 10, 0, 2)};
    auto stroke = new TStroke(points);
    stroke->setStyle(1);
    image->addStroke(stroke);
  }
  image->group(2, 2);
  image->group(1, 4);
  return image;
}
void sameDrawing(const TVectorImageP &a, const TVectorImageP &b,
                 bool names = true) {
  CHECK(a && b);
  CHECK(a->getStrokeCount() == b->getStrokeCount());
  for (UINT i = 0; i < a->getStrokeCount(); ++i) {
    CHECK(a->getGroupDepth(i) == b->getGroupDepth(i));
    CHECK(a->getStroke(i)->getStyle() == b->getStroke(i)->getStyle());
    CHECK(a->getStroke(i)->getControlPointCount() ==
          b->getStroke(i)->getControlPointCount());
    for (int p = 0; p < a->getStroke(i)->getControlPointCount(); ++p)
      CHECK(tdistance(a->getStroke(i)->getControlPoint(p),
                      b->getStroke(i)->getControlPoint(p)) < 0.01);
    for (int d = 1; d <= a->getGroupDepth(i); ++d)
      CHECK((names ? a->getGroupName(i, d) : std::wstring()) ==
            b->getGroupName(i, d));
    if (i)
      CHECK(a->getCommonGroupDepth(i - 1, i) ==
            b->getCommonGroupDepth(i - 1, i));
  }
}
void writeLevel(const QString &path, const TVectorImageP &a,
                const TVectorImageP &b) {
  TLevelWriterP writer{TFilePath(path)};
  writer->setPalette(a->getPalette());
  writer->getFrameWriter(TFrameId(1))->save(TImageP(a.getPointer()));
  writer->getFrameWriter(TFrameId(2))->save(TImageP(b.getPointer()));
}
void readLevel(const QString &path, const TVectorImageP &a,
               const TVectorImageP &b, bool names = true) {
  TLevelReaderP reader{TFilePath(path)};
  reader->loadInfo();
  sameDrawing(a, TVectorImageP(reader->getFrameReader(TFrameId(1))->load()),
              names);
  sameDrawing(b, TVectorImageP(reader->getFrameReader(TFrameId(2))->load()),
              names);
}
void nativeAndFileTests(const QString &root) {
  std::cout << "Testing native names and PLI files\n";
  auto image = drawing();
  int inner = -1;
  for (UINT i = 0; i < image->getStrokeCount(); ++i)
    if (image->getGroupDepth(i) == 2)
      inner = i;
  CHECK(inner >= 0);
  CHECK(image->setGroupName(inner, 1, L"Character"));
  const std::wstring unicode =
      QString::fromUtf8("顔 / Eyes \xF0\x9F\x91\x81").toStdWString();
  CHECK(image->setGroupName(inner, 2, unicode));
  CHECK(!image->setGroupName(inner, 2, unicode));
  CHECK(!image->setGroupName(99, 1, L"Invalid"));
  CHECK(!image->setGroupName(0, 1, L"Ungrouped"));
  CHECK(!image->setGroupName(inner, 0, L"Invalid"));
  CHECK(!image->setGroupName(inner, 3, L"Invalid"));
  CHECK(image->enterGroup(inner));
  CHECK(image->setGroupName(inner, 1, L"Body"));
  CHECK(image->isInsideGroup() == 1);
  auto state = image->getGroupStructure();
  auto originalStroke = image->getStroke(inner);
  TVectorImageP clone = image->cloneImage();
  CHECK(clone->setGroupName(inner, 1, L"Copy"));
  CHECK(image->getGroupName(inner, 1) == L"Body");
  image->ungroup(inner);
  CHECK(image->getGroupName(inner, 1) == unicode);
  CHECK(image->restoreGroupStructure(state));
  CHECK(image->getStroke(inner) == originalStroke);
  CHECK(image->getGroupName(inner, 1) == L"Body");
  CHECK(image->getGroupName(inner, 2) == unicode);
  CHECK(image->isInsideGroup() == 1);
  image->exitGroup();
  auto copiedGroup = image->splitImage({1, 2, 3, 4}, false);
  CHECK(copiedGroup->getGroupName(0, 1) == L"Body");
  CHECK(copiedGroup->getGroupName(3, 2) == unicode);
  TVectorImageP merged = image->cloneImage();
  merged->mergeImage(copiedGroup, TTranslation(0, 50), false);
  CHECK(merged->getStrokeCount() == 10);
  CHECK(merged->setGroupName(6, 1, L"Pasted"));
  CHECK(merged->getGroupName(1, 1) == L"Body");
  CHECK(copiedGroup->getGroupName(0, 1) == L"Body");
  CHECK(merged->getGroupName(9, 2) == unicode);
  auto clean = drawing();
  writeLevel(root + "/legacy.pli", clean, clean);
  readLevel(root + "/legacy.pli", clean, clean);
  writeLevel(root + "/named.pli", image, clone);
  readLevel(root + "/named.pli", image, clone);
  // Save As / a copied level carries names without any sidecar.
  writeLevel(root + "/copy.pli", image, clone);
  readLevel(root + "/copy.pli", image, clone);
  QFile file(root + "/named.pli");
  CHECK(file.open(QIODevice::ReadOnly));
  QByteArray bytes = file.readAll();
  file.close();
  // Exercise the pre-extension reader's unknown-tag path, plus invalid
  // metadata.
  for (int mode = 0; mode < 4; ++mode) {
    QByteArray modified = bytes;
    int count = 0;
    for (auto name :
         {QString("Body"), QString("Copy"), QString::fromStdWString(unicode)}) {
      QByteArray utf8 = name.toUtf8();
      int pos = 0;
      while ((pos = modified.indexOf(utf8, pos)) >= 0) {
        int header = pos - 7;
        CHECK(header >= 0 && (uchar(modified[header]) & 63) == 27);
        CHECK(uchar(modified[header + 1]) == utf8.size() + 5);
        if (mode == 0)
          modified[header] = char((uchar(modified[header]) & 192) | 28);
        if (mode == 1)
          modified[header + 2] = char(2); // unsupported version
        if (mode == 2)
          for (int j = 3; j <= 6; ++j)
            modified[header + j] = char(255);
        if (mode == 3)
          modified[pos] = char(255); // invalid UTF-8
        pos += utf8.size();
        ++count;
      }
    }
    CHECK(count == 4);
    QString path = root + "/metadata-" + QString::number(mode) + ".pli";
    QFile output(path);
    CHECK(output.open(QIODevice::WriteOnly));
    CHECK(output.write(modified) == modified.size());
    output.close();
    readLevel(path, image, clone, false);
  }
  std::cout
      << "PASS: native nested names, clone independence, entered context, PLI "
         "save/reopen, Save As, legacy and invalid metadata\n";
}
QTreeWidgetItem *expand(DrawingLayers &panel) {
  auto column = panel.topLevelItem(0);
  CHECK(column);
  column->setExpanded(true);
  auto level = column->child(0);
  CHECK(level);
  level->setExpanded(true);
  events();
  auto frame = level->child(0);
  CHECK(frame);
  frame->setExpanded(true);
  events();
  return frame;
}
QLineEdit *editor(DrawingLayers &panel) {
  for (auto line : panel.findChildren<QLineEdit *>())
    if (line->isVisible())
      return line;
  return nullptr;
}
void commitEditor(DrawingLayers &panel, const QString &name) {
  auto line = editor(panel);
  CHECK(line);
  line->setText(name);
  QTest::keyClick(line, Qt::Key_Return);
  events();
}
void panelAndUndoTests() {
  std::cout << "Testing Layers editing and undo\n";
  App app;
  ToonzScene scene;
  TestTool tool;
  TTool::setApplication(&app);
  auto xsheet = scene.getXsheet();
  app.scene.setScene(&scene);
  app.xsheet.setXsheet(xsheet);
  app.column.setColumnIndex(0);
  app.frame.setFrame(0);
  TXshSimpleLevelP level = new TXshSimpleLevel(L"Linework");
  level->setType(PLI_XSHLEVEL);
  level->setScene(&scene);
  auto image = drawing();
  level->setPalette(image->getPalette());
  level->setFrame(TFrameId(1), image);
  xsheet->setCell(0, 0, TXshCell(level.getPointer(), TFrameId(1)));
  app.level.setLevel(level.getPointer());
  DrawingLayers panel(&app), other(&app);
  // No thumbnail painting/OpenGL is needed to exercise the actual tree editor.
  panel.setUpdatesEnabled(false);
  other.setUpdatesEnabled(false);
  panel.resize(500, 700);
  panel.show();
  other.show();
  events();
  CHECK(panel.headerItem()->text(0) == "Layers");
  CHECK(panel.topLevelItem(0)->sizeHint(0).height() == 28);
  CHECK(panel.iconSize() == QSize(32, 24));
  auto frame = expand(panel);
  auto outer = frame->child(1);
  CHECK(outer);
  CHECK(!app.scene.getDirtyFlag());
  panel.scrollToItem(outer);
  events();
  QPoint point = panel.visualItemRect(outer).center();
  QTest::mouseClick(panel.viewport(), Qt::LeftButton, Qt::NoModifier, point);
  QTest::mouseDClick(panel.viewport(), Qt::LeftButton, Qt::NoModifier, point);
  events();
  CHECK(editor(panel) && editor(panel)->text().isEmpty());
  commitEditor(panel, "Character");
  CHECK(image->getGroupName(1, 1) == L"Character");
  CHECK(app.scene.getDirtyFlag());
  CHECK(expand(other)->child(1)->text(0) == "Character");
  TUndoManager::manager()->undo();
  events();
  CHECK(image->getGroupName(1, 1).empty());
  TUndoManager::manager()->redo();
  events();
  CHECK(image->getGroupName(1, 1) == L"Character");
  outer = expand(panel)->child(1);
  outer->setExpanded(true);
  events();
  auto inner = outer->child(0);
  CHECK(inner);
  panel.setCurrentItem(inner);
  QTest::keyClick(&panel, Qt::Key_F2);
  events();
  commitEditor(panel, QString::fromUtf8("Eyes / 顔"));
  CHECK(image->getGroupName(3, 2) ==
        QString::fromUtf8("Eyes / 顔").toStdWString());
  CHECK(image->isInsideGroup() == 0);
  outer = expand(panel)->child(1);
  panel.setCurrentItem(outer);
  std::cout << "Testing context-menu rename\n";
  QTimer::singleShot(10, [] {
    auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (menu) {
      menu->setActiveAction(menu->actions().front());
      QTest::keyClick(menu, Qt::Key_Return);
    }
  });
  point = panel.visualItemRect(outer).center();
  QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, point,
                              panel.viewport()->mapToGlobal(point));
  QApplication::sendEvent(panel.viewport(), &menuEvent);
  events();
  commitEditor(panel, "Renamed");
  CHECK(image->getGroupName(1, 1) == L"Renamed");
  outer = expand(panel)->child(1);
  panel.setCurrentItem(outer);
  QTest::keyClick(&panel, Qt::Key_F2);
  events();
  commitEditor(panel, "  ");
  CHECK(image->getGroupName(1, 1).empty());
  CHECK(expand(panel)->child(1)->text(0).startsWith("Group"));
  TUndoManager::manager()->undo();
  events();
  outer = expand(panel)->child(1);
  panel.setCurrentItem(outer);
  QTest::keyClick(&panel, Qt::Key_F2);
  events();
  CHECK(editor(panel));
  editor(panel)->setText("Cancelled");
  QTest::keyClick(editor(panel), Qt::Key_Escape);
  events();
  CHECK(image->getGroupName(1, 1) == L"Renamed");
  xsheet->getColumn(0)->lock(true);
  QTest::keyClick(&panel, Qt::Key_F2);
  events();
  CHECK(!editor(panel));
  xsheet->getColumn(0)->lock(false);
  // Reject an editor committed after a stroke/group structure changes.
  QTest::keyClick(&panel, Qt::Key_F2);
  events();
  CHECK(editor(panel));
  auto structure = image->getGroupStructure();
  image->ungroup(1);
  commitEditor(panel, "Stale");
  CHECK(image->getGroupName(3, 1) != L"Stale");
  CHECK(image->restoreGroupStructure(structure));
  app.level.notifyLevelChange();
  events();
  TUndoManager::manager()->reset();
  // Actual native commands: undo keeps separate groups, names and stroke
  // identity.
  std::cout << "PASS: Layers tree rename controls, two panels and stale-editor "
               "guards\n";
  // Native commands invalidate Filmstrip thumbnails even without a Viewer.
  // Stop background workers in this UI harness; model/undo work stays
  // synchronous, and no GPU thumbnail tasks should run in the test process.
  TThread::Executor::shutdown();
  app.frame.setFid(TFrameId(1));
  app.tool.onImageChanged(TImage::VECTOR);
  app.tool.setTool("LayersTestTool");
  CHECK(app.tool.getTool() == &tool);
  StrokeSelection selection;
  selection.setImage(image);
  app.selection.setSelection(&selection);
  TGroupCommand command;
  command.setSelection(&selection);
  for (int i = 1; i <= 4; ++i)
    selection.select(i, true);
  TVectorImageP before = image->cloneImage();
  auto stroke = image->getStroke(3);
  command.ungroup();
  TUndoManager::manager()->undo();
  events();
  sameDrawing(before, image);
  CHECK(image->getStroke(3) == stroke);
  TUndoManager::manager()->redo();
  events();
  CHECK(image->getGroupName(3, 1) == before->getGroupName(3, 2));
  TUndoManager::manager()->undo();
  events();
  selection.selectNone();
  selection.select(0, true);
  selection.select(5, true);
  command.group();
  TUndoManager::manager()->undo();
  events();
  sameDrawing(before, image);
  CHECK(image->getStroke(3) == stroke);
  CHECK(selection.getSelection() == std::set<int>({0, 5}));
  TUndoManager::manager()->redo();
  TUndoManager::manager()->undo();
  events();
  sameDrawing(before, image);
  panel.hide();
  other.hide();
  app.selection.setSelection(nullptr);
  TUndoManager::manager()->reset();
  TTool::setApplication(nullptr);
  std::cout << "PASS: compact Layers UI, double-click/F2/context rename, two "
               "panels, clear/cancel/lock/stale edit, rename and native "
               "group/ungroup undo/redo\n";
}
int main(int argc, char **argv) {
  std::cout << std::unitbuf << "Starting Layers integration tests\n";
#ifdef __linux__
  std::signal(SIGSEGV, [](int) {
    void *trace[40];
    int n = backtrace(trace, 40);
    backtrace_symbols_fd(trace, n, 2);
    std::_Exit(1);
  });
#endif
  QApplication application(argc, argv);
  TThread::init();
  try {
    QTemporaryDir root;
    CHECK(root.isValid());
    QDir().mkpath(root.path() + "/stuff/profiles/layouts/settings");
    TEnv::setStuffDir(TFilePath(root.path() + "/stuff"));
    TEnv::setApplicationFileName("OpenToonz");
    initImageIo(false);
    nativeAndFileTests(root.path());
    panelAndUndoTests();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const TException &error) {
    std::cerr << QString::fromStdWString(error.getMessage()).toStdString()
              << '\n';
    return 1;
  } catch (...) {
    std::cerr << "Unexpected native exception\n";
    return 1;
  }
}
