#!/usr/bin/env python3
"""Apply the PR #155 interaction update; fail rather than patch unknown code."""
from pathlib import Path

ROOT = Path.cwd()


def edit(path, old, new):
    p = ROOT / path
    text = p.read_text(encoding='utf-8')
    if text.count(old) != 1:
        raise RuntimeError(f'{path}: expected one matching edit, found {text.count(old)}')
    p.write_text(text.replace(old, new), encoding='utf-8')


def write(path, content):
    p = ROOT / path
    if p.exists():
        raise RuntimeError(f'Refusing to overwrite unexpected file: {path}')
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content, encoding='utf-8')


H = 'toonz/sources/toonz/drawinglayers.h'
C = 'toonz/sources/toonz/drawinglayers.cpp'
edit(H, '#include <QTreeWidget>', '#include <QTreeWidget>\n#include <QPersistentModelIndex>')
edit(H, 'class TApplication;', 'class TApplication;\nclass TApp;\nclass QLabel;\nclass QMenu;')
edit(H, '  QTimer *m_rebuildTimer;', '''  QTimer *m_rebuildTimer;
  QTimer *m_hoverTimer = nullptr;
  QWidget *m_hoverPreview = nullptr;
  QLabel *m_previewImage;
  QLabel *m_previewCaption;
  QPersistentModelIndex m_hoverIndex;
  QPoint m_hoverPosition;
  bool m_activating = false;''')
edit(H, '  DrawingLayers(TApplication *app, QWidget *parent = nullptr);', '''  DrawingLayers(TApplication *app, QWidget *parent = nullptr);
  // The application adapter lives separately from the testable tree widget.
  DrawingLayers(TApp *app, QWidget *parent = nullptr);

signals:
  void exposureActivated(int row, int column, bool makeCurrent);
  void exposureMenuRequested(QMenu *menu, int row, int column);''')
edit(H, '  void showEvent(QShowEvent *event) override;', '''  bool viewportEvent(QEvent *event) override;
  void showEvent(QShowEvent *event) override;''')
edit(H, '  void scheduleRebuild();', '''  void hideHoverPreview();
  void showHoverPreview();
  QModelIndex thumbnailAt(const QPoint &position) const;
  bool activateExposure(QTreeWidgetItem *item, bool makeCurrent);
  void scheduleRebuild();''')
edit(C, '#include "toonz/tframehandle.h"', '#include "toonz/tframehandle.h"\n#include "toonz/tobjecthandle.h"')
edit(C, '#include <QPersistentModelIndex>', '''#include <QPersistentModelIndex>
#include <QCursor>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QScreen>
#include <QScopedValueRollback>
#include <QVBoxLayout>''')
edit(C, '  void setEditorData(QWidget *editor, const QModelIndex &index) const override {', '''  QRect thumbnailRect(const QStyleOptionViewItem &option,
                      const QModelIndex &index) const {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.decorationSize = QSize(32, 24);
    const QWidget *widget = opt.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    return style->subElementRect(QStyle::SE_ItemViewItemDecoration, &opt,
                                 widget);
  }

  void setEditorData(QWidget *editor, const QModelIndex &index) const override {''')
edit(C, '    , m_rebuildTimer(new QTimer(this)) {', '''    , m_rebuildTimer(new QTimer(this))
    , m_hoverTimer(new QTimer(this))
    , m_hoverPreview(new QFrame(this, Qt::ToolTip))
    , m_previewImage(new QLabel(m_hoverPreview))
    , m_previewCaption(new QLabel(m_hoverPreview)) {''')
