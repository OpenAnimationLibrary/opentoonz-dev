#include "bulletphysicspopup.h"

#include "cellselection.h"
#include "columnselection.h"
#include "historytypes.h"
#include "menubarcommandids.h"
#include "tapp.h"
#include "tdoublekeyframe.h"
#include "toutputproperties.h"
#include "tundo.h"
#include "toonz/sceneproperties.h"
#include "toonz/stage.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/txshcolumn.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonzqt/menubarcommand.h"
#include "toonzqt/tselectionhandle.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <set>

namespace {
constexpr double pi = 3.141592653589793;
QDoubleSpinBox *number(QWidget *parent, double low, double high, double value) {
  auto *field = new QDoubleSpinBox(parent);
  field->setDecimals(3);
  field->setRange(low, high);
  field->setValue(value);
  return field;
}
void notifyBake() {
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);
  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->notifySceneChanged();
}
class BulletBakeUndo final : public TUndo {
  OTBullet::Bake m_bake;

public:
  explicit BulletBakeUndo(const OTBullet::Bake &bake) : m_bake(bake) {}
  void undo() const override {
    OTBullet::applyBake(m_bake, false);
    notifyBake();
  }
  void redo() const override {
    OTBullet::applyBake(m_bake, true);
    notifyBake();
  }
  int getSize() const override {
    int size = sizeof(*this);
    for (const auto &e : m_bake)
      size += (e.before->getKeyframeCount() + e.after->getKeyframeCount()) *
              sizeof(TDoubleKeyframe);
    return size;
  }
  QString getHistoryString() override {
    return QObject::tr("Bake Bullet Physics");
  }
  int getHistoryType() override { return HistoryType::Xsheet; }
};
}  // namespace

class BulletPreview final : public QWidget {
public:
  std::vector<OTBullet::Body> bodies;
  std::vector<OTBullet::Connection> connections;
  OTBullet::Clip clip;
  OTBullet::Settings settings;
  int frame = 0;
  QRectF bounds;

  explicit BulletPreview(QWidget *parent) : QWidget(parent) {
    setMinimumSize(400, 230);
  }
  void fit() {
    bounds = QRectF();
    for (const auto &poses : clip)
      for (size_t i = 0; i < poses.size(); ++i) {
        const auto &p = poses[i];
        double r      = bodies[i].length + bodies[i].width;
        QRectF box(p.pivot.x - r, p.pivot.y - r, r * 2, r * 2);
        bounds = bounds.isNull() ? box : bounds.united(box);
      }
    if (settings.floor)
      bounds = bounds.united(
          QRectF(bounds.left(), settings.floorY - 0.1, bounds.width(), 0.2));
    frame = 0;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(35, 38, 43));
    if (clip.empty()) {
      painter.setPen(Qt::lightGray);
      painter.drawText(rect(), Qt::AlignCenter,
                       tr("Simulate to preview body proxies and connections."));
      return;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    double scale = std::min((width() - 30) / bounds.width(),
                            (height() - 30) / bounds.height());
    auto point   = [&](double x, double y) {
      return QPointF(width() / 2 + (x - bounds.center().x()) * scale,
                       height() / 2 - (y - bounds.center().y()) * scale);
    };
    if (settings.floor) {
      painter.setPen(QPen(QColor(140, 140, 140), 2));
      painter.drawLine(point(bounds.left(), settings.floorY),
                       point(bounds.right(), settings.floorY));
    }
    const auto &poses = clip[std::min(frame, int(clip.size()) - 1)];
    for (size_t i = 0; i < bodies.size(); ++i) {
      const auto &body = bodies[i];
      const auto &pose = poses[i];
      double c = std::cos(pose.angle), s = std::sin(pose.angle);
      QPolygonF polygon;
      for (auto corner :
           {QPointF(-body.width / 2, 0), QPointF(body.width / 2, 0),
            QPointF(body.width / 2, body.length),
            QPointF(-body.width / 2, body.length)})
        polygon << point(pose.pivot.x + c * corner.x() - s * corner.y(),
                         pose.pivot.y + s * corner.x() + c * corner.y());
      painter.setPen(QPen(Qt::white, 1));
      painter.setBrush(body.pinned ? QColor(170, 120, 65)
                                   : QColor(65, 130, 190));
      painter.drawPolygon(polygon);
      painter.setBrush(Qt::white);
      auto root = point(pose.pivot.x, pose.pivot.y);
      painter.drawEllipse(root, 3, 3);
      painter.drawText(root + QPointF(6, -5), QString::number(i + 1));
    }
    painter.setPen(QPen(QColor(100, 230, 150), 2));
    painter.setBrush(QColor(100, 230, 150));
    for (const auto &link : connections) {
      auto anchor = [&](int index) {
        if (index < 0) return point(link.anchor.x, link.anchor.y);
        const auto &start = bodies[index].pose;
        const auto &pose  = poses[index];
        double d          = pose.angle - start.angle;
        double x          = link.anchor.x - start.pivot.x;
        double y          = link.anchor.y - start.pivot.y;
        return point(pose.pivot.x + std::cos(d) * x - std::sin(d) * y,
                     pose.pivot.y + std::sin(d) * x + std::cos(d) * y);
      };
      auto a = anchor(link.body), b = anchor(link.target);
      painter.drawLine(a, b);
      painter.drawEllipse(a, 4, 4);
    }
  }
};

