#include "touchgesturerecognizer.h"
#include "touchpresetmanager.h"

#include "toonz/preferences.h"

#include <QDateTime>
#include <QLineF>
#include <QTouchEvent>
#include <algorithm>
#include <cmath>

namespace {

const int kHoldMs               = 500;
const int kDoubleTapMinMs       = 45;
const int kDoubleTapMaxMs       = 190;
const double kDoubleTapSlop     = 26.0;
const double kTapSlop           = 40.0;
const double kSwipePx           = 100.0;
const double kHorizSwipePx      = 120.0;
const double kPinchRatio        = 0.18;
const double kHorizMaxSpanDelta = 0.10;
const int kTwoFingerHorizArmMs  = 90;
const double kEdgeBand          = 36.0;

QList<QTouchEvent::TouchPoint> livePoints(const QTouchEvent *e) {
  QList<QTouchEvent::TouchPoint> live;
  if (!e) return live;
  for (const QTouchEvent::TouchPoint &p : e->touchPoints()) {
    if (p.state() != Qt::TouchPointReleased) live.append(p);
  }
  return live;
}

QPointF centroidOf(const QList<QTouchEvent::TouchPoint> &pts) {
  QPointF c;
  if (pts.isEmpty()) return c;
  for (const QTouchEvent::TouchPoint &p : pts) c += p.pos();
  return c / pts.size();
}

double spanOf(const QList<QTouchEvent::TouchPoint> &pts) {
  if (pts.size() < 2) return 0.0;
  double maxD = 0.0;
  for (int i = 0; i < pts.size(); ++i) {
    for (int j = i + 1; j < pts.size(); ++j) {
      maxD = std::max(maxD, QLineF(pts[i].pos(), pts[j].pos()).length());
    }
  }
  return maxD;
}

int clampCount(int n) {
  if (n < 1) return 1;
  if (n > 5) return 5;
  return n;
}

int nearestEdge(const QPointF &c, const QSize &sz) {
  if (sz.width() <= 0 || sz.height() <= 0) return 0;
  const double l = c.x();
  const double r = sz.width() - c.x();
  const double t = c.y();
  const double b = sz.height() - c.y();
  const double m = std::min(std::min(l, r), std::min(t, b));
  if (m > kEdgeBand) return 0;
  if (m == l) return 1;
  if (m == r) return 2;
  if (m == t) return 3;
  return 4;
}

TouchSlot tapSlot(int n) {
  switch (n) {
  case 1:
    return TouchSlot_Tap1;
  case 2:
    return TouchSlot_Tap2;
  case 3:
    return TouchSlot_Tap3;
  case 4:
    return TouchSlot_Tap4;
  case 5:
    return TouchSlot_Tap5;
  default:
    return TouchSlot_None;
  }
}

TouchSlot doubleTapSlot(int n) {
  switch (n) {
  case 1:
    return TouchSlot_DoubleTap1;
  case 2:
    return TouchSlot_DoubleTap2;
  case 3:
    return TouchSlot_DoubleTap3;
  case 4:
    return TouchSlot_DoubleTap4;
  case 5:
    return TouchSlot_DoubleTap5;
  default:
    return TouchSlot_None;
  }
}

TouchSlot holdSlot(int n) {
  switch (n) {
  case 1:
    return TouchSlot_Hold1;
  case 2:
    return TouchSlot_Hold2;
  case 3:
    return TouchSlot_Hold3;
  case 4:
    return TouchSlot_Hold4;
  case 5:
    return TouchSlot_Hold5;
  default:
    return TouchSlot_None;
  }
}

TouchSlot swipeSlot(int n, const QPointF &delta) {
  const bool horiz = std::abs(delta.x()) >= std::abs(delta.y());
  if (n == 2) {
    if (horiz)
      return delta.x() < 0 ? TouchSlot_Swipe2Left : TouchSlot_Swipe2Right;
    return delta.y() < 0 ? TouchSlot_Swipe2Up : TouchSlot_Swipe2Down;
  }
  if (n == 3) {
    if (horiz)
      return delta.x() < 0 ? TouchSlot_Swipe3Left : TouchSlot_Swipe3Right;
    return delta.y() < 0 ? TouchSlot_Swipe3Up : TouchSlot_Swipe3Down;
  }
  if (n == 4) {
    if (horiz)
      return delta.x() < 0 ? TouchSlot_Swipe4Left : TouchSlot_Swipe4Right;
    return delta.y() < 0 ? TouchSlot_Swipe4Up : TouchSlot_Swipe4Down;
  }
  if (n == 5) {
    if (horiz)
      return delta.x() < 0 ? TouchSlot_Swipe5Left : TouchSlot_Swipe5Right;
    return delta.y() < 0 ? TouchSlot_Swipe5Up : TouchSlot_Swipe5Down;
  }
  return TouchSlot_None;
}

TouchSlot pinchSlot(int n, bool pinchIn) {
  if (n == 3) return pinchIn ? TouchSlot_Pinch3In : TouchSlot_Pinch3Out;
  if (n == 4) return pinchIn ? TouchSlot_Pinch4In : TouchSlot_Pinch4Out;
  return TouchSlot_None;
}

TouchSlot edgeSlot(int edge) {
  switch (edge) {
  case 1:
    return TouchSlot_Edge3Left;
  case 2:
    return TouchSlot_Edge3Right;
  case 3:
    return TouchSlot_Edge3Top;
  case 4:
    return TouchSlot_Edge3Bottom;
  default:
    return TouchSlot_None;
  }
}

}  // namespace