edit(C, '  setObjectName("DrawingLayers");', '''  setObjectName("DrawingLayers");
  setMouseTracking(true);
  viewport()->setMouseTracking(true);
  m_hoverTimer->setObjectName("LayersHoverTimer");
  m_hoverTimer->setSingleShot(true);
  m_hoverTimer->setInterval(600);
  m_hoverPreview->setObjectName("LayersHoverPreview");
  m_hoverPreview->setAttribute(Qt::WA_ShowWithoutActivating);
  m_hoverPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_hoverPreview->setFocusPolicy(Qt::NoFocus);
  static_cast<QFrame *>(m_hoverPreview)->setFrameStyle(QFrame::StyledPanel);
  m_previewImage->setAlignment(Qt::AlignCenter);
  m_previewCaption->setAlignment(Qt::AlignCenter);
  m_previewCaption->setTextFormat(Qt::PlainText);
  m_previewCaption->setWordWrap(true);
  auto previewLayout = new QVBoxLayout(m_hoverPreview);
  previewLayout->setContentsMargins(8, 8, 8, 8);
  previewLayout->addWidget(m_previewImage);
  previewLayout->addWidget(m_previewCaption);
  connect(m_hoverTimer, &QTimer::timeout, this,
          &DrawingLayers::showHoverPreview);
  connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
          &DrawingLayers::hideHoverPreview);
  connect(horizontalScrollBar(), &QScrollBar::valueChanged, this,
          &DrawingLayers::hideHoverPreview);
  connect(this, &QTreeWidget::itemCollapsed, this,
          &DrawingLayers::hideHoverPreview);
  connect(qApp, &QGuiApplication::applicationStateChanged, this,
          [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) hideHoverPreview();
          });''')
edit(C, '  rebuild();\n}\n\nvoid DrawingLayers::hideEvent', '''  connect(IconGenerator::instance(), &IconGenerator::iconGenerated, this,
          [this] {
            if (m_hoverPreview->isVisible()) showHoverPreview();
          });
  rebuild();
}

void DrawingLayers::hideEvent''')
edit(C, 'void DrawingLayers::hideEvent(QHideEvent *event) {', '''void DrawingLayers::hideEvent(QHideEvent *event) {
  hideHoverPreview();
  disconnect(IconGenerator::instance(), nullptr, this, nullptr);''')
edit(C, 'void DrawingLayers::scheduleRebuild() {\n', 'void DrawingLayers::scheduleRebuild() {\n  hideHoverPreview();\n')
edit(C, 'void DrawingLayers::rebuild() {\n', 'void DrawingLayers::rebuild() {\n  hideHoverPreview();\n')
edit(C, '''void DrawingLayers::refreshCurrent() {
  if (m_rebuildTimer->isActive() ||''', '''void DrawingLayers::refreshCurrent() {
  if (m_activating) return;
  hideHoverPreview();
  if (m_rebuildTimer->isActive() ||''')
edit(C, 'void DrawingLayers::expandItem(QTreeWidgetItem *treeItem) {\n', 'void DrawingLayers::expandItem(QTreeWidgetItem *treeItem) {\n  hideHoverPreview();\n')
edit(C, '''  if (item->kind == Group || item->kind == Stroke) {
    selectVectorItem(item);
    return;
  }''', '''  if (item->kind == Group || item->kind == Stroke) {
    QPersistentModelIndex index(indexFromItem(item));
    if (!activateExposure(item, false) || !index.isValid()) return;
    selectVectorItem(itemFromIndex(index));
    if (index.isValid()) setCurrentItem(itemFromIndex(index));
    return;
  }''')
