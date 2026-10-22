#include "otdevrecorder.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStorageInfo>
#include <QTemporaryFile>
#include <QToolBar>
#include <QUrl>
#include <QUuid>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
const QSize frameSize(1920, 1080);
constexpr int frameRate      = 12;
constexpr int clipFrames     = frameRate * 60 * 10;
constexpr int finishTimeout  = 15000;
constexpr qint64 diskReserve = 1024LL * 1024 * 1024;

bool excluded(QWidget *widget) {
  return widget->property("otdevNoCapture").toBool() ||
         qobject_cast<QFileDialog *>(widget) ||
         widget->windowType() == Qt::SplashScreen;
}

bool ownedBy(QWidget *widget, QWidget *main) {
  for (QWidget *p = widget; p; p = p->parentWidget())
    if (p == main) return true;
  return false;
}

QImage widgetImage(QWidget *widget) {
  // Render only this application's widget backing content. Never use
  // QScreen::grabWindow, PrintWindow, BitBlt or a desktop/window capture
  // device.
  QImage image(widget->size(), QImage::Format_RGB32);
  image.fill(Qt::black);
  widget->render(&image);
  QPainter painter(&image);
  // Qt's QWidget::render does not supply QOpenGLWidget framebuffer contents.
  // Read each visible GL child directly and clip it against sibling widgets.
  for (auto *gl : widget->findChildren<QOpenGLWidget *>()) {
    if (gl->window() != widget || !gl->isVisibleTo(widget)) continue;
    const QPoint offset = gl->mapTo(widget, QPoint());
    painter.save();
    painter.setClipRegion(gl->visibleRegion().translated(offset));
    painter.drawImage(QRect(offset, gl->size()), gl->grabFramebuffer());
    painter.restore();
  }
  return image;
}

#ifdef Q_OS_WIN
void paintCursor(QPainter &painter, const QImage &frame,
                 const QList<QWidget *> &windows) {
  CURSORINFO cursor = {};
  cursor.cbSize     = sizeof(cursor);
  if (!GetCursorInfo(&cursor) || !(cursor.flags & CURSOR_SHOWING)) return;

  // Native hit testing reads only window metadata. A cursor over another
  // application, a native dialog, or a window frame is not part of the video.
  HWND under            = WindowFromPoint(cursor.ptScreenPos);
  auto *widget          = QWidget::find(WId(GetAncestor(under, GA_ROOT)));
  const QPoint position = QCursor::pos();  // Same logical coordinates as Qt UI.
  if (!widget || !windows.contains(widget) ||
      !widget->rect().contains(widget->mapFromGlobal(position)))
    return;

  ICONINFO icon = {};
  if (!GetIconInfo(cursor.hCursor, &icon)) return;
  BITMAP bitmap    = {};
  const bool valid = GetObject(icon.hbmColor ? icon.hbmColor : icon.hbmMask,
                               sizeof(bitmap), &bitmap) != 0;
  // Monochrome cursors store the AND and XOR masks one above the other.
  const int height = icon.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
  if (icon.hbmColor) DeleteObject(icon.hbmColor);
  if (icon.hbmMask) DeleteObject(icon.hbmMask);
  if (!valid || bitmap.bmWidth <= 0 || height <= 0 || bitmap.bmWidth > 1024 ||
      height > 1024)
    return;

  // Cursor bitmap/hotspot sizes are native pixels. Convert through the hovered
  // window's DPI and the existing UI-to-video transform (including letterbox).
  const QTransform transform = painter.transform();
  const qreal sx             = transform.m11() / widget->devicePixelRatioF();
  const qreal sy             = transform.m22() / widget->devicePixelRatioF();
  const QPoint topLeft       = (transform.map(QPointF(position)) -
                          QPointF(icon.xHotspot * sx, icon.yHotspot * sy))
                             .toPoint();
  const QSize size(qMax(1, qRound(bitmap.bmWidth * sx)),
                   qMax(1, qRound(height * sy)));
  const QRect area = QRect(topLeft, size).intersected(frame.rect());
  if (area.isEmpty()) return;

  // Draw only into a memory bitmap seeded with our own composed frame. This
  // preserves alpha AND/XOR cursor shapes (e.g. I-beams) without screen reads.
  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc) return;
  BITMAPINFO dib              = {};
  dib.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
  dib.bmiHeader.biWidth       = area.width();
  dib.bmiHeader.biHeight      = -area.height();
  dib.bmiHeader.biPlanes      = 1;
  dib.bmiHeader.biBitCount    = 32;
  dib.bmiHeader.biCompression = BI_RGB;
  void *pixels                = nullptr;
  HBITMAP image =
      CreateDIBSection(dc, &dib, DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (image) {
    HGDIOBJ previous = SelectObject(dc, image);
    if (previous && previous != HGDI_ERROR) {
      QImage patch(static_cast<uchar *>(pixels), area.width(), area.height(),
                   QImage::Format_RGB32);
      patch.fill(Qt::black);
      {
        QPainter background(&patch);
        background.drawImage(-area.topLeft(), frame);
      }
      if (DrawIconEx(dc, topLeft.x() - area.x(), topLeft.y() - area.y(),
                     cursor.hCursor, size.width(), size.height(), 0, nullptr,
                     DI_NORMAL)) {
        GdiFlush();
        // GDI may clear alpha bytes; the recorded canvas is always opaque.
        for (int y = 0; y < patch.height(); ++y) {
          auto *row = reinterpret_cast<QRgb *>(patch.scanLine(y));
          for (int x = 0; x < patch.width(); ++x) row[x] |= 0xff000000;
        }
        QRegion clients;
        for (auto *window : windows)
          clients += QRect(window->mapToGlobal(QPoint()), window->size());
        painter.save();
        painter.resetTransform();
        painter.setClipRegion(transform.map(clients));
        painter.drawImage(area.topLeft(), patch);
        painter.restore();
      }
      SelectObject(dc, previous);
    }
    DeleteObject(image);
  }
  DeleteDC(dc);
}
#endif
}  // namespace

