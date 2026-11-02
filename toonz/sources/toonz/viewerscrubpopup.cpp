#include "viewerscrubpopup.h"

#include "sceneviewer.h"
#include "tapp.h"
#include "xsheetdragtool.h"
#include "tenv.h"
#include "toonz/tframehandle.h"
#include "toonz/tscenehandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/txsheethandle.h"
#include "tools/toolhandle.h"
#include "toonzqt/menubarcommand.h"
#include "toonzqt/viewcommandids.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QPointer>
#include <QScreen>
#include <QSlider>
#include <QTabletEvent>
#include <QVBoxLayout>

namespace {
TEnv::DoubleVar ViewerScrubPixels("ViewerScrubPixels", 20.0);
TEnv::DoubleVar ViewerScrubStripPixels("ViewerScrubStripPixels", 2.0);
TEnv::IntVar ViewerScrubReturn("ViewerScrubReturn", 0);
TEnv::IntVar ViewerScrubUseRange("ViewerScrubUseRange", 0);
TEnv::IntVar ViewerScrubWrap("ViewerScrubWrap", 0);

class ScrubStrip final : public QWidget {
  QPointer<ViewerScrubController> m_controller;
  bool m_dragging = false;
  bool m_tablet   = false;

  void finish(bool cancel = false) {
    if (!m_dragging) return;
    m_dragging = false;
    m_tablet   = false;
    if (!m_controller) return;
    if (cancel)
      m_controller->cancel();
    else
      m_controller->end();
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect groove = rect().adjusted(8, 10, -8, -10);
    p.setPen(palette().color(QPalette::Mid));
    p.setBrush(palette().color(QPalette::Base));
    p.drawRoundedRect(groove, 4, 4);
    for (int x = groove.left() + 8; x < groove.right(); x += 8)
      p.drawLine(x, groove.center().y() - 3, x, groove.center().y() + 3);
    TFrameHandle *fh = TApp::instance()->getCurrentFrame();
    const int last   = std::max(1, fh->getMaxFrameIndex());
    const double ratio =
        std::max(0.0, std::min(1.0, double(fh->getFrameIndex()) / last));
    const int x = groove.left() + qRound(ratio * groove.width());
    p.setPen(QPen(palette().color(QPalette::Highlight), 3));
    p.drawLine(x, groove.top() - 3, x, groove.bottom() + 3);
  }

  void mousePressEvent(QMouseEvent *e) override {
    if (m_tablet || e->button() != Qt::LeftButton) return;
    setFocus(Qt::MouseFocusReason);
    m_dragging = m_controller->begin(e->localPos().x(), true);
    e->accept();
    update();
  }

  void mouseMoveEvent(QMouseEvent *e) override {
    if (m_tablet || !m_dragging) return;
    if (!(e->buttons() & Qt::LeftButton)) {
      finish();
      return;
    }
    m_controller->move(e->localPos().x());
    e->accept();
  }

  void mouseReleaseEvent(QMouseEvent *e) override {
    if (m_tablet || e->button() != Qt::LeftButton) return;
    if (m_dragging) m_controller->move(e->localPos().x());
    finish();
    e->accept();
  }

  void tabletEvent(QTabletEvent *e) override {
    if (e->type() == QEvent::TabletPress) {
      setFocus(Qt::MouseFocusReason);
      m_dragging = m_controller->begin(e->posF().x(), true);
      m_tablet   = true;
    } else if (e->type() == QEvent::TabletMove && m_dragging) {
      m_controller->move(e->posF().x());
    } else if (e->type() == QEvent::TabletRelease) {
      if (m_dragging) m_controller->move(e->posF().x());
      finish();
      m_tablet = false;
    }
    e->accept();  // Do not also scrub through synthesized mouse events.
  }

  void keyPressEvent(QKeyEvent *e) override {
    if (e->key() == Qt::Key_Escape && m_dragging) {
      finish(true);
      e->accept();
    } else
      QWidget::keyPressEvent(e);
  }

