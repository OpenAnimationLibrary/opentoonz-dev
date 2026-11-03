#pragma once

#include "viewerscrubdrag.h"

#include <QDialog>
#include <QObject>

class SceneViewer;

// One session per viewer; both the canvas gesture and the floating strip use
// it.
class ViewerScrubController final : public QObject {
  Q_OBJECT

  SceneViewer *m_viewer;
  ViewerScrubDrag m_drag;
  std::vector<int> m_drawingStops;
  bool m_drawingMode = false;
  bool m_active      = false;
  bool m_strip       = false;
  int m_origin       = 0;
  int m_first        = 0;
  int m_last         = 0;

  void finish(bool restore);

public:
  explicit ViewerScrubController(SceneViewer *viewer);
  bool begin(double x, bool strip);
  void move(double x);
  void end();
  void cancel();
  void invalidate();
  bool isActive() const { return m_active; }

signals:
  // FlipConsole uses one-based frame indices, including level editing mode.
  void frameRequested(int frame);
};

class ViewerScrubPopup final : public QDialog {
  Q_OBJECT

public:
  explicit ViewerScrubPopup(SceneViewer *viewer);
};
