#include "otdevrecorder.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QtEndian>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif

namespace {
// Hybrid MP4 hides old fragment headers inside mdat after finalization.
// Inspect real top-level boxes, not arbitrary byte strings in compressed data.
QList<QByteArray> mp4Boxes(const QByteArray &video) {
  QList<QByteArray> boxes;
  for (qint64 offset = 0; offset < video.size();) {
    if (video.size() - offset < 8) return {};
    quint64 size          = qFromBigEndian<quint32>(video.constData() + offset);
    const QByteArray type = video.mid(offset + 4, 4);
    if (size == 1) {
      if (video.size() - offset < 16) return {};
      size = qFromBigEndian<quint64>(video.constData() + offset + 8);
      if (size < 16) return {};
    } else if (size == 0) {
      size = video.size() - offset;
    }
    if (size < 8 || size > quint64(video.size() - offset)) return {};
    boxes.append(type);
    offset += size;
  }
  return boxes;
}

void verifyPlaybackFile(const QString &path) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadOnly));
  const QByteArray video = file.readAll();
  const auto boxes       = mp4Boxes(video);
  QVERIFY(boxes.contains("ftyp"));
  QVERIFY(boxes.contains("mdat"));
  QCOMPARE(boxes.count("moov"), 1);
  QVERIFY2(!boxes.contains("moof"),
           "Finished MP4 still requires fragment support");
  QVERIFY(video.contains("avc1"));
  QVERIFY(video.contains("avcC"));
}

#ifdef Q_OS_WIN
// Exercise Windows' own MP4 source and H.264 decoder, without FFmpeg or a
// separately installed codec pack. This also catches readable-but-unseekable
// MP4.
void verifyWindowsPlayback(const QString &path, const QSize &size, int frames) {
  using Microsoft::WRL::ComPtr;
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  QVERIFY(SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE);
  struct ComScope {
    bool initialized;
    ~ComScope() {
      if (initialized) CoUninitialize();
    }
  } com{SUCCEEDED(initialized)};
  const HRESULT startup = MFStartup(MF_VERSION);
  QVERIFY2(SUCCEEDED(startup),
           "Windows Media Foundation is required for playback tests");
  struct MediaScope {
    ~MediaScope() { MFShutdown(); }
  } media;
  ComPtr<IMFSourceReader> reader;
  QVERIFY(SUCCEEDED(
      MFCreateSourceReaderFromURL(reinterpret_cast<LPCWSTR>(path.utf16()),
                                  nullptr, reader.GetAddressOf())));
  ComPtr<IMFMediaType> native;
  QVERIFY(SUCCEEDED(reader->GetNativeMediaType(
      MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, native.GetAddressOf())));
  GUID subtype;
  QVERIFY(SUCCEEDED(native->GetGUID(MF_MT_SUBTYPE, &subtype)));
  QVERIFY(subtype == MFVideoFormat_H264);
  ComPtr<IMFMediaType> decoded;
  QVERIFY(SUCCEEDED(MFCreateMediaType(decoded.GetAddressOf())));
  QVERIFY(SUCCEEDED(decoded->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)));
  QVERIFY(SUCCEEDED(decoded->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)));
  QVERIFY(SUCCEEDED(reader->SetCurrentMediaType(
      MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, decoded.Get())));
  ComPtr<IMFMediaType> current;
  QVERIFY(SUCCEEDED(reader->GetCurrentMediaType(
      MF_SOURCE_READER_FIRST_VIDEO_STREAM, current.GetAddressOf())));
  UINT32 width = 0, height = 0;
  QVERIFY(SUCCEEDED(
      MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &width, &height)));
  QCOMPARE(QSize(width, height), size);
  int count = 0;
  for (int read = 0; read < frames + 10; ++read) {
    DWORD flags        = 0;
    LONGLONG timestamp = 0;
    ComPtr<IMFSample> sample;
    QVERIFY(SUCCEEDED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                         nullptr, &flags, &timestamp,
                                         sample.GetAddressOf())));
    QVERIFY(!(flags & MF_SOURCE_READERF_ERROR));
    if (sample) {
      DWORD bytes = 0;
      QVERIFY(SUCCEEDED(sample->GetTotalLength(&bytes)));
      QVERIFY(bytes >= DWORD(size.width() * size.height() * 3 / 2));
      ++count;
    }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
  }
  QCOMPARE(count, frames);
  PROPVARIANT position   = {};
  position.vt            = VT_I8;
  position.hVal.QuadPart = 10000000;  // One second in 100-nanosecond units.
  QVERIFY(SUCCEEDED(reader->SetCurrentPosition(GUID_NULL, position)));
  ComPtr<IMFSample> sought;
  DWORD flags = 0;
  QVERIFY(SUCCEEDED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                       nullptr, &flags, nullptr,
                                       sought.GetAddressOf())));
  QVERIFY(sought);
}
#endif
}  // namespace

