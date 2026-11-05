#include "handwritingsurface.h"
#ifdef _WIN32
#include "winhandwritingrecognizer.h"
#endif

#include <QApplication>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QtTest>

class HandwritingTest final : public QObject {
  Q_OBJECT
private slots:
  void mouseStrokeAndDot() {
    HandwritingSurface surface;
    surface.show();
    QSignalSpy changes(&surface, &HandwritingSurface::strokesChanged);
    QTest::mousePress(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(30, 90));
    QTest::mouseMove(&surface, QPoint(60, 120));
    QTest::mouseRelease(&surface, Qt::LeftButton, Qt::NoModifier,
                        QPoint(90, 90));
    QCOMPARE(changes.count(), 1);
    QCOMPARE(surface.strokes().size(), 1);
    QCOMPARE(surface.strokes()[0].first(), QPointF(30, 90));
    QCOMPARE(surface.strokes()[0].last(), QPointF(90, 90));
    QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
                      QPoint(120, 80));
    QCOMPARE(changes.count(), 2);
    QCOMPARE(surface.strokes().last().size(), 1);
  }

  void changingDestinationCancelsUnfinishedStroke() {
    HandwritingSurface surface;
    QSignalSpy changes(&surface, &HandwritingSurface::strokesChanged);
    QTest::mousePress(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(30, 90));
    QVERIFY(surface.isWriting());
    surface.setContent({}, "Different note");
    QTest::mouseRelease(&surface, Qt::LeftButton, Qt::NoModifier,
                        QPoint(90, 90));
    QVERIFY(!surface.isWriting());
    QVERIFY(surface.strokes().isEmpty());
    QCOMPARE(changes.count(), 0);
  }

  void tabletDoesNotDuplicateMouseEvents() {
    HandwritingSurface surface;
    QSignalSpy changes(&surface, &HandwritingSurface::strokesChanged);
    auto tablet = [&surface](QEvent::Type type) {
      QTabletEvent event(type, QPointF(40, 100), QPointF(40, 100),
                         QTabletEvent::Stylus, QTabletEvent::Pen, 0.5, 0, 0,
                         0.0, 0.0, 0, Qt::NoModifier, 42, Qt::LeftButton,
                         Qt::LeftButton);
      QApplication::sendEvent(&surface, &event);
    };
    tablet(QEvent::TabletPress);
    tablet(QEvent::TabletRelease);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(40, 100),
                      QPointF(40, 100), QPointF(40, 100), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier,
                      Qt::MouseEventSynthesizedByQt);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(40, 100),
                        QPointF(40, 100), QPointF(40, 100), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier,
                        Qt::MouseEventSynthesizedByQt);
    QApplication::sendEvent(&surface, &press);
    QApplication::sendEvent(&surface, &release);
    QCOMPARE(changes.count(), 1);
    QCOMPARE(surface.strokes().size(), 1);
  }

  void hidingLayersKeepsTheirContent() {
    HandwritingSurface surface;
    QPolygonF stroke;
    stroke << QPointF(30, 90) << QPointF(60, 150) << QPointF(90, 90);
    surface.setContent({stroke}, "Text layer");
    const QImage both = surface.grab().toImage();
    surface.setInkVisible(false);
    const QImage text = surface.grab().toImage();
    QVERIFY(both != text);
    surface.setTextVisible(false);
    QVERIFY(text != surface.grab().toImage());
    QCOMPARE(surface.strokes().size(), 1);
    surface.setInkVisible(true);
    surface.setTextVisible(true);
    QCOMPARE(surface.grab().toImage(), both);
  }

  void surfaceResizeKeepsStrokeCoordinates() {
    HandwritingSurface surface;
    QPolygonF stroke;
    stroke << QPointF(1400, 100) << QPointF(1500, 180);
    surface.setContent({stroke}, QString());
    surface.setFixedWidth(3200);
    QCOMPARE(surface.strokes()[0], stroke);
  }

#ifdef _WIN32
  void windowsRecognizerSmokeTest() {
    WinHandwritingRecognizer recognizer;
    QSignalSpy ready(&recognizer, &WinHandwritingRecognizer::recognizersReady);
    QSignalSpy results(&recognizer, &WinHandwritingRecognizer::recognized);
    recognizer.start();
    QVERIFY(ready.wait(30000));
    if (ready.first().first().toStringList().isEmpty())
      QSKIP("Windows Handwriting language feature is not installed");
    QPolygonF first, second, middle;
    first << QPointF(40, 80) << QPointF(40, 160);
    second << QPointF(100, 80) << QPointF(100, 160);
    middle << QPointF(40, 120) << QPointF(100, 120);
    recognizer.recognize({first, second, middle}, 0, 123);
    QVERIFY(results.wait(30000));
    QCOMPARE(results.last()[0].toULongLong(), quint64(123));
    QVERIFY2(results.last()[2].toString().isEmpty(),
             qPrintable(results.last()[2].toString()));
    // The recognized spelling is deliberately not asserted for synthetic ink.
  }
#endif
};

QTEST_MAIN(HandwritingTest)
#include "handwriting_test.moc"