TouchGestureRecognizer::TouchGestureRecognizer(QObject *parent)
    : QObject(parent) {
  m_holdTimer.setSingleShot(true);
  m_holdTimer.setTimerType(Qt::PreciseTimer);
  m_tapTimer.setSingleShot(true);
  connect(&m_holdTimer, &QTimer::timeout, this,
          &TouchGestureRecognizer::onHoldTimeout);
  connect(&m_tapTimer, &QTimer::timeout, this,
          &TouchGestureRecognizer::onTapTimeout);
}

void TouchGestureRecognizer::emitSlot(TouchSlot slot, bool repeat) {
  if (slot == TouchSlot_None || touchSlotIsReserved(slot)) return;
  const TouchSlotFamily family = touchSlotFamily(slot);
  if (!TouchPresetManager::instance()->isFamilyRecognitionEnabled(family))
    return;
  m_lastSlotCentroid = m_session.lastCentroid;
  emit slotTriggered((int)slot, repeat);
}

void TouchGestureRecognizer::begin(const QTouchEvent *e, bool tabletActive,
                                   const QSize &widgetSize) {
  if (!e) return;
  QList<QTouchEvent::TouchPoint> pts = livePoints(e);
  if (pts.isEmpty()) pts = e->touchPoints();
  if (pts.isEmpty()) return;

  // Extra fingers often arrive as another TouchBegin. Keep the session.
  if (m_session.active) {
    update(e, m_session.reservedBusy);
    return;
  }

  m_tapTimer.stop();
  m_pendingTap = TouchSlot_None;

  m_session               = Session();
  m_session.active        = true;
  m_session.tablet        = tabletActive;
  m_session.startCount    = clampCount(pts.size());
  m_session.maxCount      = m_session.startCount;
  m_session.liveCount     = m_session.startCount;
  m_session.startCentroid = centroidOf(pts);
  m_session.lastCentroid  = m_session.startCentroid;
  m_session.swipeAnchor   = m_session.startCentroid;
  m_session.startSpan     = spanOf(pts);
  m_session.startMs       = QDateTime::currentMSecsSinceEpoch();
  m_session.widgetSize    = widgetSize;
  if (m_session.startCount >= 3)
    m_session.edge = nearestEdge(m_session.startCentroid, widgetSize);

  m_holdTimer.stop();
  m_holdTimer.start(kHoldMs);
  if (pts.size() >= 2) storeTwoFingerStarts(pts);
}

void TouchGestureRecognizer::storeTwoFingerStarts(
    const QList<QTouchEvent::TouchPoint> &pts) {
  if (pts.size() < 2) {
    m_session.twoFingerStartsValid = false;
    return;
  }
  m_session.fingerStart0         = pts[0].pos();
  m_session.fingerStart1         = pts[1].pos();
  m_session.twoFingerStartsValid = true;
}