int OtDevRecorder::rememberedChoice(const QString &path,
                                    const QString &buildId) {
  QSettings settings(path, QSettings::IniFormat);
  settings.beginGroup("OtDevRecorder");
  if (buildId.isEmpty() || settings.value("BuildId").toString() != buildId)
    return -1;
  bool valid = false;
  int choice = settings.value("Choice", -1).toInt(&valid);
  return valid && (choice == 0 || choice == 1) ? choice : -1;
}

bool OtDevRecorder::saveChoice(const QString &path, const QString &buildId,
                               int choice) {
  QSettings settings(path, QSettings::IniFormat);
  settings.beginGroup("OtDevRecorder");
  settings.setValue("BuildId", buildId);
  settings.setValue("Choice", choice);
  settings.sync();
  return settings.status() == QSettings::NoError;
}

QStringList OtDevRecorder::encoderArguments(const QSize &size,
                                            const QString &output) {
  // The minimal FFmpeg build disables external x86 assembly. Its default
  // scaler can still select an incompatible MMX filter layout, corrupting
  // chroma. Accurate rounding keeps the conversion consistent.
  return {"-hide_banner",
          "-loglevel",
          "error",
          "-nostdin",
          "-n",
          "-f",
          "rawvideo",
          "-pixel_format",
          "bgra",
          "-video_size",
          QString("%1x%2").arg(size.width()).arg(size.height()),
          "-framerate",
          QString::number(frameRate),
          "-i",
          "pipe:0",
          "-an",
          "-sws_flags",
          "bicubic+accurate_rnd",
          "-c:v",
          "libx264",
          "-preset",
          "ultrafast",
          "-tune",
          "zerolatency",
          "-crf",
          "18",
          "-profile:v",
          "baseline",
          "-level:v",
          "4.0",
          "-maxrate",
          "20M",
          "-bufsize",
          "20M",
          "-threads",
          "2",
          "-pix_fmt",
          "yuv420p",
          "-g",
          "24",
          "-flush_packets",
          "1",
          "-movflags",
          "+hybrid_fragmented+frag_keyframe+empty_moov+default_base_moof",
          output};
}