  bool event(QEvent *e) override {
    if (e->type() == QEvent::FocusOut || e->type() == QEvent::Hide ||
        e->type() == QEvent::WindowDeactivate ||
        e->type() == QEvent::UngrabMouse)
      finish();
    return QWidget::event(e);
  }

public:
  explicit ScrubStrip(SceneViewer *viewer, QWidget *parent)
      : QWidget(parent), m_controller(viewer->scrubController()) {
    setObjectName("ViewerScrubStrip");
    setAccessibleName(ViewerScrubPopup::tr("Scrub strip"));
    setToolTip(ViewerScrubPopup::tr(
        "Drag left or right to scrub. Escape returns to the starting frame."));
    setCursor(Qt::SizeHorCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(240, 38);
    connect(TApp::instance()->getCurrentFrame(), &TFrameHandle::frameSwitched,
            this, [this]() { update(); });
  }
};
}  // namespace

ViewerScrubController::ViewerScrubController(SceneViewer *viewer)
    : QObject(viewer), m_viewer(viewer) {
  TApp *app = TApp::instance();
  connect(app->getCurrentScene(), &TSceneHandle::sceneSwitched, this,
          &ViewerScrubController::invalidate);
  connect(app->getCurrentXsheet(), &TXsheetHandle::xsheetSwitched, this,
          &ViewerScrubController::invalidate);
  connect(app->getCurrentLevel(), &TXshLevelHandle::xshLevelSwitched, this,
          [this, app]() {
            // Scene scrubbing can naturally pass through different levels.
            if (app->getCurrentFrame()->isEditingLevel()) invalidate();
          });
  connect(app->getCurrentFrame(), &TFrameHandle::frameTypeChanged, this,
          &ViewerScrubController::invalidate);
  connect(app->getCurrentFrame(), &TFrameHandle::isPlayingStatusChanged, this,
          &ViewerScrubController::invalidate);
  connect(app, &TApp::activeViewerChanged, this, [this, app]() {
    if (app->getActiveViewer() != m_viewer) invalidate();
  });
}

bool ViewerScrubController::begin(double x, bool strip) {
  TApp *app        = TApp::instance();
  TFrameHandle *fh = app->getCurrentFrame();
  TTool *tool      = app->getCurrentTool()->getTool();
  if (m_active || fh->isPlaying() || (tool && tool->isDragging()) ||
      fh->getFrameIndex() < 0 || fh->getMaxFrameIndex() < 0)
    return false;
  app->setActiveViewer(m_viewer);
  m_origin = fh->getFrameIndex();
  m_first  = 0;
  m_last   = std::max(m_origin, fh->getMaxFrameIndex());
  if (ViewerScrubUseRange && fh->isEditingScene()) {
    int first, last, step;
    if (XsheetGUI::getPlayRange(first, last, step)) {
      m_first = std::min(m_last, std::max(0, first));
      m_last  = std::max(m_first, std::min(m_last, last));
    }
  }
  m_strip = strip;
  m_drag.begin(x);
  m_active = true;
  return true;
}

void ViewerScrubController::move(double x) {
  if (!m_active) return;
  TFrameHandle *fh = TApp::instance()->getCurrentFrame();
  const double pixels =
      m_strip ? double(ViewerScrubStripPixels) : double(ViewerScrubPixels);
  const int target =
      m_drag.move(x, std::max(0.5, std::min(40.0, pixels)), fh->getFrameIndex(),
                  m_first, m_last, ViewerScrubWrap != 0);
  if (target != fh->getFrameIndex()) emit frameRequested(target + 1);
}

void ViewerScrubController::finish(bool restore) {
  if (!m_active) return;
  m_active         = false;
  TFrameHandle *fh = TApp::instance()->getCurrentFrame();
  // A drawing may have been removed while the strip was in use.
  if (restore && (!fh->isEditingLevel() || m_origin <= fh->getMaxFrameIndex()))
    emit frameRequested(m_origin + 1);
  fh->stopScrubbing();
}

void ViewerScrubController::end() { finish(ViewerScrubReturn != 0); }
void ViewerScrubController::cancel() { finish(true); }
void ViewerScrubController::invalidate() { finish(false); }

ViewerScrubController *SceneViewer::scrubController() {
  if (!m_scrubController) m_scrubController = new ViewerScrubController(this);
  return m_scrubController;
}

void SceneViewer::showScrubSettings() {
  if (!m_scrubPopup) m_scrubPopup = new ViewerScrubPopup(this);
  if (!m_scrubPopup->isVisible()) {
    m_scrubPopup->adjustSize();
    QScreen *screen = QGuiApplication::screenAt(mapToGlobal(rect().center()));
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry()
                                   : QRect(mapToGlobal(QPoint()), size());
    QPoint pos = mapToGlobal(QPoint(width() - m_scrubPopup->width(), height()));
    pos.setX(
        std::max(available.left(),
                 std::min(pos.x(), available.right() - m_scrubPopup->width())));
    pos.setY(std::max(
        available.top(),
        std::min(pos.y(), available.bottom() - m_scrubPopup->height())));
    m_scrubPopup->move(pos);
  }
  m_scrubPopup->show();
  m_scrubPopup->raise();
}

ViewerScrubPopup::ViewerScrubPopup(SceneViewer *viewer)
    : QDialog(viewer, Qt::Tool) {
  setWindowTitle(tr("Viewer Scrub Settings"));
  setObjectName("ViewerScrubSettings");
  setSizeGripEnabled(false);
  auto *layout     = new QVBoxLayout(this);
  auto *frame      = new QLabel(this);
  auto updateFrame = [frame]() {
    TFrameHandle *fh = TApp::instance()->getCurrentFrame();
    const QString name =
        fh->isEditingLevel()
            ? QString::fromStdString(fh->getFid().expand(TFrameId::NO_PAD))
            : QString::number(fh->getFrameIndex() + 1);
    frame->setText(tr("Frame: %1").arg(name));
  };
  connect(TApp::instance()->getCurrentFrame(), &TFrameHandle::frameSwitched,
          this, updateFrame);
  updateFrame();
  layout->addWidget(frame);
  layout->addWidget(new ScrubStrip(viewer, this));

  auto *form    = new QFormLayout;
  auto makeStep = [this, form](const QString &label, TEnv::DoubleVar &setting) {
    auto *row  = new QWidget(this);
    auto *hbox = new QHBoxLayout(row);
    hbox->setContentsMargins(0, 0, 0, 0);
    auto *slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(1, 80);
    slider->setInvertedAppearance(true);
    slider->setToolTip(tr("Smaller pixel steps scrub faster."));
    auto *spin = new QDoubleSpinBox(row);
    spin->setRange(0.5, 40.0);
    spin->setSingleStep(0.5);
    spin->setDecimals(1);
    spin->setSuffix(tr(" px"));
    spin->setValue(std::max(0.5, std::min(40.0, double(setting))));
    slider->setValue(qRound(spin->value() * 2));
    hbox->addWidget(slider, 1);
    hbox->addWidget(spin);
    form->addRow(label, row);
    connect(slider, &QSlider::valueChanged, spin,
            [spin](int value) { spin->setValue(value / 2.0); });
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), slider,
            [slider, setting = &setting](double value) {
              *setting = value;
              slider->setValue(qRound(value * 2));
            });
    return spin;
  };
  auto *viewerStep = makeStep(tr("Viewer step in pixels"), ViewerScrubPixels);
  auto *stripStep =
      makeStep(tr("Strip step in pixels"), ViewerScrubStripPixels);
  layout->addLayout(form);
  auto *hint =
      new QLabel(tr("Smaller steps move through frames faster."), this);
  layout->addWidget(hint);

  auto *returnStart =
      new QCheckBox(tr("Return to starting frame on release"), this);
  auto *useRange = new QCheckBox(tr("Use playback range (scene only)"), this);
  auto *wrap     = new QCheckBox(tr("Loop between range limits"), this);
  wrap->setToolTip(
      tr("Wrap at the playback range when enabled; otherwise use scene or "
         "level limits."));
  returnStart->setChecked(ViewerScrubReturn != 0);
  useRange->setChecked(ViewerScrubUseRange != 0);
  wrap->setChecked(ViewerScrubWrap != 0);
  layout->addWidget(returnStart);
  layout->addWidget(useRange);
  layout->addWidget(wrap);
  connect(returnStart, &QCheckBox::toggled, this,
          [](bool on) { ViewerScrubReturn = on; });
  connect(useRange, &QCheckBox::toggled, this,
          [](bool on) { ViewerScrubUseRange = on; });
  connect(wrap, &QCheckBox::toggled, this,
          [](bool on) { ViewerScrubWrap = on; });

  const QAction *action = CommandManager::instance()->getAction(V_Scrub);
  const QString shortcut =
      action ? action->shortcut().toString(QKeySequence::NativeText)
             : QString();
  auto *help = new QLabel(
      shortcut.isEmpty()
          ? tr("Assign Viewer Scrub in Configure Shortcuts, then hold it and "
               "drag in the viewer.")
          : tr("Hold %1 and drag in the viewer. Drag the strip directly.")
                .arg(shortcut),
      this);
  help->setWordWrap(true);
  layout->addWidget(help);
  auto *reset = new QPushButton(tr("Reset"), this);
  reset->setAutoDefault(false);
  layout->addWidget(reset, 0, Qt::AlignRight);
  connect(reset, &QPushButton::clicked, this, [=]() {
    viewerStep->setValue(20.0);
    stripStep->setValue(2.0);
    returnStart->setChecked(false);
    useRange->setChecked(false);
    wrap->setChecked(false);
  });
  connect(this, &QDialog::finished, viewer, [viewer]() {
    viewer->scrubController()->end();
    if (viewer->isVisible()) viewer->setFocus(Qt::OtherFocusReason);
  });
}