bool TouchGestureRecognizer::twoFingerHorizontalParallel(
    const QList<QTouchEvent::TouchPoint> &pts) const {
  if (!m_session.twoFingerStartsValid || pts.size() < 2) return false;
  const QPointF d0 = pts[0].pos() - m_session.fingerStart0;
  const QPointF d1 = pts[1].pos() - m_session.fingerStart1;
  if (d0.manhattanLength() < 24 && d1.manhattanLength() < 24) return false;
  if (std::abs(d0.x()) < std::abs(d0.y()) * 1.25) return false;
  if (std::abs(d1.x()) < std::abs(d1.y()) * 1.25) return false;
  if (d0.x() * d1.x() <= 0) return false;
  const double len0 = QLineF(QPointF(), d0).length();
  const double len1 = QLineF(QPointF(), d1).length();
  if (len0 < 1.0 || len1 < 1.0) return false;
  const double dot = (d0.x() * d1.x() + d0.y() * d1.y()) / (len0 * len1);
  return dot > 0.78;
}

void TouchGestureRecognizer::update(const QTouchEvent *e, bool reservedBusy) {
  if (!e || !m_session.active) return;
  const QList<QTouchEvent::TouchPoint> pts = livePoints(e);
  if (pts.isEmpty()) return;

  const int n            = clampCount(pts.size());
  m_session.reservedBusy = reservedBusy;

  // Fingers landing one after another jump the centroid. Restart from here.
  if (n > m_session.liveCount) {
    m_session.liveCount     = n;
    m_session.maxCount      = std::max(m_session.maxCount, n);
    m_session.startCentroid = centroidOf(pts);
    m_session.lastCentroid  = m_session.startCentroid;
    m_session.swipeAnchor   = m_session.startCentroid;
    m_session.startSpan     = spanOf(pts);
    m_session.startMs       = QDateTime::currentMSecsSinceEpoch();
    if (n >= 3)
      m_session.edge =
          nearestEdge(m_session.startCentroid, m_session.widgetSize);
    m_holdTimer.stop();
    m_holdTimer.start(kHoldMs);
    if (n >= 2) storeTwoFingerStarts(pts);
    return;
  }

  m_session.liveCount = n;
  m_session.maxCount  = std::max(m_session.maxCount, n);

  // Ignore centroid drift while fingers are lifting.
  if (n < m_session.maxCount) return;

  const QPointF c        = centroidOf(pts);
  const double span      = spanOf(pts);
  m_session.lastCentroid = c;

  const double moved = QLineF(m_session.startCentroid, c).length();
  if (moved > kTapSlop) m_holdTimer.stop();

  considerSwipeOrPinch(c, span, pts);
}

void TouchGestureRecognizer::considerSwipeOrPinch(
    const QPointF &centroid, double span,
    const QList<QTouchEvent::TouchPoint> &pts) {
  if (m_session.tablet || m_session.reservedBusy) return;
  const int n = m_session.maxCount;

  const QPointF fromStart = centroid - m_session.startCentroid;
  const double moved      = QLineF(QPointF(), fromStart).length();
  const double spanRatio =
      (m_session.startSpan > 8.0) ? (span / m_session.startSpan) : 1.0;
  const double spanDelta = std::abs(spanRatio - 1.0);

  if (n == 2) {
    if (!m_session.pinchFired && spanDelta >= kPinchRatio &&
        spanDelta * m_session.startSpan > moved) {
      m_session.pinchFired = true;
      return;
    }
    if (m_session.pinchFired) return;

    const QPointF fromAnchor = centroid - m_session.swipeAnchor;
    const double segLen      = QLineF(QPointF(), fromAnchor).length();
    const bool horizSeg = std::abs(fromAnchor.x()) >= std::abs(fromAnchor.y());

    if (horizSeg) {
      if (segLen < kHorizSwipePx) return;
      const qint64 elapsed =
          QDateTime::currentMSecsSinceEpoch() - m_session.startMs;
      if (elapsed < kTwoFingerHorizArmMs) return;
      if (spanDelta >= kHorizMaxSpanDelta) return;
      if (!twoFingerHorizontalParallel(pts)) return;
    } else if (segLen < kSwipePx) {
      return;
    }

    emitSlot(swipeSlot(2, fromAnchor), true);
    m_session.swipeAnchor = centroid;
    m_session.swipeArmed  = true;
    return;
  }

  if (n < 3) return;

  if (!m_session.pinchFired && spanDelta >= kPinchRatio &&
      spanDelta * m_session.startSpan > moved) {
    m_session.pinchFired = true;
    emitSlot(pinchSlot(n, spanRatio < 1.0), false);
    return;
  }

  if (m_session.pinchFired) return;

  if (m_session.edge && n == 3 && moved >= kSwipePx) {
    const bool inward = (m_session.edge == 1 && fromStart.x() > 0) ||
                        (m_session.edge == 2 && fromStart.x() < 0) ||
                        (m_session.edge == 3 && fromStart.y() > 0) ||
                        (m_session.edge == 4 && fromStart.y() < 0);
    if (inward && !m_session.swipeArmed) {
      m_session.swipeArmed = true;
      emitSlot(edgeSlot(m_session.edge), false);
      return;
    }
  }

  const QPointF fromAnchor = centroid - m_session.swipeAnchor;
  if (QLineF(QPointF(), fromAnchor).length() >= kSwipePx) {
    emitSlot(swipeSlot(n, fromAnchor), true);
    m_session.swipeAnchor = centroid;
    m_session.swipeArmed  = true;
  }
}

