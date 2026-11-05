#include "handwritingpanel.h"
#include "handwritingsurface.h"
#include "winhandwritingrecognizer.h"

#include "tapp.h"
#include "historytypes.h"
#include "tundo.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/tscenehandle.h"
#include "toonz/txshcolumn.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshlevelhandle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
#include <QtMath>

namespace {

class TranscribeNoteUndo final : public TUndo {
  TXshSoundTextLevelP m_level;
  ToonzScene *m_scene;
  int m_index;
  QString m_before, m_after;

  void apply(const QString &text) const {
    TApp *app = TApp::instance();
    if (app->getCurrentScene()->getScene() != m_scene) return;
    m_level->setFrameText(m_index, text);
    app->getCurrentScene()->notifySceneChanged();
    app->getCurrentXsheet()->notifyXsheetChanged();
    app->getCurrentLevel()->notifyLevelChange();
  }

public:
  TranscribeNoteUndo(TXshSoundTextLevel *level, ToonzScene *scene, int index,
                     const QString &before, const QString &after)
      : m_level(level)
      , m_scene(scene)
      , m_index(index)
      , m_before(before)
      , m_after(after) {}
  void undo() const override { apply(m_before); }
  void redo() const override { apply(m_after); }
  int getSize() const override {
    return sizeof(*this) + (m_before.size() + m_after.size()) * sizeof(QChar);
  }
  QString getHistoryString() override {
    return QObject::tr("Transcribe Handwriting to Note Level");
  }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

QString combinedText(const QString &prefix, const QString &recognized) {
  if (prefix.isEmpty()) return recognized;
  if (recognized.isEmpty()) return prefix;
  return prefix +
         (prefix.endsWith(QLatin1Char('\n')) ? QString() : QString(" ")) +
         recognized;
}

}  // namespace

HandwritingPanel::HandwritingPanel(QWidget *parent) : TPanel(parent) {
  setIsMaximizable(true);
  allowMultipleInstances(false);
  resize(800, 520);
  QWidget *body       = new QWidget(this);
  QVBoxLayout *main   = new QVBoxLayout(body);
  QHBoxLayout *target = new QHBoxLayout;
  QPushButton *bind   = new QPushButton(tr("Use Selected Note Cell"), body);
  m_follow            = new QCheckBox(tr("Follow current frame"), body);
  m_follow->setChecked(true);
  target->addWidget(bind);
  target->addWidget(m_follow);
  main->addLayout(target);
  m_destination =
      new QLabel(tr("Select an existing Note Level cell, then bind it."), body);
  m_destination->setWordWrap(true);
  m_destination->setTextFormat(Qt::PlainText);
  main->addWidget(m_destination);

  QHBoxLayout *recognition = new QHBoxLayout;
  m_languages              = new QComboBox(body);
  QPushButton *check       = new QPushButton(tr("Recheck Windows"), body);
  QPushButton *settings    = new QPushButton(tr("Language Settings"), body);
  QSpinBox *pause          = new QSpinBox(body);
  pause->setRange(150, 1500);
  pause->setSingleStep(50);
  pause->setValue(350);
  pause->setSuffix(tr(" ms"));
  recognition->addWidget(m_languages, 1);
  recognition->addWidget(pause);
  recognition->addWidget(check);
  recognition->addWidget(settings);
  main->addLayout(recognition);

  QHBoxLayout *layers = new QHBoxLayout;
  QCheckBox *ink      = new QCheckBox(tr("Ink"), body);
  QCheckBox *text     = new QCheckBox(tr("Text underneath"), body);
  QSlider *opacity    = new QSlider(Qt::Horizontal, body);
  opacity->setRange(0, 100);
  opacity->setValue(75);
  opacity->setMaximumWidth(120);
  QSpinBox *width = new QSpinBox(body);
  width->setRange(600, 6400);
  width->setSingleStep(200);
  width->setValue(1600);
  ink->setChecked(true);
  text->setChecked(true);
  layers->addWidget(ink);
  layers->addWidget(text);
  layers->addWidget(opacity);
  layers->addWidget(new QLabel(tr("Writing width:"), body));
  layers->addWidget(width);
  layers->addStretch();
  main->addLayout(layers);
  m_surface         = new HandwritingSurface(body);
  QScrollArea *area = new QScrollArea(body);
  area->setWidget(m_surface);
  area->setMinimumHeight(265);
  main->addWidget(area);
  QHBoxLayout *actions = new QHBoxLayout;
  QPushButton *undo    = new QPushButton(tr("Undo Ink"), body);
  QPushButton *clear   = new QPushButton(tr("Clear Ink"), body);
  QPushButton *use     = new QPushButton(tr("Use Recognition"), body);
  QPushButton *now     = new QPushButton(tr("Recognize Now"), body);
  actions->addWidget(undo);
  actions->addWidget(clear);
  actions->addWidget(use);
  actions->addWidget(now);
  main->addLayout(actions);
  m_text = new QPlainTextEdit(body);
  m_text->setPlaceholderText(tr("Recognized text; edit here to correct it."));
  main->addWidget(m_text, 1);
  QHBoxLayout *transferLayout = new QHBoxLayout;
  m_automatic = new QCheckBox(tr("Auto transfer to Note Level"), body);
  m_automatic->setChecked(true);
  m_transfer = new QPushButton(tr("Transfer Text"), body);
  transferLayout->addWidget(m_automatic);
  transferLayout->addStretch();
  transferLayout->addWidget(m_transfer);
  main->addLayout(transferLayout);
  m_status =
      new QLabel(tr("Checking local Windows handwriting support..."), body);
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  main->addWidget(m_status);
  QLabel *session =
      new QLabel(tr("Ink stays in this panel for this scene session. "
                    "Transferred note text is saved with the scene."),
                 body);
  session->setWordWrap(true);
  main->addWidget(session);
  setWidget(body);

  m_recognizer = new WinHandwritingRecognizer(this);
  m_recognitionTimer.setSingleShot(true);
  m_recognitionTimer.setInterval(350);
  // Group a writing burst into one normal undoable text edit.
  m_transferTimer.setSingleShot(true);
  m_transferTimer.setInterval(1000);
  connect(&m_recognitionTimer, &QTimer::timeout, this,
          &HandwritingPanel::recognize);
  connect(&m_transferTimer, &QTimer::timeout, this, [this] { transfer(true); });
  connect(bind, &QPushButton::clicked, this, &HandwritingPanel::bindSelected);
  connect(m_follow, &QCheckBox::toggled, this, [this] { frameChanged(); });
  connect(check, &QPushButton::clicked, this, &HandwritingPanel::recheck);
  connect(settings, &QPushButton::clicked, this, [] {
    QDesktopServices::openUrl(QUrl("ms-settings:regionlanguage"));
  });
  connect(pause, QOverload<int>::of(&QSpinBox::valueChanged),
          &m_recognitionTimer, QOverload<int>::of(&QTimer::setInterval));
  connect(ink, &QCheckBox::toggled, m_surface,
          &HandwritingSurface::setInkVisible);
  connect(text, &QCheckBox::toggled, m_surface,
          &HandwritingSurface::setTextVisible);
  connect(opacity, &QSlider::valueChanged, m_surface,
          &HandwritingSurface::setInkOpacity);
  connect(width, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this, width](int value) {
            int required = 600;
            for (const Draft &entry : m_drafts)
              for (const QPolygonF &stroke : entry.strokes)
                for (const QPointF &point : stroke)
                  required = qMax(required, qCeil(point.x()));
            const int actual = qMax(required, value);
            QSignalBlocker block(width);
            width->setValue(actual);
            m_surface->setFixedWidth(actual);
          });
  connect(m_surface, &HandwritingSurface::writingStarted, this,
          &HandwritingPanel::invalidate);
  connect(m_surface, &HandwritingSurface::strokesChanged, this,
          &HandwritingPanel::inkChanged);
  connect(undo, &QPushButton::clicked, this, [this] {
    Draft *d = draft();
    if (!d || d->strokes.isEmpty()) return;
    d->strokes.removeLast();
    m_surface->setContent(d->strokes, d->text);
    inkChanged();
  });
  connect(clear, &QPushButton::clicked, this, [this] {
    Draft *d = draft();
    if (!d) return;
    d->strokes.clear();
    d->recognized.clear();
    if (!d->manual) d->text = d->prefix;
    m_surface->setContent(d->strokes, d->text);
    invalidate();
    updateText();
  });
  connect(use, &QPushButton::clicked, this, [this] {
    Draft *d = draft();
    if (!d) return;
    d->manual = false;
    d->text   = combinedText(d->prefix, d->recognized);
    updateText();
    if (m_automatic->isChecked() && !d->conflict) m_transferTimer.start();
  });
  connect(now, &QPushButton::clicked, this, &HandwritingPanel::recognize);
  connect(m_transfer, &QPushButton::clicked, this, [this] { transfer(false); });
  connect(m_automatic, &QCheckBox::toggled, this, [this](bool on) {
    m_transferTimer.stop();
    Draft *d = draft();
    if (on && d && !d->conflict) m_transferTimer.start();
  });
  connect(m_text, &QPlainTextEdit::textChanged, this, [this] {
    Draft *d = draft();
    if (!d) return;
    d->manual = true;
    d->text   = m_text->toPlainText();
    m_surface->setText(d->text);
    if (m_automatic->isChecked() && !d->conflict) m_transferTimer.start();
  });
  connect(m_languages, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this] {
            invalidate();
            if (draft() && !draft()->strokes.isEmpty())
              m_recognitionTimer.start();
          });
  connect(
      m_recognizer, &WinHandwritingRecognizer::recognizersReady, this,
      [this](const QStringList &names) {
        QSignalBlocker block(m_languages);
        m_languages->clear();
        m_languages->addItems(names);
        if (!names.isEmpty()) {
          m_status->setText(tr("Ready. Recognition runs on this computer."));
          if (draft() && !draft()->strokes.isEmpty())
            m_recognitionTimer.start();
        }
      });
  connect(m_recognizer, &WinHandwritingRecognizer::recognized, this,
          &HandwritingPanel::receiveRecognition);
  TApp *app = TApp::instance();
  connect(app->getCurrentFrame(), &TFrameHandle::frameSwitched, this,
          &HandwritingPanel::frameChanged);
  connect(app->getCurrentFrame(), &TFrameHandle::isPlayingStatusChanged, this,
          &HandwritingPanel::frameChanged);
  connect(app->getCurrentXsheet(), &TXsheetHandle::xsheetChanged, this,
          &HandwritingPanel::sheetChanged);
  connect(app->getCurrentXsheet(), &TXsheetHandle::xsheetSwitched, this,
          &HandwritingPanel::clearBinding);
  connect(app->getCurrentScene(), &TSceneHandle::sceneSwitched, this,
          &HandwritingPanel::resetScene);
  clearBinding();
}