start = '  int frame = m_app->getCurrentFrame()->getFrame();\n  if (item->kind == Drawing) {'
end = '\nvoid DrawingLayers::beginRename(QTreeWidgetItem *treeItem) {'
p = ROOT / C
text = p.read_text(encoding='utf-8')
a, b = text.index(start), text.index(end)
text = text[:a] + '''  activateExposure(treeItem, true);
}

bool DrawingLayers::activateExposure(QTreeWidgetItem *treeItem,
                                     bool makeCurrent) {
  hideHoverPreview();
  if (!treeItem || m_rebuildTimer->isActive() || !m_xsheet ||
      m_xsheet != m_app->getCurrentXsheet()->getXsheet())
    return false;
  auto item = static_cast<LayerItem *>(treeItem);
  int c = item->data(Name, ColumnRole).toInt();
  TXshColumn *column = m_xsheet->getColumn(c);
  if (!column || column != item->column) return false;

  int frame = qMax(0, m_app->getCurrentFrame()->getFrame());
  // Copy the target before notifying handles: those notifications can rebuild
  // drawing children. A drawing/group/stroke must match the exact drawing,
  // not merely another exposure of the same level.
  if (item->kind != Column) {
    auto drawing = drawingParent(item);
    TXshLevel *level = item->level.data();
    TXshCellColumn *cells = column->getCellColumn();
    if (!level || !cells) return false;
    TFrameId fid = drawing ? drawing->fid : TFrameId();
    int first, last;
    cells->getRange(first, last);
    int nearest = -1;
    qint64 distance = std::numeric_limits<qint64>::max();
    for (int row = first; row <= last; ++row) {
      const TXshCell &cell = cells->getCell(row);
      if (cell.m_level.getPointer() != level ||
          (drawing && cell.m_frameId != fid))
        continue;
      qint64 delta = qAbs(qint64(row) - frame);
      if (delta < distance) {
        nearest = row;
        distance = delta;
      }
      if (delta == 0) break;
    }
    if (nearest < 0) return false;
    frame = nearest;
  }
  {
    QScopedValueRollback<bool> guard(m_activating, true);
    m_app->getCurrentSelection()->setSelection(nullptr);
    m_app->getCurrentColumn()->setColumnIndex(c);
    m_app->getCurrentFrame()->setFrame(frame);
    m_app->getCurrentObject()->setObjectId(TStageObjectId::ColumnId(c));
  }
  refreshCurrent();
  emit exposureActivated(frame, c, makeCurrent);
  return true;
}
''' + text[b:]
p.write_text(text, encoding='utf-8')
start = 'void DrawingLayers::contextMenuEvent(QContextMenuEvent *event) {'
end = '\nvoid DrawingLayers::keyPressEvent(QKeyEvent *event) {'
p = ROOT / C
text = p.read_text(encoding='utf-8')
a, b = text.index(start), text.index(end)
text = text[:a] + '''void DrawingLayers::contextMenuEvent(QContextMenuEvent *event) {
  hideHoverPreview();
  QTreeWidgetItem *item = itemAt(event->pos());
  if (event->reason() == QContextMenuEvent::Keyboard) item = currentItem();
  if (!item) return;
  QPersistentModelIndex index(indexFromItem(item));
  bool group = static_cast<LayerItem *>(item)->kind == Group;
  if (!activateExposure(item, true) || !index.isValid()) return;
  setCurrentItem(itemFromIndex(index));
  QMenu menu(this);
  QAction *rename = nullptr;
  if (group) {
    rename = menu.addAction(tr("Rename Group..."));
    menu.addSeparator();
  }
  emit exposureMenuRequested(&menu, m_app->getCurrentFrame()->getFrame(),
                            m_app->getCurrentColumn()->getColumnIndex());
  // Any scene/structure change while a menu is open invalidates its target.
  connect(m_app->getCurrentXsheet(), &TXsheetHandle::xsheetChanged,
          &menu, &QMenu::close);
  connect(m_app->getCurrentXsheet(), &TXsheetHandle::xsheetSwitched,
          &menu, &QMenu::close);
  connect(m_app->getCurrentScene(), &TSceneHandle::sceneSwitched,
          &menu, &QMenu::close);
  if (!menu.isEmpty()) {
    QPoint position = event->reason() == QContextMenuEvent::Keyboard
                          ? viewport()->mapToGlobal(
                                visualItemRect(itemFromIndex(index)).center())
                          : event->globalPos();
    QAction *chosen = menu.exec(position);
    if (rename && chosen == rename && index.isValid())
      beginRename(itemFromIndex(index));
  }
  event->accept();
}
''' + text[b:]
p.write_text(text, encoding='utf-8')
edit(C, 'void DrawingLayers::keyPressEvent(QKeyEvent *event) {\n', 'void DrawingLayers::keyPressEvent(QKeyEvent *event) {\n  hideHoverPreview();\n')
edit(C, '  QTreeWidget::keyPressEvent(event);\n}', '''  QTreeWidgetItem *before = currentItem();
  QTreeWidget::keyPressEvent(event);
  if (currentItem() != before &&
      (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down ||
       event->key() == Qt::Key_Home || event->key() == Qt::Key_End ||
       event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown))
    activateItem(currentItem(), Name);
}''')
with (ROOT / C).open('a', encoding='utf-8') as f:
    f.write('''

QModelIndex DrawingLayers::thumbnailAt(const QPoint &position) const {
  if (!isVisible() || m_rebuildTimer->isActive() || !m_xsheet ||
      m_xsheet != m_app->getCurrentXsheet()->getXsheet())
    return QModelIndex();
  QModelIndex index = indexAt(position);
  if (!index.isValid() || index.column() != Name ||
      index.data(KindRole).toInt() != Level)
    return QModelIndex();
  auto item = static_cast<LayerItem *>(itemFromIndex(index));
  int column = index.data(ColumnRole).toInt();
  int row = index.data(RowRole).toInt();
  if (m_xsheet->getColumn(column) != item->column || !item->level)
    return QModelIndex();
  const TXshCell &cell = m_xsheet->getCell(row, column);
  if (cell.isEmpty() || cell.m_level.getPointer() != item->level ||
      !cell.m_level->getSimpleLevel())
    return QModelIndex();
  QStyleOptionViewItem option = viewOptions();
  option.rect = visualRect(index);
  auto delegate = static_cast<LayersDelegate *>(itemDelegate());
  return delegate->thumbnailRect(option, index).contains(position)
             ? index
             : QModelIndex();
}

bool DrawingLayers::viewportEvent(QEvent *event) {
  switch (event->type()) {
  case QEvent::MouseMove: {
    auto mouse = static_cast<QMouseEvent *>(event);
    QModelIndex index = mouse->buttons() == Qt::NoButton
                           ? thumbnailAt(mouse->pos())
                           : QModelIndex();
    if (!index.isValid()) {
      hideHoverPreview();
    } else if (m_hoverIndex != index ||
               (mouse->pos() - m_hoverPosition).manhattanLength() >
                   QApplication::startDragDistance()) {
      hideHoverPreview();
      m_hoverIndex = index;
      m_hoverPosition = mouse->pos();
      m_hoverTimer->start();
    }
    break;
  }
  case QEvent::ToolTip:
    if (thumbnailAt(viewport()->mapFromGlobal(QCursor::pos())).isValid()) {
      event->accept();
      return true;
    }
    break;
  case QEvent::Leave:
  case QEvent::MouseButtonPress:
  case QEvent::MouseButtonDblClick:
  case QEvent::Wheel:
  case QEvent::Resize:
  case QEvent::Hide:
    hideHoverPreview();
    break;
  default:
    break;
  }
  return QTreeWidget::viewportEvent(event);
}

void DrawingLayers::hideHoverPreview() {
  if (m_hoverTimer) m_hoverTimer->stop();
  if (m_hoverPreview) m_hoverPreview->hide();
  m_hoverIndex = QPersistentModelIndex();
}

void DrawingLayers::showHoverPreview() {
  const QPoint position = viewport()->mapFromGlobal(QCursor::pos());
  if (!m_hoverIndex.isValid() || thumbnailAt(position) != m_hoverIndex ||
      QApplication::mouseButtons() != Qt::NoButton ||
      m_app->getCurrentFrame()->isPlaying()) {
    hideHoverPreview();
    return;
  }
  const int row = m_hoverIndex.data(RowRole).toInt();
  const int column = m_hoverIndex.data(ColumnRole).toInt();
  const TXshCell cell = m_xsheet->getCell(row, column);
  const QPoint anchor = viewport()->mapToGlobal(m_hoverPosition);
  QScreen *screen = QGuiApplication::screenAt(anchor);
  if (!screen) screen = QGuiApplication::primaryScreen();
  if (!screen) return;
  const QRect available = screen->availableGeometry();
  const QSize size(qMax(32, qMin(320, available.width() - 24)),
                   qMax(24, qMin(240, available.height() - 96)));
  const qreal ratio = screen->devicePixelRatio();
  const TDimension pixels(qRound(size.width() * ratio),
                           qRound(size.height() * ratio));
  // Responsive icons are cached at this size and generated asynchronously.
  // Do not resize the Filmstrip's global icon size or scale its tiny icon.
  QPixmap preview = IconGenerator::instance()->getResponsiveIcon(
      cell.m_level.getPointer(), cell.m_frameId, pixels);
  m_previewImage->setFixedSize(size);
  if (preview.isNull()) {
    m_previewImage->setText(tr("Loading preview..."));
  } else {
    preview.setDevicePixelRatio(ratio);
    m_previewImage->setPixmap(preview);
  }
  m_previewCaption->setFixedWidth(size.width());
  m_previewCaption->setText(
      tr("%1 — Drawing %2\\nXsheet frame %3, column %4")
          .arg(QString::fromStdWString(cell.m_level->getName()),
               QString::fromStdString(cell.m_frameId.expand()))
          .arg(row + 1)
          .arg(column + 1));
  m_hoverPreview->adjustSize();
  QPoint topLeft = anchor + QPoint(20, 20);
  if (topLeft.x() + m_hoverPreview->width() > available.right() + 1)
    topLeft.setX(anchor.x() - m_hoverPreview->width() - 12);
  if (topLeft.y() + m_hoverPreview->height() > available.bottom() + 1)
    topLeft.setY(anchor.y() - m_hoverPreview->height() - 12);
  topLeft.setX(qMax(available.left(),
                    qMin(topLeft.x(), available.right() + 1 -
                                            m_hoverPreview->width())));
  topLeft.setY(qMax(available.top(),
                    qMin(topLeft.y(), available.bottom() + 1 -
                                            m_hoverPreview->height())));
  QToolTip::hideText();
  m_hoverPreview->move(topLeft);
  m_hoverPreview->show();
}
''')

