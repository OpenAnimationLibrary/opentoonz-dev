#include "cpitool.h"
#include "tools/tool.h"
#include "tools/toolhandle.h"
#include "tools/tooloptions.h"
#include "tools/toolcommandids.h"
#include "toonzqt/selectioncommandids.h"
#include "tstrokeutil.h"
#include "tstrokedeformations.h"
#include <QPolygonF>
#include "toonz/tframehandle.h"
#include "toonz/tobjecthandle.h"
#include "toonz/txsheethandle.h"
#include "toonz/tscenehandle.h"
#include "toonz/txsheet.h"
#include "toonz/txshsimplelevel.h"
#include "tstroke.h"
#include "tproperty.h"
#include "tthread.h"
#include "tundo.h"
#include "tenv.h"
#include "tgl.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
TEnv::IntVar CpiExtremeDistance("CpiExtremeDistance", 72);
TEnv::DoubleVar CpiBrushRadius("CpiBrushRadius", 40);
TEnv::DoubleVar CpiBrushStrength("CpiBrushStrength", 0.25);
void notifyCpi() {
  auto app = TTool::getApplication();
  app->getCurrentScene()->setDirtyFlag(true);
  app->getCurrentXsheet()->getXsheet()->updateFrameCount();
  app->getCurrentXsheet()->notifyXsheetChanged();
  app->getCurrentObject()->notifyObjectIdChanged(false);
  app->getCurrentTool()->getTool()->invalidate();
}
class CpiUndo final : public TUndo {
  TXshLevelColumnP m_column;
  Cpi::Snapshot m_before, m_after;
  TXshCell m_cell;
  std::vector<int> m_exposed;
  QString m_label;

public:
  CpiUndo(TXshLevelColumnP column, Cpi::Snapshot before, Cpi::Snapshot after,
          const TXshCell &cell, const std::vector<int> &exposed,
          const QString &label)
      : m_column(column)
      , m_before(before)
      , m_after(after)
      , m_cell(cell)
      , m_exposed(exposed)
      , m_label(label) {}
  void undo() const override {
    m_column->setCpi(m_before);
    for (int row : m_exposed) m_column->setCell(row, TXshCell());
    notifyCpi();
  }
  void redo() const override {
    m_column->setCpi(m_after);
    for (int row : m_exposed) m_column->setCell(row, m_cell);
    notifyCpi();
  }
  int getSize() const override {
    size_t size = sizeof(*this) + m_exposed.capacity() * sizeof(int);
    if (m_before) size += m_before->memorySize();
    if (m_after) size += m_after->memorySize();
    return int(std::min(size, size_t((std::numeric_limits<int>::max)())));
  }
  QString getHistoryString() override { return m_label; }
};
}  // namespace

