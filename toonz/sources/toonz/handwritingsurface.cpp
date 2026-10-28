#include "handwritingsurface.h"

#include <QMouseEvent>
#include <QPainter>
#include <QTabletEvent>

HandwritingSurface::HandwritingSurface(QWidget *parent) : QWidget(parent) {
  setFixedSize(1600, 240);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::CrossCursor);
}

void HandwritingSurface::setContent(const QVector<QPolygonF> &strokes,
                                    const QString &text) {
  m_strokes = strokes;
  m_current.clear();
  m_tablet = false;
  m_text   = text;
  update();
}

void HandwritingSurface::setText(const QString &text) {
  m_text = text;
  update();
}

void HandwritingSurface::setInkVisible(bool on) {
  m_showInk = on;
  update();
}

void HandwritingSurface::setTextVisible(bool on) {
  m_showText = on;
  update();
}

void HandwritingSurface::setInkOpacity(int opacity) {
  m_inkOpacity = qBound(0, opacity, 100) / 100.0;
  update();
}

QPointF HandwritingSurface::bounded(const QPointF &point) const {
  return QPointF(qBound(0.0, point.x(), double(width())),
                 qBound(0.0, point.y(), double(height())));
}

void HandwritingSurface::begin(const QPointF &point) {
  setFocus();
  m_current.clear();
  m_current.append(bounded(point));
  emit writingStarted();
  update();
}

void HandwritingSurface::move(const QPointF &point) {
  if (m_current.isEmpty()) return;
  const QPointF next = bounded(point);
  if ((next - m_current.last()).manhattanLength() >= 0.6) {
    m_current.append(next);
    update();
  }
}

void HandwritingSurface::finish() {
  if (m_current.isEmpty()) return;
  m_strokes.append(m_current);
  m_current.clear();
  emit strokesChanged();
  update();
}

void HandwritingSurface::mousePressEvent(QMouseEvent *event) {
  if (acceptMouse(event) && event->button() == Qt::LeftButton)
    begin(event->localPos());
  event->accept();
}

void HandwritingSurface::mouseMoveEvent(QMouseEvent *event) {
  if (acceptMouse(event)) move(event->localPos());
  event->accept();
}

void HandwritingSurface::mouseReleaseEvent(QMouseEvent *event) {
  if (acceptMouse(event) && event->button() == Qt::LeftButton) {
    move(event->localPos());
    finish();
  }
  event->accept();
}

bool HandwritingSurface::acceptMouse(const QMouseEvent *event) const {
  // Some drivers deliver synthesized mouse events after TabletRelease, too.
  // Mouse-only tablets still work when there has been no tablet event.
  return !m_tablet &&
         !(event->source() != Qt::MouseEventNotSynthesized &&
           m_tabletClock.isValid() && m_tabletClock.elapsed() < 250);
}

void HandwritingSurface::tabletEvent(QTabletEvent *event) {
  m_tabletClock.restart();
  if (event->type() == QEvent::TabletPress) {
    m_tablet = true;
    begin(event->posF());
  } else if (event->type() == QEvent::TabletMove) {
    move(event->posF());
  } else if (event->type() == QEvent::TabletRelease) {
    move(event->posF());
    finish();
    m_tablet = false;
  }
  event->accept();  // Suppress synthesized duplicate mouse strokes.
}

void HandwritingSurface::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), palette().base());
  QColor guide = palette().text().color();
  guide.setAlpha(35);
  p.setPen(guide);
  for (int y = 55; y < height(); y += 60) p.drawLine(20, y, width() - 20, y);
  if (m_showText) {
    QFont font = p.font();
    font.setPixelSize(30);
    p.setFont(font);
    QColor text = palette().text().color();
    text.setAlpha(130);
    p.setPen(text);
    p.drawText(QRect(20, 120, width() - 40, height() - 120),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_text);
  }
  if (m_showInk) {
    p.setOpacity(m_inkOpacity);
    p.setPen(QPen(palette().text().color(), 3, Qt::SolidLine, Qt::RoundCap,
                  Qt::RoundJoin));
    auto draw = [&p](const QPolygonF &stroke) {
      if (stroke.size() == 1)
        p.drawPoint(stroke.first());
      else
        p.drawPolyline(stroke);
    };
    for (const QPolygonF &stroke : m_strokes) draw(stroke);
    draw(m_current);
  }
}