class ColorGL final : public QOpenGLWidget, protected QOpenGLFunctions {
protected:
  void initializeGL() override { initializeOpenGLFunctions(); }
  void paintGL() override {
    glClearColor(0.f, 1.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
};

class RecorderTest final : public QObject {
  Q_OBJECT
private slots:
  void choicesArePerBuild() {
    QTemporaryDir dir;
    const QString path = dir.filePath("preferences.ini");
    QCOMPARE(OtDevRecorder::rememberedChoice(path, "build-1"), -1);
    QVERIFY(OtDevRecorder::saveChoice(path, "build-1", 1));
    QCOMPARE(OtDevRecorder::rememberedChoice(path, "build-1"), 1);
    QCOMPARE(OtDevRecorder::rememberedChoice(path, "build-2"), -1);
    QVERIFY(OtDevRecorder::saveChoice(path, "build-1", 0));
    QCOMPARE(OtDevRecorder::rememberedChoice(path, "build-1"), 0);
    QVERIFY(OtDevRecorder::saveChoice(path, "build-1", -1));
    QCOMPARE(OtDevRecorder::rememberedChoice(path, "build-1"), -1);
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue("OtDevRecorder/Choice", "invalid");
    settings.sync();
    QCOMPARE(OtDevRecorder::rememberedChoice(path, "build-1"), -1);
  }

  void escapeIsOptOut() {
    QTemporaryDir dir;
    QVERIFY(OtDevRecorder::saveChoice(dir.filePath("preferences.ini"),
                                      "old-build", 0));
    QMainWindow window;
    window.show();
    OtDevRecorder recorder(&window, dir.filePath("preferences.ini"),
                           "test-build", dir.path(),
                           dir.filePath("missing-ffmpeg.exe"));
    QTimer::singleShot(50, [] {
      auto *dialog =
          qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      QTest::keyClick(dialog, Qt::Key_Escape);
    });
    recorder.initialize();
    QVERIFY(!QFileInfo::exists(dir.filePath("recordings")));
    auto *bar = window.findChild<QToolBar *>("OtDevSessionRecorder");
    QVERIFY(bar);
    QVERIFY(!bar->actions().first()->isChecked());
  }

  void rememberedOptOutNeverCreatesOutput() {
    QTemporaryDir dir;
    const QString prefs = dir.filePath("preferences.ini");
    QVERIFY(OtDevRecorder::saveChoice(prefs, "test-build", 0));
    QMainWindow window;
    OtDevRecorder recorder(&window, prefs, "test-build", dir.path(), "missing");
    recorder.initialize();
    QVERIFY(!QFileInfo::exists(dir.filePath("recordings")));
    QVERIFY(!QApplication::activeModalWidget());
  }

  void startupOverrideWinsOverRememberedOptIn() {
    QTemporaryDir dir;
    const QString prefs = dir.filePath("preferences.ini");
    QVERIFY(OtDevRecorder::saveChoice(prefs, "test-build", 1));
    QMainWindow window;
    OtDevRecorder recorder(&window, prefs, "test-build", dir.path(), "missing");
    qputenv("OTDEV_RECORD", "off");
    recorder.initialize();
    qunsetenv("OTDEV_RECORD");
    QVERIFY(!QFileInfo::exists(dir.filePath("recordings")));
    QVERIFY(!QApplication::activeModalWidget());
  }

  void optInReportsMissingEncoder() {
    QTemporaryDir dir;
    const QString prefs = dir.filePath("preferences.ini");
    QVERIFY(OtDevRecorder::saveChoice(prefs, "test-build", -1));
    QMainWindow window;
    window.show();
    OtDevRecorder recorder(&window, prefs, "test-build", dir.path(), "missing");
    QTimer::singleShot(50, [] {
      auto *dialog =
          qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      dialog->checkBox()->setChecked(true);
      for (auto *button : dialog->buttons())
        if (dialog->buttonRole(button) == QMessageBox::AcceptRole) {
          button->click();
          return;
        }
      QFAIL("Start Recording button missing");
    });
    recorder.initialize();
    QCOMPARE(OtDevRecorder::rememberedChoice(prefs, "test-build"), 1);
    QVERIFY(!QFileInfo::exists(dir.filePath("recordings")));
    auto *error = window.findChild<QMessageBox *>();
    QVERIFY(error);
    QVERIFY(error->text().contains("FFmpeg not found"));
    error->close();
  }

  void controllerCreatesAndFinalizesClip() {
    QTemporaryDir dir;
    const QString prefs = dir.filePath("preferences.ini");
    QVERIFY(OtDevRecorder::saveChoice(prefs, "test-build", 1));
    QMainWindow window;
    window.resize(320, 240);
    window.show();
    window.activateWindow();
    QTest::qWait(100);
    OtDevRecorder recorder(&window, prefs, "test-build", dir.path(),
                           qEnvironmentVariable("OTDEV_TEST_FFMPEG"));
    recorder.initialize();
    QTRY_VERIFY_WITH_TIMEOUT(!QDir(dir.filePath("recordings"))
                                  .entryList({"*.mp4"}, QDir::Files)
                                  .isEmpty(),
                             10000);
    QTest::qWait(500);
    QCOMPARE(QDir(dir.filePath("recordings"))
                 .entryList({"*.recording.mp4"}, QDir::Files)
                 .size(),
             1);
    auto *bar = window.findChild<QToolBar *>("OtDevSessionRecorder");
    QVERIFY(bar->actions().first()->isChecked());
    bar->actions().first()->trigger();
    QCOMPARE(OtDevRecorder::rememberedChoice(prefs, "test-build"), 0);
    QTRY_COMPARE_WITH_TIMEOUT(bar->findChild<QLabel *>()->text(),
                              QString("Recording off"), 10000);
    const auto files =
        QDir(dir.filePath("recordings")).entryList({"*.mp4"}, QDir::Files);
    QCOMPARE(files.size(), 1);
    QVERIFY(!files.first().endsWith(".recording.mp4"));
    verifyPlaybackFile(dir.filePath("recordings/" + files.first()));
  }

  void shutdownFinalizesWithoutEventLoop() {
    QTemporaryDir dir;
    const QString prefs = dir.filePath("preferences.ini");
    QVERIFY(OtDevRecorder::saveChoice(prefs, "test-build", 1));
    QMainWindow window;
    window.resize(320, 240);
    window.show();
    window.activateWindow();
    QTest::qWait(100);
    {
      OtDevRecorder recorder(&window, prefs, "test-build", dir.path(),
                             qEnvironmentVariable("OTDEV_TEST_FFMPEG"));
      recorder.initialize();
      auto *bar = window.findChild<QToolBar *>("OtDevSessionRecorder");
      QTRY_COMPARE_WITH_TIMEOUT(bar->findChild<QLabel *>()->text(),
                                QString("RECORDING"), 10000);
      QTest::qWait(500);
      recorder.stop();  // aboutToQuit; no further event processing before
                        // destruction.
    }
    const auto files =
        QDir(dir.filePath("recordings")).entryList({"*.mp4"}, QDir::Files);
    QCOMPARE(files.size(), 1);
    QVERIFY(!files.first().endsWith(".recording.mp4"));
    verifyPlaybackFile(dir.filePath("recordings/" + files.first()));
  }

  void openGLFramebuffer() {
    QMainWindow window;
    auto *gl = new ColorGL;
    window.setCentralWidget(gl);
    window.resize(320, 240);
    window.show();
    QTest::qWait(200);
    if (!gl->isValid())
      QSKIP(
          "No OpenGL context on this test platform; interactive GL acceptance "
          "still required");
    const QImage frame = OtDevRecorder::capture(&window, QSize(320, 240));
    QCOMPARE(frame.pixelColor(160, 120), QColor(Qt::green));
  }

  void pipeOnlyEncoderArguments() {
    const auto args =
        OtDevRecorder::encoderArguments(QSize(640, 480), "test.mp4");
    QCOMPARE(args.at(args.indexOf("-i") + 1), QString("pipe:0"));
    QVERIFY(args.contains("-an"));
    QVERIFY(args.contains("-n"));
    QVERIFY(!args.contains("gdigrab"));
    QVERIFY(!args.contains("desktop"));
    QVERIFY(!args.contains("-y"));
    QCOMPARE(args.last(), QString("test.mp4"));
  }

  void frameSizeAndOwnership() {
    QMainWindow window;
    window.setStyleSheet("background: rgb(255,0,0)");
    window.resize(201, 151);
    window.show();
    QTest::qWait(50);
    QImage frame = OtDevRecorder::capture(&window, QSize(320, 240));
    QCOMPARE(frame.size(), QSize(320, 240));
    QCOMPARE(frame.sizeInBytes(), qint64(320 * 240 * 4));
    QCOMPARE(frame.pixelColor(160, 120), QColor(Qt::red));

    // An unrelated top-level widget never belongs to the composition.
    QWidget foreign;
    foreign.setStyleSheet("background: rgb(0,255,0)");
    foreign.setGeometry(window.geometry());
    foreign.show();
    foreign.raise();
    QTest::qWait(50);
    frame = OtDevRecorder::capture(&window, QSize(320, 240));
    QCOMPARE(frame.pixelColor(160, 120), QColor(Qt::red));

    QDialog dialog(&window);
    dialog.setStyleSheet("background: rgb(0,0,255)");
    dialog.resize(80, 60);
    dialog.move(window.mapToGlobal(QPoint(50, 40)));
    dialog.show();
    dialog.activateWindow();
    QTest::qWait(50);
    frame = OtDevRecorder::capture(&window, QSize(320, 240));
    QCOMPARE(frame.pixelColor(160, 120), QColor(Qt::blue));
    dialog.hide();
    window.resize(301, 203);
    frame = OtDevRecorder::capture(&window, QSize(320, 240));
    QCOMPARE(frame.sizeInBytes(), qint64(320 * 240 * 4));
  }

  void actualEncoder_data() {
    QTest::addColumn<QSize>("size");
    QTest::newRow("small") << QSize(64, 64);
    QTest::newRow("full-hd") << QSize(1920, 1080);
  }

  void interruptedCaptureKeepsReadableFragments() {
    QTemporaryDir dir;
    const QString output = dir.filePath("interrupted.recording.mp4");
    QProcess encoder;
    encoder.start(qEnvironmentVariable("OTDEV_TEST_FFMPEG"),
                  OtDevRecorder::encoderArguments(QSize(64, 64), output));
    QVERIFY(encoder.waitForStarted());
    QImage frame(64, 64, QImage::Format_RGB32);
    for (int i = 0; i < 60; ++i) {
      frame.fill(i % 2 ? Qt::red : Qt::blue);
      QCOMPARE(encoder.write(reinterpret_cast<const char *>(frame.constBits()),
                             frame.sizeInBytes()),
               frame.sizeInBytes());
      while (encoder.bytesToWrite() > 0)
        QVERIFY(encoder.waitForBytesWritten(15000));
    }
    auto fragments = [&] {
      QFile file(output);
      return file.open(QIODevice::ReadOnly)
                 ? mp4Boxes(file.readAll()).count("moof")
                 : 0;
    };
    QTRY_VERIFY_WITH_TIMEOUT(fragments() >= 2, 10000);
    encoder
        .kill();  // Intentionally omit EOF, as in a crash or forced shutdown.
    QVERIFY(encoder.waitForFinished());
    const QString decoded = dir.filePath("recovered.bgra");
    QProcess decoder;
    decoder.start(qEnvironmentVariable("OTDEV_TEST_DECODER"),
                  {"-v", "error", "-nostdin", "-n", "-i", output, "-an", "-c:v",
                   "rawvideo", "-pix_fmt", "bgra", "-f", "rawvideo", decoded});
    QVERIFY(decoder.waitForStarted());
    QVERIFY(decoder.waitForFinished(15000));
    QVERIFY2(decoder.exitCode() == 0,
             decoder.readAllStandardError().constData());
    QVERIFY(QFileInfo(decoded).size() >= 48LL * 64 * 64 * 4);
  }

  void actualEncoder() {
    QFETCH(QSize, size);
    const QString encoder = qEnvironmentVariable("OTDEV_TEST_FFMPEG");
    const QString decoder = qEnvironmentVariable("OTDEV_TEST_DECODER");
    QVERIFY2(!encoder.isEmpty(),
             "Set OTDEV_TEST_FFMPEG to the packaged encoder");
    QVERIFY2(!decoder.isEmpty(),
             "Set OTDEV_TEST_DECODER to a separate reference FFmpeg decoder");
    QTemporaryDir dir;
    const QString output  = dir.filePath("test.mp4");
    const QString decoded = dir.filePath("decoded.bgra");
    // Include neutral tones, each primary and multiple frame changes across
    // the 24-frame keyframe boundary. Black-only/container-only smoke tests
    // missed the pink chroma and horizontal stripes in real recordings.
    const QColor colors[] = {Qt::black,  Qt::white, QColor(128, 128, 128),
                             Qt::red,    Qt::green, Qt::blue,
                             Qt::yellow, Qt::cyan};
    auto frameFor         = [&](int index) {
      QImage frame(size, QImage::Format_RGB32);
      QPainter painter(&frame);
      for (int row = 0; row < 2; ++row)
        for (int column = 0; column < 8; ++column)
          painter.fillRect(column * size.width() / 8, row * size.height() / 2,
                                   size.width() / 8, size.height() / 2,
                                   colors[(column + row * 3 + index / 6) % 8]);
      return frame;
    };
    constexpr int frames = 30;
    QProcess process;
    process.start(encoder, OtDevRecorder::encoderArguments(size, output));
    QVERIFY(process.waitForStarted());
    for (int i = 0; i < frames; ++i) {
      const QImage frame = frameFor(i);
      QCOMPARE(process.write(reinterpret_cast<const char *>(frame.constBits()),
                             frame.sizeInBytes()),
               frame.sizeInBytes());
      while (process.bytesToWrite() > 0)
        QVERIFY(process.waitForBytesWritten(15000));
    }
    process.closeWriteChannel();
    QVERIFY(process.waitForFinished(15000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0,
             process.readAllStandardError().constData());
    verifyPlaybackFile(output);
#ifdef Q_OS_WIN
    verifyWindowsPlayback(output, size, frames);
#endif

    // Decode every frame through a separate executable, not the recorder's
    // deliberately raw-input-only binary. No playback dependencies are shipped.
    process.start(decoder,
                  {"-v", "error", "-nostdin", "-n", "-i", output, "-an", "-c:v",
                   "rawvideo", "-pix_fmt", "bgra", "-f", "rawvideo", decoded});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished(20000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0,
             process.readAllStandardError().constData());
    QVERIFY2(process.readAllStandardError().isEmpty(),
             "Decoder reported errors");
    QFile raw(decoded);
    QVERIFY(raw.open(QIODevice::ReadOnly));
    const qint64 frameBytes = qint64(size.width()) * size.height() * 4;
    QCOMPARE(raw.size(), frames * frameBytes);
    for (int i = 0; i < frames; ++i) {
      const QByteArray bytes = raw.read(frameBytes);
      QCOMPARE(qint64(bytes.size()), frameBytes);
      const QImage actual(reinterpret_cast<const uchar *>(bytes.constData()),
                          size.width(), size.height(), QImage::Format_RGB32);
      const QImage expected = frameFor(i);
      for (int column = 0; column < 8; ++column) {
        const int x = (2 * column + 1) * size.width() / 16;
        for (int row = 0; row < 32; ++row) {
          const int y = (2 * row + 1) * size.height() / 64;
          // Lossy 4:2:0 conversion blends the boundary between the two rows.
          if (qAbs(y - size.height() / 2) < 4) continue;
          const QColor a = actual.pixelColor(x, y);
          const QColor e = expected.pixelColor(x, y);
          const QString message =
              QString("Frame %1 at (%2,%3): expected %4, decoded %5")
                  .arg(i)
                  .arg(x)
                  .arg(y)
                  .arg(e.name(), a.name());
          QVERIFY2(qAbs(a.red() - e.red()) <= 8 &&
                       qAbs(a.green() - e.green()) <= 8 &&
                       qAbs(a.blue() - e.blue()) <= 8,
                   qPrintable(message));
        }
      }
    }
  }
};

QTEST_MAIN(RecorderTest)
#include "recorder_test.moc"
