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
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>

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
    auto *bar = window.findChild<QToolBar *>("OtDevSessionRecorder");
    QVERIFY(bar->actions().first()->isChecked());
    bar->actions().first()->trigger();
    QCOMPARE(OtDevRecorder::rememberedChoice(prefs, "test-build"), 0);
    QTRY_COMPARE_WITH_TIMEOUT(bar->findChild<QLabel *>()->text(),
                              QString("Recording off"), 10000);
    const auto files =
        QDir(dir.filePath("recordings")).entryList({"*.mp4"}, QDir::Files);
    QCOMPARE(files.size(), 1);
    QFile file(dir.filePath("recordings/" + files.first()));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("moof"));
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

  void actualEncoder() {
    const QString encoder = qEnvironmentVariable("OTDEV_TEST_FFMPEG");
    QVERIFY2(!encoder.isEmpty(),
             "Set OTDEV_TEST_FFMPEG to the packaged encoder");
    QTemporaryDir dir;
    const QString output = dir.filePath("test.mp4");
    QProcess process;
    process.start(encoder,
                  OtDevRecorder::encoderArguments(QSize(64, 64), output));
    QVERIFY(process.waitForStarted());
    QByteArray frame(64 * 64 * 4, '\0');
    for (int i = 0; i < 24; ++i)
      QCOMPARE(process.write(frame), qint64(frame.size()));
    process.closeWriteChannel();
    QVERIFY(process.waitForFinished(15000));
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(QFileInfo(output).size() > 100);
    QFile file(output);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QByteArray video = file.readAll();
    QVERIFY(video.contains("ftyp"));
    QVERIFY(video.contains("moof"));
  }
};

QTEST_MAIN(RecorderTest)
#include "recorder_test.moc"