void TouchGestureRecognizer::end(const QTouchEvent *) {
  m_holdTimer.stop();
  if (!m_session.active) return;
  classifyRelease();
  m_session.active = false;
}

void TouchGestureRecognizer::cancel() {
  m_holdTimer.stop();
  m_tapTimer.stop();
  m_pendingTap     = TouchSlot_None;
  m_session.active = false;
}

void TouchGestureRecognizer::classifyRelease() {
  if (m_session.holdFired || m_session.swipeArmed || m_session.pinchFired)
    return;
  if (m_session.reservedBusy) return;

  const double moved =
      QLineF(m_session.startCentroid, m_session.lastCentroid).length();
  if (moved > kTapSlop) return;

  // Stylus contact: hold only. Hover + two fingers is still a tap.
  if (m_session.tablet && m_session.maxCount < 2) return;

  const int n         = m_session.maxCount;
  const TouchSlot tap = tapSlot(n);
  const TouchSlot dbl = doubleTapSlot(n);
  if (tap == TouchSlot_None) return;

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qint64 gap = now - m_lastTapMs;
  const bool canDouble =
      dbl != TouchSlot_None &&
      TouchPresetManager::instance()->hasCommand(dbl) && m_lastTap == tap &&
      gap >= kDoubleTapMinMs && gap <= kDoubleTapMaxMs &&
      QLineF(m_lastTapPos, m_session.startCentroid).length() < kDoubleTapSlop;

  if (canDouble) {
    m_tapTimer.stop();
    m_pendingTap = TouchSlot_None;
    m_lastTap    = TouchSlot_None;
    emitSlot(dbl, false);
    return;
  }

  const bool deferTap =
      Preferences::instance()->isTouchDeferTapForDoubleTapEnabled() &&
      dbl != TouchSlot_None && TouchPresetManager::instance()->hasCommand(dbl);

  m_lastTap    = tap;
  m_lastTapMs  = now;
  m_lastTapPos = m_session.startCentroid;

  if (deferTap) {
    m_pendingTap = tap;
    m_tapTimer.start(kDoubleTapMaxMs);
    return;
  }

  emitSlot(tap, false);
}

void TouchGestureRecognizer::onTapTimeout() {
  if (m_pendingTap == TouchSlot_None) return;
  const TouchSlot tap = m_pendingTap;
  m_pendingTap        = TouchSlot_None;
  emitSlot(tap, false);
}

void TouchGestureRecognizer::onHoldTimeout() {
  if (!m_session.active || m_session.holdFired || m_session.swipeArmed ||
      m_session.pinchFired)
    return;
  const double moved =
      QLineF(m_session.startCentroid, m_session.lastCentroid).length();
  if (moved > kTapSlop) return;

  if (m_session.tablet) {
    const int n = m_session.maxCount;
    if (n == 1)
      emitSlot(TouchSlot_StylusPlus1, false);
    else if (n >= 2)
      emitSlot(TouchSlot_StylusPlus2, false);
    m_session.holdFired = true;
    return;
  }

  if (m_session.reservedBusy) return;
  const int n = m_session.maxCount;
  emitSlot(holdSlot(n), false);
  m_session.holdFired = true;
}