HandwritingPanel::Draft *HandwritingPanel::draft() {
  if (!m_bound) return nullptr;
  auto it = m_drafts.find(m_key);
  return it == m_drafts.end() ? nullptr : &it.value();
}

bool HandwritingPanel::validTarget() const {
  TApp *app = TApp::instance();
  if (!m_bound || app->getCurrentScene()->getScene() != m_scene ||
      app->getCurrentXsheet()->getXsheet() != m_xsheet ||
      !app->getCurrentFrame()->isEditingScene() ||
      app->getCurrentFrame()->isPlaying() ||
      (m_follow->isChecked() && app->getCurrentFrame()->getFrame() != m_row))
    return false;
  TXshColumn *column = m_xsheet->getColumn(m_column);
  if (!column || column->isLocked() || !column->getSoundTextColumn())
    return false;
  const TXshCell &cell = m_xsheet->getCell(m_row, m_column);
  return cell.getSoundTextLevel() &&
         quintptr(cell.getSoundTextLevel()) == m_key.first &&
         cell.m_frameId.getNumber() - 1 == m_key.second;
}

void HandwritingPanel::invalidate() {
  ++m_revision;
  m_recognitionTimer.stop();
  m_transferTimer.stop();
  m_recognizer->cancelPending();
}

void HandwritingPanel::bindSelected() {
  TApp *app = TApp::instance();
  if (!app->getCurrentFrame()->isEditingScene()) {
    clearBinding();
    return;
  }
  bindRow(app->getCurrentFrame()->getFrame(),
          app->getCurrentColumn()->getColumnIndex());
}

