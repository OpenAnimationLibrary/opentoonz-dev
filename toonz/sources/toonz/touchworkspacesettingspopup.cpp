#include "touchworkspacesettingspopup.h"

#include "touchpresetpopup.h"
#include "menubarcommandids.h"
#include "sceneviewer.h"
#include "tapp.h"
#include "touchpresetmanager.h"

#include "toonz/preferences.h"
#include "toonz/preferencesitemids.h"
#include "toonzqt/menubarcommand.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QRadioButton>
#include <QShowEvent>
#include <QSlider>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {

QPointer<TouchWorkspaceSettingsPopup> s_workspaceSettingsPopup;

void syncOpenTouchPresetPicker(bool rebuildLayout) {
  SceneViewer *viewer = TApp::instance()->getActiveViewer();
  if (viewer) viewer->syncTouchPresetPickerFromSettings(rebuildLayout);
}

}  // namespace

//-----------------------------------------------------------------------------

TouchWorkspaceSettingsPopup::TouchWorkspaceSettingsPopup(QWidget *parent)
    : DVGui::Dialog(parent ? parent : TApp::instance()->getMainWindow(), true,
                    false, "TouchWorkspaceSettings") {
  setModal(false);
  setAttribute(Qt::WA_DeleteOnClose, false);
  setWindowTitle(tr("Touch Workspace Settings"));

  auto *pickerGroup = new QGroupBox(tr("Touch preset picker"), this);
  auto *pickerLay   = new QVBoxLayout(pickerGroup);
  pickerLay->setSpacing(10);
  pickerLay->addSpacing(6);

  auto *layoutLabel = new QLabel(tr("Layout:"), this);
  auto *lookLabel   = new QLabel(tr("Look:"), this);
  m_layoutHorizontal = new QRadioButton(tr("Horizontal list"), this);
  m_layoutVertical   = new QRadioButton(tr("Vertical list"), this);
  m_layoutWheel      = new QRadioButton(tr("Wheel"), this);
  auto *layoutGroup  = new QButtonGroup(this);
  layoutGroup->addButton(m_layoutHorizontal);
  layoutGroup->addButton(m_layoutVertical);
  layoutGroup->addButton(m_layoutWheel);
  auto *layoutRadios = new QHBoxLayout();
  layoutRadios->setContentsMargins(0, 0, 0, 0);
  layoutRadios->addWidget(m_layoutHorizontal);
  layoutRadios->addWidget(m_layoutVertical);
  layoutRadios->addWidget(m_layoutWheel);
  layoutRadios->addStretch(1);

  m_styleClassic = new QRadioButton(tr("Style A"), this);
  m_styleGhost   = new QRadioButton(tr("Style B"), this);
  auto *styleGroup = new QButtonGroup(this);
  styleGroup->addButton(m_styleClassic);
  styleGroup->addButton(m_styleGhost);
  auto *styleRadios = new QHBoxLayout();
  styleRadios->setContentsMargins(0, 0, 0, 0);
  styleRadios->addWidget(m_styleClassic);
  styleRadios->addWidget(m_styleGhost);
  styleRadios->addStretch(1);

  auto *layoutLookGrid = new QGridLayout();
  layoutLookGrid->setContentsMargins(0, 0, 0, 0);
  layoutLookGrid->addWidget(layoutLabel, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
  layoutLookGrid->addLayout(layoutRadios, 0, 1);
  layoutLookGrid->addWidget(lookLabel, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
  layoutLookGrid->addLayout(styleRadios, 1, 1);
  layoutLookGrid->setColumnStretch(1, 1);
  pickerLay->addLayout(layoutLookGrid);

  connect(m_layoutHorizontal, &QRadioButton::toggled, this,
          &TouchWorkspaceSettingsPopup::onLayoutChanged);
  connect(m_layoutVertical, &QRadioButton::toggled, this,
          &TouchWorkspaceSettingsPopup::onLayoutChanged);
  connect(m_layoutWheel, &QRadioButton::toggled, this,
          &TouchWorkspaceSettingsPopup::onLayoutChanged);
  connect(m_styleClassic, &QRadioButton::toggled, this,
          &TouchWorkspaceSettingsPopup::onStyleChanged);
  connect(m_styleGhost, &QRadioButton::toggled, this,
          &TouchWorkspaceSettingsPopup::onStyleChanged);

  auto *themeBlock = new QVBoxLayout();
  themeBlock->setContentsMargins(0, 0, 0, 0);
  themeBlock->setSpacing(3);
  auto *themeHeading = new QLabel(tr("Color theme:"), this);
  QFont themeHeadingFont = themeHeading->font();
  themeHeadingFont.setBold(true);
  themeHeading->setFont(themeHeadingFont);
  themeBlock->addWidget(themeHeading);
  m_themeCombo = new QComboBox(this);
  m_themeCombo->addItem(
      tr("OpenToonz"),
      static_cast<int>(TouchPresetPickerTheme::OpenToonz));
  m_themeCombo->addItem(tr("Hologram"),
                        static_cast<int>(TouchPresetPickerTheme::HologramCyan));
  m_themeCombo->addItem(
      tr("White & green"),
      static_cast<int>(TouchPresetPickerTheme::WhiteGreen));
  m_themeCombo->addItem(
      tr("Cream & sage"),
      static_cast<int>(TouchPresetPickerTheme::CreamSage));
  themeBlock->addWidget(m_themeCombo);
  pickerLay->addLayout(themeBlock);
  connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &TouchWorkspaceSettingsPopup::onThemeChanged);

  auto *sizeBlock = new QVBoxLayout();
  sizeBlock->setContentsMargins(0, 0, 0, 0);
  sizeBlock->setSpacing(2);
  auto *sizeHeading = new QLabel(tr("Size:"), this);
  QFont sizeHeadingFont = sizeHeading->font();
  sizeHeadingFont.setBold(true);
  sizeHeading->setFont(sizeHeadingFont);
  sizeBlock->addWidget(sizeHeading);
  m_sizeSlider = new QSlider(Qt::Horizontal, this);
  m_sizeSlider->setRange(75, 150);
  m_sizeSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_sizeSlider->setFixedHeight(m_sizeSlider->sizeHint().height());
  sizeBlock->addWidget(m_sizeSlider);
  auto *sizeEndLabels = new QHBoxLayout();
  sizeEndLabels->setContentsMargins(0, 0, 0, 0);
  auto *sizeSmall = new QLabel(tr("Small"), this);
  auto *sizeLarge = new QLabel(tr("Large"), this);
  sizeSmall->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  sizeLarge->setAlignment(Qt::AlignRight | Qt::AlignTop);
  sizeEndLabels->addWidget(sizeSmall, 1);
  sizeEndLabels->addWidget(sizeLarge, 1);
  sizeBlock->addLayout(sizeEndLabels);
  pickerLay->addLayout(sizeBlock);
  connect(m_sizeSlider, &QSlider::sliderPressed, this, [this]() {
    m_livePickerPreview = true;
  });
  connect(m_sizeSlider, &QSlider::sliderReleased, this, [this]() {
    m_livePickerPreview = false;
    syncOpenTouchPresetPicker(false);
  });
  connect(m_sizeSlider, &QSlider::valueChanged, this,
          &TouchWorkspaceSettingsPopup::onSizeChanged);

  auto *opacityBlock = new QVBoxLayout();
  opacityBlock->setContentsMargins(0, 0, 0, 0);
  opacityBlock->setSpacing(2);
  auto *opacityHeading = new QLabel(tr("Transparency:"), this);
  QFont opacityHeadingFont = opacityHeading->font();
  opacityHeadingFont.setBold(true);
  opacityHeading->setFont(opacityHeadingFont);
  opacityBlock->addWidget(opacityHeading);
  m_opacitySlider = new QSlider(Qt::Horizontal, this);
  m_opacitySlider->setRange(25, 100);
  m_opacitySlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_opacitySlider->setFixedHeight(m_opacitySlider->sizeHint().height());
  opacityBlock->addWidget(m_opacitySlider);
  auto *opacityEndLabels = new QHBoxLayout();
  opacityEndLabels->setContentsMargins(0, 0, 0, 0);
  auto *opacityLo = new QLabel(tr("Transparent"), this);
  auto *opacityHi = new QLabel(tr("Opaque"), this);
  opacityLo->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  opacityHi->setAlignment(Qt::AlignRight | Qt::AlignTop);
  opacityEndLabels->addWidget(opacityLo, 1);
  opacityEndLabels->addWidget(opacityHi, 1);
  opacityBlock->addLayout(opacityEndLabels);
  pickerLay->addLayout(opacityBlock);
  connect(m_opacitySlider, &QSlider::sliderPressed, this, [this]() {
    m_livePickerPreview = true;
  });
  connect(m_opacitySlider, &QSlider::sliderReleased, this, [this]() {
    m_livePickerPreview = false;
    syncOpenTouchPresetPicker(false);
  });
  connect(m_opacitySlider, &QSlider::valueChanged, this,
          &TouchWorkspaceSettingsPopup::onOpacityChanged);

  pickerLay->addWidget(new QLabel(tr("Pinned presets (shown in the picker):"), this));
  m_pinnedList = new QListWidget(this);
  m_pinnedList->setMaximumHeight(140);
  pickerLay->addWidget(m_pinnedList);
  connect(m_pinnedList, &QListWidget::itemChanged, this,
          &TouchWorkspaceSettingsPopup::onPinnedItemChanged);

  auto *gestureGroup = new QGroupBox(tr("Gesture recognition"), this);
  auto *gestureLay   = new QVBoxLayout(gestureGroup);
  m_deferTapCheck =
      new QCheckBox(tr("Defer simple tap when a double tap is assigned"), this);
  gestureLay->addWidget(m_deferTapCheck);
  connect(m_deferTapCheck, &QCheckBox::toggled, this,
          &TouchWorkspaceSettingsPopup::onDeferTapChanged);

  m_topLayout->addWidget(pickerGroup);
  m_topLayout->addWidget(gestureGroup);

  QPushButton *openManagerBtn =
      new QPushButton(tr("Touch Preset Manager..."), this);
  QPushButton *closeBtn = new QPushButton(tr("Close"), this);
  addButtonBarWidget(openManagerBtn, closeBtn);
  connect(openManagerBtn, &QPushButton::clicked, this,
          &TouchWorkspaceSettingsPopup::onOpenPresetManager);
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

  loadFromSettings();
}

TouchWorkspaceSettingsPopup::~TouchWorkspaceSettingsPopup() = default;

void TouchWorkspaceSettingsPopup::showEvent(QShowEvent *event) {
  DVGui::Dialog::showEvent(event);
  loadFromSettings();
}

void TouchWorkspaceSettingsPopup::syncDeferTapUiFromPreferences() {
  if (!s_workspaceSettingsPopup || !s_workspaceSettingsPopup->m_deferTapCheck)
    return;
  QSignalBlocker block(s_workspaceSettingsPopup->m_deferTapCheck);
  s_workspaceSettingsPopup->m_deferTapCheck->setChecked(
      Preferences::instance()->isTouchDeferTapForDoubleTapEnabled());
}

bool TouchWorkspaceSettingsPopup::isPanelOpen() {
  return s_workspaceSettingsPopup && s_workspaceSettingsPopup->isVisible();
}

bool TouchWorkspaceSettingsPopup::isLivePickerPreviewActive() {
  return s_workspaceSettingsPopup &&
         s_workspaceSettingsPopup->m_livePickerPreview;
}

void TouchWorkspaceSettingsPopup::showPanel(QWidget *parent) {
  if (!parent) {
    QMainWindow *mw = TApp::instance()->getMainWindow();
    if (mw) parent = mw;
  }
  if (!s_workspaceSettingsPopup)
    s_workspaceSettingsPopup = new TouchWorkspaceSettingsPopup(parent);
  s_workspaceSettingsPopup->show();
  s_workspaceSettingsPopup->raise();
  s_workspaceSettingsPopup->activateWindow();
}

void TouchWorkspaceSettingsPopup::loadFromSettings() {
  TouchPresetManager *mgr = TouchPresetManager::instance();
  QSignalBlocker blockLayout(m_layoutHorizontal);
  QSignalBlocker blockLayout2(m_layoutVertical);
  QSignalBlocker blockLayout3(m_layoutWheel);
  switch (mgr->pickerLayout()) {
  case TouchPresetPickerLayout::Vertical:
    m_layoutVertical->setChecked(true);
    break;
  case TouchPresetPickerLayout::Wheel:
    m_layoutWheel->setChecked(true);
    break;
  default:
    m_layoutHorizontal->setChecked(true);
    break;
  }

  QSignalBlocker blockStyle(m_styleClassic);
  QSignalBlocker blockStyle2(m_styleGhost);
  if (mgr->pickerStyle() == TouchPresetPickerStyle::Ghost)
    m_styleGhost->setChecked(true);
  else
    m_styleClassic->setChecked(true);

  QSignalBlocker blockTheme(m_themeCombo);
  const int themeIdx =
      m_themeCombo->findData(static_cast<int>(mgr->pickerTheme()));
  if (themeIdx >= 0) m_themeCombo->setCurrentIndex(themeIdx);

  QSignalBlocker blockSize(m_sizeSlider);
  m_sizeSlider->setValue(mgr->pickerSizePercent());
  QSignalBlocker blockOpacity(m_opacitySlider);
  m_opacitySlider->setValue(mgr->pickerOpacityPercent());

  QSignalBlocker blockDefer(m_deferTapCheck);
  m_deferTapCheck->setChecked(
      Preferences::instance()->isTouchDeferTapForDoubleTapEnabled());

  rebuildPinnedList();
}

void TouchWorkspaceSettingsPopup::rebuildPinnedList() {
  QSignalBlocker block(m_pinnedList);
  m_pinnedList->clear();
  TouchPresetManager *mgr = TouchPresetManager::instance();
  const QStringList names = mgr->presetNames();
  const int maxPin        = TouchPresetManager::maxPinnedPresetsForPicker();
  const int pinnedCount   = mgr->pinnedPresetsForPicker().size();
  for (const QString &name : names) {
    auto *item = new QListWidgetItem(name, m_pinnedList);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(mgr->isPinnedForPicker(name) ? Qt::Checked
                                                      : Qt::Unchecked);
    const bool canPin =
        mgr->isPinnedForPicker(name) || pinnedCount < maxPin;
    if (!canPin) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
  }
}

void TouchWorkspaceSettingsPopup::onThemeChanged(int index) {
  if (index < 0) return;
  const auto theme = static_cast<TouchPresetPickerTheme>(
      m_themeCombo->itemData(index).toInt());
  TouchPresetManager::instance()->setPickerTheme(theme);
  syncOpenTouchPresetPicker(false);
}

void TouchWorkspaceSettingsPopup::onLayoutChanged(bool checked) {
  if (!checked) return;
  TouchPresetManager *mgr = TouchPresetManager::instance();
  if (m_layoutWheel->isChecked())
    mgr->setPickerLayout(TouchPresetPickerLayout::Wheel);
  else if (m_layoutVertical->isChecked())
    mgr->setPickerLayout(TouchPresetPickerLayout::Vertical);
  else
    mgr->setPickerLayout(TouchPresetPickerLayout::Horizontal);
  syncOpenTouchPresetPicker(true);
}

void TouchWorkspaceSettingsPopup::onStyleChanged(bool checked) {
  if (!checked) return;
  TouchPresetManager::instance()->setPickerStyle(
      m_styleGhost->isChecked() ? TouchPresetPickerStyle::Ghost
                                : TouchPresetPickerStyle::Classic);
  syncOpenTouchPresetPicker(true);
}

void TouchWorkspaceSettingsPopup::onSizeChanged(int value) {
  TouchPresetManager::instance()->setPickerSizePercent(value);
  syncOpenTouchPresetPicker(false);
}

void TouchWorkspaceSettingsPopup::onOpacityChanged(int value) {
  TouchPresetManager::instance()->setPickerOpacityPercent(value);
  syncOpenTouchPresetPicker(false);
}

void TouchWorkspaceSettingsPopup::onDeferTapChanged(bool checked) {
  Preferences::instance()->setValue(touchDeferTapForDoubleTap, checked);
  TouchPresetPopup::syncDeferTapUiFromPreferences();
}

void TouchWorkspaceSettingsPopup::onOpenPresetManager() {
  CommandManager::instance()->execute(MI_TouchPresetManager);
}

void TouchWorkspaceSettingsPopup::onPinnedItemChanged(QListWidgetItem *item) {
  if (!item) return;
  const QString name = item->text();
  const bool want    = item->checkState() == Qt::Checked;
  TouchPresetManager *mgr = TouchPresetManager::instance();
  if (!mgr->setPinnedForPicker(name, want)) {
    QSignalBlocker block(m_pinnedList);
    item->setCheckState(want ? Qt::Unchecked : Qt::Checked);
    return;
  }
  rebuildPinnedList();
  syncOpenTouchPresetPicker(true);
}

//-----------------------------------------------------------------------------

OpenPopupCommandHandler<TouchWorkspaceSettingsPopup> openTouchWorkspaceSettings(
    MI_TouchWorkspaceSettings);