CpiTool::CpiTool(TTool *tool, QObject *parent) : QObject(parent), m_tool(tool) {
  m_brushRadius = std::max(1.0, std::min(1000.0, double(CpiBrushRadius)));
  m_strength    = std::max(0.01, std::min(1.0, double(CpiBrushStrength)));
}
CpiTool::~CpiTool() {
  cancelPreview();
  delete m_dialog.data();
}
bool CpiTool::context(TXshLevelColumnP &column, TXshCell &cell,
                      TVectorImageP &image, bool editing) const {
  auto app = TTool::getApplication();
  if (!app) return false;
  if (!m_enabled) return false;
  auto frame = app->getCurrentFrame();
  auto id    = m_tool->getObjectId();
  if (!frame->isEditingScene() || (editing && frame->isPlaying()) ||
      !id.isColumn())
    return false;
  auto xsheet = m_tool->getXsheet();
  if (!xsheet) return false;
  auto col = xsheet->getColumn(id.getIndex());
  if (!col || !col->getLevelColumn() || (editing && col->isLocked()))
    return false;
  column     = col->getLevelColumn();
  cell       = column->getCell(m_tool->getFrame());
  auto level = cell.getSimpleLevel();
  if (!level || level->getType() != PLI_XSHLEVEL) return false;
  image = cell.getImage(false);
  return bool(image);
}
void CpiTool::cancelPreview() {
  if (m_column && m_preview && m_column->getCpiPreview() == m_preview)
    m_column->setCpiPreview({});
  m_preview.reset();
  m_before.reset();
  m_startImage = TVectorImageP();
  m_movingPoints.clear();
  m_brushWeights.clear();
  m_dragging = m_rectangle = m_changed = false;
  m_lasso.clear();
}
void CpiTool::syncContext() {
  TXshLevelColumnP column;
  TXshCell cell;
  TVectorImageP image;
  bool ok = context(column, cell, image, false);
  if (m_dragging && m_tool->getFrame() != m_row) cancelPreview();
  if (!ok || column != m_column || cell != m_cell) {
    cancelPreview();
    m_dragging  = false;
    m_rectangle = false;
    m_before.reset();
    m_selection.clear();
    m_group.clear();
    m_column = ok ? column : TXshLevelColumnP();
    m_cell   = ok ? cell : TXshCell();
  }
  auto data = m_column ? m_column->getCpi() : Cpi::Snapshot();
  if (!m_group.empty() && (!data || !data->group(m_group))) m_group.clear();
}
void CpiTool::activate() {
  m_enabled = true;
  makeCurrent();
  if (!m_connected) {
    auto app = TTool::getApplication();
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
            [this] { deactivate(); });
    connect(app->getCurrentFrame(), &TFrameHandle::frameSwitched, this,
            [this] { refresh(); });
    connect(app->getCurrentXsheet(), &TXsheetHandle::xsheetSwitched, this,
            [this] { refresh(); });
    connect(app->getCurrentXsheet(), &TXsheetHandle::xsheetChanged, this,
            [this] { refresh(); });
    connect(app->getCurrentObject(), &TObjectHandle::objectSwitched, this,
            [this] { refresh(); });
    m_connected = true;
  }
  refresh();
}
void CpiTool::deactivate() {
  cancelPreview();
  m_enabled = false;
  makeNotCurrent();
  if (m_dialog) m_dialog->hide();
}
void CpiTool::message(const QString &text) {
  QMessageBox::information(
      m_dialog ? m_dialog.data() : QApplication::activeWindow(),
      tr("Control Point Interpolation"), text);
}
void CpiTool::commit(Cpi::Snapshot before, Cpi::Snapshot after,
                     const QString &label, const std::vector<int> &exposed) {
  if (!m_column || !after || !after->valid()) return;
  auto undo = new CpiUndo(m_column, before, after, m_cell, exposed, label);
  m_column->setCpi(after);
  for (int row : exposed) m_column->setCell(row, m_cell);
  TUndoManager::manager()->add(undo);
  notifyCpi();
  refresh();
}
void CpiTool::selectLinkedEndpoints(const TVectorImageP &image) {
  auto selected = m_selection;
  for (auto id : selected) {
    auto s = Cpi::strokeIndex(id);
    if (s >= image->getStrokeCount()) continue;
    auto stroke   = image->getStroke(s);
    unsigned last = stroke->getControlPointCount() - 1;
    if (stroke->isSelfLoop() &&
        (Cpi::pointIndex(id) == 0 || Cpi::pointIndex(id) == last)) {
      m_selection.insert(Cpi::pointId(s, 0));
      m_selection.insert(Cpi::pointId(s, last));
    }
  }
}
void CpiTool::newGroup() {
  cancelPreview();
  syncContext();
  TXshLevelColumnP column;
  TXshCell cell;
  TVectorImageP image;
  if (!context(column, cell, image) || m_selection.empty()) {
    message(
        tr("Select control points in the Viewer first. Shift-click adds "
           "points; drag a rectangle to select several."));
    return;
  }
  auto before    = column->getCpi();
  Cpi::Data data = before ? *before : Cpi::Data();
  auto binding   = data.binding(cell.m_level.getPointer(), cell.m_frameId);
  if (binding && !binding->matches(image)) {
    message(
        tr("The source drawing has changed. Remove its CPI groups and bind it "
           "again."));
    return;
  }
  selectLinkedEndpoints(image);
  bool ok   = false;
  auto name = QInputDialog::getText(m_dialog, tr("New CP Group"), tr("Name:"),
                                    QLineEdit::Normal, QString(), &ok);
  if (!ok || name.trimmed().isEmpty()) return;
  Cpi::Binding captured = binding
                              ? *binding
                              : Cpi::Binding::capture(cell.m_level.getPointer(),
                                                      cell.m_frameId, image);
  if (!data.addGroup(captured, name.toStdString(), m_selection, m_group)) {
    message(
        tr("Choose a unique group name and points that do not already belong "
           "to a CPI group."));
    return;
  }
  commit(before, std::make_shared<Cpi::Data>(std::move(data)),
         tr("Create CPI Group"));
}
void CpiTool::renameGroup() {
  cancelPreview();
  syncContext();
  TXshLevelColumnP col;
  TXshCell cell;
  TVectorImageP image;
  if (!context(col, cell, image)) return;
  auto before = col->getCpi();
  auto g      = before ? before->group(m_group) : nullptr;
  if (!g) return;
  bool ok   = false;
  auto name = QInputDialog::getText(m_dialog, tr("Rename CP Group"),
                                    tr("Name:"), QLineEdit::Normal,
                                    QString::fromStdString(g->name), &ok)
                  .trimmed();
  if (!ok || name.isEmpty()) return;
  Cpi::Data data            = *before;
  data.group(m_group)->name = name.toStdString();
  if (!data.valid()) {
    message(tr("A group already uses that name."));
    return;
  }
  commit(before, std::make_shared<Cpi::Data>(std::move(data)),
         tr("Rename CPI Group"));
}
void CpiTool::removeGroup() {
  cancelPreview();
  syncContext();
  TXshLevelColumnP col;
  TXshCell cell;
  TVectorImageP image;
  if (!context(col, cell, image)) return;
  auto before = col->getCpi();
  if (!before || !before->group(m_group)) return;
  Cpi::Data data = *before;
  data.groups.erase(
      std::remove_if(data.groups.begin(), data.groups.end(),
                     [this](const Cpi::Group &g) { return g.id == m_group; }),
      data.groups.end());
  data.bindings.erase(std::remove_if(data.bindings.begin(), data.bindings.end(),
                                     [&data](const Cpi::Binding &b) {
                                       return std::none_of(
                                           data.groups.begin(),
                                           data.groups.end(),
                                           [&b](const Cpi::Group &g) {
                                             return g.bindingId == b.id;
                                           });
                                     }),
                      data.bindings.end());
  m_group.clear();
  commit(before, std::make_shared<Cpi::Data>(std::move(data)),
         tr("Remove CPI Group"));
}
void CpiTool::key(bool newPair) {
  cancelPreview();
  syncContext();
  TXshLevelColumnP col;
  TXshCell cell;
  TVectorImageP image;
  if (!context(col, cell, image)) return;
  auto before = col->getCpi();
  auto g      = before ? before->group(m_group) : nullptr;
  if (!g) {
    message(tr("Create or select a named CP group first."));
    return;
  }
  auto b = before->binding(g->bindingId);
  if (!b || !b->matches(image)) {
    message(tr("The source drawing no longer matches this group's binding."));
    return;
  }
  int row        = m_tool->getFrame();
  newPair        = newPair || !g->pairAt(row);
  Cpi::Data data = *before;
  auto group     = data.group(m_group);
  std::vector<int> exposed;
  if (newPair) {
    int distance =
        m_distance ? m_distance->value() : std::max(1, int(CpiExtremeDistance));
    if (!group->createPair(row, distance)) {
      message(
          tr("The new extreme pair overlaps an existing pair or exceeds the "
             "frame range."));
      return;
    }
    for (int f = row; f <= row + distance; ++f) {
      auto c = col->getCell(f);
      if (!c.isEmpty() && c != cell) {
        message(
            tr("Another drawing is exposed inside this pair. Choose a shorter "
               "distance or an empty span."));
        return;
      }
      if (c.isEmpty()) exposed.push_back(f);
    }
  } else {
    auto pair = group->pairAt(row);
    if (!pair) {
      message(tr("Create a pair of Key Extremes covering this frame first."));
      return;
    }
    pair->setPose(row, group->evaluate(row));
  }
  commit(before, std::make_shared<Cpi::Data>(std::move(data)),
         newPair ? tr("Create CPI Key Extremes") : tr("Set CPI Key"), exposed);
}
void CpiTool::removeKey() {
  cancelPreview();
  syncContext();
  TXshLevelColumnP col;
  TXshCell cell;
  TVectorImageP image;
  if (!context(col, cell, image)) return;
  auto before = col->getCpi();
  auto g      = before ? before->group(m_group) : nullptr;
  if (!g) return;
  int row   = m_tool->getFrame();
  auto pair = g->pairAt(row);
  if (!pair) return;
  Cpi::Data data = *before;
  auto group     = data.group(m_group);
  bool extreme   = row == pair->first || row == pair->last;
  if (extreme) {
    group->pairs.erase(std::remove_if(group->pairs.begin(), group->pairs.end(),
                                      [row](const Cpi::ExtremePair &p) {
                                        return row == p.first || row == p.last;
                                      }),
                       group->pairs.end());
  } else
    group->pairAt(row)->keys.erase(row);
  commit(before, std::make_shared<Cpi::Data>(std::move(data)),
         extreme ? tr("Remove CPI Extreme Pair") : tr("Remove CPI Offset Key"));
}
void CpiTool::rotateGroup() {
  cancelPreview();
  syncContext();
  TXshLevelColumnP col;
  TXshCell cell;
  TVectorImageP image;
  if (!context(col, cell, image)) return;
  auto before = col->getCpi();
  auto group  = before ? before->group(m_group) : nullptr;
  if (!group || !group->pairAt(m_tool->getFrame())) {
    message(tr("Create a Key Extreme pair before rotating a group."));
    return;
  }
  if (!before->binding(group->bindingId)->matches(image)) return;
  QDialog dialog(m_dialog);
  dialog.setWindowTitle(tr("Rotate CP Group"));
  QFormLayout form(&dialog);
  QComboBox axis;
  axis.addItems({tr("X (tilt)"), tr("Y (tilt)"), tr("Z (drawing plane)")});
  axis.setCurrentIndex(2);
  QDoubleSpinBox angle;
  angle.setRange(-180, 180);
  angle.setDecimals(3);
  angle.setSuffix(tr(" degrees"));
  form.addRow(tr("Local axis:"), &axis);
  form.addRow(tr("Rotate by:"), &angle);
  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  form.addRow(&buttons);
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;
  Cpi::Data data = *before;
  auto g         = data.group(m_group);
  auto pose      = g->evaluate(m_tool->getFrame());
  Cpi::Vec3 a(axis.currentIndex() == 0, axis.currentIndex() == 1,
              axis.currentIndex() == 2);
  pose.rotation = pose.rotation * Cpi::Rotation::axisAngle(a, angle.value());
  g->pairAt(m_tool->getFrame())->setPose(m_tool->getFrame(), pose);
  commit(before, std::make_shared<Cpi::Data>(std::move(data)),
         tr("Rotate CPI Group"));
}
void CpiTool::translateGroup() {
  cancelPreview();
  syncContext();
  TXshLevelColumnP col;
  TXshCell cell;
  TVectorImageP image;
  if (!context(col, cell, image)) return;
  auto before = col->getCpi();
  auto group  = before ? before->group(m_group) : nullptr;
  if (!group || !group->pairAt(m_tool->getFrame())) {
    message(tr("Create a Key Extreme pair before editing channels."));
    return;
  }
  if (!before->binding(group->bindingId)->matches(image)) return;
  auto pose = group->evaluate(m_tool->getFrame());
  QDialog dialog(m_dialog);
  dialog.setWindowTitle(tr("CPI Group Position"));
  QFormLayout form(&dialog);
  QDoubleSpinBox x, y, z;
  for (auto spin : {&x, &y, &z}) {
    spin->setRange(-1e9, 1e9);
    spin->setDecimals(6);
  }
  x.setValue(pose.translation.x);
  y.setValue(pose.translation.y);
  z.setValue(pose.translation.z);
  form.addRow(tr("X (pixels):"), &x);
  form.addRow(tr("Y (pixels):"), &y);
  form.addRow(tr("Z:"), &z);
  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  form.addRow(&buttons);
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;
  Cpi::Data data   = *before;
  pose.translation = Cpi::Vec3(x.value(), y.value(), z.value());
  data.group(m_group)
      ->pairAt(m_tool->getFrame())
      ->setPose(m_tool->getFrame(), pose);
  commit(before, std::make_shared<Cpi::Data>(std::move(data)),
         tr("Set CPI Group Position"));
}
void CpiTool::selectAll() {
  cancelPreview();
  syncContext();
  TXshLevelColumnP col;
  TXshCell cell;
  TVectorImageP image;
  if (!context(col, cell, image)) return;
  auto data = col->getCpi();
  auto g    = data ? data->group(m_group) : nullptr;
  m_selection.clear();
  if (g)
    m_selection = g->points;
  else
    for (unsigned s = 0; s < image->getStrokeCount(); ++s)
      for (int p = 0; p < image->getStroke(s)->getControlPointCount(); ++p)
        m_selection.insert(Cpi::pointId(s, p));
  refresh();
  m_tool->invalidate();
}
void CpiTool::openChannels() {
  activate();
  if (!m_dialog) {
    m_dialog = new QDialog(QApplication::activeWindow(), Qt::Tool);
    m_dialog->setWindowTitle(tr("Animate Tool - Control Point Interpolation"));
    auto layout = new QVBoxLayout(m_dialog);
    auto intro =
        new QLabel(tr("Select points in the Viewer. Shift-click adds points; "
                      "drag empty space for a box selection. Name the "
                      "selection, then create its Key Extremes."));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto row = new QHBoxLayout;
    m_groups = new QComboBox;
    row->addWidget(m_groups, 1);
    auto add    = new QPushButton(tr("New Group"));
    auto rename = new QPushButton(tr("Rename"));
    auto remove = new QPushButton(tr("Remove Group"));
    row->addWidget(add);
    row->addWidget(rename);
    row->addWidget(remove);
    layout->addLayout(row);
    auto form = new QFormLayout;
    m_target  = new QComboBox;
    m_target->addItems({tr("Individual points"), tr("Entire group")});
    m_target->setCurrentIndex(m_entireGroup ? 1 : 0);
    connect(m_target, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
              cancelPreview();
              m_entireGroup = index == 1;
              refresh();
            });
    form->addRow(tr("Drag:"), m_target);
    m_distance = new QSpinBox;
    m_distance->setRange(1, 1000000);
    m_distance->setValue(
        std::max(1, std::min(1000000, int(CpiExtremeDistance))));
    m_distance->setSuffix(tr(" frames"));
    form->addRow(tr("Extreme distance:"), m_distance);
    layout->addLayout(form);
    auto actions = new QHBoxLayout;
    auto pair    = new QPushButton(tr("Create Key Extremes"));
    auto set     = new QPushButton(tr("Set Key"));
    auto erase   = new QPushButton(tr("Remove Key / Pair"));
    actions->addWidget(pair);
    actions->addWidget(set);
    actions->addWidget(erase);
    layout->addLayout(actions);
    auto transform = new QHBoxLayout;
    auto select    = new QPushButton(tr("Select All"));
    auto position  = new QPushButton(tr("Position XYZ..."));
    auto rotate    = new QPushButton(tr("Rotate Group..."));
    transform->addWidget(select);
    transform->addWidget(position);
    transform->addWidget(rotate);
    layout->addLayout(transform);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    m_keys = new QTableWidget;
    m_keys->setColumnCount(5);
    m_keys->setHorizontalHeaderLabels(
        {tr("Frame"), tr("Kind"), tr("X"), tr("Y"), tr("Z")});
    m_keys->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_keys->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_keys->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_keys);
    auto hint = new QLabel(tr(
        "Click a key row to visit its frame. Removing an extreme removes its "
        "entire pair and the offset keys between them. X/Y/Z show group "
        "translation; point offsets and quaternion rotation are also stored."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    connect(add, &QPushButton::clicked, this, [this] { newGroup(); });
    connect(rename, &QPushButton::clicked, this, [this] { renameGroup(); });
    connect(remove, &QPushButton::clicked, this, [this] { removeGroup(); });
    connect(pair, &QPushButton::clicked, this, [this] { key(true); });
    connect(set, &QPushButton::clicked, this, [this] { key(false); });
    connect(erase, &QPushButton::clicked, this, [this] { removeKey(); });
    connect(select, &QPushButton::clicked, this, [this] { selectAll(); });
    connect(position, &QPushButton::clicked, this,
            [this] { translateGroup(); });
    connect(rotate, &QPushButton::clicked, this, [this] { rotateGroup(); });
    connect(m_distance, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int v) { CpiExtremeDistance = v; });
    connect(m_groups, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
              if (m_refreshing) return;
              cancelPreview();
              m_group = m_groups->itemData(index).toString().toStdString();
              m_selection.clear();
              selectAll();
              refresh();
            });
    connect(m_keys, &QTableWidget::cellClicked, this, [this](int row, int) {
      auto item = m_keys->item(row, 0);
      if (item)
        TTool::getApplication()->getCurrentFrame()->setFrame(
            item->data(Qt::UserRole).toInt());
    });
    m_dialog->resize(610, 480);
  }
  refresh();
  m_dialog->show();
  m_dialog->raise();
}
void CpiTool::refresh() {
  if (m_refreshing) return;
  syncContext();
  m_refreshing = true;
  if (m_dialog) {
    QSignalBlocker block(m_groups);
    m_groups->clear();
    m_groups->addItem(tr("All points"), QString());
    auto data = m_column ? m_column->getCpi() : Cpi::Snapshot();
    auto binding =
        data ? data->binding(m_cell.m_level.getPointer(), m_cell.m_frameId)
             : nullptr;
    if (binding)
      for (const auto &g : data->groups)
        if (g.bindingId == binding->id)
          m_groups->addItem(QString::fromStdString(g.name),
                            QString::fromStdString(g.id));
    int index = m_groups->findData(QString::fromStdString(m_group));
    m_groups->setCurrentIndex(std::max(0, index));
    if (index < 0) m_group.clear();
    auto group = data ? data->group(m_group) : nullptr;
    m_keys->setRowCount(0);
    if (group)
      for (const auto &p : group->pairs) {
        auto add = [this](int frame, const QString &kind,
                          const Cpi::Pose &pose) {
          int r = m_keys->rowCount();
          m_keys->insertRow(r);
          QStringList values = {QString::number(frame + 1), kind,
                                QString::number(pose.translation.x, 'g', 8),
                                QString::number(pose.translation.y, 'g', 8),
                                QString::number(pose.translation.z, 'g', 8)};
          for (int c = 0; c < 5; ++c)
            m_keys->setItem(r, c, new QTableWidgetItem(values[c]));
          m_keys->item(r, 0)->setData(Qt::UserRole, frame);
        };
        add(p.first, tr("Extreme A"), p.start);
        for (const auto &k : p.keys) add(k.first, tr("Offset"), k.second);
        add(p.last, tr("Extreme B"), p.end);
      }
    TXshLevelColumnP col;
    TXshCell cell;
    TVectorImageP image;
    bool editable  = context(col, cell, image);
    QString status = !editable
                         ? tr("Choose an unlocked vector column in scene mode.")
                         : tr("Frame %1 — %2 points selected")
                               .arg(m_tool->getFrame() + 1)
                               .arg(qulonglong(m_selection.size()));
    if (editable && binding && !binding->matches(image))
      status =
          tr("Source drawing changed: CPI is suspended. Remove its groups and "
             "bind the revised drawing again.");
    else if (group) {
      auto p = group->pairAt(m_tool->getFrame());
      status += p ? tr(" — Extremes %1 / %2").arg(p->first + 1).arg(p->last + 1)
                  : tr(" — No extreme pair at this frame");
    }
    m_status->setText(status);
    QSignalBlocker targetBlock(m_target);
    m_target->setCurrentIndex(m_entireGroup ? 1 : 0);
  }
  for (auto bar : m_bars) {
    if (!bar) continue;
    auto groups = bar->findChild<QComboBox *>("cpiGroup");
    QSignalBlocker block(groups);
    groups->clear();
    groups->addItem(tr("All points"), QString());
    auto data = m_column ? m_column->getCpi() : Cpi::Snapshot();
    auto binding =
        data ? data->binding(m_cell.m_level.getPointer(), m_cell.m_frameId)
             : nullptr;
    if (binding)
      for (const auto &g : data->groups)
        if (g.bindingId == binding->id)
          groups->addItem(QString::fromStdString(g.name),
                          QString::fromStdString(g.id));
    groups->setCurrentIndex(
        std::max(0, groups->findData(QString::fromStdString(m_group))));
    auto target = bar->findChild<QComboBox *>("cpiTarget");
    QSignalBlocker targetBlock(target);
    target->setCurrentIndex(m_entireGroup ? 1 : 0);
    auto operation = bar->findChild<QComboBox *>("cpiOperation");
    QSignalBlocker operationBlock(operation);
    operation->setCurrentIndex(int(m_operation));
    auto shape = bar->findChild<QComboBox *>("cpiShape");
    QSignalBlocker shapeBlock(shape);
    shape->setCurrentIndex(m_lassoSelection ? 1 : 0);
    shape->setVisible(m_operation == Select);
    bool brush = m_operation == Magnet || m_operation == Smooth;
    for (const char *name :
         {"cpiRadius", "cpiStrength", "cpiRadiusLabel", "cpiStrengthLabel"})
      bar->findChild<QWidget *>(name)->setVisible(brush);
    auto radius   = bar->findChild<QDoubleSpinBox *>("cpiRadius");
    auto strength = bar->findChild<QDoubleSpinBox *>("cpiStrength");
    QSignalBlocker radiusBlock(radius), strengthBlock(strength);
    radius->setValue(m_brushRadius);
    strength->setValue(m_strength * 100);
    bar->findChild<QLabel *>("cpiStatus")->setText(statusText());
  }
  m_refreshing = false;
}