void HandwritingPanel::bindRow(int row, int column) {
  invalidate();
  m_bound   = false;
  TApp *app = TApp::instance();
  m_scene   = app->getCurrentScene()->getScene();
  m_xsheet  = app->getCurrentXsheet()->getXsheet();
  m_column  = column;
  m_row     = row;
  TXshColumn *col =
      m_xsheet && column >= 0 ? m_xsheet->getColumn(column) : nullptr;
  TXshCell cell             = m_xsheet && row >= 0 && column >= 0
                                  ? m_xsheet->getCell(row, column)
                                  : TXshCell();
  TXshSoundTextLevel *level = cell.getSoundTextLevel();
  if (!col || col->isLocked() || !col->getSoundTextColumn() || !level ||
      cell.m_frameId.getNumber() < 1 ||
      !app->getCurrentFrame()->isEditingScene() ||
      app->getCurrentFrame()->isPlaying()) {
    m_destination->setText(
        tr("No writable Note Level cell at column %1, frame %2.")
            .arg(column + 1)
            .arg(row + 1));
    m_surface->setEnabled(false);
    m_text->setEnabled(false);
    m_transfer->setEnabled(false);
    return;
  }
  m_key = qMakePair(quintptr(level), cell.m_frameId.getNumber() - 1);
  if (!m_drafts.contains(m_key)) {
    Draft entry;
    entry.level    = level;
    entry.expected = level->getFrameText(m_key.second);
    entry.prefix =
        entry.expected.trimmed().isEmpty() ? QString() : entry.expected;
    entry.text = entry.prefix;
    m_drafts.insert(m_key, entry);
  }
  m_bound  = true;
  Draft *d = draft();
  if (level->getFrameText(m_key.second) != d->expected) d->conflict = true;
  m_destination->setText(tr("Column %1, Xsheet frame %2 — Note drawing %3. "
                            "Held cells share this text.")
                             .arg(column + 1)
                             .arg(row + 1)
                             .arg(m_key.second + 1));
  m_surface->setEnabled(true);
  m_text->setEnabled(true);
  m_transfer->setEnabled(true);
  m_surface->setContent(d->strokes, d->text);
  updateText();
  m_status->setText(
      d->conflict
          ? tr("Note text changed outside this panel. Auto transfer is paused. "
               "Review the preview before using Transfer Text.")
          : tr("Write here. Existing note text is retained before the "
               "transcription."));
  if (!d->strokes.isEmpty()) m_recognitionTimer.start();
}

