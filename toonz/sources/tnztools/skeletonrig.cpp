#include "skeletonrig.h"

#include "tools/tool.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/txsheet.h"
#include "toonz/tscenehandle.h"
#include "toonz/tobjecthandle.h"
#include "toonz/tframehandle.h"
#include "toonz/txsheethandle.h"
#include "toonz/stage.h"
#include "tundo.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
void notifyRig(TXsheet *xsheet) {
  auto app = TTool::getApplication();
  app->getCurrentScene()->setDirtyFlag(true);
  if (app->getCurrentXsheet()->getXsheet() == xsheet) {
    app->getCurrentXsheet()->notifyXsheetChanged();
    app->getCurrentObject()->notifyObjectIdChanged(false);
  }
}

class BoneEditUndo final : public TUndo {
  TXsheetP m_xsheet;
  TStageObjectP m_object;
  TRigBone m_before, m_after;
  std::string m_oldName, m_newName;

  void apply(const TRigBone &bone, const std::string &name) const {
    // Replay the recorded state, including an unresolved target from a scene.
    m_object->assignRigBone(bone);
    m_object->setName(name);
    notifyRig(m_xsheet.getPointer());
  }

public:
  BoneEditUndo(TXsheet *xsheet, TStageObject *object, const TRigBone &before,
               const TRigBone &after, const std::string &oldName,
               const std::string &newName)
      : m_xsheet(xsheet)
      , m_object(object)
      , m_before(before)
      , m_after(after)
      , m_oldName(oldName)
      , m_newName(newName) {}
  void undo() const override { apply(m_before, m_oldName); }
  void redo() const override { apply(m_after, m_newName); }
  int getSize() const override {
    size_t bytes = sizeof(*this);
    for (const auto *bone : {&m_before, &m_after})
      for (const auto &c : bone->constraints)
        bytes +=
            sizeof(c) + c.targetId.size() +
            c.enforcementKeys.size() * (sizeof(std::pair<int, double>) + 32);
    return int(std::min(bytes, size_t(std::numeric_limits<int>::max())));
  }
  QString getHistoryString() override {
    return QObject::tr("Edit Bone and Constraints");
  }
};

class SkeletonRigDialog final : public QDialog {
  Q_DECLARE_TR_FUNCTIONS(SkeletonRigDialog)

  TXsheet *m_xsheet;
  TStageObject *m_object;
  TRigBone m_bone;
  int m_frame, m_row = -1;
  bool m_loading = false, m_weightEdited = false;
  QLineEdit *m_name;
  QCheckBox *m_boneEnabled, *m_enabled;
  QDoubleSpinBox *m_tipX, *m_tipY, *m_weight, *m_offsetX, *m_offsetY, *m_angle;
  QDoubleSpinBox *m_minX, *m_minY, *m_maxX, *m_maxY, *m_min, *m_max;
  QSpinBox *m_first, *m_last;
  QListWidget *m_list;
  QComboBox *m_kind, *m_target, *m_endpoint;
  QGroupBox *m_properties;
  QLabel *m_status;

  static QDoubleSpinBox *number(double low = -10000, double high = 10000) {
    auto *spin = new QDoubleSpinBox;
    spin->setDecimals(4);
    spin->setRange(low, high);
    spin->setSingleStep(0.1);
    return spin;
  }

  QString typeName(int type) const {
    switch (type) {
    case TRigConstraint::TranslateTo:
      return tr("Translate To");
    case TRigConstraint::OrientLike:
      return tr("Orient Like");
    case TRigConstraint::AimAt:
      return tr("Aim At");
    case TRigConstraint::ParentLike:
      return tr("Parent Like");
    case TRigConstraint::Distance:
      return tr("Distance Limit");
    case TRigConstraint::LimitPosition:
      return tr("Position Limit");
    case TRigConstraint::LimitRotation:
      return tr("Rotation Limit");
    case TRigConstraint::LimitScale:
      return tr("Scale Limit");
    default:
      return QString();
    }
  }

