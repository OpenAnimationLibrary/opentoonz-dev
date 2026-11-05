#pragma once

#ifndef HANDWRITINGSURFACE_H
#define HANDWRITINGSURFACE_H

#include <QPolygonF>
#include <QElapsedTimer>
#include <QVector>
#include <QWidget>

class HandwritingSurface final : public QWidget {
  Q_OBJECT

  QVector<QPolygonF> m_strokes;
  QPolygonF m_current;
  QString m_text;
  bool m_tablet  = false;
  bool m_showInk = true, m_showText = true;
  qreal m_inkOpacity = 0.75;
  QElapsedTimer m_tabletClock;

  QPointF bounded(const QPointF &point) const;
  void begin(const QPointF &point);
  void move(const QPointF &point);
  void finish();
  bool acceptMouse(const QMouseEvent *event) const;

public:
  explicit HandwritingSurface(QWidget *parent = nullptr);
  void setContent(const QVector<QPolygonF> &strokes, const QString &text);
  void setText(const QString &text);
  void setInkVisible(bool on);
  void setTextVisible(bool on);
  void setInkOpacity(int opacity);
  bool isWriting() const { return !m_current.isEmpty(); }
  const QVector<QPolygonF> &strokes() const { return m_strokes; }

signals:
  void writingStarted();
  void strokesChanged();

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void tabletEvent(QTabletEvent *) override;
};

#endif
