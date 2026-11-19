#include "toonzqt/constraintsimulationpopup.h"

#include "historytypes.h"
#include "tdoublekeyframe.h"
#include "toutputproperties.h"
#include "tundo.h"
#include "toonz/constraintsimulation.h"
#include "toonz/sceneproperties.h"
#include "toonz/tframehandle.h"
#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonzqt/stageschematicscene.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace {
class GravityBakeUndo final : public TUndo {
  ConstraintSimulation::Bake m_bake;
  TXsheetHandle *m_xsheetHandle;
  TSceneHandle *m_sceneHandle;

  void notify() const {
    m_sceneHandle->setDirtyFlag(true);
    m_xsheetHandle->notifyXsheetChanged();
  }

public:
  GravityBakeUndo(ConstraintSimulation::Bake bake, StageSchematicScene *scene)
      : m_bake(std::move(bake))
      , m_xsheetHandle(scene->getXsheetHandle())
      , m_sceneHandle(scene->getSceneHandle()) {}
  void undo() const override {
    ConstraintSimulation::apply(m_bake, false);
    notify();
  }
  void redo() const override {
    ConstraintSimulation::apply(m_bake, true);
    notify();
  }
  int getSize() const override {
    int size = sizeof(*this);
    for (const auto &edit : m_bake.channels)
      size +=
          (edit.before->getKeyframeCount() + edit.after->getKeyframeCount()) *
          sizeof(TDoubleKeyframe);
    return size;
  }
  QString getHistoryString() override {
    return QObject::tr("Simulate and Bake Gravity");
  }
  int getHistoryType() override { return HistoryType::Xsheet; }
};
}  // namespace

void runConstraintSimulation(StageSchematicScene *scene) {
  if (!scene->getFrameHandle()->isEditingScene()) return;
  TXsheet *xsheet = scene->getXsheet();
  int mobile      = 0;
  auto *tree      = xsheet->getStageObjectTree();
  for (int i = 0; i < tree->getStageObjectCount(); ++i)
    mobile += tree->getStageObject(i)->getSimulationMotion() ==
              TStageObject::Constraint::Mobile;

  QDialog dialog(scene->getSchematicViewer());
  dialog.setWindowTitle(QObject::tr("Simulate and Bake Gravity"));
  dialog.setMinimumWidth(420);
  auto *layout = new QVBoxLayout(&dialog);
  auto *intro  = new QLabel(
       QObject::tr("%n Mobile object(s). Mobile objects fall from rest. Fixed "
                    "and unassigned "
                    "objects keep their authored motion. Aim At stays active.",
                   nullptr, mobile),
       &dialog);
  intro->setWordWrap(true);
  layout->addWidget(intro);
  auto *form  = new QFormLayout;
  auto *first = new QSpinBox(&dialog), *last = new QSpinBox(&dialog);
  first->setRange(1, 1000001);
  last->setRange(1, 1000001);
  int start = scene->getFrameHandle()->getFrame();
  first->setValue(start + 1);
  int end = xsheet->getFrameCount() > start + 1 ? xsheet->getFrameCount()
                                                : start + 72;
  last->setValue(std::min(start + 1200, end));
  auto *gravity = new QDoubleSpinBox(&dialog);
  gravity->setRange(0, 1000);
  gravity->setDecimals(3);
  gravity->setValue(9.81);
  form->addRow(QObject::tr("First frame:"), first);
  form->addRow(QObject::tr("Last frame:"), last);
  form->addRow(QObject::tr("Gravity (in/s²):"), gravity);
  layout->addLayout(form);
  auto *status = new QLabel(
      QObject::tr(
          "Baking replaces affected position/rotation keys in this range. "
          "Fixed children receive compensation when needed to keep their "
          "authored "
          "world motion. One Undo restores all affected channels."),
      &dialog);
  status->setWordWrap(true);
  layout->addWidget(status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
  auto *bake    = buttons->addButton(QObject::tr("Simulate and Bake"),
                                     QDialogButtonBox::AcceptRole);
  bake->setEnabled(mobile > 0);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  QObject::connect(bake, &QPushButton::clicked, &dialog, [&] {
    if (scene->getXsheet() != xsheet) {
      dialog.reject();
      return;
    }
    ConstraintSimulation::Settings settings;
    settings.firstFrame = first->value() - 1;
    settings.lastFrame  = last->value() - 1;
    settings.gravity    = gravity->value();
    settings.fps        = xsheet->getScene()
                       ->getProperties()
                       ->getOutputProperties()
                       ->getFrameRate();
    ConstraintSimulation::Bake result;
    std::string error;
    if (!ConstraintSimulation::prepare(*xsheet, settings, result, error)) {
      status->setText(QString::fromStdString(error));
      return;
    }
    auto *undo = new GravityBakeUndo(std::move(result), scene);
    undo->redo();
    TUndoManager::manager()->add(undo);
    dialog.accept();
  });
  dialog.exec();
}