BulletPhysicsPopup::BulletPhysicsPopup()
    : QDialog(TApp::instance()->getMainWindow()) {
  setWindowTitle(tr("Explore Bullet Physics"));
  setModal(true);
  resize(960, 780);
  auto *layout = new QVBoxLayout(this);
  auto *intro =
      new QLabel(tr("Select drawing columns, then connect their body proxies. "
                    "Connections start at each driven column's pivot. "
                    "Pinned bodies stay at their starting pose."),
                 this);
  intro->setWordWrap(true);
  layout->addWidget(intro);
  auto *reload = new QPushButton(tr("Reload Selection"), this);
  layout->addWidget(reload);
  m_table = new QTableWidget(0, 6, this);
  m_table->setHorizontalHeaderLabels(
      {tr("Column"), tr("Motion"), tr("Connect to"), tr("Joint"),
       tr("Length (in)"), tr("Direction (deg)")});
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  layout->addWidget(m_table);

  auto *fields = new QHBoxLayout;
  auto *left = new QFormLayout, *right = new QFormLayout;
  m_first = new QSpinBox(this);
  m_last  = new QSpinBox(this);
  for (auto *f : {m_first, m_last}) f->setRange(1, 1000001);
  m_substeps = new QSpinBox(this);
  m_substeps->setRange(1, 32);
  m_substeps->setValue(5);
  m_gravity = number(this, -100, 100, -9.81);
  m_width   = number(this, 0.1, 100, 0.25);
  m_mass    = number(this, 0.01, 1000, 1);
  m_scale   = number(this, 0.1, 10, 1);
  m_lower   = number(this, -180, 180, -180);
  m_upper   = number(this, -180, 180, 180);
  m_floor   = new QCheckBox(tr("Floor"), this);
  m_floorY  = number(this, -1000, 1000, -4);
  left->addRow(tr("First frame:"), m_first);
  left->addRow(tr("Last frame:"), m_last);
  left->addRow(tr("Steps per frame:"), m_substeps);
  left->addRow(tr("Gravity (m/s²):"), m_gravity);
  left->addRow(tr("Meters per OT inch:"), m_scale);
  right->addRow(tr("Proxy width (in):"), m_width);
  right->addRow(tr("Dynamic mass (kg):"), m_mass);
  right->addRow(tr("Hinge lower limit (deg):"), m_lower);
  right->addRow(tr("Hinge upper limit (deg):"), m_upper);
  right->addRow(m_floor, m_floorY);
  m_floorY->setSuffix(tr(" in"));
  fields->addLayout(left);
  fields->addLayout(right);
  layout->addLayout(fields);
  m_preview = new BulletPreview(this);
  layout->addWidget(m_preview, 1);
  m_frame = new QSlider(Qt::Horizontal, this);
  m_frame->setRange(0, 0);
  layout->addWidget(m_frame);
  m_status = new QLabel(this);
  m_status->setWordWrap(true);
  layout->addWidget(m_status);
  auto *buttons = new QHBoxLayout;
  auto *run     = new QPushButton(tr("Simulate"), this);
  m_bakeButton  = new QPushButton(tr("Bake X/Y/Rotation Keys"), this);
  auto *close   = new QPushButton(tr("Close"), this);
  buttons->addWidget(run);
  buttons->addWidget(m_bakeButton);
  buttons->addWidget(close);
  layout->addLayout(buttons);
  connect(reload, &QPushButton::clicked, this,
          &BulletPhysicsPopup::reloadSelection);
  connect(run, &QPushButton::clicked, this, &BulletPhysicsPopup::simulate);
  connect(m_bakeButton, &QPushButton::clicked, this, &BulletPhysicsPopup::bake);
  connect(close, &QPushButton::clicked, this, &QDialog::close);
  for (auto *f : {m_first, m_last, m_substeps})
    connect(f, qOverload<int>(&QSpinBox::valueChanged), this,
            [this] { invalidate(); });
  for (auto *f :
       {m_gravity, m_width, m_mass, m_scale, m_floorY, m_lower, m_upper})
    connect(f, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this] { invalidate(); });
  connect(m_floor, &QCheckBox::toggled, this, [this] { invalidate(); });
  connect(m_frame, &QSlider::valueChanged, this, [this](int value) {
    m_preview->frame = value;
    m_preview->update();
  });
  auto reset = [this] {
    m_columns.clear();
    m_table->setRowCount(0);
    invalidate();
  };
  auto *app = TApp::instance();
  connect(app->getCurrentXsheet(), &TXsheetHandle::xsheetSwitched, this, reset);
  connect(app->getCurrentXsheet(), &TXsheetHandle::xsheetChanged, this, reset);
  connect(app->getCurrentScene(), &TSceneHandle::sceneSwitched, this, reset);
  connect(app->getCurrentScene(), &TSceneHandle::sceneChanged, this,
          [this] { invalidate(); });
  invalidate();
}

