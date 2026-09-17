#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

class QAction;
class QLabel;
class QMainWindow;

// Built and installed only by the OT-Dev Windows CI injection step.
class OtDevRecorder final : public QObject {
public:
  OtDevRecorder(QMainWindow *window, const QString &preferencesPath,
                const QString &buildId, const QString &portableRoot,
                const QString &encoder);
  ~OtDevRecorder() override;
  void initialize();
  void requestRecording();
  void stop();

  static int rememberedChoice(const QString &path, const QString &buildId);
  static bool saveChoice(const QString &path, const QString &buildId,
                         int choice);
  static QImage capture(QMainWindow *window, const QSize &size,
                        bool includeCursor = true);
  static QStringList encoderArguments(const QSize &size, const QString &output);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void begin();
  void tick();
  void finishClip();
  bool publishClip();
  void fail(const QString &message);
  void updateStatus(const QString &text);
  bool captureAllowed() const;

  QPointer<QMainWindow> m_window;
  QString m_preferencesPath, m_buildId, m_portableRoot, m_encoderPath;
  QString m_outputDir, m_outputFile, m_encoderError;
  QAction *m_toggle;
  QLabel *m_status;
  QProcess m_encoder;
  QTimer m_timer, m_finishTimer;
  QElapsedTimer m_diskCheck, m_backpressure;
  bool m_enabled = false, m_finishing = false, m_prompting = false;
  bool m_remember      = false;
  bool m_errorReported = false;
  int m_frames         = 0;
};