void HandwritingPanel::frameChanged() {
  invalidate();
  if (!TApp::instance()->getCurrentFrame()->isEditingScene()) {
    clearBinding();
    return;
  }
  if (isVisible() && m_follow->isChecked() && m_column >= 0)
    bindRow(TApp::instance()->getCurrentFrame()->getFrame(), m_column);
}

void HandwritingPanel::sheetChanged() {
  if (m_writing || !m_bound) return;
  if (!validTarget()) {
    clearBinding();
    return;
  }
  Draft *d = draft();
  if (d && d->level->getFrameText(m_key.second) != d->expected) {
    invalidate();
    d->conflict = true;
    m_status->setText(
        tr("Note text changed outside this panel. Auto transfer is paused. "
           "Review the preview before using Transfer Text."));
  }
}

void HandwritingPanel::clearBinding() {
  invalidate();
  m_bound  = false;
  m_column = m_row = -1;
  m_surface->setEnabled(false);
  m_text->setEnabled(false);
  m_transfer->setEnabled(false);
  m_destination->setText(
      tr("Select an existing Note Level cell, then bind it."));
}

void HandwritingPanel::resetScene() {
  clearBinding();
  m_drafts.clear();
  m_scene  = nullptr;
  m_xsheet = nullptr;
  m_surface->setContent({}, QString());
  QSignalBlocker block(m_text);
  m_text->clear();
}