QImage OtDevRecorder::capture(QMainWindow *window, const QSize &size,
                              bool includeCursor) {
  QList<QWidget *> windows{window};
  QRect bounds(window->mapToGlobal(QPoint()), window->size());
  QWidget *active = QApplication::activeWindow();
  QWidget *popup  = QApplication::activePopupWidget();
  auto candidates = QApplication::topLevelWidgets();
#ifdef Q_OS_WIN
  // Preserve native top-level stacking order using handles, not screen pixels.
  candidates.clear();
  for (HWND handle = GetTopWindow(nullptr); handle;
       handle      = GetWindow(handle, GW_HWNDNEXT)) {
    QWidget *widget = QWidget::find(WId(handle));
    if (widget && widget->isWindow()) candidates.prepend(widget);
  }
#endif
  // Only Qt-owned windows in the main window's parent chain are composed.
  // Detached windows outside that chain are omitted, never desktop-captured.
  for (auto *widget : candidates) {
    if (widget == window || !widget->isVisible() || widget->isMinimized() ||
        excluded(widget) || !ownedBy(widget, window))
      continue;
    if (widget != active && widget != popup) windows.append(widget);
    bounds |= QRect(widget->mapToGlobal(QPoint()), widget->size());
  }
  if (active && active != window && !excluded(active) &&
      ownedBy(active, window) && active->isVisible())
    windows.append(active);
  if (popup && popup != active && !excluded(popup) && ownedBy(popup, window) &&
      popup->isVisible())
    windows.append(popup);
  // Paint directly into a fixed, bounded canvas. Moving/resizing windows or
  // changing DPI cannot change raw-frame byte counts or expose desktop gaps.
  QImage frame(size, QImage::Format_RGB32);
  frame.fill(Qt::black);
  QPainter painter(&frame);
  const QSize fitted = bounds.size().scaled(size, Qt::KeepAspectRatio);
  painter.translate((size.width() - fitted.width()) / 2.0,
                    (size.height() - fitted.height()) / 2.0);
  painter.scale(double(fitted.width()) / bounds.width(),
                double(fitted.height()) / bounds.height());
  painter.translate(-bounds.topLeft());
  for (auto *widget : windows)
    painter.drawImage(QRect(widget->mapToGlobal(QPoint()), widget->size()),
                      widgetImage(widget));
#ifdef Q_OS_WIN
  if (includeCursor) paintCursor(painter, frame, windows);
#else
  Q_UNUSED(includeCursor);
#endif
  return frame;
}