namespace {
bool poseChanged(const Cpi::Pose &a, const Cpi::Pose &b) {
  auto different = [](const Cpi::Vec3 &x, const Cpi::Vec3 &y) {
    auto d = x - y;
    return d.x * d.x + d.y * d.y + d.z * d.z > 1e-18;
  };
  if (different(a.translation, b.translation)) return true;
  double dot = a.rotation.w * b.rotation.w + a.rotation.x * b.rotation.x +
               a.rotation.y * b.rotation.y + a.rotation.z * b.rotation.z;
  if (std::abs(std::abs(dot) - 1) > 1e-12) return true;
  for (const auto &v : a.offsets)
    if (different(v.second, b.offset(v.first))) return true;
  for (const auto &v : b.offsets)
    if (different(v.second, a.offset(v.first))) return true;
  return false;
}
class CpiOptions final : public ToolOptionsBox {
  CpiTool *m_session;

public:
  CpiOptions(CpiTool *session) : ToolOptionsBox(nullptr), m_session(session) {}
  void updateStatus() override { m_session->refresh(); }
};
}  // namespace

void CpiTool::setOperation(const std::string &name) {
  cancelPreview();
  m_operation = name == T_Edit || name == T_Selection ? Select
                : name == T_Magnet                    ? Magnet
                : name == T_Iron                      ? Smooth
                                                      : Unavailable;
  refresh();
}
bool CpiTool::editable() const {
  TXshLevelColumnP col;
  TXshCell cell;
  TVectorImageP source;
  if (m_operation == Unavailable || !context(col, cell, source)) return false;
  auto data = col->getCpi();
  auto b =
      data ? data->binding(cell.m_level.getPointer(), cell.m_frameId) : nullptr;
  return !b || b->matches(source);
}
TVectorImageP CpiTool::image() const {
  TXshLevelColumnP col;
  TXshCell cell;
  TVectorImageP source;
  if (!context(col, cell, source, false)) return {};
  return col->applyCpi(source, cell, m_tool->getFrame());
}
TRectD CpiTool::selectionBounds() const {
  auto current = image();
  if (!current || m_selection.empty()) return {};
  auto points = m_selection;
  auto data   = m_column ? m_column->getCpi() : Cpi::Snapshot();
  auto group  = data ? data->group(m_group) : nullptr;
  if (m_entireGroup && group) points = group->points;
  TRectD bounds;
  bool first = true;
  for (auto id : points) {
    auto s = Cpi::strokeIndex(id), p = Cpi::pointIndex(id);
    if (s >= current->getStrokeCount() ||
        p >= unsigned(current->getStroke(s)->getControlPointCount()))
      continue;
    TPointD v = current->getStroke(s)->getControlPoint(p);
    if (first) {
      bounds = TRectD(v.x, v.y, v.x, v.y);
      first  = false;
    } else {
      bounds.x0 = std::min(bounds.x0, v.x);
      bounds.x1 = std::max(bounds.x1, v.x);
      bounds.y0 = std::min(bounds.y0, v.y);
      bounds.y1 = std::max(bounds.y1, v.y);
    }
  }
  return first ? TRectD() : bounds.enlarge(8 * m_tool->getPixelSize());
}
QString CpiTool::statusText() const {
  if (m_operation == Unavailable)
    return tr("Choose Selection, Magnet or Iron/Smooth, or exit CPI.");
  if (!editable())
    return tr(
        "CPI: choose an unlocked, unchanged vector drawing in scene mode.");
  return tr("%1 CPs — frame %2")
      .arg(qulonglong(m_selection.size()))
      .arg(m_tool->getFrame() + 1);
}
ToolOptionsBox *CpiTool::createOptionsBox() {
  auto bar = new CpiOptions(this);
  m_bars.push_back(bar);
  auto layout = bar->hLayout();
  auto label  = new QLabel(tr("CPI"));
  label->setStyleSheet("font-weight: bold;");
  layout->addWidget(label);
  auto operation = new QComboBox;
  operation->setObjectName("cpiOperation");
  operation->addItems({tr("Selection / Transform"), tr("Magnet"), tr("Smooth"),
                       tr("Unavailable tool")});
  layout->addWidget(operation);
  connect(
      operation, QOverload<int>::of(&QComboBox::activated), bar, [this](int i) {
        if (i < 3)
          TTool::getApplication()->getCurrentTool()->setTool(i == 0
                                                                 ? T_Selection
                                                             : i == 1 ? T_Magnet
                                                                      : T_Iron);
      });
  auto groups = new QComboBox;
  groups->setObjectName("cpiGroup");
  groups->setMinimumWidth(125);
  layout->addWidget(groups);
  connect(groups, QOverload<int>::of(&QComboBox::currentIndexChanged), bar,
          [this, groups](int i) {
            if (m_refreshing) return;
            cancelPreview();
            m_group = groups->itemData(i).toString().toStdString();
            m_selection.clear();
            selectAll();
            refresh();
          });
  auto target = new QComboBox;
  target->setObjectName("cpiTarget");
  target->setToolTip(
      tr("Brushes affect the active group when no CPs are selected."));
  target->addItems({tr("Selected CPs"), tr("Entire group")});
  layout->addWidget(target);
  connect(target, QOverload<int>::of(&QComboBox::currentIndexChanged), bar,
          [this](int i) {
            if (m_refreshing) return;
            cancelPreview();
            m_entireGroup = i == 1;
            refresh();
            m_tool->invalidate();
          });
  auto shape = new QComboBox;
  shape->setObjectName("cpiShape");
  shape->addItems({tr("Rectangle"), tr("Lasso")});
  layout->addWidget(shape);
  connect(shape, QOverload<int>::of(&QComboBox::currentIndexChanged), bar,
          [this](int i) {
            if (!m_refreshing) {
              cancelPreview();
              m_lassoSelection = i == 1;
            }
          });
  auto addButton = [this, bar, layout](const QString &title, const char *name,
                                       auto action) {
    auto button = new QPushButton(title);
    button->setObjectName(name);
    layout->addWidget(button);
    connect(button, &QPushButton::clicked, bar, action);
  };
  addButton(tr("New Group"), "cpiNewGroup", [this] {
    cancelPreview();
    newGroup();
  });
  addButton(tr("Set Key"), "cpiSetKey", [this] {
    cancelPreview();
    key(false);
  });
  addButton(tr("Channels..."), "cpiChannels", [this] { openChannels(); });
  auto radiusLabel = new QLabel(tr("Radius:"));
  radiusLabel->setObjectName("cpiRadiusLabel");
  layout->addWidget(radiusLabel);
  auto radius = new QDoubleSpinBox;
  radius->setObjectName("cpiRadius");
  radius->setRange(1, 1000);
  radius->setDecimals(1);
  layout->addWidget(radius);
  auto strengthLabel = new QLabel(tr("Strength:"));
  strengthLabel->setObjectName("cpiStrengthLabel");
  layout->addWidget(strengthLabel);
  auto strength = new QDoubleSpinBox;
  strength->setObjectName("cpiStrength");
  strength->setRange(1, 100);
  strength->setSuffix("%");
  strength->setDecimals(0);
  layout->addWidget(strength);
  connect(radius, QOverload<double>::of(&QDoubleSpinBox::valueChanged), bar,
          [this](double v) {
            if (m_refreshing) return;
            cancelPreview();
            m_brushRadius  = v;
            CpiBrushRadius = v;
            refresh();
            m_tool->invalidate();
          });
  connect(strength, QOverload<double>::of(&QDoubleSpinBox::valueChanged), bar,
          [this](double v) {
            if (m_refreshing) return;
            cancelPreview();
            m_strength       = v / 100;
            CpiBrushStrength = m_strength;
            refresh();
          });
  addButton(tr("Exit CPI"), "cpiExit", [] {
    TTool::getApplication()->getCurrentTool()->setCpiMode(false);
  });
  auto status = new QLabel;
  status->setObjectName("cpiStatus");
  layout->addWidget(status);
  layout->addStretch(1);
  refresh();
  return bar;
}
void CpiTool::selectNone() {
  // Changing the global selection owner (for example to the Xsheet) should
  // retain the CPI workspace selection. Escape explicitly clears it.
  cancelPreview();
  if (!m_enabled) m_selection.clear();
}
void CpiTool::clearSelection() {
  cancelPreview();
  m_selection.clear();
  refresh();
  m_tool->invalidate();
}
void CpiTool::resolveSelectionGroup() {
  if (m_selection.empty() || !m_column) return;
  auto data = m_column->getCpi();
  auto binding =
      data ? data->binding(m_cell.m_level.getPointer(), m_cell.m_frameId)
           : nullptr;
  m_group.clear();
  if (!binding) return;
  for (const auto &g : data->groups)
    if (g.bindingId == binding->id &&
        std::all_of(m_selection.begin(), m_selection.end(),
                    [&g](Cpi::PointId id) { return g.points.count(id); })) {
      m_group = g.id;
      return;
    }
}
void CpiTool::invertSelection() {
  auto previous = m_selection;
  m_selection.clear();
  selectAll();
  for (auto id : previous) m_selection.erase(id);
  resolveSelectionGroup();
  refresh();
  m_tool->invalidate();
}
void CpiTool::enableCommands() {
  enableCommand(this, MI_SelectAll, &CpiTool::selectAll);
  enableCommand(this, MI_InvertSelection, &CpiTool::invertSelection);
}
void CpiTool::draw() {
  syncContext();
  auto current = image();
  if (!current) return;
  double pixel = m_tool->getPixelSize();
  for (unsigned s = 0; s < current->getStrokeCount(); ++s)
    for (int p = 0; p < current->getStroke(s)->getControlPointCount(); ++p) {
      TPointD v     = current->getStroke(s)->getControlPoint(p);
      bool selected = m_selection.count(Cpi::pointId(s, p));
      glColor3d(selected ? 1.0 : 0.35, selected ? 0.6 : 0.75,
                selected ? 0.1 : 1.0);
      tglDrawDisk(v, (selected ? 3.5 : 2.5) * pixel);
    }
  if (m_rectangle) {
    glColor3d(0.9, 0.7, 0.1);
    if (m_lassoSelection) {
      glBegin(GL_LINE_STRIP);
      for (const auto &p : m_lasso) glVertex2d(p.x, p.y);
      glEnd();
    } else
      tglDrawRect(TRectD(m_first, m_last));
  }
  if (m_cursorVisible && (m_operation == Magnet || m_operation == Smooth)) {
    glColor3d(0.9, 0.7, 0.1);
    tglDrawCircle(m_cursor, m_brushRadius);
  }
}
void CpiTool::move(const TPointD &pos) {
  m_cursor        = pos;
  m_cursorVisible = true;
  if (m_operation == Magnet || m_operation == Smooth) m_tool->invalidate();
}
void CpiTool::leave() {
  m_cursorVisible = false;
  m_tool->invalidate();
}