XH = 'toonz/sources/toonz/xshcellviewer.h'
menu_decl = '''  /*! Creates the right-click menu that appears when clicking on a cell,
      distinguishing between the two cases: full cell, empty cell. */
  void createCellMenu(QMenu &menu, bool isCellSelected, TXshCell cell, int row,
                      int col);
'''
edit(XH, menu_decl, '')
edit(XH, '  void hideRenameField() { m_renameCell->hide(); }',
     '  void hideRenameField() { m_renameCell->hide(); }\n\n' + menu_decl)

write('toonz/sources/toonz/drawinglayersxsheet.cpp', '''#include "drawinglayers.h"

#include "cellselection.h"
#include "keyframeselection.h"
#include "tapp.h"
#include "xsheetviewer.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonzqt/tselectionhandle.h"

#include <QApplication>
#include <QMenu>
#include <QPointer>

namespace {

// Own the fallback for layouts containing Layers but no Xsheet/Timeline. It
// stays hidden and supplies the same native selection and menu implementation.
class LayersXsheetBridge final : public QObject {
  TApp *m_app;
  DrawingLayers *m_layers;
  QPointer<XsheetViewer> m_fallback;

  QList<XsheetViewer *> visibleViewers() const {
    QList<XsheetViewer *> result;
    for (QWidget *widget : QApplication::allWidgets()) {
      auto viewer = qobject_cast<XsheetViewer *>(widget);
      if (viewer && viewer->isVisible() &&
          viewer->getXsheet() == m_app->getCurrentXsheet()->getXsheet())
        result.append(viewer);
    }
    return result;
  }

  XsheetViewer *viewer() {
    auto viewers = visibleViewers();
    XsheetViewer *current = m_app->getCurrentXsheetViewer();
    if (viewers.contains(current)) return current;
    if (!viewers.isEmpty()) return viewers.front();
    if (!m_fallback) {
      m_fallback = new XsheetViewer(m_layers);
      m_fallback->setObjectName("LayersExposureMenuXsheet");
      m_fallback->hide();
    }
    return m_fallback;
  }

  void selectExposure(int row, int column, bool makeCurrent) {
    XsheetViewer *target = viewer();
    auto viewers = visibleViewers();
    if (!viewers.contains(target)) viewers.append(target);
    for (XsheetViewer *view : viewers) {
      view->getKeyframeSelection()->selectNone();
      view->getCellSelection()->selectCell(row, column);
      if (view->isVisible()) view->scrollTo(row, column);
      view->updateCells();
      view->updateColumns();
    }
    if (makeCurrent) {
      m_app->setCurrentXsheetViewer(target);
      target->getCellSelection()->makeCurrent();
      m_app->getCurrentSelection()->notifySelectionChanged();
    }
  }

public:
  LayersXsheetBridge(TApp *app, DrawingLayers *layers)
      : QObject(layers), m_app(app), m_layers(layers) {
    connect(layers, &DrawingLayers::exposureActivated, this,
            [this](int row, int column, bool makeCurrent) {
              selectExposure(row, column, makeCurrent);
            });
    connect(layers, &DrawingLayers::exposureMenuRequested, this,
            [this](QMenu *menu, int row, int column) {
              selectExposure(row, column, true);
              XsheetViewer *target = viewer();
              auto area = target->findChild<XsheetGUI::CellArea *>();
              if (!area) return;
              const TXshCell cell = target->getXsheet()->getCell(row, column);
              area->createCellMenu(*menu, !cell.isEmpty(), cell, row, column);
            });
  }

  ~LayersXsheetBridge() override {
    if (!m_fallback) return;
    if (m_app->getCurrentSelection()->getSelection() ==
        m_fallback->getCellSelection())
      m_app->getCurrentSelection()->setSelection(nullptr);
    if (m_app->getCurrentXsheetViewer() == m_fallback)
      m_app->setCurrentXsheetViewer(nullptr);
  }
};

}  // namespace

DrawingLayers::DrawingLayers(TApp *app, QWidget *parent)
    : DrawingLayers(static_cast<TApplication *>(app), parent) {
  new LayersXsheetBridge(app, this);
}
''')
edit('toonz/sources/toonz/CMakeLists.txt', '    drawinglayers.cpp\n',
     '    drawinglayers.cpp\n    drawinglayersxsheet.cpp\n')