OtDevRecorder::OtDevRecorder(QMainWindow *window,
                             const QString &preferencesPath,
                             const QString &buildId,
                             const QString &portableRoot,
                             const QString &encoder)
    : QObject(window)
    , m_window(window)
    , m_preferencesPath(preferencesPath)
    , m_buildId(buildId)
    , m_portableRoot(portableRoot)
    , m_encoderPath(encoder) {
  m_outputDir = QDir(portableRoot).filePath("recordings");
  auto *bar   = new QToolBar(tr("OT-Dev Session Recording"), window);
  bar->setObjectName("OtDevSessionRecorder");
  bar->setMovable(false);
  window->addToolBar(Qt::BottomToolBarArea, bar);
  // Do not allow the recorder controls to be hidden while capture is enabled.
  bar->toggleViewAction()->setEnabled(false);
  m_toggle = bar->addAction(tr("Record OT-Dev Session"));
  m_toggle->setCheckable(true);
  m_status = new QLabel(tr("Recording off"), bar);
  bar->addWidget(m_status);
  auto *open = bar->addAction(tr("Open Recordings Folder"));
  connect(open, &QAction::triggered, this, [this] {
    if (!QFileInfo(m_outputDir).isDir()) {
      fail(
          tr("No recordings folder exists yet. Start recording to create it."));
      return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputDir));
  });
  connect(m_toggle, &QAction::triggered, this, [this](bool checked) {
    if (checked)
      requestRecording();
    else {
      stop();
      if (m_remember && !saveChoice(m_preferencesPath, m_buildId, 0))
        fail(tr("Unable to save the recording choice."));
    }
  });
  m_timer.setInterval(1000 / frameRate);
  connect(&m_timer, &QTimer::timeout, this, &OtDevRecorder::tick);
  m_finishTimer.setSingleShot(true);
  connect(&m_finishTimer, &QTimer::timeout, this, [this] {
    m_encoder.kill();
    fail(tr("FFmpeg did not finish in time. The last clip may be incomplete."));
  });
  connect(&m_encoder, &QProcess::readyReadStandardError, this, [this] {
    m_encoderError = (m_encoderError +
                      QString::fromLocal8Bit(m_encoder.readAllStandardError()))
                         .right(4096);
  });
  m_encoder.setStandardOutputFile(QProcess::nullDevice());
  connect(&m_encoder, &QProcess::started, this, [this] {
    if (!m_enabled) {
      finishClip();
      return;
    }
    m_frames = 0;
    m_backpressure.invalidate();
    updateStatus(tr("Recording armed"));
  });
  connect(&m_encoder, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError) {
            fail(tr("FFmpeg error: %1").arg(m_encoder.errorString()));
          });
  connect(
      &m_encoder, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      this, [this](int code, QProcess::ExitStatus status) {
        m_finishTimer.stop();
        const bool expected = m_finishing;
        m_finishing         = false;
        if (!expected || code != 0 || status != QProcess::NormalExit) {
          fail(tr("Recording stopped unexpectedly. %1\nFile: %2")
                   .arg(m_encoderError, m_outputFile));
          return;
        }
        if (!publishClip()) {
          fail(tr("Unable to finish the recording filename. The video remains "
                  "at: %1")
                   .arg(m_outputFile));
          return;
        }
        updateStatus(m_enabled ? tr("Recording armed") : tr("Recording off"));
      });
  connect(qApp, &QCoreApplication::aboutToQuit, this, &OtDevRecorder::stop);
}

OtDevRecorder::~OtDevRecorder() {
  m_timer.stop();
  m_finishTimer.stop();
  m_encoder.disconnect(this);
  const bool pending = m_finishing || m_encoder.state() != QProcess::NotRunning;
  m_encoder.closeWriteChannel();
  // aboutToQuit stops capture, but the event loop may already have ended.
  // Let EOF finalize the MP4 before destroying QProcess, then publish it here
  // if the asynchronous finished handler did not run.
  if (m_encoder.state() != QProcess::NotRunning &&
      !m_encoder.waitForFinished(finishTimeout)) {
    m_encoder.kill();
    m_encoder.waitForFinished(1000);
  }
  if (pending && m_encoder.state() == QProcess::NotRunning &&
      m_encoder.exitStatus() == QProcess::NormalExit &&
      m_encoder.exitCode() == 0)
    publishClip();
}

bool OtDevRecorder::publishClip() {
  // Only successfully finalized files receive the normal playback filename.
  // Preserve interrupted/failed working files for recovery; never overwrite.
  if (!m_outputFile.endsWith(".recording.mp4")) return true;
  const QString completed =
      m_outputFile.left(m_outputFile.size() -
                        QString(".recording.mp4").size()) +
      ".mp4";
  if (!QFile::rename(m_outputFile, completed)) return false;
  m_outputFile = completed;
  return true;
}

void OtDevRecorder::updateStatus(const QString &text) {
  m_status->setText(text);
  m_status->setToolTip(m_outputFile.isEmpty() ? m_outputDir : m_outputFile);
  const QSignalBlocker blocker(m_toggle);
  m_toggle->setChecked(m_enabled);
  m_toggle->setText(m_enabled ? tr("Stop Recording")
                              : tr("Record OT-Dev Session"));
}

void OtDevRecorder::initialize() {
  // No encoder process, directory or video is created before consent.
  // An opt-out always overrides even a remembered opt-in.
  if (qEnvironmentVariable("OTDEV_RECORD")
          .compare("off", Qt::CaseInsensitive) == 0) {
    updateStatus(tr("Recording off (OTDEV_RECORD=off)"));
    return;
  }
  const QString root = QFileInfo(m_portableRoot).canonicalFilePath() + "/";
  if (!QFileInfo(m_portableRoot).isDir() ||
      !QFileInfo(m_preferencesPath)
           .canonicalFilePath()
           .startsWith(root, Qt::CaseInsensitive)) {
    fail(
        tr("Recording requires preferences.ini inside this build's "
           "portablestuff."));
    return;
  }
  const int choice = rememberedChoice(m_preferencesPath, m_buildId);
  m_remember       = choice != -1;
  if (choice == 1)
    begin();
  else if (choice == -1)
    requestRecording();
}