  TRigConstraint *current() {
    return m_row >= 0 && m_row < int(m_bone.constraints.size())
               ? &m_bone.constraints[m_row]
               : nullptr;
  }

  void saveCurrent() {
    auto *c = current();
    if (!c || m_loading) return;
    c->enabled  = m_enabled->isChecked();
    c->targetId = m_target->currentData().toString().toStdString();
    c->endpoint =
        static_cast<TRigConstraint::Endpoint>(m_endpoint->currentIndex());
    if (m_weightEdited) {
      if (c->enforcementKeys.empty())
        c->enforcement = m_weight->value() / 100;
      else
        c->enforcementKeys[m_frame] = m_weight->value() / 100;
    }
    m_weightEdited = false;
    c->firstFrame  = m_first->value() - 1;
    c->lastFrame   = m_last->value() - 1;
    c->offset      = TPointD(m_offsetX->value(), m_offsetY->value());
    c->angleOffset = m_angle->value();
    c->minimum     = TPointD(m_minX->value(), m_minY->value());
    c->maximum     = TPointD(m_maxX->value(), m_maxY->value());
    c->minValue    = m_min->value();
    c->maxValue    = m_max->value();
  }

  void selectRow(int row) {
    saveCurrent();
    m_row     = row;
    m_loading = true;
    auto *c   = current();
    m_properties->setEnabled(c != nullptr);
    if (c) {
      m_properties->setTitle(typeName(c->type));
      m_enabled->setChecked(c->enabled);
      m_target->clear();
      m_target->addItem(tr("Choose a target"), QString());
      auto *tree = m_xsheet->getStageObjectTree();
      for (int i = 0; i < tree->getStageObjectCount(); ++i) {
        auto *object = tree->getStageObject(i);
        if (object == m_object) continue;
        m_target->addItem(QString::fromStdString(object->getFullName()),
                          QString::fromStdString(object->getRigId()));
      }
      QString target = QString::fromStdString(c->targetId);
      int index      = m_target->findData(target);
      if (index < 0) {
        m_target->addItem(tr("Missing target"), target);
        index = m_target->count() - 1;
      }
      m_target->setCurrentIndex(index);
      m_target->setEnabled(c->hasTarget());
      m_endpoint->setCurrentIndex(c->endpoint);
      m_endpoint->setEnabled(c->hasTarget());
      m_weight->setValue(c->weight(m_frame) * 100);
      // Show the editable enforcement even when muted or outside its range.
      TRigConstraint active = *c;
      active.enabled        = true;
      active.firstFrame = active.lastFrame = -1;
      m_weight->setValue(active.weight(m_frame) * 100);
      m_first->setValue(c->firstFrame + 1);
      m_last->setValue(c->lastFrame + 1);
      m_offsetX->setValue(c->offset.x);
      m_offsetY->setValue(c->offset.y);
      m_angle->setValue(c->angleOffset);
      m_minX->setValue(c->minimum.x);
      m_minY->setValue(c->minimum.y);
      m_maxX->setValue(c->maximum.x);
      m_maxY->setValue(c->maximum.y);
      m_min->setValue(c->minValue);
      m_max->setValue(c->maxValue);
      bool position = c->type == TRigConstraint::LimitPosition;
      for (auto *field : {m_minX, m_minY, m_maxX, m_maxY})
        field->setEnabled(position);
      bool scalar = c->type == TRigConstraint::Distance ||
                    c->type == TRigConstraint::LimitRotation ||
                    c->type == TRigConstraint::LimitScale;
      m_min->setEnabled(scalar);
      m_max->setEnabled(scalar);
      m_offsetX->setEnabled(c->type == TRigConstraint::TranslateTo ||
                            c->type == TRigConstraint::ParentLike);
      m_offsetY->setEnabled(m_offsetX->isEnabled());
      m_angle->setEnabled(c->type == TRigConstraint::AimAt ||
                          c->type == TRigConstraint::OrientLike ||
                          c->type == TRigConstraint::ParentLike);
      QString problem =
          QString::fromStdString(m_object->rigConstraintProblem(*c));
      m_status->setText(tr("Frame %1. %2 enforcement keys. %3")
                            .arg(m_frame + 1)
                            .arg(c->enforcementKeys.size())
                            .arg(problem.isEmpty() ? tr("Ready") : problem));
    }
    m_weightEdited = false;
    m_loading      = false;
  }