TEST = '.github/ot-dev/layers-tests/layers_test.cpp'
edit(TEST, '#include <QTimer>', '#include <QTimer>\n#include <QSignalSpy>')
edit(TEST, '  auto frame = expand(panel);\n  auto outer = frame->child(1);', '''  QSignalSpy activated(&panel, &DrawingLayers::exposureActivated);
  QSignalSpy menuRequested(&panel, &DrawingLayers::exposureMenuRequested);
  auto layerItem = panel.topLevelItem(0)->child(0);
  app.frame.setFrame(12);
  events();
  QTest::mouseClick(panel.viewport(), Qt::LeftButton, Qt::NoModifier,
                    panel.visualItemRect(layerItem).center());
  CHECK(app.frame.isEditingScene());
  CHECK(app.frame.getFrame() == 0);
  CHECK(app.column.getColumnIndex() == 0);
  CHECK(!activated.isEmpty());
  CHECK(activated.last().at(0).toInt() == 0);
  CHECK(activated.last().at(1).toInt() == 0);
  CHECK(activated.last().at(2).toBool());
  CHECK(!app.scene.getDirtyFlag());
  // Thumbnail rendering remains outside this non-GPU harness.
  auto timer = panel.findChild<QTimer *>("LayersHoverTimer");
  CHECK(timer && timer->isSingleShot() && timer->interval() == 600);
  auto popup = panel.findChild<QWidget *>("LayersHoverPreview");
  CHECK(popup && !popup->isVisible());
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(panel.viewport(), &leave);
  CHECK(!timer->isActive() && !popup->isVisible());
  auto frame = expand(panel);
  auto outer = frame->child(1);''')
