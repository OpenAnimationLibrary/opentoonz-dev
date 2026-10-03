#pragma once

#ifndef DRAWINGLAYERS_H
#define DRAWINGLAYERS_H

#include <QTreeWidget>
#include <QPersistentModelIndex>

class TApplication;
class TApp;
class QLabel;
class QMenu;
class TXsheet;
class QTimer;

class DrawingLayers final : public QTreeWidget {
  Q_OBJECT

  TApplication *m_app;
  TXsheet *m_xsheet;
  QTimer *m_rebuildTimer;
  QTimer *m_hoverTimer    = nullptr;
  QWidget *m_hoverPreview = nullptr;
  QLabel *m_previewImage;
  QLabel *m_previewCaption;
  QPersistentModelIndex m_hoverIndex;
  QPoint m_hoverPosition;
  bool m_activating = false;

public:
  DrawingLayers(TApplication *app, QWidget *parent = nullptr);
  // The application adapter lives separately from the testable tree widget.
  DrawingLayers(TApp *app, QWidget *parent = nullptr);

signals:
  void exposureActivated(int row, int column, bool makeCurrent);
  void exposureMenuRequested(QMenu *menu, int row, int column);

protected:
  bool viewportEvent(QEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void contextMenuEvent(QContextMenuEvent *event) override;

private:
  void hideHoverPreview();
  void showHoverPreview();
  QModelIndex thumbnailAt(const QPoint &position) const;
  bool activateExposure(QTreeWidgetItem *item, bool makeCurrent);
  void scheduleRebuild();
  void rebuild();
  void refreshCurrent();
  void expandItem(QTreeWidgetItem *item);
  void refreshDrawing(QTreeWidgetItem *levelItem);
  void selectVectorItem(QTreeWidgetItem *item);
  void beginRename(QTreeWidgetItem *item);
  void renameGroup(QTreeWidgetItem *item, int section);
  void activateItem(QTreeWidgetItem *item, int section);
};

#endif  // DRAWINGLAYERS_H