void BulletPhysicsPopup::invalidate() {
  m_bake.clear();
  m_bakeButton->setEnabled(false);
  m_preview->clip.clear();
  m_preview->update();
  m_frame->setRange(0, 0);
  m_status->setText(
      tr("Simulate before baking. Baking replaces X/Y/rotation "
         "keys in the chosen range and supports Undo/Redo."));
}

void BulletPhysicsPopup::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);
  auto *app = TApp::instance();
  int first = app->getCurrentFrame()->getFrame();
  int last  = first + 71;
  if (auto *selection = dynamic_cast<TCellSelection *>(
          app->getCurrentSelection()->getSelection())) {
    auto range = selection->getSelectedCells();
    if (!range.isEmpty()) {
      first = range.m_r0;
      if (range.m_r1 > first) last = range.m_r1;
    }
  }
  m_first->setValue(first + 1);
  m_last->setValue(std::max(first + 1, last + 1));
  reloadSelection();
}

void BulletPhysicsPopup::reloadSelection() {
  invalidate();
  auto *app    = TApp::instance();
  auto *xsheet = app->getCurrentXsheet()->getXsheet();
  std::set<int> columns;
  auto *selection = app->getCurrentSelection()->getSelection();
  if (auto *s = dynamic_cast<TColumnSelection *>(selection))
    columns = s->getIndices();
  else if (auto *s = dynamic_cast<TCellSelection *>(selection)) {
    auto range = s->getSelectedCells();
    if (!range.isEmpty())
      for (int c = range.m_c0; c <= range.m_c1; ++c) columns.insert(c);
  }
  if (columns.empty())
    columns.insert(app->getCurrentColumn()->getColumnIndex());
  m_columns.clear();
  for (int c : columns)
    if (c >= 0 && xsheet->getColumn(c) &&
        xsheet->getColumn(c)->getLevelColumn())
      m_columns.push_back(c);
  m_table->setRowCount(int(m_columns.size()));
  if (m_columns.empty() || m_columns.size() > 64) {
    m_status->setText(tr("Select 1 to 64 drawing columns in the Xsheet."));
    return;
  }
  std::vector<TStageObject *> objects;
  for (int c : m_columns)
    objects.push_back(xsheet->getStageObject(TStageObjectId::ColumnId(c)));
  for (size_t i = 0; i < objects.size(); ++i) {
    auto *object = objects[i];
    auto *name =
        new QTableWidgetItem(QString::fromStdString(object->getFullName()));
    name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    m_table->setItem(int(i), 0, name);
    auto *motion = new QComboBox(m_table);
    motion->addItems({tr("Dynamic"), tr("Pinned")});
    auto *target = new QComboBox(m_table);
    target->addItem(tr("Free"), -2);
    target->addItem(tr("World"), -1);
    for (size_t j = 0; j < objects.size(); ++j)
      if (j != i)
        target->addItem(QString::fromStdString(objects[j]->getFullName()),
                        int(j));
    auto *joint = new QComboBox(m_table);
    joint->addItems({tr("Hinge"), tr("Point"), tr("Fixed")});
    TAffine placement = object->getPlacement(m_first->value() - 1);
    TPointD pivot =
        placement * (object->getCenter(m_first->value() - 1) * Stage::inch);
    double length    = 1;
    double direction = std::atan2(placement.a22, placement.a12) * 180 / pi - 90;
    bool hasChild    = false;
    for (auto *child : objects)
      if (child->getParent() == object->getId()) {
        TPointD tip = child->getPlacement(m_first->value() - 1) *
                      (child->getCenter(m_first->value() - 1) * Stage::inch);
        TPointD vector = (tip - pivot) / Stage::inch;
        if (norm(vector) > 0.1) {
          length    = norm(vector);
          direction = std::atan2(vector.y, vector.x) * 180 / pi - 90;
        }
        hasChild = true;
        break;
      }
    int parent = -1;
    for (size_t j = 0; j < objects.size(); ++j)
      if (objects[j]->getId() == object->getParent()) parent = int(j);
    if (parent >= 0)
      target->setCurrentIndex(target->findData(parent));
    else if (hasChild)
      motion->setCurrentIndex(1);
    auto *lengthField    = number(m_table, 0.1, 100, length);
    auto *directionField = number(m_table, -360, 360, direction);
    for (auto *field : {lengthField, directionField})
      connect(field, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
              [this] { invalidate(); });
    for (auto *field : {motion, target, joint})
      connect(field, qOverload<int>(&QComboBox::currentIndexChanged), this,
              [this] { invalidate(); });
    m_table->setCellWidget(int(i), 1, motion);
    m_table->setCellWidget(int(i), 2, target);
    m_table->setCellWidget(int(i), 3, joint);
    m_table->setCellWidget(int(i), 4, lengthField);
    m_table->setCellWidget(int(i), 5, directionField);
  }
}

