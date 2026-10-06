#pragma once

#ifndef TOUCH_GESTURE_RECOGNIZER_H
#define TOUCH_GESTURE_RECOGNIZER_H

#include "touchslots.h"

#include <QObject>
#include <QPointF>
#include <QSize>
#include <QTimer>

class QTouchEvent;

class TouchGestureRecognizer final : public QObject {
  Q_OBJECT

  struct Session {
    bool active         = false;
    bool tablet         = false;
    int startCount      = 0;
    int maxCount        = 0;
    QPointF startCentroid;
    QPointF lastCentroid;
    QPointF swipeAnchor;
    double startSpan    = 0.0;
    qint64 startMs      = 0;
    bool holdFired      = false;
    bool swipeArmed     = false;
    bool pinchFired     = false;
    bool reservedBusy   = false;
    int edge            = 0;
    int liveCount       = 0;
    QSize widgetSize;
  } m_session;

  QTimer m_holdTimer;
  QTimer m_tapTimer;
  TouchSlot m_pendingTap      = TouchSlot_None;
  TouchSlot m_lastTap         = TouchSlot_None;
  qint64 m_lastTapMs          = 0;
  QPointF m_lastTapPos;

public:
  explicit TouchGestureRecognizer(QObject *parent = nullptr);

  void begin(const QTouchEvent *e, bool tabletActive, const QSize &widgetSize);
  void update(const QTouchEvent *e, bool reservedBusy);
  void end(const QTouchEvent *e);
  void cancel();

signals:
  void slotTriggered(int slot, bool repeat);

private:
  void emitSlot(TouchSlot slot, bool repeat);
  void onHoldTimeout();
  void onTapTimeout();
  void classifyRelease();
  void considerSwipeOrPinch(const QPointF &centroid, double span);
};

#endif
