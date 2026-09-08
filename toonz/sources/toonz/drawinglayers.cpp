#include "drawinglayers.h"

#include "toonz/tapplication.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/tscenehandle.h"
#include "toonz/tstageobject.h"
#include "toonz/txshcell.h"
#include "toonz/txshcolumn.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/preferences.h"
#include "toonzqt/gutil.h"
#include "toonzqt/icongenerator.h"
#include "toonzqt/tselectionhandle.h"
#include "tools/toolhandle.h"
#include "tools/tool.h"
#include "tools/strokeselection.h"
#include "tstroke.h"

#include <QApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolTip>
#include <QMutexLocker>

#include <limits>
#include <vector>

namespace {

enum Section { Name, View, Render, Lock };
enum Role { StateRole = Qt::UserRole, ColumnRole, RowRole, KindRole };
enum Kind { Column, Level, Drawing, Group, Stroke };

class LayerItem final : public QTreeWidgetItem {
public:
  TXshColumn *column;
  QPointer<TXshLevel> level;
  int firstRow;
  Kind kind;
  TFrameId fid;
  TVectorImageP image;
  std::vector<int> structure;
  int firstStroke = -1, lastStroke = -1, depth = 0;

  LayerItem(TXshColumn *column, TXshLevel *level = nullptr, int row = -1)
      : column(column)
      , level(level)
      , firstRow(row)
      , kind(level ? Level : Column) {
    setData(Name, KindRole, kind);
    setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    setSizeHint(Name, QSize(0, 52));
  }

  LayerItem(LayerItem *parent, Kind type)
      : LayerItem(parent->column, parent->level, parent->firstRow) {
    kind = type;
    setData(Name, KindRole, kind);
    setData(Name, ColumnRole, parent->data(Name, ColumnRole));
    setData(Name, RowRole, parent->data(Name, RowRole));
    fid = parent->fid;
    setSizeHint(Name, QSize(0, 28));
  }
};

// These are transient locators, not persistent group identities. Check the
// complete structure before using an index after a model/tool notification.
std::vector<int> vectorStructure(const TVectorImageP &image) {
  std::vector<int> result;
  result.reserve(3 * image->getStrokeCount());
  for (UINT s = 0; s < image->getStrokeCount(); ++s) {
    result.push_back(image->getStroke(s)->getId());
    result.push_back(image->getGroupDepth(s));
    result.push_back(s ? image->getCommonGroupDepth(s - 1, s) : 0);
  }
  return result;
}

LayerItem *drawingParent(LayerItem *item) {
  while (item && item->kind != Drawing)
    item = static_cast<LayerItem *>(item->parent());
  return item;
}

struct ExpandedLevel {
  bool drawingExpanded = false;
  TFrameId fid;
  TVectorImageP image;
  std::vector<int> structure;
  QSet<QPair<int, int>> groups;
};

QString levelType(TXshLevel *level) {
  switch (level->getType()) {
  case PLI_XSHLEVEL:
    return DrawingLayers::tr("Vector Level");
  case TZP_XSHLEVEL:
    return DrawingLayers::tr("Toonz Raster Level");
  case OVL_XSHLEVEL:
  case TZI_XSHLEVEL:
    return DrawingLayers::tr("Raster Level");
  case CHILD_XSHLEVEL:
    return DrawingLayers::tr("Sub-Xsheet");
  case SND_XSHLEVEL:
    return DrawingLayers::tr("Sound Level");
  case SND_TXT_XSHLEVEL:
    return DrawingLayers::tr("Note Level");
  case PLT_XSHLEVEL:
    return DrawingLayers::tr("Palette Level");
  case ZERARYFX_XSHLEVEL:
    return DrawingLayers::tr("FX Level");
  case MESH_XSHLEVEL:
    return DrawingLayers::tr("Mesh Level");
  default:
    return DrawingLayers::tr("Level");
  }
}

class LayersDelegate final : public QStyledItemDelegate {
  TApplication *m_app;

public:
  LayersDelegate(TApplication *app, QObject *parent)
      : QStyledItemDelegate(parent), m_app(app) {}

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    if (index.column() == Name) {
      if (index.data(KindRole).toInt() == Level) {
        TXsheet *xsheet = m_app->getCurrentXsheet()->getXsheet();
        if (xsheet) {
          const TXshCell &cell = xsheet->getCell(
              index.data(RowRole).toInt(), index.data(ColumnRole).toInt());
          if (!cell.isEmpty() && cell.m_level->getSimpleLevel()) {
            QPixmap thumbnail = IconGenerator::instance()->getIcon(
                cell.m_level.getPointer(), cell.m_frameId, false);
            if (!thumbnail.isNull()) opt.icon = QIcon(thumbnail);
          }
        }
      }
      const QWidget *widget = opt.widget;
      QStyle *style         = widget ? widget->style() : QApplication::style();
      style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
      return;
    }

