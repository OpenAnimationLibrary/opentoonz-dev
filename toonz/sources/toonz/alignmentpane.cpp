#include "alignmentpane.h"

#include "menubarcommandids.h"
#include "tapp.h"

#include "tools/tool.h"
#include "tools/toolcommandids.h"
#include "tools/toolhandle.h"

#include "toonzqt/menubarcommand.h"

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>

namespace {

QPushButton *createCommandButton(QWidget *parent, const char *commandId,
                                 const QString &tooltip) {
  QAction *action = CommandManager::instance()->getAction(commandId);
  auto *button     = new QPushButton(parent);
  button->setFixedSize(30, 30);
  button->setToolTip(tooltip);
  button->setIcon(action->icon());
  button->addAction(action);
  QObject::connect(button, &QPushButton::clicked, action, &QAction::trigger);
  return button;
}

}  // namespace

AlignmentPane::AlignmentPane(QWidget *parent) : QFrame(parent) {
  setObjectName("AlignmentPanel");
  m_alignMethod = new QComboBox(this);
  m_alignMethod->addItems(
      {tr("Selection Area"), tr("Smallest Object"), tr("Largest Object"),
       tr("Camera")});

  m_alignLeft = createCommandButton(this, MI_AlignLeft, tr("Align Left"));
  m_alignRight = createCommandButton(this, MI_AlignRight, tr("Align Right"));
  m_alignTop = createCommandButton(this, MI_AlignTop, tr("Align Top"));
  m_alignBottom =
      createCommandButton(this, MI_AlignBottom, tr("Align Bottom"));
  m_alignCenterHorizontal = createCommandButton(
      this, MI_AlignCenterHorizontal, tr("Align Center Horizontally"));
  m_alignCenterVertical = createCommandButton(
      this, MI_AlignCenterVertical, tr("Align Center Vertically"));
  m_distributeHorizontal = createCommandButton(
      this, MI_DistributeHorizontal, tr("Distribute Horizontally"));
  m_distributeVertical = createCommandButton(
      this, MI_DistributeVertical, tr("Distribute Vertically"));

  auto *layout = new QGridLayout(this);
  layout->addWidget(new QLabel(tr("Relative to:"), this), 0, 0);
  layout->addWidget(m_alignMethod, 0, 1);

  auto *alignBox = new QGroupBox(tr("Align"), this);
  auto *alignLayout = new QGridLayout(alignBox);
  alignLayout->addWidget(m_alignLeft, 0, 0);
  alignLayout->addWidget(m_alignCenterVertical, 0, 1);
  alignLayout->addWidget(m_alignRight, 0, 2);
  alignLayout->addWidget(m_alignTop, 1, 0);
  alignLayout->addWidget(m_alignCenterHorizontal, 1, 1);
  alignLayout->addWidget(m_alignBottom, 1, 2);
  layout->addWidget(alignBox, 1, 0, 1, 2);

  auto *distributeBox = new QGroupBox(tr("Distribute"), this);
  auto *distributeLayout = new QGridLayout(distributeBox);
  distributeLayout->addWidget(m_distributeHorizontal, 0, 0);
  distributeLayout->addWidget(m_distributeVertical, 0, 1);
  layout->addWidget(distributeBox, 2, 0, 1, 2);

  connect(m_alignMethod, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &AlignmentPane::onAlignMethodChanged);
}

void AlignmentPane::showEvent(QShowEvent *event) {
  QFrame::showEvent(event);
  updateButtons();
}

void AlignmentPane::hideEvent(QHideEvent *event) { QFrame::hideEvent(event); }

void AlignmentPane::onAlignMethodChanged(int index) {
  if (TTool *tool = TApp::instance()->getCurrentTool()->getTool())
    tool->setAlignMethod(index);
  updateButtons();
}

void AlignmentPane::onLevelSwitched(TXshLevel *) { updateButtons(); }
void AlignmentPane::onSelectionSwitched(TSelection *, TSelection *) {
  updateButtons();
}
void AlignmentPane::onToolSwitched() { updateButtons(); }

void AlignmentPane::updateButtons() {
  const bool enabled = TApp::instance()->getCurrentTool()->getTool()->getName() ==
                       T_Selection;
  m_alignMethod->setEnabled(enabled);
  for (QPushButton *button : {m_alignLeft, m_alignRight, m_alignTop,
                              m_alignBottom, m_alignCenterHorizontal,
                              m_alignCenterVertical, m_distributeHorizontal,
                              m_distributeVertical})
    button->setEnabled(enabled && !button->actions().isEmpty() &&
                       button->actions().front()->isEnabled());
}