void HandwritingPanel::inkChanged() {
  invalidate();
  Draft *d = draft();
  if (!d || !validTarget()) return;
  d->strokes = m_surface->strokes();
  int points = 0;
  for (const QPolygonF &stroke : d->strokes) points += stroke.size();
  if (points > 200000 || d->strokes.size() > 20000) {
    m_status->setText(
        tr("This note is too large to transcribe. Continue in another note."));
    return;
  }
  if (d->strokes.isEmpty()) {
    d->recognized.clear();
    if (!d->manual) d->text = d->prefix;
    updateText();
  } else
    m_recognitionTimer.start();
}

void HandwritingPanel::recognize() {
  Draft *d = draft();
  if (!isVisible() || !d || !validTarget() || m_surface->isWriting() ||
      d->strokes.isEmpty() || m_languages->currentIndex() < 0)
    return;
  m_recognitionTimer.stop();
  int points = 0;
  for (const QPolygonF &stroke : d->strokes) points += stroke.size();
  if (points > 200000 || d->strokes.size() > 20000) return;
  m_recognizer->recognize(d->strokes, m_languages->currentIndex(), m_revision);
}

void HandwritingPanel::receiveRecognition(quint64 revision, const QString &text,
                                          const QString &error) {
  if (revision == 0) {
    if (!error.isEmpty()) m_status->setText(error);
    return;
  }
  if (revision != m_revision || !isVisible() || !validTarget()) return;
  if (!error.isEmpty()) {
    m_status->setText(error);
    return;
  }
  Draft *d      = draft();
  d->recognized = text;
  if (!d->manual) d->text = combinedText(d->prefix, text);
  updateText();
  if (m_automatic->isChecked() && !d->conflict) m_transferTimer.start();
  m_status->setText(
      d->manual
          ? tr("Manual correction retained. Use Recognition to replace it.")
          : tr("Recognized locally."));
}

void HandwritingPanel::updateText() {
  Draft *d = draft();
  if (!d) return;
  QSignalBlocker block(m_text);
  if (m_text->toPlainText() != d->text) m_text->setPlainText(d->text);
  m_surface->setText(d->text);
}

void HandwritingPanel::transfer(bool automatic) {
  Draft *d = draft();
  if (!isVisible() || !d || !validTarget() || m_surface->isWriting()) return;
  const QString before = d->level->getFrameText(m_key.second);
  if (automatic && (d->conflict || before != d->expected)) {
    d->conflict = true;
    m_status->setText(
        tr("Note text changed outside this panel. Auto transfer is paused."));
    return;
  }
  if (before == d->text) {
    if (!automatic) {
      d->expected = before;
      d->conflict = false;
    }
    return;
  }
  QScopedValueRollback<bool> writing(m_writing, true);
  TUndoManager::manager()->add(new TranscribeNoteUndo(
      d->level.getPointer(), m_scene, m_key.second, before, d->text));
  d->level->setFrameText(m_key.second, d->text);
  d->expected = d->text;
  d->conflict = false;
  TApp *app   = TApp::instance();
  app->getCurrentScene()->notifySceneChanged();
  app->getCurrentXsheet()->notifyXsheetChanged();
  app->getCurrentLevel()->notifyLevelChange();
  m_status->setText(
      tr("Transferred to Note drawing %1. Undo restores its previous text.")
          .arg(m_key.second + 1));
}

void HandwritingPanel::recheck() {
  invalidate();
  m_languages->clear();
  m_status->setText(tr("Checking local Windows handwriting support..."));
  m_recognizer->recheck();
}

void HandwritingPanel::showEvent(QShowEvent *event) {
  TPanel::showEvent(event);
  if (!m_recognizer->isRunning()) m_recognizer->start();
  frameChanged();
}

void HandwritingPanel::hideEvent(QHideEvent *event) {
  invalidate();
  TPanel::hideEvent(event);
}
