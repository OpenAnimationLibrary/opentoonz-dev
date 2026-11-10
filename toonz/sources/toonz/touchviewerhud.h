#pragma once

#ifndef TOUCH_VIEWER_HUD_H
#define TOUCH_VIEWER_HUD_H

#include <QPixmap>
#include <QWidget>

class QTimer;
class SceneViewer;

class TouchViewerHud final : public QWidget {
  Q_OBJECT

  SceneViewer *m_viewer = nullptr;
  QTimer *m_fadeTimer   = nullptr;
  int m_holdMs          = 0;
  qreal m_opacity       = 0.0;

  QString m_sizeText;
  QString m_hardnessText;
  int m_prevFrame     = 0;
  int m_currentFrame  = 0;
  int m_nextFrame     = 0;
  bool m_showSize     = false;
  bool m_showHardness = false;
  bool m_showFrame    = false;
  QPixmap m_frost;

public:
  explicit TouchViewerHud(SceneViewer *viewer);

  void clearForInactiveContext();
  void positionAtTop();
  void pulseFrameContext(int prevFrame, int currentFrame, int nextFrame);
  void pulseBrushSize(const QString &text);
  void pulseBrushHardness(const QString &text);

protected:
  void showEvent(QShowEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  void bump();
  void relayout();
  void captureFrost();
  bool hasContent() const;

private slots:
  void onFadeTick();
};

bool touchViewerHudHostActive(const QWidget *viewer);

QString touchViewerHudFormatBrushSize();
QString touchViewerHudFormatBrushHardness();

#endif