bool CpiTool::beginTransform(const TPointD &pos) {
  cancelPreview();
  syncContext();
  if (!editable() || !m_column) return false;
  auto data = m_column->getCpi();
  auto g    = data ? data->group(m_group) : nullptr;
  if (!g) {
    message(tr("Select and name a CP group before editing its pose."));
    return false;
  }
  if (!g->pairAt(m_tool->getFrame())) {
    message(tr(
        "Use Set Key to create a pair of Key Extremes at this frame first."));
    return false;
  }
  if (!m_entireGroup &&
      std::any_of(m_selection.begin(), m_selection.end(),
                  [g](Cpi::PointId id) { return !g->points.count(id); })) {
    message(tr("Choose points from one CPI group for this edit."));
    return false;
  }
  m_movingPoints =
      m_entireGroup || m_selection.empty() ? g->points : m_selection;
  if (m_movingPoints.empty()) return false;
  m_startPose = m_workingPose = g->evaluate(m_tool->getFrame());
  Cpi::Vec3 local;
  if ((!m_entireGroup || m_operation != Select) &&
      !g->localDelta(m_startPose, TPointD(1, 0), local)) {
    message(
        tr("This group is edge-on. Rotate it away from the edge-on view before "
           "editing points."));
    m_movingPoints.clear();
    return false;
  }
  m_before     = data;
  m_startImage = image();
  m_first = m_last = pos;
  m_row            = m_tool->getFrame();
  m_dragging       = true;
  return true;
}
bool CpiTool::publishPose(const Cpi::Pose &pose) {
  if (!m_dragging || !m_before || !m_column || m_column->getCpi() != m_before ||
      m_tool->getFrame() != m_row || !editable()) {
    cancelPreview();
    return false;
  }
  auto preview     = std::make_shared<Cpi::Preview>();
  preview->base    = m_before;
  preview->groupId = m_group;
  preview->frame   = m_row;
  preview->pose    = pose;
  if (!preview->valid()) return false;
  m_workingPose = pose;
  m_changed     = poseChanged(m_startPose, pose);
  m_column->setCpiPreview(preview);
  m_preview = preview;
  m_tool->invalidate();
  return true;
}
bool CpiTool::transform(const TAffine &affine) {
  if (!m_dragging || !m_before || !m_startImage) return false;
  auto g    = m_before->group(m_group);
  auto pose = m_startPose;
  if (m_entireGroup && std::abs(affine.det() - 1) < 1e-10 &&
      std::abs(affine.a11 * affine.a11 + affine.a21 * affine.a21 - 1) < 1e-10 &&
      std::abs(affine.a12 * affine.a12 + affine.a22 * affine.a22 - 1) < 1e-10) {
    auto origin      = g->pivot + pose.translation;
    auto transformed = affine * TPointD(origin.x, origin.y);
    pose.translation =
        Cpi::Vec3(transformed.x - g->pivot.x, transformed.y - g->pivot.y,
                  pose.translation.z);
    double degrees = std::atan2(affine.a21, affine.a11) * 180 / std::acos(-1.0);
    pose.rotation =
        (Cpi::Rotation::axisAngle(Cpi::Vec3(0, 0, 1), degrees) * pose.rotation)
            .normalized();
    return publishPose(pose);
  }
  for (auto id : m_movingPoints) {
    TPointD v = m_startImage->getStroke(Cpi::strokeIndex(id))
                    ->getControlPoint(Cpi::pointIndex(id));
    Cpi::Vec3 local;
    if (!g->localDelta(m_startPose, affine * v - v, local)) return false;
    pose.offsets[id] = m_startPose.offset(id) + local;
  }
  return publishPose(pose);
}
void CpiTool::finishEdit() {
  if (!m_dragging) return;
  auto before  = m_before;
  auto pose    = m_workingPose;
  bool changed = m_changed && editable() && m_column &&
                 m_column->getCpi() == before && m_tool->getFrame() == m_row;
  int row = m_row;
  cancelPreview();
  if (!changed || !before) return;
  Cpi::Data data = *before;
  auto group     = data.group(m_group);
  if (!group || !group->pairAt(row)) return;
  group->pairAt(row)->setPose(row, pose);
  commit(before, std::make_shared<Cpi::Data>(std::move(data)),
         m_operation == Magnet   ? tr("CPI Magnet")
         : m_operation == Smooth ? tr("CPI Smooth")
                                 : tr("Transform CPI Points"));
}
void CpiTool::endTransform() { finishEdit(); }

