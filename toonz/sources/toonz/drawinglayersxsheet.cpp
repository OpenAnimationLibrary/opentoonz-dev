#include "drawinglayers.h"

#include "cellselection.h"
#include "keyframeselection.h"
#include "tapp.h"
#include "xsheetviewer.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonzqt/tselectionhandle.h"

#include <QApplication>
#include <QMenu>
#include <QPointer>

namespace {

// Own the fallback for layouts containing Layers but no Xsheet/Timeline. It
// stays hidden and supplies the same native selection and menu implementation.
class LayersXsheetBridge final : public QObject {
  TApp *m_app;
  DrawingLayers *m_layers;
  QPointer<XsheetViewer> m_fallback;

  QList<XsheetViewer *> visibleViewers() const {
    QList<XsheetViewer *> result;
    for (QWidget *widget : QApplication::allWidgets()) {
      auto viewer = qobject_cast<XsheetViewer *>(widget);
      if (viewer && viewer->isVisible() &&
          viewer->getXsheet() == m_app->getCurrentXsheet()->getXsheet())
        result.append(viewer);
    }
    return result;
  }

  XsheetViewer *viewer() {
    auto viewers          = visibleViewers();
    XsheetViewer *current = m_app->getCurrentXsheetViewer();
    if (viewers.contains(current)) return current;
    if (!viewers.isEmpty()) return viewers.front();
    if (!m_fallback) {
      m_fallback = new XsheetViewer(m_layers);
      m_fallback->setObjectName("LayersExposureMenuXsheet");
      m_fallback->hide();
    }
    return m_fallback;
  }

  void selectExposure(int row, int column, bool makeCurrent) {
    XsheetViewer *target = viewer();
    auto viewers         = visibleViewers();
    if (!viewers.contains(target)) viewers.append(target);
    for (XsheetViewer *view : viewers) {
      view->getKeyframeSelection()->selectNone();
      view->getCellSelection()->selectCell(row, column);
      if (view->isVisible()) view->scrollTo(row, column);
      view->updateCells();
      view->updateColumns();
    }
    if (makeCurrent) {
      m_app->setCurrentXsheetViewer(target);
      target->getCellSelection()->makeCurrent();
      m_app->getCurrentSelection()->notifySelectionChanged();
    }
  }

public:
  LayersXsheetBridge(TApp *app, DrawingLayers *layers)
      : QObject(layers), m_app(app), m_layers(layers) {
    connect(layers, &DrawingLayers::exposureActivated, this,
            [this](int row, int column, bool makeCurrent) {
              selectExposure(row, column, makeCurrent);
            });
    connect(layers, &DrawingLayers::exposureMenuRequested, this,
            [this](QMenu *menu, int row, int column) {
              selectExposure(row, column, true);
              XsheetViewer *target = viewer();
              auto area            = target->findChild<XsheetGUI::CellArea *>();
              if (!area) return;
              const TXshCell cell = target->getXsheet()->getCell(row, column);
              area->createCellMenu(*menu, !cell.isEmpty(), cell, row, column);
            });
  }

  ~LayersXsheetBridge() override {
    if (!m_fallback) return;
    if (m_app->getCurrentSelection()->getSelection() ==
        m_fallback->getCellSelection())
      m_app->getCurrentSelection()->setSelection(nullptr);
    if (m_app->getCurrentXsheetViewer() == m_fallback)
      m_app->setCurrentXsheetViewer(nullptr);
  }
};

}  // namespace

DrawingLayers::DrawingLayers(TApp *app, QWidget *parent)
    : DrawingLayers(static_cast<TApplication *>(app), parent) {
  new LayersXsheetBridge(app, this);
}