    QStyledItemDelegate::paint(painter, opt, index);
    QVariant state = index.data(StateRole);
    if (!state.isValid()) return;
    painter->save();
    QColor color = opt.palette.color((opt.state & QStyle::State_Selected)
                                         ? QPalette::HighlightedText
                                         : QPalette::Text);
    if (!state.toBool()) painter->setOpacity(0.35);
    QRect rect = QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter,
                                     QSize(18, 18), opt.rect);
    if (index.column() == View) {
      painter->setRenderHint(QPainter::Antialiasing);
      painter->setPen(QPen(color, 1.5));
      painter->setBrush(Qt::NoBrush);
      QPainterPath eye;
      eye.moveTo(rect.left(), rect.center().y());
      eye.quadTo(rect.center().x(), rect.top() - 3, rect.right(),
                 rect.center().y());
      eye.quadTo(rect.center().x(), rect.bottom() + 3, rect.left(),
                 rect.center().y());
      painter->drawPath(eye);
      painter->drawEllipse(QPointF(rect.center()), 3, 3);
    } else {
      createQIcon(index.column() == Render ? "render" : "lock")
          .paint(painter, rect, Qt::AlignCenter, QIcon::Normal,
                 state.toBool() ? QIcon::On : QIcon::Off);
    }
    painter->restore();
  }
};

}  // namespace