bool CpiTool::hitPoint(const TPointD &pos, Cpi::PointId &hit) const {
  auto current = image();
  if (!current) return false;
  double best = std::pow(6 * m_tool->getPixelSize(), 2);
  bool found  = false;
  for (unsigned s = 0; s < current->getStrokeCount(); ++s)
    for (int p = 0; p < current->getStroke(s)->getControlPointCount(); ++p) {
      TPointD v       = current->getStroke(s)->getControlPoint(p);
      double distance = norm2(v - pos);
      if (distance < best) {
        best  = distance;
        hit   = Cpi::pointId(s, p);
        found = true;
      }
    }
  return found;
}

void CpiTool::down(const TPointD &pos, const TMouseEvent &e) {
  syncContext();
  if (!editable()) return;
  makeCurrent();
  auto current = image();
  if (!current) return;
  if (m_operation == Magnet || m_operation == Smooth) {
    if (!beginTransform(pos)) return;
    TStrokePointDeformation weights(TPointD(1, 0), pos, m_brushRadius);
    for (auto id : m_movingPoints)
      m_brushWeights[id] =
          weights
              .getDisplacementForControlPoint(
                  *m_startImage->getStroke(Cpi::strokeIndex(id)),
                  Cpi::pointIndex(id))
              .x;
    if (m_operation == Smooth) smoothAt(pos);
    return;
  }
  Cpi::PointId hit = 0;
  bool found       = hitPoint(pos, hit);
  if (found) {
    if (e.isShiftPressed() || e.isCtrlPressed()) {
      if (e.isCtrlPressed() || m_selection.count(hit)) {
        m_selection.erase(hit);
        auto stroke   = current->getStroke(Cpi::strokeIndex(hit));
        unsigned last = stroke->getControlPointCount() - 1;
        if (stroke->isSelfLoop() &&
            (Cpi::pointIndex(hit) == 0 || Cpi::pointIndex(hit) == last)) {
          m_selection.erase(Cpi::pointId(Cpi::strokeIndex(hit), 0));
          m_selection.erase(Cpi::pointId(Cpi::strokeIndex(hit), last));
        }
      } else
        m_selection.insert(hit);
      selectLinkedEndpoints(current);
      resolveSelectionGroup();
      refresh();
      m_tool->invalidate();
      return;
    }
    if (!m_selection.count(hit)) {
      m_selection.clear();
      m_selection.insert(hit);
    }
    selectLinkedEndpoints(current);
    auto data = m_column->getCpi();
    auto binding =
        data ? data->binding(m_cell.m_level.getPointer(), m_cell.m_frameId)
             : nullptr;
    m_group.clear();
    if (binding)
      for (const auto &g : data->groups)
        if (g.bindingId == binding->id && g.points.count(hit)) {
          m_group = g.id;
          break;
        }
    refresh();
    m_tool->invalidate();
    // Unassigned points can be selected and named without a modal interruption.
    if (!m_group.empty()) beginTransform(pos);
    return;
  }
  cancelPreview();
  m_first = m_last  = pos;
  m_addSelection    = e.isShiftPressed();
  m_removeSelection = e.isCtrlPressed();
  if (!m_addSelection && !m_removeSelection) m_selection.clear();
  m_rectangle = m_dragging = true;
  m_row                    = m_tool->getFrame();
  m_lasso                  = {pos};
  m_tool->invalidate();
}
void CpiTool::smoothAt(const TPointD &pos) {
  if (!m_before || !m_startImage) return;
  const auto g = m_before->group(m_group);
  const auto b = m_before->binding(g->bindingId);
  auto pose    = m_workingPose;
  std::map<unsigned, std::unique_ptr<TStroke>> strokes;
  for (auto id : m_movingPoints) {
    auto s = Cpi::strokeIndex(id);
    if (strokes.count(s)) continue;
    auto stroke =
        std::unique_ptr<TStroke>(new TStroke(*m_startImage->getStroke(s)));
    for (int p = 0; p < stroke->getControlPointCount(); ++p) {
      auto pid = Cpi::pointId(s, p);
      if (!g->points.count(pid)) continue;
      auto v = g->position(*b, pid, m_workingPose);
      stroke->setControlPoint(p, TPointD(v.x, v.y));
    }
    strokes.emplace(s, std::move(stroke));
  }
  TStrokePointDeformation weights(TPointD(1, 0), pos, m_brushRadius);
  for (auto id : m_movingPoints) {
    unsigned s = Cpi::strokeIndex(id), p = Cpi::pointIndex(id);
    unsigned last = unsigned(b->strokes[s].size() - 1);
    bool closed   = b->loops[s];
    if ((!closed && (p == 0 || p == last)) || (closed && p == last)) continue;
    double weight = weights.getDisplacementForControlPoint(*strokes[s], p).x;
    if (weight <= 0) continue;
    auto left        = Cpi::pointId(s, p == 0 ? last - 1 : p - 1);
    auto right       = Cpi::pointId(s, p + 1 == last && closed ? 0 : p + 1);
    auto stroke      = strokes[s].get();
    TPointD v        = stroke->getControlPoint(p);
    TPointD a        = stroke->getControlPoint(Cpi::pointIndex(left));
    TPointD c        = stroke->getControlPoint(Cpi::pointIndex(right));
    TPointD smoothed = smoothControlPoint(v, a, c, 0.5, m_strength * weight);
    Cpi::Vec3 local;
    if (!g->localDelta(m_workingPose, smoothed - v, local)) continue;
    pose.offsets[id] = m_workingPose.offset(id) + local;
    if (closed && p == 0) pose.offsets[Cpi::pointId(s, last)] = pose.offset(id);
  }
  publishPose(pose);
}
void CpiTool::drag(const TPointD &pos, const TMouseEvent &) {
  if (!m_dragging) return;
  syncContext();
  if (!m_dragging || !editable() || m_tool->getFrame() != m_row) {
    cancelPreview();
    return;
  }
  if (m_rectangle) {
    m_last = pos;
    if (m_lasso.empty() ||
        norm2(pos - m_lasso.back()) > std::pow(m_tool->getPixelSize(), 2))
      m_lasso.push_back(pos);
    m_tool->invalidate();
    return;
  }
  if (!m_before || m_column->getCpi() != m_before) {
    cancelPreview();
    return;
  }
  auto g = m_before->group(m_group);
  if (m_operation == Smooth) {
    // Stamp by travelled distance so smoothing does not depend on event rate.
    double spacing  = std::max(m_tool->getPixelSize(), m_brushRadius * 0.12);
    double distance = norm(pos - m_last);
    int count       = int(std::min(2048.0, std::floor(distance / spacing)));
    if (count > 0) {
      TPointD step  = (pos - m_last) * (spacing / distance);
      TPointD start = m_last;
      for (int i = 1; i <= count && m_dragging; ++i) smoothAt(start + step * i);
      m_last = start + step * count;
    }
  } else {
    auto pose     = m_startPose;
    TPointD delta = pos - m_first;
    if (m_operation == Select && m_entireGroup)
      pose.translation = pose.translation + Cpi::Vec3(delta.x, delta.y);
    else {
      Cpi::Vec3 local;
      if (!g->localDelta(m_startPose, delta, local)) return;
      for (auto id : m_movingPoints)
        pose.offsets[id] =
            m_startPose.offset(id) +
            local *
                (m_operation == Magnet ? m_strength * m_brushWeights[id] : 1.0);
    }
    publishPose(pose);
    m_last = pos;
  }
  move(pos);
}
void CpiTool::up(const TPointD &pos, const TMouseEvent &e) {
  if (!m_dragging) return;
  syncContext();
  if (!m_dragging || !editable()) {
    cancelPreview();
    return;
  }
  if (m_rectangle) {
    m_last       = pos;
    auto current = image();
    QPolygonF polygon;
    for (const auto &p : m_lasso) polygon << QPointF(p.x, p.y);
    polygon << QPointF(pos.x, pos.y);
    TRectD rect(m_first, m_last);
    if (current)
      for (unsigned s = 0; s < current->getStrokeCount(); ++s)
        for (int p = 0; p < current->getStroke(s)->getControlPointCount();
             ++p) {
          TPointD v = current->getStroke(s)->getControlPoint(p);
          bool inside =
              m_lassoSelection
                  ? polygon.containsPoint(QPointF(v.x, v.y), Qt::OddEvenFill)
                  : rect.contains(v);
          if (inside) {
            auto id = Cpi::pointId(s, p);
            if (m_removeSelection)
              m_selection.erase(id);
            else
              m_selection.insert(id);
          }
        }
    if (current) selectLinkedEndpoints(current);
    resolveSelectionGroup();
    cancelPreview();
    refresh();
    m_tool->invalidate();
    return;
  }
  drag(pos, e);
  finishEdit();
}
bool CpiTool::keyDown(QKeyEvent *e) {
  if (e->key() == Qt::Key_Escape) {
    if (m_dragging)
      cancelPreview();
    else
      clearSelection();
    refresh();
    m_tool->invalidate();
    return true;
  }
  if (e->matches(QKeySequence::SelectAll)) {
    selectAll();
    return true;
  }
  return false;
}
void CpiTool::contextMenu(QMenu *menu) {
  menu->addAction(tr("CPI Channels..."), this, [this] { openChannels(); });
  menu->addAction(tr("New CP Group..."), this, [this] {
    cancelPreview();
    newGroup();
  });
  menu->addAction(tr("Select All CPs"), this, [this] { selectAll(); });
  menu->addAction(tr("Deselect CPs"), this, [this] { clearSelection(); });
  menu->addAction(tr("Invert CP Selection"), this,
                  [this] { invertSelection(); });
  menu->addAction(tr("Create Key Extremes"), this, [this] {
    cancelPreview();
    key(true);
  });
  menu->addAction(tr("Set CPI Key"), this, [this] {
    cancelPreview();
    key(false);
  });
  menu->addSeparator();
  menu->addAction(tr("Exit CPI"), this, [] {
    TTool::getApplication()->getCurrentTool()->setCpiMode(false);
  });
}
