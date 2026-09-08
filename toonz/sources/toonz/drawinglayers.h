#pragma once

#ifndef DRAWINGLAYERS_H
#define DRAWINGLAYERS_H

#include <QTreeWidget>

class TApplication;
class TXsheet;
class QTimer;

class DrawingLayers final : public QTreeWidget {
  Q_OBJECT

  TApplication *m_app;
  TXsheet *m_xsheet;
  QTimer *m_rebuildTimer;

public:
  DrawingLayers(TApplication *app, QWidget *parent = nullptr);

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  void scheduleRebuild();
  void rebuild();
  void refreshCurrent();
  void expandItem(QTreeWidgetItem *item);
  void refreshDrawing(QTreeWidgetItem *levelItem);
  void selectVectorItem(QTreeWidgetItem *item);
  void activateItem(QTreeWidgetItem *item, int section);
};

#endif  // DRAWINGLAYERS_H