  void rebuild(int row) {
    m_loading = true;
    m_list->blockSignals(true);
    m_list->clear();
    for (const auto &c : m_bone.constraints)
      m_list->addItem((c.enabled ? QString() : tr("[Off] ")) +
                      typeName(c.type));
    m_list->setCurrentRow(row);
    m_list->blockSignals(false);
    m_row     = -1;
    m_loading = false;
    selectRow(row);
  }

  void captureOffset() {
    saveCurrent();
    auto *c = current();
    if (!c || !c->hasTarget()) return;
    auto *target = m_xsheet->getStageObjectTree()->getRigObject(c->targetId);
    if (!target) return;
    TAffine a          = m_object->getPlacement(m_frame);
    TAffine b          = target->getPlacement(m_frame);
    TPointD p          = a * (m_object->getCenter(m_frame) * Stage::inch);
    TPointD q          = b * (target->getCenter(m_frame) * Stage::inch);
    TPointD targetAxis = b * (target->getCenter(m_frame) * Stage::inch +
                              target->getRigBone().tip * Stage::inch) -
                         q;
    TPointD ownerAxis =
        a * (m_object->getCenter(m_frame) * Stage::inch +
             TPointD(m_tipX->value(), m_tipY->value()) * Stage::inch) -
        p;
    if (c->endpoint == TRigConstraint::Tip) q += targetAxis;
    if (norm2(ownerAxis) < 1e-12 || norm2(targetAxis) < 1e-12) return;
    c->offset = (p - q) / Stage::inch;
    if (c->type == TRigConstraint::ParentLike)
      c->offset = TRotation(-rigAngle(targetAxis)) * c->offset;
    TPointD direction = c->type == TRigConstraint::AimAt ? q - p : targetAxis;
    if (norm2(direction) > 1e-12)
      c->angleOffset =
          std::remainder(rigAngle(ownerAxis) - rigAngle(direction), 360.0);
    rebuild(m_row);
  }

