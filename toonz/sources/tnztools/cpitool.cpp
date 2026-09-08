#include "cpitool.h"
#include "tools/tool.h"
#include "tools/toolhandle.h"
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

CpiTool::CpiTool(TTool *tool, QObject *parent)
    : QObject(parent), m_tool(tool) {}
CpiTool::~CpiTool() { delete m_dialog.data(); }
bool CpiTool::context(TXshLevelColumnP &column, TXshCell &cell,
                      TVectorImageP &image, bool editing) const {
  auto app = TTool::getApplication();
  if (!app) return false;
  auto axis = dynamic_cast<TEnumProperty *>(
      m_tool->getProperties(0)->getProperty("Active Axis"));
  if (!axis || axis->getValue() != L"CPI") return false;
  auto frame = app->getCurrentFrame();
  auto id    = m_tool->getObjectId();
  if (!frame->isEditingScene() || (editing && frame->isPlaying()) ||
      !id.isColumn())
    return false;
  auto col = m_tool->getXsheet()->getColumn(id.getIndex());
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
  if (m_column && m_preview && m_column->getCpi() == m_preview)
    m_column->setCpi(m_before);
  m_preview.reset();
  m_before.reset();
  m_dragging  = false;
  m_rectangle = false;
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
}
void CpiTool::activate() {
  if (!m_connected) {
    auto app = TTool::getApplication();
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
  if (!m_dialog) return;
  m_refreshing = true;
  QSignalBlocker block(m_groups);
  m_groups->clear();
  m_groups->addItem(tr("Unassigned points"), QString());
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
      auto add = [this](int frame, const QString &kind, const Cpi::Pose &pose) {
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
  m_refreshing = false;
}
void CpiTool::draw() {
  syncContext();
  TXshLevelColumnP col;
  TXshCell cell;
  TVectorImageP source;
  if (!context(col, cell, source, false)) return;
  auto data = col->getCpi();
  auto b =
      data ? data->binding(cell.m_level.getPointer(), cell.m_frameId) : nullptr;
  if (b && !b->matches(source)) return;
  TVectorImageP image = col->applyCpi(source, cell, m_tool->getFrame());
  glPushAttrib(GL_CURRENT_BIT | GL_POINT_BIT | GL_LINE_BIT);
  glPointSize(5.0f);
  glBegin(GL_POINTS);
  for (unsigned s = 0; s < image->getStrokeCount(); ++s)
    for (int p = 0; p < image->getStroke(s)->getControlPointCount(); ++p) {
      bool selected = m_selection.count(Cpi::pointId(s, p));
      glColor3d(selected ? 1.0 : 0.2, selected ? 0.6 : 0.8,
                selected ? 0.1 : 1.0);
      auto point = image->getStroke(s)->getControlPoint(p);
      glVertex2d(point.x, point.y);
    }
  glEnd();
  if (m_rectangle) {
    glColor3d(1, 0.6, 0.1);
    glBegin(GL_LINE_LOOP);
    glVertex2d(m_first.x, m_first.y);
    glVertex2d(m_last.x, m_first.y);
    glVertex2d(m_last.x, m_last.y);
    glVertex2d(m_first.x, m_last.y);
    glEnd();
  }
  glPopAttrib();
}
void CpiTool::down(const TPointD &pos, const TMouseEvent &e) {
  syncContext();
  TXshLevelColumnP col;
  TXshCell cell;
  TVectorImageP source;
  if (!context(col, cell, source)) return;
  auto data = col->getCpi();
  auto b =
      data ? data->binding(cell.m_level.getPointer(), cell.m_frameId) : nullptr;
  if (b && !b->matches(source)) {
    message(
        tr("The source drawing has changed. Rebind its CP groups before "
           "animating."));
    return;
  }
  TVectorImageP image = col->applyCpi(source, cell, m_tool->getFrame());
  double best         = std::pow(8 * m_tool->getPixelSize(), 2);
  Cpi::PointId hit    = 0;
  bool found          = false;
  for (unsigned s = 0; s < image->getStrokeCount(); ++s)
    for (int p = 0; p < image->getStroke(s)->getControlPointCount(); ++p) {
      auto v   = image->getStroke(s)->getControlPoint(p);
      double d = std::pow(v.x - pos.x, 2) + std::pow(v.y - pos.y, 2);
      if (d < best) {
        best  = d;
        hit   = Cpi::pointId(s, p);
        found = true;
      }
    }
  if (found) {
    if (e.isShiftPressed() && m_selection.count(hit)) {
      m_selection.erase(hit);
      auto stroke = image->getStroke(Cpi::strokeIndex(hit));
      if (stroke->isSelfLoop() &&
          (Cpi::pointIndex(hit) == 0 ||
           Cpi::pointIndex(hit) ==
               unsigned(stroke->getControlPointCount() - 1))) {
        m_selection.erase(Cpi::pointId(Cpi::strokeIndex(hit), 0));
        m_selection.erase(Cpi::pointId(Cpi::strokeIndex(hit),
                                       stroke->getControlPointCount() - 1));
      }
      refresh();
      m_tool->invalidate();
      return;
    }
    if (!e.isShiftPressed() && !m_selection.count(hit)) m_selection.clear();
    m_selection.insert(hit);
    selectLinkedEndpoints(image);
    std::string hitGroup;
    if (data && b)
      for (const auto &g : data->groups)
        if (g.bindingId == b->id && g.points.count(hit)) {
          hitGroup = g.id;
          break;
        }
    m_group = hitGroup;
  } else if (!e.isShiftPressed())
    m_selection.clear();
  m_dragGroup = m_target && m_target->currentIndex() == 1;
  m_preview.reset();
  m_first = m_last = pos;
  m_dragging       = true;
  m_rectangle      = !found;
  m_changed        = false;
  m_row            = m_tool->getFrame();
  m_before         = data;
  auto g           = data ? data->group(m_group) : nullptr;
  if (g) m_startPose = g->evaluate(m_row);
  refresh();
  m_tool->invalidate();
}
void CpiTool::drag(const TPointD &pos, const TMouseEvent &) {
  if (!m_dragging) return;
  TXshLevelColumnP col;
  TXshCell cell;
  TVectorImageP source;
  if (!context(col, cell, source) || col != m_column || cell != m_cell ||
      m_tool->getFrame() != m_row ||
      col->getCpi() != (m_preview ? m_preview : m_before)) {
    cancelPreview();
    refresh();
    return;
  }
  m_last    = pos;
  m_changed = norm2(m_last - m_first) > 1e-12;
  if (!m_rectangle && m_before) {
    auto preview = std::make_shared<Cpi::Data>(*m_before);
    auto g       = preview->group(m_group);
    auto b       = g ? preview->binding(g->bindingId) : nullptr;
    if (g && b && b->matches(source) && g->pairAt(m_row) &&
        (m_dragGroup ||
         std::all_of(m_selection.begin(), m_selection.end(),
                     [g](Cpi::PointId id) { return g->points.count(id); }))) {
      auto pose     = m_startPose;
      TPointD delta = pos - m_first;
      if (m_dragGroup)
        pose.translation = pose.translation + Cpi::Vec3(delta.x, delta.y);
      else {
        Cpi::Vec3 local;
        if (!g->localDelta(pose, delta, local)) return;
        for (auto id : m_selection)
          if (g->points.count(id)) pose.offsets[id] = pose.offset(id) + local;
      }
      g->pairAt(m_row)->setPose(m_row, pose);
      m_preview = preview;
      col->setCpi(m_preview);
    }
  }
  m_tool->invalidate();
}
void CpiTool::up(const TPointD &pos, const TMouseEvent &) {
  if (!m_dragging) return;
  drag(pos, TMouseEvent());
  if (!m_dragging) return;
  m_last     = pos;
  m_dragging = false;
  TXshLevelColumnP col;
  TXshCell cell;
  TVectorImageP source;
  if (!context(col, cell, source) || col != m_column || cell != m_cell ||
      m_tool->getFrame() != m_row ||
      col->getCpi() != (m_preview ? m_preview : m_before)) {
    cancelPreview();
    refresh();
    return;
  }
  if (m_preview) {
    col->setCpi(m_before);
    m_preview.reset();
  }
  if (m_rectangle) {
    m_rectangle         = false;
    TVectorImageP image = col->applyCpi(source, cell, m_row);
    TRectD box(std::min(m_first.x, pos.x), std::min(m_first.y, pos.y),
               std::max(m_first.x, pos.x), std::max(m_first.y, pos.y));
    for (unsigned s = 0; s < image->getStrokeCount(); ++s)
      for (int p = 0; p < image->getStroke(s)->getControlPointCount(); ++p)
        if (box.contains(image->getStroke(s)->getControlPoint(p)))
          m_selection.insert(Cpi::pointId(s, p));
    selectLinkedEndpoints(image);
    refresh();
    m_tool->invalidate();
    return;
  }
  if (!m_changed || !m_before) {
    m_tool->invalidate();
    return;
  }
  Cpi::Data data = *m_before;
  auto g         = data.group(m_group);
  auto b         = g ? data.binding(g->bindingId) : nullptr;
  if (!g || !b || !b->matches(source)) return;
  if (!m_dragGroup &&
      std::any_of(m_selection.begin(), m_selection.end(),
                  [g](Cpi::PointId id) { return !g->points.count(id); })) {
    message(tr("Animate points from one CP group at a time."));
    m_tool->invalidate();
    return;
  }
  if (!g->pairAt(m_row)) {
    message(tr("Create Key Extremes for this group before moving its points."));
    return;
  }
  auto pose     = m_startPose;
  TPointD delta = pos - m_first;
  if (m_dragGroup)
    pose.translation = pose.translation + Cpi::Vec3(delta.x, delta.y);
  else {
    Cpi::Vec3 local;
    if (!g->localDelta(pose, delta, local)) {
      message(
          tr("This group's plane is edge-on. Rotate it away from 90 degrees "
             "before dragging points."));
      return;
    }
    for (auto id : m_selection)
      if (g->points.count(id)) pose.offsets[id] = pose.offset(id) + local;
  }
  g->pairAt(m_row)->setPose(m_row, pose);
  commit(m_before, std::make_shared<Cpi::Data>(std::move(data)),
         tr("Move CPI Control Points"));
  m_before.reset();
}
bool CpiTool::keyDown(QKeyEvent *e) {
  if (e->key() == Qt::Key_Escape) {
    cancelPreview();
    m_selection.clear();
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
  menu->addAction(tr("New CP Group..."), this, [this] { newGroup(); });
  menu->addAction(tr("Select All CPs"), this, [this] { selectAll(); });
  menu->addAction(tr("Create Key Extremes"), this, [this] { key(true); });
  menu->addAction(tr("Set CPI Key"), this, [this] { key(false); });
}