void BulletPhysicsPopup::simulate() {
  invalidate();
  auto fail = [this](const QString &error) { m_status->setText(error); };
  if (!TApp::instance()->getCurrentFrame()->isEditingScene()) {
    fail(tr("Switch to scene editing before simulating columns."));
    return;
  }
  if (m_columns.empty() || m_columns.size() > 64 ||
      m_last->value() < m_first->value() ||
      m_last->value() - m_first->value() >= 1200) {
    fail(tr("Use 1 to 64 drawing columns and a range of 1 to 1200 frames."));
    return;
  }
  auto *app    = TApp::instance();
  auto *xsheet = app->getCurrentXsheet()->getXsheet();
  std::vector<TStageObject *> objects;
  for (int c : m_columns) {
    auto *column = xsheet->getColumn(c);
    if (!column || column->isLocked()) {
      fail(tr("Unlock the selected drawing columns first."));
      return;
    }
    auto *object = xsheet->getStageObject(TStageObjectId::ColumnId(c));
    std::set<TStageObject *> visited;
    for (auto *ancestor = object; ancestor;) {
      if (!visited.insert(ancestor).second ||
          ancestor->getStatus() != TStageObject::XY ||
          ancestor->isCycleEnabled()) {
        fail(
            tr("This exploration requires XY parenting without paths, IK "
               "or cyclic animation throughout the selected hierarchy."));
        return;
      }
      ancestor = xsheet->getStageObjectTree()->getStageObject(
          ancestor->getParent(), false);
    }
    objects.push_back(object);
  }
  OTBullet::Settings settings;
  settings.fps = app->getCurrentScene()
                     ->getScene()
                     ->getProperties()
                     ->getOutputProperties()
                     ->getFrameRate();
  settings.frames   = m_last->value() - m_first->value() + 1;
  settings.substeps = m_substeps->value();
  settings.gravity  = m_gravity->value();
  settings.floor    = m_floor->isChecked();
  settings.floorY   = m_floorY->value() * m_scale->value();
  std::string error;
  std::vector<OTBullet::Binding> bindings;
  if (!OTBullet::capture(objects, m_first->value() - 1, settings.frames,
                         bindings, error)) {
    fail(QString::fromStdString(error));
    return;
  }
  std::vector<OTBullet::Body> bodies;
  std::vector<OTBullet::Connection> connections;
  for (int i = 0; i < int(bindings.size()); ++i) {
    const auto &source = bindings[i].source.front();
    TPointD pivot =
        source.world * source.center / Stage::inch * m_scale->value();
    OTBullet::Body body;
    body.pose.pivot = {pivot.x, pivot.y};
    body.pose.angle =
        static_cast<QDoubleSpinBox *>(m_table->cellWidget(i, 5))->value() * pi /
        180;
    body.length =
        static_cast<QDoubleSpinBox *>(m_table->cellWidget(i, 4))->value() *
        m_scale->value();
    body.width = m_width->value() * m_scale->value();
    body.mass  = m_mass->value();
    body.pinned =
        static_cast<QComboBox *>(m_table->cellWidget(i, 1))->currentIndex() ==
        1;
    bodies.push_back(body);
    int target = static_cast<QComboBox *>(m_table->cellWidget(i, 2))
                     ->currentData()
                     .toInt();
    if (target < -1) continue;
    OTBullet::Connection connection;
    connection.body   = i;
    connection.target = target;
    connection.anchor = body.pose.pivot;
    connection.type   = static_cast<OTBullet::Connection::Type>(
        static_cast<QComboBox *>(m_table->cellWidget(i, 3))->currentIndex());
    connection.lowerAngle = m_lower->value() * pi / 180;
    connection.upperAngle = m_upper->value() * pi / 180;
    connections.push_back(connection);
  }
  OTBullet::Clip clip;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  bool ok = OTBullet::simulate(bodies, connections, settings, clip, error);
  QApplication::restoreOverrideCursor();
  if (!ok) {
    fail(QString::fromStdString(error));
    return;
  }
  m_preview->bodies      = bodies;
  m_preview->connections = connections;
  m_preview->clip        = clip;
  m_preview->settings    = settings;
  m_preview->fit();
  m_frame->setRange(0, settings.frames - 1);
  if (!OTBullet::makeBake(bindings, bodies, clip, m_first->value() - 1,
                          m_scale->value(), m_bake, error)) {
    fail(tr("Preview only: %1").arg(QString::fromStdString(error)));
    return;
  }
  m_bakeButton->setEnabled(!m_bake.empty());
  m_status->setText(tr("Preview frames %1–%2. Bake will replace X/Y/rotation "
                       "keys on dynamic columns; adjacent interpolation may "
                       "change. Connections are kept for this dialog session.")
                        .arg(m_first->value())
                        .arg(m_last->value()));
}

void BulletPhysicsPopup::bake() {
  if (m_bake.empty()) return;
  // Signals emitted by redo invalidate the dialog. Give Undo its own snapshot
  // before any channel is changed or any notification is sent.
  auto *undo = new BulletBakeUndo(m_bake);
  undo->redo();
  TUndoManager::manager()->add(undo);
  accept();
}

OpenPopupCommandHandler<BulletPhysicsPopup> openBulletPhysics(
    MI_ExploreBulletPhysics);