void OtDevRecorder::requestRecording() {
  if (m_prompting || m_enabled) return;
  m_errorReported    = false;
  const QString root = QFileInfo(m_portableRoot).canonicalFilePath() + "/";
  if (!QFileInfo(m_portableRoot).isDir() ||
      !QFileInfo(m_preferencesPath)
           .canonicalFilePath()
           .startsWith(root, Qt::CaseInsensitive)) {
    fail(
        tr("Recording requires preferences.ini inside this build's "
           "portablestuff."));
    return;
  }
  m_prompting = true;
  QMessageBox dialog(m_window);
  dialog.setProperty("otdevNoCapture", true);
  dialog.setWindowTitle(tr("OT-Dev Session Recording"));
  dialog.setIcon(QMessageBox::Information);
  dialog.setText(tr("Record this OpenToonz session?"));
  dialog.setInformativeText(
      tr("This experimental build records the OpenToonz main UI and its owned "
         "Qt "
         "panels and dialogs. Visible artwork, names and paths in that UI can "
         "appear "
         "in the video. Native file dialogs pause capture.\n\n"
         "No desktop, other applications, microphone or system audio is "
         "recorded. "
         "Nothing is uploaded.\n\nRecordings are saved to:\n%1\n\n"
         "Use the recording toolbar to stop or resume at any time.")
          .arg(QDir::toNativeSeparators(m_outputDir)));
  auto *yes = dialog.addButton(tr("Start Recording"), QMessageBox::AcceptRole);
  auto *no  = dialog.addButton(tr("Do Not Record"), QMessageBox::RejectRole);
  dialog.setDefaultButton(yes);
  dialog.setEscapeButton(no);
  auto *remember =
      new QCheckBox(tr("Remember my choice for this build"), &dialog);
  remember->setChecked(m_remember);
  dialog.setCheckBox(remember);
  dialog.exec();
  const bool enabled = dialog.clickedButton() == yes;
  m_remember         = remember->isChecked();
  m_prompting        = false;
  if (!saveChoice(m_preferencesPath, m_buildId,
                  m_remember ? (enabled ? 1 : 0) : -1)) {
    fail(tr("Unable to save the recording choice. Recording remains off."));
    return;
  }
  if (enabled)
    begin();
  else
    stop();
}

void OtDevRecorder::begin() {
  m_errorReported = false;
  if (!QFileInfo(m_portableRoot).isDir()) {
    fail(tr("Portable data folder not found: %1").arg(m_portableRoot));
    return;
  }
  const QString root  = QFileInfo(m_portableRoot).canonicalFilePath() + "/";
  const QString prefs = QFileInfo(m_preferencesPath).canonicalFilePath();
  if (!prefs.startsWith(root, Qt::CaseInsensitive)) {
    fail(
        tr("Recording requires preferences.ini inside this build's "
           "portablestuff."));
    return;
  }
  if (!QFileInfo(m_encoderPath).isFile()) {
    fail(tr("Recorder FFmpeg not found: %1").arg(m_encoderPath));
    return;
  }
  if (!QDir().mkpath(m_outputDir)) {
    fail(tr("Cannot create recordings folder: %1").arg(m_outputDir));
    return;
  }
  if (!QFileInfo(m_outputDir)
           .canonicalFilePath()
           .startsWith(root, Qt::CaseInsensitive)) {
    fail(tr("Recordings folder must stay inside portablestuff."));
    return;
  }
  QTemporaryFile probe(QDir(m_outputDir).filePath("write-test-XXXXXX"));
  if (!probe.open()) {
    fail(tr("Recordings folder is not writable: %1").arg(m_outputDir));
    return;
  }
  m_enabled = true;
  m_diskCheck.invalidate();
  m_timer.start();
  updateStatus(tr("Recording armed"));
}