DrawingLayers::DrawingLayers(TApplication *app, QWidget *parent)
    : QTreeWidget(parent)
    , m_app(app)
    , m_xsheet(nullptr)
    , m_rebuildTimer(new QTimer(this)) {
  setObjectName("DrawingLayers");
  setAccessibleName(tr("Drawing Layers"));
  setColumnCount(4);
  setHeaderLabels({tr("Drawing Layers"), QString(), QString(), QString()});
  headerItem()->setIcon(View, createQIcon("viewer"));
  headerItem()->setIcon(Render, createQIcon("render"));
  headerItem()->setIcon(Lock, createQIcon("lock"));
  headerItem()->setToolTip(Name,
                           tr("Columns in reverse Xsheet order. Expand a "
                              "column to see its levels. Expand a vector "
                              "drawing to inspect its groups and strokes."));
  headerItem()->setToolTip(View, tr("Visible in the Viewer"));
  headerItem()->setToolTip(Render, tr("Visible in Preview and Render"));
  headerItem()->setToolTip(Lock, tr("Lock Column"));
  header()->setStretchLastSection(false);
  header()->setSectionResizeMode(Name, QHeaderView::Stretch);
  for (int section = View; section <= Lock; ++section) {
    header()->setSectionResizeMode(section, QHeaderView::Fixed);
    setColumnWidth(section, 30);
  }
  setIconSize(QSize(48, 36));
  setIndentation(18);
  setUniformRowHeights(false);
  setRootIsDecorated(true);
  setAllColumnsShowFocus(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setSelectionBehavior(QAbstractItemView::SelectRows);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setItemDelegate(new LayersDelegate(app, this));
  m_rebuildTimer->setSingleShot(true);
  connect(m_rebuildTimer, &QTimer::timeout, this, &DrawingLayers::rebuild);
  connect(this, &QTreeWidget::itemClicked, this, &DrawingLayers::activateItem);
  connect(this, &QTreeWidget::itemExpanded, this, &DrawingLayers::expandItem);
}

void DrawingLayers::showEvent(QShowEvent *event) {
  QTreeWidget::showEvent(event);
  connect(m_app->getCurrentXsheet(), &TXsheetHandle::xsheetChanged, this,
          &DrawingLayers::scheduleRebuild);
  connect(m_app->getCurrentXsheet(), &TXsheetHandle::xsheetSwitched, this,
          &DrawingLayers::scheduleRebuild);
  connect(m_app->getCurrentScene(), &TSceneHandle::sceneSwitched, this,
          &DrawingLayers::scheduleRebuild);
  connect(m_app->getCurrentScene(), &TSceneHandle::sceneChanged, this,
          &DrawingLayers::scheduleRebuild);
  connect(m_app->getCurrentScene(), &TSceneHandle::castChanged, this,
          &DrawingLayers::scheduleRebuild);
  connect(m_app->getCurrentLevel(), &TXshLevelHandle::xshLevelTitleChanged,
          this, &DrawingLayers::scheduleRebuild);
  connect(m_app->getCurrentLevel(), &TXshLevelHandle::xshLevelChanged, this,
          &DrawingLayers::scheduleRebuild);
  connect(m_app->getCurrentLevel(), &TXshLevelHandle::xshLevelViewChanged, this,
          &DrawingLayers::scheduleRebuild);
  connect(m_app->getCurrentLevel(), &TXshLevelHandle::xshLevelSwitched, this,
          &DrawingLayers::refreshCurrent);
  connect(m_app->getCurrentColumn(), &TColumnHandle::columnIndexSwitched, this,
          &DrawingLayers::refreshCurrent);
  connect(m_app->getCurrentFrame(), &TFrameHandle::frameSwitched, this,
          &DrawingLayers::refreshCurrent);
  connect(IconGenerator::instance(), &IconGenerator::iconGenerated, viewport(),
          QOverload<>::of(&QWidget::update));
  rebuild();
}

void DrawingLayers::hideEvent(QHideEvent *event) {
  disconnect(m_app->getCurrentXsheet(), nullptr, this, nullptr);
  disconnect(m_app->getCurrentScene(), nullptr, this, nullptr);
  disconnect(m_app->getCurrentLevel(), nullptr, this, nullptr);
  disconnect(m_app->getCurrentColumn(), nullptr, this, nullptr);
  disconnect(m_app->getCurrentFrame(), nullptr, this, nullptr);
  disconnect(IconGenerator::instance(), nullptr, viewport(), nullptr);
  m_rebuildTimer->stop();
  QTreeWidget::hideEvent(event);
}

void DrawingLayers::scheduleRebuild() {
  if (isVisible()) m_rebuildTimer->start();
}

void DrawingLayers::rebuild() {
  m_rebuildTimer->stop();
  QSignalBlocker blocker(this);
  TXsheet *xsheet = m_app->getCurrentXsheet()->getXsheet();
  QMap<TXshColumn *, bool> expanded;
  QMap<QPair<TXshColumn *, TXshLevel *>, ExpandedLevel> expandedLevels;
  if (xsheet == m_xsheet)
    for (int i = 0; i < topLevelItemCount(); ++i) {
      auto item              = static_cast<LayerItem *>(topLevelItem(i));
      expanded[item->column] = item->isExpanded();
      for (int j = 0; j < item->childCount(); ++j) {
        auto level = static_cast<LayerItem *>(item->child(j));
        if (!level->isExpanded()) continue;
        ExpandedLevel &state = expandedLevels[{item->column, level->level}];
        if (!level->childCount()) continue;
        auto drawing          = static_cast<LayerItem *>(level->child(0));
        state.drawingExpanded = drawing->isExpanded();
        state.fid             = drawing->fid;
        state.image           = drawing->image;
        state.structure       = drawing->structure;
        QList<QTreeWidgetItem *> pending;
        for (int k = 0; k < drawing->childCount(); ++k)
          pending.append(drawing->child(k));
        while (!pending.isEmpty()) {
          auto child = static_cast<LayerItem *>(pending.takeLast());
          if (child->kind != Group || !child->isExpanded()) continue;
          state.groups.insert({child->firstStroke, child->depth});
          for (int k = 0; k < child->childCount(); ++k)
            pending.append(child->child(k));
        }
      }
    }
  int scroll = verticalScrollBar()->value();
  clear();
  m_xsheet = xsheet;
  if (!xsheet) return;

  for (int c = xsheet->getColumnCount() - 1; c >= 0; --c) {
    TXshColumn *column = xsheet->getColumn(c);
    if (!column) continue;
    auto group = new LayerItem(column);
    group->setData(Name, ColumnRole, c);
    TStageObject *object = xsheet->getStageObject(TStageObjectId::ColumnId(c));
    QString name         = object->hasSpecifiedName()
                               ? QString::fromStdString(object->getName())
                               : tr("Col %1").arg(c + 1);
    group->setText(Name, name);
    group->setIcon(Name, createQIcon("folder"));
    group->setToolTip(Name, tr("Xsheet column %1").arg(c + 1));
    addTopLevelItem(group);

    QSet<TXshLevel *> levels;
    TXshCellColumn *cells = column->getCellColumn();
    int first = 0, last = -1;
    if (cells) cells->getRange(first, last);
    for (int row = first; row <= last; ++row) {
      const TXshCell &cell = cells->getCell(row);
      TXshLevel *level     = cell.m_level.getPointer();
      if (!level || levels.contains(level)) continue;
      levels.insert(level);
      auto item = new LayerItem(column, level, row);
      item->setData(Name, ColumnRole, c);
      item->setData(Name, RowRole, row);
      item->setIcon(
          Name, createQIcon(level->getChildLevel() ? "folder" : "level_strip"));
      group->addChild(item);
      if (level->getType() == PLI_XSHLEVEL) {
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        item->setExpanded(expandedLevels.contains({column, level}));
      }
    }
    group->setExpanded(expanded.value(column, true));
  }
  refreshCurrent();
  for (int i = 0; i < topLevelItemCount(); ++i) {
    auto column = static_cast<LayerItem *>(topLevelItem(i));
    for (int j = 0; j < column->childCount(); ++j) {
      auto level = static_cast<LayerItem *>(column->child(j));
      auto it    = expandedLevels.constFind({column->column, level->level});
      if (it == expandedLevels.cend() || !level->childCount() ||
          !it->drawingExpanded)
        continue;
      auto drawing = static_cast<LayerItem *>(level->child(0));
      drawing->setExpanded(true);
      expandItem(drawing);
      if (drawing->fid != it->fid || drawing->image != it->image ||
          drawing->structure != it->structure)
        continue;
      QList<QTreeWidgetItem *> pending;
      for (int k = 0; k < drawing->childCount(); ++k)
        pending.append(drawing->child(k));
      while (!pending.isEmpty()) {
        auto child = static_cast<LayerItem *>(pending.takeLast());
        if (child->kind != Group ||
            !it->groups.contains({child->firstStroke, child->depth}))
          continue;
        child->setExpanded(true);
        expandItem(child);
        for (int k = 0; k < child->childCount(); ++k)
          pending.append(child->child(k));
      }
    }
  }
  verticalScrollBar()->setValue(scroll);
}

void DrawingLayers::refreshCurrent() {
  if (m_rebuildTimer->isActive() ||
      m_xsheet != m_app->getCurrentXsheet()->getXsheet() || !m_xsheet)
    return;
  QSignalBlocker blocker(this);
  int frame                 = m_app->getCurrentFrame()->getFrame();
  bool editingScene         = m_app->getCurrentFrame()->isEditingScene();
  int currentColumn         = m_app->getCurrentColumn()->getColumnIndex();
  QTreeWidgetItem *selected = nullptr;
  for (int i = 0; i < topLevelItemCount(); ++i) {
    auto group         = static_cast<LayerItem *>(topLevelItem(i));
    int c              = group->data(Name, ColumnRole).toInt();
    TXshColumn *column = m_xsheet->getColumn(c);
    if (column != group->column) continue;
    bool hasVisibility = !column->isEmpty() && !column->getPaletteColumn() &&
                         !column->getSoundTextColumn();
    group->setData(
        View, StateRole,
        hasVisibility ? QVariant(column->isCamstandVisible()) : QVariant());
    group->setData(
        Render, StateRole,
        hasVisibility ? QVariant(column->isPreviewVisible()) : QVariant());
    group->setData(Lock, StateRole, column->isLocked());
    for (int section = View; section <= Lock; ++section) {
      QString status =
          group->data(section, StateRole).toBool() ? tr("On") : tr("Off");
      QString text = headerItem()->toolTip(section) + ": " + status;
      group->setToolTip(section, text);
      group->setData(section, Qt::AccessibleTextRole, text);
    }
    if (editingScene && c == currentColumn) selected = group;
    const TXshCell &current = m_xsheet->getCell(frame, c);
    for (int j = 0; j < group->childCount(); ++j) {
      auto item        = static_cast<LayerItem *>(group->child(j));
      TXshLevel *level = item->level.data();
      if (!level) continue;
      bool active = editingScene && current.m_level.getPointer() == level;
      item->setData(Name, RowRole, active ? frame : item->firstRow);
      QString detail = levelType(level);
      if (active)
        detail += tr(" - Drawing %1")
                      .arg(QString::fromStdString(current.m_frameId.expand()));
      item->setText(Name,
                    QString::fromStdWString(level->getName()) + "\n" + detail);
      item->setToolTip(Name,
                       tr("%1\nFirst exposed at frame %2. Click to select "
                          "the nearest exposure.")
                           .arg(levelType(level))
                           .arg(item->firstRow + 1));
      QFont font = item->font(Name);
      font.setBold(active);
      item->setFont(Name, font);
      item->setForeground(
          Name, active ? QBrush()
                       : palette().brush(QPalette::Disabled, QPalette::Text));
      if (active && c == currentColumn && group->isExpanded()) selected = item;
      refreshDrawing(item);
    }
  }
  setCurrentItem(selected);
  viewport()->update();
}

void DrawingLayers::refreshDrawing(QTreeWidgetItem *treeItem) {
  auto levelItem = static_cast<LayerItem *>(treeItem);
  if (!levelItem->level || levelItem->level->getType() != PLI_XSHLEVEL ||
      (!levelItem->isExpanded() && !levelItem->childCount()))
    return;
  int c                = levelItem->data(Name, ColumnRole).toInt();
  int row              = levelItem->data(Name, RowRole).toInt();
  const TXshCell &cell = m_xsheet->getCell(row, c);
  if (cell.m_level.getPointer() != levelItem->level) return;
  TFrameId fid = cell.m_frameId;
  if (m_app->getCurrentFrame()->isEditingLevel() &&
      m_app->getCurrentLevel()->getLevel() == levelItem->level)
    fid = m_app->getCurrentFrame()->getFid();

  auto drawing  = levelItem->childCount()
                      ? static_cast<LayerItem *>(levelItem->child(0))
                      : nullptr;
  bool expanded = drawing && drawing->isExpanded();
  if (drawing && drawing->fid != fid) {
    delete drawing;
    drawing = nullptr;
  }
  if (!drawing) {
    drawing      = new LayerItem(levelItem, Drawing);
    drawing->fid = fid;
    drawing->setText(
        Name, tr("Drawing %1").arg(QString::fromStdString(fid.expand())));
    drawing->setToolTip(Name, tr("Groups belong to this drawing, not the whole "
                                 "level. Expanding only inspects the drawing; "
                                 "it does not enter a group for editing."));
    drawing->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    levelItem->addChild(drawing);
    drawing->setExpanded(expanded);
  }
  drawing->setData(Name, RowRole, row);
  if (expanded && levelItem->isExpanded()) expandItem(drawing);
}

void DrawingLayers::expandItem(QTreeWidgetItem *treeItem) {
  if (!treeItem || m_rebuildTimer->isActive() || !m_xsheet ||
      m_xsheet != m_app->getCurrentXsheet()->getXsheet())
    return;
  auto item = static_cast<LayerItem *>(treeItem);
  if (item->kind == Level) {
    refreshDrawing(item);
    return;
  }
  if ((item->kind != Drawing && item->kind != Group) || item->childCount())
    return;
  auto drawing = drawingParent(item);
  TXshSimpleLevel *level =
      drawing->level ? drawing->level->getSimpleLevel() : nullptr;
  if (!level) return;
  TVectorImageP image = level->getFrame(drawing->fid, false);
  if (!image) {
    item->setToolTip(Name, tr("This drawing could not be loaded."));
    return;
  }
  QMutexLocker locker(image->getMutex());
  if (drawing->image && (drawing->image != image ||
                         drawing->structure != vectorStructure(image))) {
    scheduleRebuild();
    return;
  }
  if (!drawing->image) {
    drawing->image       = image;
    drawing->structure   = vectorStructure(image);
    drawing->firstStroke = 0;
    drawing->lastStroke  = int(image->getStrokeCount()) - 1;
  }

  QList<QTreeWidgetItem *> children;
  for (int s = item->firstStroke; s <= item->lastStroke;) {
    int last     = s;
    bool grouped = image->getGroupDepth(s) > item->depth;
    if (grouped)
      while (last < item->lastStroke &&
             image->getCommonGroupDepth(s, last + 1) > item->depth)
        ++last;
    auto child         = new LayerItem(item, grouped ? Group : Stroke);
    child->firstStroke = s;
    child->lastStroke  = last;
    child->depth       = item->depth + (grouped ? 1 : 0);
    if (grouped) {
      child->setText(Name, tr("Group (%1 strokes)").arg(last - s + 1));
      child->setIcon(Name, createQIcon("folder"));
      child->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
      child->setToolTip(Name, tr("Vector group containing strokes %1–%2. "
                                 "Click to select it at the current editing "
                                 "depth. Enter its parent groups in the Viewer "
                                 "to select deeper content.")
                                  .arg(s + 1)
                                  .arg(last + 1));
    } else {
      TStroke *stroke = image->getStroke(s);
      child->setText(
          Name, tr("Stroke %1 · Style %2").arg(s + 1).arg(stroke->getStyle()));
      child->setToolTip(Name, tr("%1\n%2 control points. Click to select this "
                                 "stroke after entering its group in the "
                                 "Viewer.")
                                  .arg(stroke->isSelfLoop() ? tr("Closed path")
                                                            : tr("Open path"))
                                  .arg(stroke->getControlPointCount()));
    }
    children.prepend(child);
    s = last + 1;
  }
  item->addChildren(children);
  item->setChildIndicatorPolicy(
      QTreeWidgetItem::DontShowIndicatorWhenChildless);
  if (item->kind == Drawing && children.isEmpty())
    item->setToolTip(Name, tr("This vector drawing has no strokes."));
}

void DrawingLayers::selectVectorItem(QTreeWidgetItem *treeItem) {
  auto item    = static_cast<LayerItem *>(treeItem);
  auto drawing = drawingParent(item);
  if (!drawing || !drawing->level || !drawing->image) return;
  int c                = drawing->data(Name, ColumnRole).toInt();
  TFrameHandle *frame  = m_app->getCurrentFrame();
  const TXshCell &cell = m_xsheet->getCell(frame->getFrame(), c);
  bool active          = frame->isEditingScene()
                             ? m_app->getCurrentColumn()->getColumnIndex() == c &&
                          cell.m_level.getPointer() == drawing->level &&
                          cell.m_frameId == drawing->fid
                             : m_app->getCurrentLevel()->getLevel() == drawing->level &&
                          frame->getFid() == drawing->fid;
  auto explain         = [this, item](const QString &text) {
    QToolTip::showText(viewport()->mapToGlobal(visualItemRect(item).center()),
                               text, this);
  };
  if (!active) {
    explain(tr("Select this drawing in the Xsheet or Filmstrip first."));
    return;
  }
  if (frame->isEditingScene() && m_xsheet->getColumn(c)->isLocked()) {
    explain(tr("Unlock this column to select its vector content."));
    return;
  }
  auto selection = dynamic_cast<StrokeSelection *>(
      m_app->getCurrentSelection()->getSelection());
  // Navigation can clear the current selection without changing the active
  // tool. In that case recover its real selection, not a new panel-owned one.
  if (!selection && !m_app->getCurrentSelection()->getSelection()) {
    ToolHandle *handle = m_app->getCurrentTool();
    TTool *tool        = handle ? handle->getTool() : nullptr;
    if (tool) selection = dynamic_cast<StrokeSelection *>(tool->getSelection());
  }
  if (!selection) {
    explain(
        tr("Activate the Vector Selection tool to select groups or strokes."));
    return;
  }
  TXshSimpleLevel *level = drawing->level->getSimpleLevel();
  if (!level) return;
  TVectorImageP image = level->getFrame(drawing->fid, false);
  if (!image) return;
  QMutexLocker locker(image->getMutex());
  if (image != drawing->image || drawing->structure != vectorStructure(image)) {
    scheduleRebuild();
    return;
  }
  int s             = item->firstStroke;
  int expectedDepth = image->isInsideGroup() + (item->kind == Group ? 1 : 0);
  // selectable() distinguishes subgroup selection. It is false for a direct
  // stroke in the entered group, which the native tool selects individually.
  if (item->depth != expectedDepth || !image->isEnteredGroupStroke(s) ||
      (item->kind == Group && !image->selectable(s))) {
    explain(
        tr("Use Enter Group or Exit Group in the Viewer to edit at this "
           "depth. Expanding this tree does not change the editing depth."));
    return;
  }
  // Keep the real tool-owned selection: an independent StrokeSelection would
  // not update the Viewer's bounds, commands or transform handles.
  selection->selectNone();
  selection->setImage(image);
  for (; s <= item->lastStroke; ++s) selection->select(s, true);
  locker.unlock();
  m_app->getCurrentSelection()->setSelection(selection);
  selection->notifyView();
  m_app->getCurrentSelection()->notifySelectionChanged();
}

void DrawingLayers::activateItem(QTreeWidgetItem *treeItem, int section) {
  if (!treeItem || m_rebuildTimer->isActive() || !m_xsheet ||
      m_xsheet != m_app->getCurrentXsheet()->getXsheet())
    return;
  auto item          = static_cast<LayerItem *>(treeItem);
  int c              = item->data(Name, ColumnRole).toInt();
  TXshColumn *column = m_xsheet->getColumn(c);
  if (!column || column != item->column) return;
  if (item->kind == Group || item->kind == Stroke) {
    selectVectorItem(item);
    return;
  }
  if (item->parent()) section = Name;
  if (section != Name) {
    if (!item->data(section, StateRole).isValid()) return;
    bool on = !item->data(section, StateRole).toBool();
    if (section == Lock)
      column->lock(on);
    else {
      if (section == View ||
          Preferences::instance()->isUnifyColumnVisibilityTogglesEnabled())
        column->setCamstandVisible(on);
      if (section == Render ||
          Preferences::instance()->isUnifyColumnVisibilityTogglesEnabled())
        column->setPreviewVisible(on);
      if (column->getSoundColumn())
        m_app->getCurrentXsheet()->notifyXsheetSoundChanged();
    }
    ToolHandle *handle = m_app->getCurrentTool();
    TTool *tool        = handle ? handle->getTool() : nullptr;
    if (tool && tool->getViewer()) tool->getViewer()->invalidateToolStatus();
    m_app->getCurrentScene()->notifySceneChanged();
    m_app->getCurrentXsheet()->notifyXsheetChanged();
    return;
  }

  int frame = m_app->getCurrentFrame()->getFrame();
  if (item->kind == Drawing) {
    frame                = item->data(Name, RowRole).toInt();
    const TXshCell &cell = m_xsheet->getCell(frame, c);
    if (cell.m_level.getPointer() != item->level || cell.m_frameId != item->fid)
      return;
  } else if (item->kind == Level) {
    TXshLevel *level      = item->level.data();
    TXshCellColumn *cells = column->getCellColumn();
    if (!level || !cells) return;
    int first, last;
    cells->getRange(first, last);
    int nearest     = -1;
    qint64 distance = std::numeric_limits<qint64>::max();
    for (int row = first; row <= last; ++row) {
      if (cells->getCell(row).m_level.getPointer() != level) continue;
      qint64 delta = qAbs(qint64(row) - frame);
      if (delta < distance) {
        nearest  = row;
        distance = delta;
      }
      if (delta == 0) break;
    }
    if (nearest < 0) return;
    frame = nearest;
  }
  m_app->getCurrentSelection()->setSelection(nullptr);
  m_app->getCurrentColumn()->setColumnIndex(c);
  m_app->getCurrentFrame()->setFrame(frame);
  refreshCurrent();
}

void DrawingLayers::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return ||
      event->key() == Qt::Key_Enter) {
    activateItem(currentItem(), currentColumn());
    event->accept();
    return;
  }
  QTreeWidget::keyPressEvent(event);
}

void DrawingLayers::paintEvent(QPaintEvent *event) {
  QTreeWidget::paintEvent(event);
  if (topLevelItemCount() == 0) {
    QPainter painter(viewport());
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        viewport()->rect().adjusted(16, 16, -16, -16),
        Qt::AlignCenter | Qt::TextWordWrap,
        tr("Expose a level in the Xsheet to see its layers here."));
  }
}