edit(TEST, '  commitEditor(panel, "Renamed");\n', '''  CHECK(!menuRequested.isEmpty());
  CHECK(menuRequested.last().at(1).toInt() == 0);
  CHECK(menuRequested.last().at(2).toInt() == 0);
  commitEditor(panel, "Renamed");
''')

write('doc/layers-preview-and-exposure-menu.md', '''# Layers: hover preview and exposure commands

This extends PR #155 without changing native group names or PLI metadata.

## Behavior

- Leave the pointer over a level thumbnail for 600 ms to display a 320 x 240
  logical-pixel preview. High-DPI screens request correspondingly sized native
  responsive icons. The caption identifies the level, drawing and exposure.
- Hovering never selects a layer, changes the current frame, enters a vector
  group, or modifies the drawing. The popup is non-focusing, stays on screen,
  and disappears on leaving the thumbnail, clicking, scrolling, keyboard
  navigation, hiding the panel, or a relevant model/frame change.
- Click a column/level/drawing to select the corresponding native exposure cell
  and stage object. Levels use their closest exposure; drawings and vector
  descendants require an exact drawing ID match. Visible Xsheet and Timeline
  panels scroll to the selected exposure.
- Group/stroke selection still uses the native Vector Selection tool and editing
  depth; browsing the tree does not enter groups. Its hosting exposure is also
  reflected in visible Xsheet/Timeline panels.
- Right-click a row (or use the keyboard context-menu key) to target its exposure
  and show the actual Xsheet/Timeline cell menu. Native command availability,
  level-type checks and command implementations are reused. Group rows retain
  Rename Group as their first item. This is an exposure menu, not a promise to
  apply cell commands to an individual vector stroke.
- A Layers-only layout lazily owns a hidden native Xsheet viewer to supply its
  selection and commands. The fallback is not a new visible panel.

## Regression checks

The existing `layers-integration` harness adds checks for exposure targeting,
non-dirty navigation, hover timer cancellation and context-menu forwarding.
Rendering remains outside that non-GPU harness. Existing native group/PLI tests
remain unchanged in scope.

Manual application acceptance is still required:

1. Hover Vector, Toonz Raster and full-color raster thumbnails on standard/high
   DPI displays; confirm larger artwork, correct captions and no selection.
2. Leave/click/scroll before and after the delay; switch scenes, frames and rooms
   while the pointer is stationary; confirm that no old preview remains.
3. In both Xsheet orientations, select a different layer, a repeated exposure,
   a drawing and a nested group. Check the active object and selected cell;
   check the existing group editing-depth guards.
4. Compare exposure menus with native right-click for raster, vector, sound,
   Sub-Xsheet, locked and empty columns. Exercise Copy, Paste, Duplicate,
   Replace Level, Cell Mark and undo against the intended cell only.
5. Keep both Xsheet and Timeline visible; then try a Layers-only room and close
   the panel. Confirm synchronized cells and safe fallback cleanup.

No Windows runtime, GPU-preview or manual acceptance pass is implied by these
source changes or by the non-GPU integration checks.
''')
print('Applied Layers preview, native exposure selection/menu adapter, and tests.')
