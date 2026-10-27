#pragma once

#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>

namespace QtCompat {

inline QPoint mouseEventPosition(const QMouseEvent *event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return event->position().toPoint();
#else
  return event->pos();
#endif
}

inline QPoint wheelEventPosition(const QWheelEvent *event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return event->position().toPoint();
#else
  return event->pos();
#endif
}

inline int wheelEventAngleDeltaY(const QWheelEvent *event) {
  return event->angleDelta().y();
}

inline QImage mirroredImage(const QImage &image) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  return image.flipped(Qt::Vertical);
#else
  return image.mirrored();
#endif
}

}  // namespace QtCompat
