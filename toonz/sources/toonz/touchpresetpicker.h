#pragma once

#ifndef TOUCH_PRESET_PICKER_H
#define TOUCH_PRESET_PICKER_H

#include <QEvent>
#include <QStringList>
#include <QWidget>

class QPainter;
class QPushButton;
class SceneViewer;

class TouchPresetPickerPanel final : public QWidget {
  Q_OBJECT

  SceneViewer *m_viewer;
  QPointF m_anchor;
  int m_pageStart = 0;
  int m_wheelDiam     = 88;
  int m_wheelRing     = 62;
  int m_wheelDead     = 24;
  int m_stripW        = 132;
  int m_stripH        = 34;
  int m_edgeCircle    = 26;
  qint64 m_blockInputUntilMs = 0;
  QPoint m_touchStartLocal;
  bool m_touchActive          = false;
  bool m_touchPagingCandidate = false;
  QRect m_bodyRect;

  void applyPickerMetricsFromSettings();
  void clearPickerContent();
  void applyPageDelta(int delta);
  bool tryPageBySwipe(const QPoint &local);
  void finishPickerLayout();
  bool pickerInputAllowed() const;
  QPoint anchorTopLeft() const;

public:
  explicit TouchPresetPickerPanel(SceneViewer *viewer);

  bool isOpen() const { return isVisible(); }
  void openAt(const QPointF &anchorWidgetPos);
  void closePicker();
  void refreshFromSettings(bool rebuildLayout = false);
  void refreshAppearanceLive();
  void syncToActivePreset();
  void toggleAt(const QPointF &anchorWidgetPos);
  bool handleViewerTouch(const QPoint &viewerPos, QEvent::Type type);

public slots:
  void onPresetClicked();

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  bool event(QEvent *event) override;

private:
  void rebuild();
  void positionPanel();
  void applyWheelGeometry(const QStringList &pageNames, bool paging,
                          bool canPrev, bool canNext);
  void applyLinearGeometry(bool vertical, const QStringList &pageNames,
                           bool paging, bool canPrev, bool canNext);
  QPushButton *slotAt(int index) const;
  QPushButton *edgeButton(bool prev) const;
  QPushButton *buttonAt(const QPoint &local) const;
  bool relabelSlots(const QStringList &pageNames);
  bool showCurrentPage();
  QPoint clampTopLeft(QPoint topLeft) const;
  int visibleSlotCount() const;
  int wheelPageSize() const;
  int pageSizeForCurrentLayout() const;
  QStringList pageDisplaySlots(const QStringList &pinned, int pageStart) const;
  void alignPageToActive(const QStringList &names, const QString &active);
  bool isGhostLook() const;
  int ghostPieSlotIndex(const QPoint &local) const;
  void paintGhostLook(QPainter &p);

private slots:
  void onPageEdgeClicked();
};

#endif