bool OtDevRecorder::captureAllowed() const {
  if (!m_window || !m_window->isVisible() || m_window->isMinimized() ||
      QApplication::applicationState() != Qt::ApplicationActive)
    return false;
  for (auto *widget : QApplication::topLevelWidgets())
    if (widget->isVisible() && qobject_cast<QFileDialog *>(widget))
      return false;
#ifdef Q_OS_WIN
  // Native modal dialogs disable their owner and may not have a QWidget at all.
  // Fail closed when the foreground native window cannot be matched to Qt.
  HWND foreground = GetForegroundWindow();
  DWORD processId = 0;
  GetWindowThreadProcessId(foreground, &processId);
  if (processId != GetCurrentProcessId()) return false;
  QWidget *foregroundWidget = QWidget::find(WId(foreground));
  if (!foregroundWidget || excluded(foregroundWidget) ||
      !ownedBy(foregroundWidget, m_window))
    return false;
#endif
  return true;
}

void OtDevRecorder::tick() {
  if (!m_enabled || m_finishing) return;
  if (!captureAllowed()) {
    updateStatus(tr("Recording paused (inactive or native dialog)"));
    return;
  }
  if (!m_diskCheck.isValid() || m_diskCheck.elapsed() > 2000) {
    QStorageInfo storage(m_outputDir);
    if (!storage.isValid() || !storage.isReady() ||
        storage.bytesAvailable() < diskReserve) {
      fail(
          tr("Recording stopped: keep at least 1 GiB free on the recordings "
             "drive."));
      return;
    }
    m_diskCheck.start();
  }
  if (m_encoder.state() == QProcess::Starting) return;
  if (m_encoder.state() == QProcess::NotRunning) {
    m_outputFile =
        QDir(m_outputDir)
            .filePath("otdev-" +
                      QDateTime::currentDateTimeUtc().toString(
                          "yyyyMMdd-hhmmss-zzz") +
                      "-" + QUuid::createUuid().toString(QUuid::WithoutBraces) +
                      ".recording.mp4");
    m_encoderError.clear();
    m_encoder.start(m_encoderPath, encoderArguments(frameSize, m_outputFile));
    return;
  }
  const qint64 frameBytes = qint64(frameSize.width()) * frameSize.height() * 4;
  if (m_encoder.bytesToWrite() > frameBytes) {
    if (!m_backpressure.isValid()) m_backpressure.start();
    if (m_backpressure.elapsed() > 5000)
      fail(tr("Recording stopped: FFmpeg cannot keep up with capture."));
    else
      updateStatus(tr("Recording busy (dropping frames)"));
    return;
  }
  m_backpressure.invalidate();
  const QImage frame = capture(m_window, frameSize);
  if (frame.isNull()) {
    fail(tr("Unable to allocate a recording frame."));
    return;
  }
  if (m_encoder.write(reinterpret_cast<const char *>(frame.constBits()),
                      frameBytes) != frameBytes) {
    fail(tr("Unable to send video frame to FFmpeg."));
    return;
  }
  updateStatus(tr("RECORDING"));
  if (++m_frames >= clipFrames) finishClip();
}

void OtDevRecorder::finishClip() {
  if (m_encoder.state() == QProcess::NotRunning || m_finishing) return;
  m_finishing = true;
  m_encoder.closeWriteChannel();
  m_finishTimer.start(finishTimeout);
  updateStatus(tr("Preparing recording for playback..."));
}

void OtDevRecorder::stop() {
  m_enabled = false;
  m_timer.stop();
  finishClip();
  if (!m_finishing) updateStatus(tr("Recording off"));
}

void OtDevRecorder::fail(const QString &message) {
  stop();
  updateStatus(tr("Recording off - error"));
  if (m_errorReported) return;
  m_errorReported = true;
  // Non-blocking error window, excluded from subsequent capture.
  auto *dialog =
      new QMessageBox(QMessageBox::Warning, tr("OT-Dev Session Recording"),
                      message, QMessageBox::Ok, m_window);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setProperty("otdevNoCapture", true);
  dialog->open();
}