  void accept() override {
    saveCurrent();
    m_bone.enabled = m_boneEnabled->isChecked();
    m_bone.tip     = TPointD(m_tipX->value(), m_tipY->value());
    for (const auto &c : m_bone.constraints)
      if (c.hasTarget() && c.targetId.empty()) {
        QMessageBox::warning(this, tr("Bone and Constraints"),
                             tr("Choose a target for each target constraint."));
        return;
      }
    TRigBone before     = m_object->getRigBone();
    std::string oldName = m_object->getName();
    std::string error;
    if (!m_object->setRigBone(m_bone, &error)) {
      QMessageBox::warning(this, tr("Bone and Constraints"),
                           QString::fromStdString(error));
      return;
    }
    std::string name = m_name->text().trimmed().toStdString();
    m_object->setName(name);
    SkeletonRig::recordEdit(m_xsheet, m_object, before, m_bone, oldName, name);
    QDialog::accept();
  }

public:
  SkeletonRigDialog(TXsheet *xsheet, TStageObject *object, QWidget *parent)
      : QDialog(parent)
      , m_xsheet(xsheet)
      , m_object(object)
      , m_bone(object->getRigBone())
      , m_frame(TTool::getApplication()->getCurrentFrame()->getFrame()) {
    setWindowTitle(tr("Bone and Constraints — %1")
                       .arg(QString::fromStdString(object->getFullName())));
    resize(760, 690);
    setMinimumSize(620, 480);
    auto *layout = new QVBoxLayout(this);
    auto *hint   = new QLabel(
          tr("Build Skeleton: drag tips to edit bones; Ctrl-drag a bone body to "
               "reparent. Animate: drag bones to rotate; Ctrl-drag to move. "
               "Constraints evaluate from top to bottom."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *boneForm = new QFormLayout;
    m_name         = new QLineEdit(QString::fromStdString(object->getName()));
    m_boneEnabled  = new QCheckBox(tr("Use persistent bone tip"));
    m_boneEnabled->setChecked(m_bone.enabled);
    m_tipX = number();
    m_tipY = number();
    if (!m_bone.enabled && m_bone.constraints.empty()) {
      TStageObject *first = nullptr;
      for (auto *child : object->getChildren())
        if (child->getId().isColumn() &&
            (!first || child->getId().getIndex() < first->getId().getIndex()))
          first = child;
      TAffine placement = object->getPlacement(m_frame);
      if (first && std::abs(placement.det()) > 1e-10) {
        TPointD tip = placement.inv() * first->getPlacement(m_frame) *
                      (first->getCenter(m_frame) * Stage::inch);
        tip = tip / Stage::inch - object->getCenter(m_frame);
        if (norm2(tip) > 1e-8) m_bone.tip = tip;
      }
    }
    m_tipX->setValue(m_bone.tip.x);
    m_tipY->setValue(m_bone.tip.y);
    boneForm->addRow(tr("Bone name:"), m_name);
    boneForm->addRow(m_boneEnabled);
    auto *tips = new QHBoxLayout;
    tips->addWidget(m_tipX);
    tips->addWidget(m_tipY);
    boneForm->addRow(tr("Tip X / Y (inches):"), tips);
    layout->addLayout(boneForm);
    auto *columns = new QHBoxLayout;
    auto *stack   = new QVBoxLayout;
    m_list        = new QListWidget;
    stack->addWidget(m_list, 1);
    m_kind = new QComboBox;
    for (int type = 0; type < TRigConstraint::TypeCount; ++type)
      m_kind->addItem(typeName(type));
    stack->addWidget(m_kind);
    auto *add = new QPushButton(tr("Add Constraint"));
    stack->addWidget(add);
    auto *remove = new QPushButton(tr("Remove"));
    stack->addWidget(remove);
    auto *order = new QHBoxLayout;
    auto *up    = new QPushButton(tr("Move Up"));
    auto *down  = new QPushButton(tr("Move Down"));
    order->addWidget(up);
    order->addWidget(down);
    stack->addLayout(order);
    columns->addLayout(stack, 1);
    m_properties = new QGroupBox(tr("Constraint"));
    auto *form   = new QFormLayout(m_properties);
    m_enabled    = new QCheckBox(tr("Enabled"));
    form->addRow(m_enabled);
    m_target = new QComboBox;
    form->addRow(tr("Target:"), m_target);
    m_endpoint = new QComboBox;
    m_endpoint->addItems({tr("Pivot"), tr("Bone Tip")});
    form->addRow(tr("Target point:"), m_endpoint);
    m_weight = number(0, 100);
    m_weight->setSuffix(tr(" %"));
    m_weight->setToolTip(
        tr("Unkeyed enforcement is constant. After creating a key, editing "
           "enforcement adds or updates a Hold key at this frame."));
    form->addRow(tr("Enforcement:"), m_weight);
    auto *keys  = new QHBoxLayout;
    auto *key   = new QPushButton(tr("Set Key"));
    auto *unkey = new QPushButton(tr("Remove Key"));
    keys->addWidget(key);
    keys->addWidget(unkey);
    form->addRow(keys);
    m_first = new QSpinBox;
    m_last  = new QSpinBox;
    for (auto *spin : {m_first, m_last}) {
      spin->setRange(0, 10000000);
      spin->setSpecialValueText(tr("All"));
    }
    form->addRow(tr("Start frame:"), m_first);
    form->addRow(tr("End frame:"), m_last);
    m_offsetX = number();
    m_offsetY = number();
    m_angle   = number(-360000, 360000);
    form->addRow(tr("Offset X (inches):"), m_offsetX);
    form->addRow(tr("Offset Y (inches):"), m_offsetY);
    form->addRow(tr("Angle offset (degrees):"), m_angle);
    auto *capture = new QPushButton(tr("Capture Current Offset"));
    capture->setToolTip(
        tr("Keep the pose currently visible in the viewer when following the "
           "selected target."));
    form->addRow(capture);
    m_minX = number();
    m_minY = number();
    m_maxX = number();
    m_maxY = number();
    form->addRow(tr("Minimum X (inches):"), m_minX);
    form->addRow(tr("Maximum X (inches):"), m_maxX);
    form->addRow(tr("Minimum Y (inches):"), m_minY);
    form->addRow(tr("Maximum Y (inches):"), m_maxY);
    m_min = number(-10000, 10000);
    m_max = number(-10000, 10000);
    m_min->setToolTip(
        tr("Distance: inches. Rotation: degrees in [-180, 180]. Scale: "
           "positive factors."));
    m_max->setToolTip(m_min->toolTip());
    form->addRow(tr("Minimum:"), m_min);
    form->addRow(tr("Maximum:"), m_max);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    form->addRow(m_status);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_properties);
    columns->addWidget(scroll, 2);
    layout->addLayout(columns, 1);
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this,
            &SkeletonRigDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int row) { selectRow(row); });
    connect(m_weight, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) {
              if (!m_loading) m_weightEdited = true;
            });
    connect(add, &QPushButton::clicked, this, [this] {
      saveCurrent();
      if (m_bone.constraints.size() >= 128) return;
      m_boneEnabled->setChecked(true);
      TRigConstraint c;
      c.type = static_cast<TRigConstraint::Type>(m_kind->currentIndex());
      if (c.type == TRigConstraint::LimitRotation) {
        c.minValue = -90;
        c.maxValue = 90;
      }
      if (c.type == TRigConstraint::LimitScale) {
        c.minValue = 0.01;
        c.maxValue = 10;
      }
      m_bone.constraints.push_back(c);
      rebuild(int(m_bone.constraints.size()) - 1);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
      if (!current()) return;
      int row = m_row;
      m_bone.constraints.erase(m_bone.constraints.begin() + row);
      rebuild(std::min(row, int(m_bone.constraints.size()) - 1));
    });
    for (auto move : {std::make_pair(up, -1), std::make_pair(down, 1)})
      connect(move.first, &QPushButton::clicked, this, [this, move] {
        saveCurrent();
        int next = m_row + move.second;
        if (!current() || next < 0 || next >= int(m_bone.constraints.size()))
          return;
        std::swap(m_bone.constraints[m_row], m_bone.constraints[next]);
        rebuild(next);
      });
    connect(capture, &QPushButton::clicked, this, [this] { captureOffset(); });
    connect(key, &QPushButton::clicked, this, [this] {
      saveCurrent();
      if (!current()) return;
      current()->enforcementKeys[m_frame] = m_weight->value() / 100;
      rebuild(m_row);
    });
    connect(unkey, &QPushButton::clicked, this, [this] {
      saveCurrent();
      if (!current()) return;
      current()->enforcementKeys.erase(m_frame);
      rebuild(m_row);
    });
    rebuild(m_bone.constraints.empty() ? -1 : 0);
  }
};
}  // namespace

void SkeletonRig::recordEdit(TXsheet *xsheet, TStageObject *object,
                             const TRigBone &before, const TRigBone &after,
                             const std::string &oldName,
                             const std::string &newName) {
  TUndoManager::manager()->add(
      new BoneEditUndo(xsheet, object, before, after, oldName, newName));
  notifyRig(xsheet);
}

void SkeletonRig::edit(TXsheet *xsheet, TStageObject *object, QWidget *parent) {
  SkeletonRigDialog dialog(xsheet, object, parent);
  dialog.exec();
}
