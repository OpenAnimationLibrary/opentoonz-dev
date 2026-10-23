

#include "cellselection.h"

// Tnz6 includes
#include "celldata.h"
#include "keyframeselection.h"
#include "keyframedata.h"
#include "drawingdata.h"
#include "cellkeyframedata.h"
#include "filmstripcommand.h"
#include "menubarcommandids.h"
#include "timestretchpopup.h"
#include "tapp.h"
#include "xsheetviewer.h"
#include "levelcommand.h"
#include "columncommand.h"
#include "canvassizepopup.h"

// TnzTools includes
#include "tools/toolutils.h"
#include "tools/toolhandle.h"
#include "tools/toolcommandids.h"

// TnzQt includes
#include "toonzqt/strokesdata.h"
#include "toonzqt/rasterimagedata.h"
#include "toonzqt/icongenerator.h"
#include "toonzqt/tselectionhandle.h"
#include "toonzqt/menubarcommand.h"
#include "toonzqt/dvdialog.h"
#include "toonzqt/gutil.h"
#include "historytypes.h"

// TnzLib includes
#include "toonz/palettecontroller.h"
#include "toonz/preferences.h"
#include "toonz/tpalettehandle.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tscenehandle.h"
#include "toonz/tframehandle.h"
#include "toonz/tobjecthandle.h"
#include "toonz/txsheet.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/txshchildlevel.h"
#include "toonz/toonzscene.h"
#include "toonz/txshleveltypes.h"
#include "toonz/tcamera.h"
#include "toonz/levelproperties.h"
#include "toonz/toonzimageutils.h"
#include "toonz/trasterimageutils.h"
#include "toonz/levelset.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/stage.h"
#include "vectorizerpopup.h"
#include "tools/rasterselection.h"
#include "tools/strokeselection.h"
#include "toonz/sceneproperties.h"
#include "toutputproperties.h"
#include "toonz/tstageobjectcmd.h"

// TnzCore includes
#include "timagecache.h"
#include "tundo.h"
#include "tropcm.h"
#include "tvectorimage.h"
#include "tsystem.h"
#include "filebrowsermodel.h"

// Qt includes
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

// C++ includes
#include <limits>
#include <memory>
#include <cmath>

//=============================================================================
namespace {
//-----------------------------------------------------------------------------

TXshCell drawingPasteReference(TXsheet *xsh, int row, int col) {
  TXshCell cell = xsh->getCell(row, col);
  if (!cell.isEmpty()) return cell;
  int first, last;
  xsh->getCellRange(col, first, last);
  if (first > last) return TXshCell();
  for (int r = std::min(row - 1, last); r >= first; --r) {
    cell = xsh->getCell(r, col);
    if (!cell.isEmpty()) return cell;
  }
  for (int r = std::max(row + 1, first); r <= last; ++r) {
    cell = xsh->getCell(r, col);
    if (!cell.isEmpty()) return cell;
  }
  return TXshCell();
}

//-----------------------------------------------------------------------------

bool hasDrawingSelection(const QMimeData *mime);

// Copy cells to clipboard without undo
void copyCellsWithoutUndo(int r0, int c0, int r1, int c1) {
  int colCount = c1 - c0 + 1;
  int rowCount = r1 - r0 + 1;
  if (colCount <= 0 || rowCount <= 0) return;
  TXsheet *xsh    = TApp::instance()->getCurrentXsheet()->getXsheet();
  TCellData *data = new TCellData();
  data->setCells(xsh, r0, c0, r1, c1);
  QApplication::clipboard()->setMimeData(data, QClipboard::Clipboard);
}

//-----------------------------------------------------------------------------

// Paste cells without undo
bool pasteCellsWithoutUndo(const TCellData *cellData, int &r0, int &c0, int &r1,
                           int &c1, bool insert = true,
                           bool doZeraryClone = true) {
  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  if (!cellData) return false;
  if (r0 < 0 || c0 < 0) return false;

  bool ret =
      cellData->getCells(xsh, r0, c0, r1, c1, insert, doZeraryClone, false);
  if (!ret) return false;

  return true;
}

//-----------------------------------------------------------------------------

// Delete cells without undo
void deleteCellsWithoutUndo(int &r0, int &c0, int &r1, int &c1, bool doShift) {
  try {
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    int c;
    for (c = c0; c <= c1; c++) {
      if (xsh->isColumnEmpty(c)) continue;

      if (doShift)
        xsh->removeCells(r0, c, r1 - r0 + 1);
      else
        xsh->clearCells(r0, c, r1 - r0 + 1);
      // when the column becomes empty after deletion,
      // ColumnCmd::DeleteColumn() will take care of column related operations
      // like disconnecting from fx nodes etc.
      /*
      TXshColumn* column = xsh->getColumn(c);
      if (column && column->isEmpty()) {
        column->resetColumnProperties();
        TFx *fx = column->getFx();
        if (fx) {
          int i;
          for (i = fx->getOutputConnectionCount() - 1; i >= 0; i--) {
            TFxPort *port = fx->getOutputConnection(i);
            port->setFx(0);
          }
        }
      }
      */
    }
  } catch (...) {
    DVGui::error(QObject::tr("It is not possible to delete the selection."));
  }
}

//-----------------------------------------------------------------------------

// Cut cells without undo
void cutCellsWithoutUndo(int &r0, int &c0, int &r1, int &c1) {
  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  int c;
  for (c = c0; c <= c1; c++) {
    xsh->removeCells(r0, c, r1 - r0 + 1);
    // when the column becomes empty after deletion,
    // ColumnCmd::DeleteColumn() will take care of column related operations
    // like disconnecting from fx nodes etc.
    /*
    TXshColumn* column = xsh->getColumn(c);
    if (column && column->isEmpty()) {
      column->resetColumnProperties();
      TFx* fx = column->getFx();
      if (!fx) continue;
      int i;
      for (i = fx->getOutputConnectionCount() - 1; i >= 0; i--) {
        TFxPort* port = fx->getOutputConnection(i);
        port->setFx(0);
      }
    }
    */
  }

  // If the current selection is TCellSelection, clear the selection.
  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection) cellSelection->selectNone();
}

//-----------------------------------------------------------------------------
// Check if the operation may remove expression reference as column becomes
// empty and deleted after the operation. Return true to continue the operation.

bool checkColumnRemoval(const int r0, const int c0, const int r1, const int c1,
                        std::set<int> &removedColIds) {
  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  // std::set<int> colIndicesToBeRemoved;
  for (int c = c0; c <= c1; c++) {
    TXshColumnP column = xsh->getColumn(c);
    if (!column || column->isEmpty() || column->isLocked()) continue;
    int tmp_r0, tmp_r1;
    xsh->getCellRange(c, tmp_r0, tmp_r1);
    if (r0 <= tmp_r0 && r1 >= tmp_r1) removedColIds.insert(c);
  }

  if (removedColIds.empty() ||
      !Preferences::instance()->isModifyExpressionOnMovingReferencesEnabled())
    return true;
  std::set<TFx *> dummy_fxs;
  return ColumnCmd::checkExpressionReferences(removedColIds, dummy_fxs, true);
}

//=============================================================================
//  PasteCellsUndo
//-----------------------------------------------------------------------------

class PasteCellsUndo final : public TUndo {
  TCellSelection *m_oldSelection;
  TCellSelection *m_newSelection;
  TCellData *m_data;
  std::vector<bool> m_areOldColumnsEmpty;
  bool m_containsSoundColumn;
  std::map<int, std::string> m_oldEmptyColumnNames;

public:
  PasteCellsUndo(int r0, int c0, int r1, int c1, int oldR0, int oldC0,
                 int oldR1, int oldC1, const std::vector<bool> &areColumnsEmpty,
                 bool containsSoundColumn,
                 const std::map<int, std::string> &oldEmptyColumnNames =
                     std::map<int, std::string>())
      : m_areOldColumnsEmpty(areColumnsEmpty)
      , m_containsSoundColumn(containsSoundColumn)
      , m_oldEmptyColumnNames(oldEmptyColumnNames) {
    m_data       = new TCellData();
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    m_data->setCells(xsh, r0, c0, r1, c1);
    m_newSelection = new TCellSelection();
    m_newSelection->selectCells(r0, c0, r1, c1);
    m_oldSelection = new TCellSelection();
    m_oldSelection->selectCells(oldR0, oldC0, oldR1, oldC1);
  }

  ~PasteCellsUndo() {
    delete m_newSelection;
    delete m_oldSelection;
    delete m_data;
  }

  void undo() const override {
    int r0, c0, r1, c1;
    m_newSelection->getSelectedCells(r0, c0, r1, c1);

    int oldR0, oldC0, oldR1, oldC1;
    m_oldSelection->getSelectedCells(oldR0, oldC0, oldR1, oldC1);

    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    for (const auto &item : m_oldEmptyColumnNames) {
      xsh->getStageObject(TStageObjectId::ColumnId(item.first))
          ->setName(item.second);
    }

    // Cut cells that are in newSelection
    cutCellsWithoutUndo(r0, c0, r1, c1);
    // If the columns were empty, reset them (this is necessary for special
    // columns, sound columns or palette)
    assert(c1 - c0 + 1 == (int)m_areOldColumnsEmpty.size());

    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    if (m_containsSoundColumn)
      TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
  }

  void redo() const override {
    int r0, c0, r1, c1;
    m_newSelection->getSelectedCells(r0, c0, c1, r1);
    // Cut cells that are in newSelection
    pasteCellsWithoutUndo(m_data, r0, c0, c1, r1, true, false);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    if (m_containsSoundColumn)
      TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
  }

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override { return QObject::tr("Paste Cells"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//=============================================================================
//  DeleteCellsUndo
//  Recovering the column information (such as reconnecting nodes) when
//  undoing deletion of entire column will be done by DeleteColumnsUndo which
//  will be called in the same undo block. So here we only need to recover the
//  cell arrangement.
//-----------------------------------------------------------------------------

class DeleteCellsUndo final : public TUndo {
  TCellSelection *m_selection;
  TCellData *m_data;

  bool m_doShift;  // whether clear cell or remove and shift cells up
  bool m_containsSoundColumn;

public:
  DeleteCellsUndo(TCellSelection *selection, TCellData *data, bool doShift)
      : m_data(data), m_doShift(doShift), m_containsSoundColumn(false) {
    int r0, c0, r1, c1;
    selection->getSelectedCells(r0, c0, r1, c1);
    if (c0 < 0) c0 = 0;  // Ignore camera column
    m_selection = new TCellSelection();
    m_selection->selectCells(r0, c0, r1, c1);

    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    for (int c = c0; c <= c1; c++) {
      TXshColumn *column = xsh->getColumn(c);
      if (column && column->getSoundColumn()) {
        m_containsSoundColumn = true;
        break;
      }
    }
  }

  ~DeleteCellsUndo() { delete m_selection; }

  void undo() const override {
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();

    int r0, c0, r1, c1;
    m_selection->getSelectedCells(r0, c0, r1, c1);

    // insert cells if the delete operation had shifted cells up
    pasteCellsWithoutUndo(m_data, r0, c0, r1, c1, m_doShift, false);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    if (m_containsSoundColumn)
      TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
  }

  void redo() const override {
    int r0, c0, r1, c1;
    m_selection->getSelectedCells(r0, c0, r1, c1);
    deleteCellsWithoutUndo(r0, c0, r1, c1, m_doShift);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    if (m_containsSoundColumn)
      TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
  }

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override { return QObject::tr("Delete Cells"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
  bool containsSoundColumn() { return m_containsSoundColumn; }
};

//=============================================================================
//  CutCellsUndo
//  Just like DeleteCellsUndo, recovering the column information (such as
//  reconnecting nodes) when undoing deletion of entire column will be done
//  by DeleteColumnsUndo which will be called in the same undo block.
//-----------------------------------------------------------------------------

class CutCellsUndo final : public TUndo {
  TCellSelection *m_selection;
  TCellData *m_data;

  bool m_containsSoundColumn;

public:
  CutCellsUndo(TCellSelection *selection)
      : m_data(), m_containsSoundColumn(false) {
    int r0, c0, r1, c1;
    selection->getSelectedCells(r0, c0, r1, c1);
    if (c0 < 0) c0 = 0;  // Ignore camera column
    m_selection = new TCellSelection();
    m_selection->selectCells(r0, c0, r1, c1);
  }

  void setCurrentData(int r0, int c0, int r1, int c1) {
    m_data       = new TCellData();
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    m_data->setCells(xsh, r0, c0, r1, c1);

    for (int c = c0; c <= c1; c++) {
      TXshColumn *column = xsh->getColumn(c);
      if (column && column->getSoundColumn()) {
        m_containsSoundColumn = true;
        break;
      }
    }
  }

  ~CutCellsUndo() {
    delete m_selection;
    delete m_data;
  }

  void undo() const override {
    int r0, c0, r1, c1;
    m_selection->getSelectedCells(r0, c0, r1, c1);

    pasteCellsWithoutUndo(m_data, r0, c0, r1, c1, true);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    if (m_containsSoundColumn)
      TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
  }

  void redo() const override {
    QClipboard *clipboard  = QApplication::clipboard();
    QMimeData *currentData = cloneData(clipboard->mimeData());

    int r0, c0, r1, c1;
    m_selection->getSelectedCells(r0, c0, r1, c1);
    cutCellsWithoutUndo(r0, c0, r1, c1);

    clipboard->setMimeData(currentData, QClipboard::Clipboard);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    if (m_containsSoundColumn)
      TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
  }

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override { return QObject::tr("Cut Cells"); }
  int getHistoryType() override { return HistoryType::Xsheet; }

  bool containsSoundColumn() { return m_containsSoundColumn; }
};

//=============================================================================
//  InsertUndo
//-----------------------------------------------------------------------------

class InsertUndo final : public TUndo {
  TCellSelection::Range m_range;
  bool m_containsSoundColumn;

public:
  InsertUndo(const TCellSelection::Range &range) : m_range(range) {
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    for (int c = m_range.m_c0; c <= m_range.m_c1; c++) {
      TXshColumn *column = xsh->getColumn(c);
      if (column && column->getSoundColumn()) {
        m_containsSoundColumn = true;
        break;
      }
    }
  }

  void undo() const override {
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    int rowCount = m_range.getRowCount();
    int c;
    for (c = m_range.m_c0; c <= m_range.m_c1; c++)
      xsh->removeCells(m_range.m_r0, c, rowCount);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    if (m_containsSoundColumn)
      TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
  }
  void redo() const override {
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    int rowCount = m_range.getRowCount();
    int c;
    for (c = m_range.m_c0; c <= m_range.m_c1; c++)
      xsh->insertCells(m_range.m_r0, c, rowCount);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    if (m_containsSoundColumn)
      TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
  }
  int getSize() const override { return sizeof(*this); }
  QString getHistoryString() override { return QObject::tr("Insert Cells"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//=============================================================================
//  RenumberUndo
//-----------------------------------------------------------------------------

class RenumberUndo {
public:
  class RedoNotifier;
  class UndoNotifier;
};

class RenumberUndo::RedoNotifier final : public TUndo {
  void undo() const override {}
  void redo() const override {
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    TApp::instance()->getCurrentScene()->setDirtyFlag(true);
  }

  int getSize() const override { return sizeof(*this); }
};

class RenumberUndo::UndoNotifier final : public TUndo {
  void redo() const override {}
  void undo() const override {
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    TApp::instance()->getCurrentScene()->setDirtyFlag(true);
  }

  int getSize() const override { return sizeof(*this); }
};

//=============================================================================

// Paste strokes in cell without undo
bool pasteStrokesInCellWithoutUndo(
    int row, int col, const StrokesData *strokesData,
    std::vector<int> &indices)  // in indices the added strokes
{
  TApp *app     = TApp::instance();
  TXsheet *xsh  = app->getCurrentXsheet()->getXsheet();
  TXshCell cell = xsh->getCell(row, col);
  TVectorImageP vi;
  TXshSimpleLevel *sl = 0;
  TFrameId fid(1);
  if (cell.isEmpty()) {
    cell = drawingPasteReference(xsh, row, col);
    sl = cell.getSimpleLevel();
    if (!sl || sl->getType() != PLI_XSHLEVEL) {
      ToonzScene *scene = app->getCurrentScene()->getScene();
      TXshLevel *xl     = scene->createNewLevel(PLI_XSHLEVEL);
      if (!xl) return false;
      sl = xl->getSimpleLevel();
      assert(sl);
      app->getCurrentScene()->notifyCastChange();
    }
    vi = sl->createEmptyFrame();
    assert(vi);
    std::vector<TFrameId> fids;
    sl->getFids(fids);
    if (fids.size() > 0) fid = TFrameId(fids.back().getNumber() + 1);
    sl->setFrame(fid, vi);
    app->getCurrentLevel()->setLevel(sl);
    app->getCurrentLevel()->notifyLevelChange();
    xsh->setCell(row, col, TXshCell(sl, fid));
    app->getCurrentXsheet()->notifyXsheetChanged();
  } else {
    vi = cell.getImage(true);
    sl = cell.getSimpleLevel();
    if (sl->getType() == OVL_XSHLEVEL && sl->getPath().isUneditable())
      return false;
    fid = cell.getFrameId();
    if (!vi) {
      DVGui::error(QObject::tr(
          "It is not possible to paste vectors in the current cell."));
      return false;
    }
  }
  if (vi) {
    std::set<int> indicesSet;
    strokesData->getImage(vi, indicesSet, true);
    for (int index : indicesSet) {
      indices.push_back(index);
    }
    assert(sl);
    app->getPaletteController()
        ->getCurrentLevelPalette()
        ->notifyPaletteChanged();
    IconGenerator::instance()->invalidate(sl, fid);
  }
  return true;
}

//=============================================================================
//  PasteStrokesInCellsUndo
//-----------------------------------------------------------------------------

class PasteStrokesInCellUndo final : public TUndo {
  int m_row, m_col;
  StrokesData *m_strokesData;
  TFrameId m_fid;
  TVectorImageP m_image;
  TPaletteP m_oldPalette;
  TXshSimpleLevelP m_sl;
  bool m_createdFrame;
  bool m_isLevelCreated;
  std::vector<int> m_indices;

public:
  PasteStrokesInCellUndo(int row, int col, const StrokesData *data)
      : m_row(row)
      , m_col(col)
      , m_strokesData(data->clone())
      , m_image()
      , m_oldPalette(0)
      , m_createdFrame(false)
      , m_isLevelCreated(false) {
    TXsheet *xsh         = TApp::instance()->getCurrentXsheet()->getXsheet();
    TXshCell cell        = xsh->getCell(row, col);
    TPaletteP oldPalette = 0;
    if (!cell.getSimpleLevel()) {
      m_createdFrame      = true;
      TXshSimpleLevel *sl =
          drawingPasteReference(xsh, row, col).getSimpleLevel();
      if (!sl || sl->getType() != PLI_XSHLEVEL)
        m_isLevelCreated = true;
      else
        oldPalette = sl->getPalette();
    } else
      oldPalette = cell.getSimpleLevel()->getPalette();
    if (oldPalette) m_oldPalette = oldPalette->clone();
  }

  ~PasteStrokesInCellUndo() { delete m_strokesData; }

  void setIndices(const std::vector<int> &indices) { m_indices = indices; }

  void onAdd() override {
    TXsheet *xsh  = TApp::instance()->getCurrentXsheet()->getXsheet();
    TXshCell cell = xsh->getCell(m_row, m_col);
    m_sl          = cell.getSimpleLevel();
    m_fid         = cell.m_frameId;
    m_image       = cell.getImage(false);
  }

  void undo() const override {
    m_image->removeStrokes(m_indices, true, true);
    if (m_createdFrame) {
      TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
      m_sl->eraseFrame(m_fid);
      xsh->clearCells(m_row, m_col);
      if (m_isLevelCreated) {
        // remove the level
        TLevelSet *levelSet =
            TApp::instance()->getCurrentScene()->getScene()->getLevelSet();
        if (levelSet) {
          levelSet->removeLevel(m_sl.getPointer());
          TApp::instance()->getCurrentScene()->notifyCastChange();
        }
      }
    }
    if (m_oldPalette.getPointer()) {
      m_sl->getPalette()->assign(m_oldPalette->clone());
      TApp::instance()
          ->getPaletteController()
          ->getCurrentLevelPalette()
          ->notifyPaletteChanged();
    }
    IconGenerator::instance()->invalidate(m_sl.getPointer(), m_fid);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }
  void redo() const override {
    if (m_isLevelCreated) {
      TLevelSet *levelSet =
          TApp::instance()->getCurrentScene()->getScene()->getLevelSet();
      if (levelSet) {
        levelSet->insertLevel(m_sl.getPointer());
        TApp::instance()->getCurrentScene()->notifyCastChange();
      }
    }
    if (m_createdFrame) {
      TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
      m_sl->setFrame(m_fid, m_image.getPointer());
      TXshCell cell(m_sl.getPointer(), m_fid);
      xsh->setCell(m_row, m_col, cell);
    }
    std::vector<int> indices = m_indices;
    pasteStrokesInCellWithoutUndo(m_row, m_col, m_strokesData, indices);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }

  int getSize() const override { return sizeof *this; }
  QString getHistoryString() override { return QObject::tr("Paste (Strokes)"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//=============================================================================

// Paste raster image in cell without undo
bool pasteRasterImageInCellWithoutUndo(int row, int col,
                                       const RasterImageData *rasterImageData,
                                       TTileSet **tiles, bool &isLevelCreated) {
  isLevelCreated = false;
  if (!tiles) return false;
  *tiles = nullptr;
  if (!rasterImageData || row < 0 || col < 0) return false;

  TApp *app         = TApp::instance();
  TXsheet *xsh      = app->getCurrentXsheet()->getXsheet();
  ToonzScene *scene = app->getCurrentScene()->getScene();
  if (!xsh || !scene) return false;
  TXshColumn *column = xsh->getColumn(col);
  if (column && column->isLocked()) return false;

  const TXshCell originalCell = xsh->getCell(row, col);
  const bool createFrame      = originalCell.isEmpty();
  const TXshCell referenceCell =
      createFrame ? drawingPasteReference(xsh, row, col) : originalCell;
  TXshSimpleLevelP sl = referenceCell.getSimpleLevel();
  const bool createLevel =
      createFrame && (!sl || (sl->getType() == OVL_XSHLEVEL &&
                              sl->getPath().getFrame() == TFrameId::NO_FRAME));
  const int levelType =
      createLevel ? (dynamic_cast<const ToonzImageData *>(rasterImageData)
                         ? TZP_XSHLEVEL
                     : dynamic_cast<const FullColorImageData *>(rasterImageData)
                         ? OVL_XSHLEVEL
                         : NO_XSHLEVEL)
      : sl        ? sl->getType()
                  : NO_XSHLEVEL;
  if (levelType != TZP_XSHLEVEL && levelType != OVL_XSHLEVEL &&
      levelType != TZI_XSHLEVEL)
    return false;
  if (!createLevel && (sl->isReadOnly() || sl->getPath().isUneditable()))
    return false;

  TFrameId fid = createFrame ? TFrameId(1) : originalCell.getFrameId();
  TImageP originalImage;
  TPaletteP originalPalette;
  TPaletteP paletteBackup;
  bool wasDirty             = false;
  bool wasPaletteDirty      = false;
  bool levelCreated         = false;
  bool frameTouched         = false;
  bool cellTouched          = false;
  bool paletteTouched       = false;
  const size_t cellOpsCount = TTool::m_cellsData.size();

  // A failed commit may undo only the frame/cell/level created by this paste.
  // Preparation below never edits the live image or palette.
  auto rollback = [&]() {
    if (!levelCreated && !frameTouched && !cellTouched && !paletteTouched)
      return;
    TTool::m_cellsData.resize(cellOpsCount);
    if (!sl) return;
    if (cellTouched && xsh->getCell(row, col) == TXshCell(sl.getPointer(), fid))
      xsh->setCell(row, col, originalCell);
    if (frameTouched) {
      if (createFrame)
        sl->eraseFrame(fid);
      else
        sl->setFrame(fid, originalImage);
    }
    if (paletteTouched && !levelCreated) {
      if (originalPalette) {
        originalPalette->assign(paletteBackup.getPointer());
        originalPalette->setDirtyFlag(wasPaletteDirty);
      } else
        sl->setPalette(nullptr);
    }
    if (levelCreated) {
      scene->getLevelSet()->removeLevel(sl.getPointer());
      app->getCurrentScene()->notifyCastChange();
    } else
      sl->setDirtyFlag(wasDirty);
  };

  try {
    // Bound both the raster allocation and the transformed temporary allocated
    // by TRop::over. Reject invalid coordinates before rounding them to
    // integers.
    const int maxDimension      = 16384;
    const qint64 maxRasterBytes = 256LL * 1024 * 1024;
    auto validSize              = [&](const TDimension &dim, int pixelSize) {
      return dim.lx > 0 && dim.ly > 0 && dim.lx <= maxDimension &&
             dim.ly <= maxDimension && pixelSize > 0 &&
             qint64(dim.lx) * dim.ly * pixelSize <= maxRasterBytes;
    };
    auto validCoordinate = [](double value) {
      return std::isfinite(value) && std::abs(value) <= 268435456.0;
    };
    auto validRect = [&](const TRectD &rect) {
      return validCoordinate(rect.x0) && validCoordinate(rect.y0) &&
             validCoordinate(rect.x1) && validCoordinate(rect.y1);
    };
    auto validDpi = [](double dpi) { return std::isfinite(dpi) && dpi > 0.0; };
    TPointD cameraDpi = scene->getCurrentCamera()->getDpi();
    if (!validDpi(cameraDpi.x)) cameraDpi.x = 120.0;
    if (!validDpi(cameraDpi.y)) cameraDpi.y = 120.0;
    auto normalizedDpi = [&](TPointD dpi) {
      if (!validDpi(dpi.x)) dpi.x = cameraDpi.x;
      if (!validDpi(dpi.y)) dpi.y = cameraDpi.y;
      return dpi;
    };

    TPaletteP palette;
    TImageP referenceImage;
    if (!createLevel) {
      wasDirty        = sl->getDirtyFlag();
      originalPalette = sl->getPalette();
      if (originalPalette) {
        wasPaletteDirty = originalPalette->getDirtyFlag();
        paletteBackup   = originalPalette->clone();
      }
      palette = originalPalette ? originalPalette->clone() : new TPalette();
      // Request full resolution without marking the source frame as modified.
      referenceImage = referenceCell.getImage(false, 1);
      if (!createFrame) {
        originalImage = referenceImage;
        if (!originalImage) return false;
      } else {
        std::vector<TFrameId> fids;
        sl->getFids(fids);
        if (!fids.empty()) {
          if (fids.back().getNumber() == std::numeric_limits<int>::max())
            return false;
          fid = TFrameId(fids.back().getNumber() + 1);
        }
        sl->formatFId(fid, scene->getProperties()->formatTemplateFIdForInput());
        if (sl->isFid(fid)) return false;
      }
    } else
      palette = new TPalette();

    TRasterP ras;
    double dpiX = 0.0, dpiY = 0.0;
    std::vector<TRectD> rects;
    std::vector<TStroke> strokes, originalStrokes;
    TAffine affine;
    rasterImageData->getData(ras, dpiX, dpiY, rects, strokes, originalStrokes,
                             affine, palette.getPointer());
    if (!ras || !validSize(ras->getSize(), ras->getPixelSize()) ||
        (rects.empty() && strokes.empty()))
      return false;
    TRasterCM32P rasCM = ras;
    if (levelType == TZP_XSHLEVEL && !rasCM) return false;
    const TPointD sourceDpi = normalizedDpi(TPointD(dpiX, dpiY));

    TRasterP referenceRaster;
    TPointD destinationDpi = sourceDpi;
    TDimension dim         = rasterImageData->getDim();
    if (!createLevel) {
      destinationDpi = sl->getProperties()->getImageDpi();
      dim            = sl->getProperties()->getImageRes();
      if (TToonzImageP ti = referenceImage) {
        referenceRaster = ti->getRaster();
        ti->getDpi(destinationDpi.x, destinationDpi.y);
      } else if (TRasterImageP ri = referenceImage) {
        referenceRaster = ri->getRaster();
        ri->getDpi(destinationDpi.x, destinationDpi.y);
      }
      if (!createFrame) {
        if (!referenceRaster) return false;
        dim = referenceRaster->getSize();
      } else if ((dim.lx <= 0 || dim.ly <= 0) && referenceRaster)
        dim = referenceRaster->getSize();
      destinationDpi = normalizedDpi(destinationDpi);
    } else if (dim.lx <= 0 || dim.ly <= 0)
      dim = ras->getSize();

    // Transform in source pixels first, then convert to destination pixels.
    // Left multiplication also scales translations and supports non-square DPI.
    affine =
        TScale(destinationDpi.x / sourceDpi.x, destinationDpi.y / sourceDpi.y) *
        affine;
    if (!std::isfinite(affine.a11) || !std::isfinite(affine.a12) ||
        !std::isfinite(affine.a13) || !std::isfinite(affine.a21) ||
        !std::isfinite(affine.a22) || !std::isfinite(affine.a23))
      return false;
    TRectD boxD;
    for (const auto &rect : rects) {
      if (!validRect(rect)) return false;
      boxD += rect;
    }
    for (const auto &stroke : strokes) {
      const TRectD rect = stroke.getBBox();
      if (!validRect(rect)) return false;
      boxD += rect;
    }
    boxD = affine * boxD;
    if (!validRect(boxD) || boxD.isEmpty()) return false;

    // Match TRop::over's temporary exactly: its footprint can be larger than
    // the copied raster after rotation, scaling, or a fractional translation.
    TDimension paintSize = ras->getSize();
    if (!affine.isIdentity()) {
      const TRect bounds = ras->getBounds();
      const TRectD transformed =
          affine * TRectD(bounds.x0, bounds.y0, bounds.x1, bounds.y1);
      if (!validRect(transformed)) return false;
      paintSize =
          TDimension(tceil(transformed.x1) - tfloor(transformed.x0) + 1,
                     tceil(transformed.y1) - tfloor(transformed.y0) + 1);
    }
    if (!validSize(paintSize, ras->getPixelSize())) return false;

    if (createLevel) {
      // Native v1 clipboard data records a cropped raster, not its canvas size.
      // Keep the source canvas as a minimum and grow symmetrically so that an
      // off-center selection retains its position without being clipped.
      const double halfWidth =
          std::max(std::max(std::abs(boxD.x0), std::abs(boxD.x1)),
                   std::abs(boxD.x0 + paintSize.lx));
      const double halfHeight =
          std::max(std::max(std::abs(boxD.y0), std::abs(boxD.y1)),
                   std::abs(boxD.y0 + paintSize.ly));
      if (halfWidth > maxDimension * 0.5 || halfHeight > maxDimension * 0.5)
        return false;
      dim.lx = std::max(dim.lx, tceil(2.0 * halfWidth));
      dim.ly = std::max(dim.ly, tceil(2.0 * halfHeight));
    }
    const int pixelSize =
        referenceRaster && !createFrame ? referenceRaster->getPixelSize() : 4;
    if (!validSize(dim, pixelSize)) return false;

    TImageP img;
    TRasterP outRas;
    if (!createFrame) {
      img = originalImage->cloneImage();
      if (TToonzImageP ti = img)
        outRas = ti->getRaster();
      else if (TRasterImageP ri = img)
        outRas = ri->getRaster();
    } else if (levelType == TZP_XSHLEVEL) {
      TRasterCM32P raster(dim);
      raster->fill(TPixelCM32());
      TToonzImageP ti(raster, TRect());
      ti->setDpi(destinationDpi.x, destinationDpi.y);
      img    = ti;
      outRas = raster;
    } else {
      TRaster32P raster(dim);
      raster->fill(TPixel32(0, 0, 0, 0));
      TRasterImageP ri(raster);
      ri->setDpi(destinationDpi.x, destinationDpi.y);
      img    = ri;
      outRas = raster;
    }
    if (!img || !outRas) return false;
    TRasterCM32P outRasCM = outRas;
    if ((levelType == TZP_XSHLEVEL) != bool(outRasCM)) return false;
    img->setPalette(palette.getPointer());
    const TPointD origin = boxD.getP00() + outRas->getCenterD();
    const TPoint pos(tfloor(origin.x), tfloor(origin.y));
    const TRect paintRect(pos, paintSize);
    if ((paintRect * outRas->getBounds()).isEmpty()) return false;

    std::unique_ptr<TTileSet> preparedTiles;
    if (levelType == TZP_XSHLEVEL)
      preparedTiles.reset(new TTileSetCM32(outRas->getSize()));
    else
      preparedTiles.reset(new TTileSetFullColor(outRas->getSize()));
    preparedTiles->add(outRas, paintRect);
    if (!outRasCM && rasCM)
      TRop::over(outRas, rasCM, palette.getPointer(), pos, affine);
    else
      TRop::over(outRas, ras, pos, affine);

    // All potentially expensive conversion and compositing has succeeded.
    // Only now create the level/frame or replace the occupied destination.
    if (createLevel) {
      TXshLevel *level = scene->createNewLevel(levelType);
      if (!level) return false;
      sl           = level->getSimpleLevel();
      levelCreated = true;
      if (!sl) {
        scene->getLevelSet()->removeLevel(level);
        return false;
      }
      sl->getProperties()->setImageRes(dim);
      sl->getProperties()->setImageDpi(destinationDpi);
      sl->getProperties()->setDpi(destinationDpi);
      sl->getProperties()->setDpiPolicy(LevelProperties::DP_ImageDpi);
      sl->formatFId(fid, scene->getProperties()->formatTemplateFIdForInput());
      sl->setPalette(palette.getPointer());
    } else {
      paletteTouched = true;
      if (originalPalette) {
        originalPalette->assign(palette.getPointer());
        originalPalette->setDirtyFlag(wasPaletteDirty ||
                                      palette->getDirtyFlag());
      } else
        sl->setPalette(palette.getPointer());
    }
    frameTouched = true;
    sl->setFrame(fid, img);
    if (createFrame) {
      cellTouched = true;
      if (!xsh->setCell(row, col, TXshCell(sl.getPointer(), fid))) {
        rollback();
        return false;
      }
      TTool::m_cellsData.push_back({row, row, TTool::CellOps::BlankToNew});
    }
    ToolUtils::updateSaveBox(sl, fid);
    IconGenerator::instance()->invalidate(sl.getPointer(), fid);
    sl->setDirtyFlag(true);
    if (levelCreated) app->getCurrentScene()->notifyCastChange();
    app->getPaletteController()
        ->getCurrentLevelPalette()
        ->notifyPaletteChanged();
    *tiles         = preparedTiles.release();
    isLevelCreated = levelCreated;
    return true;
  } catch (...) {
    rollback();
    return false;
  }
}

//=============================================================================
//  PasteToonzImageInCellsUndo
//-----------------------------------------------------------------------------

class PasteToonzImageInCellsUndo final : public ToolUtils::TRasterUndo {
  RasterImageData *m_rasterImageData;

public:
  PasteToonzImageInCellsUndo(int row, int col, const RasterImageData *data,
                             TTileSetCM32 *tiles, TXshSimpleLevel *level,
                             const TFrameId &id, TPaletteP oldPalette,
                             bool createdFrame, bool isLevelCreated)
      : ToolUtils::TRasterUndo(tiles, level, id, createdFrame, isLevelCreated,
                               oldPalette)
      , m_rasterImageData(data->clone()) {}

  ~PasteToonzImageInCellsUndo() { delete m_rasterImageData; }

  void redo() const override {
    insertLevelAndFrameIfNeeded();
    TTileSet *tiles;
    bool isLevelCreated;
    pasteRasterImageInCellWithoutUndo(m_row, m_col, m_rasterImageData, &tiles,
                                      isLevelCreated);
    delete tiles;
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }

  int getSize() const override {
    return m_rasterImageData->getMemorySize() + TRasterUndo::getSize();
  }
  QString getHistoryString() override { return QObject::tr("Paste"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//=============================================================================
//  PasteFullColorImageInCellsUndo
//-----------------------------------------------------------------------------

class PasteFullColorImageInCellsUndo final
    : public ToolUtils::TFullColorRasterUndo {
  RasterImageData *m_rasterImageData;

public:
  PasteFullColorImageInCellsUndo(const RasterImageData *data,
                                 TTileSetFullColor *tiles,
                                 TXshSimpleLevel *level, const TFrameId &id,
                                 TPaletteP oldPalette, bool createdFrame,
                                 bool isLevelCreated, int col = -1)
      : ToolUtils::TFullColorRasterUndo(tiles, level, id, createdFrame,
                                        isLevelCreated, oldPalette)
      , m_rasterImageData(data->clone()) {
    if (col >= 0) m_col = col;
  }

  ~PasteFullColorImageInCellsUndo() { delete m_rasterImageData; }

  void redo() const override {
    insertLevelAndFrameIfNeeded();
    TTileSet *tiles = 0;
    bool isLevelCreated;
    pasteRasterImageInCellWithoutUndo(m_row, m_col, m_rasterImageData, &tiles,
                                      isLevelCreated);
    m_level->setDirtyFlag(true);
    if (tiles) delete tiles;
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }

  int getSize() const override {
    return m_rasterImageData->getMemorySize() + TFullColorRasterUndo::getSize();
  }
  QString getHistoryString() override { return QObject::tr("Paste (Raster)"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//=============================================================================

// Paste drawings in cell without undo use smart pointer for free
void pasteDrawingsInCellWithoutUndo(TXsheet *xsh, TXshSimpleLevelP level,
                                    const std::set<TFrameId> &frameIds, int r0,
                                    int c0) {
  int frameToInsert = frameIds.size();
  xsh->insertCells(r0, c0, frameToInsert);
  std::set<TFrameId>::const_iterator it;
  int r = r0;
  // Use smart pointer to ensure the level stays alive during the operation
  TXshSimpleLevelP levelRef = level;
  for (it = frameIds.begin(); it != frameIds.end(); it++, r++) {
    TXshCell cell(levelRef.getPointer(), *it);
    xsh->setCell(r, c0, cell);
  }
}

//=============================================================================
//  PasteDrawingsInCellUndo
//-----------------------------------------------------------------------------

class PasteDrawingsInCellUndo final : public TUndo {
  TXsheet *m_xsheet;
  int m_r0, m_c0;
  std::set<TFrameId> m_frameIds;
  TXshSimpleLevelP m_level;

public:
  PasteDrawingsInCellUndo(TXshSimpleLevel *level,
                          const std::set<TFrameId> &frameIds, int r0, int c0)
      : TUndo(), m_level(level), m_frameIds(frameIds), m_r0(r0), m_c0(c0) {
    m_xsheet = TApp::instance()->getCurrentXsheet()->getXsheet();
    m_xsheet->addRef();
  }

  //--------------------------------

  ~PasteDrawingsInCellUndo() { m_xsheet->release(); }

  void undo() const override {
    int cellsToRemove = m_frameIds.size();
    m_xsheet->removeCells(m_r0, m_c0, cellsToRemove);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }

  void redo() const override {
    pasteDrawingsInCellWithoutUndo(m_xsheet, m_level.getPointer(), m_frameIds,
                                   m_r0, m_c0);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }

  int getSize() const override { return sizeof *this; }

  QString getHistoryString() override { return QObject::tr("Paste"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//=============================================================================

// Paste cells without undo (overloaded version)
bool pasteCellsWithoutUndo(int &r0, int &c0, int &r1, int &c1,
                           bool insert = true, bool doZeraryClone = true,
                           bool skipEmptyCells = true) {
  TXsheet *xsh              = TApp::instance()->getCurrentXsheet()->getXsheet();
  QClipboard *clipboard     = QApplication::clipboard();
  const QMimeData *mimeData = clipboard->mimeData();
  const TCellData *cellData = dynamic_cast<const TCellData *>(mimeData);

  if (!cellData) return false;

  if (r0 < 0 || c0 < 0) return false;

  /*-- In this function, r1,c1 are resized according to the paste range --*/
  bool ret = cellData->getCells(xsh, r0, c0, r1, c1, insert, doZeraryClone,
                                skipEmptyCells);
  if (!ret) return false;

  // If the current selection is TCellSelection, select the pasted cells
  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection) cellSelection->selectCells(r0, c0, r1, c1);
  return true;
}

//=============================================================================

// Overwrite paste cells undo class
class OverwritePasteCellsUndo final : public TUndo {
  TCellSelection *m_oldSelection;
  TCellSelection *m_newSelection;
  QMimeData *m_data;
  std::vector<bool> m_areOldColumnsEmpty;

  QMimeData *m_beforeData;
  bool m_containsSoundColumn;

public:
  OverwritePasteCellsUndo(int r0, int c0, int r1, int c1, int oldR0, int oldC0,
                          int oldR1, int oldC1,
                          const std::vector<bool> &areColumnsEmpty,
                          TCellData *beforeData, bool containsSoundColumn)
      : m_areOldColumnsEmpty(areColumnsEmpty)
      , m_containsSoundColumn(containsSoundColumn) {
    TCellData *data = new TCellData();
    TXsheet *xsh    = TApp::instance()->getCurrentXsheet()->getXsheet();
    data->setCells(xsh, r0, c0, r1, c1);
    m_data         = data->clone();
    m_newSelection = new TCellSelection();
    m_newSelection->selectCells(r0, c0, r1, c1);
    m_oldSelection = new TCellSelection();
    m_oldSelection->selectCells(oldR0, oldC0, oldR1, oldC1);

    /*-- Also keep the cells before pasting --*/
    m_beforeData = beforeData->clone();
  }

  ~OverwritePasteCellsUndo() {
    delete m_newSelection;
    delete m_oldSelection;
    delete m_data;
    delete m_beforeData;
  }

  void undo() const override {
    int r0, c0, r1, c1;
    m_newSelection->getSelectedCells(r0, c0, r1, c1);

    int oldR0, oldC0, oldR1, oldC1;
    m_oldSelection->getSelectedCells(oldR0, oldC0, oldR1, oldC1);

    QClipboard *clipboard = QApplication::clipboard();
    cutCellsWithoutUndo(r0, c0, r1, c1);

    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    assert(c1 - c0 + 1 == (int)m_areOldColumnsEmpty.size());
    int c;
    for (c = c0; c <= c1; c++) {
      if (!m_areOldColumnsEmpty[c - c0] || !xsh->getColumn(c)) continue;
      xsh->removeColumn(c);
      xsh->insertColumn(c);
    }

    /*-- Keep the clipboard content --*/
    QMimeData *mimeData = cloneData(clipboard->mimeData());

    /*-- Put the cell arrangement that was there before pasting into cellData
     * and paste it --*/
    clipboard->setMimeData(cloneData(m_beforeData), QClipboard::Clipboard);
    pasteCellsWithoutUndo(r0, c0, r1, c1, true, false, false);

    /*-- Restore clipboard --*/
    clipboard->setMimeData(mimeData, QClipboard::Clipboard);

    // If the current selection is TCellSelection, select the cells that are in
    // oldSelection
    TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
        TApp::instance()->getCurrentSelection()->getSelection());
    if (cellSelection && oldR0 != -1 && oldC0 != -1)
      cellSelection->selectCells(oldR0, oldC0, oldR1, oldC1);

    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    if (m_containsSoundColumn)
      TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
  }

  void redo() const override {
    int r0, c0, r1, c1;
    m_newSelection->getSelectedCells(r0, c0, c1, r1);
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setMimeData(cloneData(m_data), QClipboard::Clipboard);
    // Cut cells that are in newSelection
    pasteCellsWithoutUndo(r0, c0, c1, r1, false, false);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    if (m_containsSoundColumn)
      TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
  }

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override {
    return QObject::tr("Overwrite Paste Cells");
  }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//=============================================================================

// Paste numbers without undo
bool pasteNumbersWithoutUndo(int &r0, int &c0, int &r1, int &c1) {
  TXsheet *xsh              = TApp::instance()->getCurrentXsheet()->getXsheet();
  QClipboard *clipboard     = QApplication::clipboard();
  const QMimeData *mimeData = clipboard->mimeData();
  const TCellData *cellData = dynamic_cast<const TCellData *>(mimeData);

  if (!cellData) return false;

  if (r0 < 0 || c0 < 0) return false;

  bool ret = cellData->getNumbers(xsh, r0, c0, r1, c1);
  if (!ret) return false;

  // If the current selection is TCellSelection, select the pasted cells
  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection) cellSelection->selectCells(r0, c0, r1, c1);
  return true;
}

//=============================================================================

// Overwrite paste numbers undo class
class OverwritePasteNumbersUndo final : public TUndo {
  TCellSelection *m_oldSelection;
  TCellSelection *m_newSelection;
  QMimeData *m_data;
  QMimeData *m_beforeData;

public:
  OverwritePasteNumbersUndo(int r0, int c0, int r1, int c1, int oldR0,
                            int oldC0, int oldR1, int oldC1,
                            TCellData *beforeData) {
    // keep the pasted data
    TCellData *data = new TCellData();
    TXsheet *xsh    = TApp::instance()->getCurrentXsheet()->getXsheet();
    data->setCells(xsh, r0, c0, r1, c1);
    m_data         = data->clone();
    m_newSelection = new TCellSelection();
    m_newSelection->selectCells(r0, c0, r1, c1);
    m_oldSelection = new TCellSelection();
    m_oldSelection->selectCells(oldR0, oldC0, oldR1, oldC1);
    // keep the cells before pasting
    m_beforeData = beforeData->clone();
  }

  ~OverwritePasteNumbersUndo() {
    delete m_newSelection;
    delete m_oldSelection;
    delete m_data;
    delete m_beforeData;
  }

  void undo() const override {
    int r0, c0, r1, c1;
    m_newSelection->getSelectedCells(r0, c0, r1, c1);

    int oldR0, oldC0, oldR1, oldC1;
    m_oldSelection->getSelectedCells(oldR0, oldC0, oldR1, oldC1);

    QClipboard *clipboard = QApplication::clipboard();
    cutCellsWithoutUndo(r0, c0, r1, c1);

    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();

    // keep the clipboard content
    QMimeData *mimeData = cloneData(clipboard->mimeData());

    // execute pasting
    clipboard->setMimeData(cloneData(m_beforeData), QClipboard::Clipboard);
    pasteCellsWithoutUndo(r0, c0, r1, c1, true, false, false);

    // restore clipboard
    clipboard->setMimeData(mimeData, QClipboard::Clipboard);

    // revert old cell selection
    TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
        TApp::instance()->getCurrentSelection()->getSelection());
    if (cellSelection && oldR0 != -1 && oldC0 != -1)
      cellSelection->selectCells(oldR0, oldC0, oldR1, oldC1);

    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }

  void redo() const override {
    int r0, c0, r1, c1;
    m_newSelection->getSelectedCells(r0, c0, c1, r1);
    QClipboard *clipboard = QApplication::clipboard();

    // keep the clipboard content
    QMimeData *mimeData = cloneData(clipboard->mimeData());

    clipboard->setMimeData(cloneData(m_data), QClipboard::Clipboard);
    // Cut cells that are in newSelection
    pasteCellsWithoutUndo(r0, c0, c1, r1, false, false);

    // restore clipboard
    clipboard->setMimeData(mimeData, QClipboard::Clipboard);

    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override { return QObject::tr("Paste Numbers"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//-----------------------------------------------------------------------------
// Check if all cells in the range have the same content
bool checkIfCellsHaveTheSameContent(const int &r0, const int &c0, const int &r1,
                                    const int &c1, const TXshCell &cell) {
  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  for (int c = c0; c <= c1; c++) {
    for (int r = r0; r <= r1; r++) {
      if (cell != xsh->getCell(r, c)) return false;
    }
  }
  return true;
}

// Rename cells without undo
void renameCellsWithoutUndo(int &r0, int &c0, int &r1, int &c1,
                            const TXshCell &cell) {
  TApp *app    = TApp::instance();
  TXsheet *xsh = app->getCurrentXsheet()->getXsheet();
  for (int c = c0; c <= c1; c++) {
    for (int r = r0; r <= r1; r++) {
      xsh->setCell(r, c, TXshCell(cell));
    }
  }
  app->getCurrentXsheet()->notifyXsheetChanged();
  int currentFrame = app->getCurrentFrame()->getFrame();
  if (r0 <= currentFrame && currentFrame <= r1) {
    app->getCurrentTool()->onImageChanged(
        (TImage::Type)app->getCurrentImageType());
    xsh->getStageObjectTree()->invalidateAll();
  }
}

// Rename cells undo class
class RenameCellsUndo final : public TUndo {
  TCellSelection *m_selection;
  QMimeData *m_data;
  TXshCell m_cell;

public:
  RenameCellsUndo(TCellSelection *selection, QMimeData *data, TXshCell &cell)
      : m_data(data), m_cell(cell) {
    int r0, c0, r1, c1;
    selection->getSelectedCells(r0, c0, r1, c1);
    m_selection = new TCellSelection();
    m_selection->selectCells(r0, c0, r1, c1);
  }

  RenameCellsUndo(int r0, int c0, int r1, int c1, QMimeData *data,
                  TXshCell &cell)
      : m_data(data), m_cell(cell) {
    m_selection = new TCellSelection();
    m_selection->selectCells(r0, c0, r1, c1);
  }

  ~RenameCellsUndo() {
    delete m_selection;
    delete m_data;
  }

  void undo() const override {
    int r0, c0, r1, c1;
    m_selection->getSelectedCells(r0, c0, r1, c1);
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    for (int c = c0; c <= c1; c++) {
      xsh->clearCells(r0, c, r1 - r0 + 1);
    }
    const TCellData *cellData = dynamic_cast<const TCellData *>(m_data);
    pasteCellsWithoutUndo(cellData, r0, c0, r1, c1, false, false);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }

  void redo() const override {
    int r0, c0, r1, c1;
    m_selection->getSelectedCells(r0, c0, r1, c1);
    renameCellsWithoutUndo(r0, c0, r1, c1, m_cell);
  }

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override {
    int r0, c0, r1, c1;
    m_selection->getSelectedCells(r0, c0, r1, c1);
    QString colRange = QString::number(c0 + 1);
    if (c0 != c1)
      colRange.append(QString(" - %1").arg(QString::number(c1 + 1)));
    QString frameRange = QString::number(r0 + 1);
    if (r0 != r1)
      frameRange.append(QString(" - %1").arg(QString::number(r1 + 1)));

    return QObject::tr("Rename Cell  at Column %1  Frame %2")
        .arg(colRange)
        .arg(frameRange);
  }

  int getHistoryType() override { return HistoryType::Xsheet; }
};

//-----------------------------------------------------------------------------

// Create blank drawing undo class
class CreateBlankDrawingUndo final : public ToolUtils::TToolUndo {
  int row;
  int col;

public:
  CreateBlankDrawingUndo(TXshSimpleLevel *level, const TFrameId &frameId,
                         bool levelCreated, const TPaletteP &oldPalette)
      : TToolUndo(level, frameId, true, levelCreated, oldPalette) {}

  ~CreateBlankDrawingUndo() {}

  void undo() const override {
    removeLevelAndFrameIfNeeded();
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    notifyImageChanged();
  }

  void redo() const override {
    insertLevelAndFrameIfNeeded();

    IconGenerator::instance()->invalidate(m_level.getPointer(), m_frameId);

    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    notifyImageChanged();
  }

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override {
    return QObject::tr("Create Blank Drawing");
  }

  int getHistoryType() override { return HistoryType::Xsheet; }
  //-----------------------------------------------------------------------------
};

//-----------------------------------------------------------------------------

// Duplicate drawing undo class
class DuplicateDrawingUndo final : public ToolUtils::TToolUndo {
  TFrameId origFrameId;
  TFrameId dupFrameId;

public:
  DuplicateDrawingUndo(TXshSimpleLevel *level, const TFrameId &srcFrameId,
                       const TFrameId &tgtFrameId)
      : TToolUndo(level, tgtFrameId, true) {
    origFrameId = srcFrameId;
    dupFrameId  = tgtFrameId;
  }

  ~DuplicateDrawingUndo() {}

  void undo() const override {
    removeLevelAndFrameIfNeeded();
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    notifyImageChanged();
  }

  void redo() const override {
    insertLevelAndFrameIfNeeded();

    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    notifyImageChanged();
  }

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override {
    return QObject::tr("Duplicate Drawing");
  }

  int getHistoryType() override { return HistoryType::Xsheet; }
  //-----------------------------------------------------------------------------
};

//-----------------------------------------------------------------------------

// Fill empty cell undo class
class FillEmptyCellUndo final : public TUndo {
  TCellSelection *m_selection;
  TXshCell m_cell;

public:
  FillEmptyCellUndo(int r0, int r1, int c, TXshCell &cell) : m_cell(cell) {
    m_selection = new TCellSelection();
    m_selection->selectCells(r0, c, r1, c);
  }

  ~FillEmptyCellUndo() { delete m_selection; }

  void undo() const override {
    int r0, c0, r1, c1;
    m_selection->getSelectedCells(r0, c0, r1, c1);
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    for (int c = c0; c <= c1; c++) {
      xsh->clearCells(r0, c, r1 - r0 + 1);
    }
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }

  void redo() const override {
    int r0, c0, r1, c1;
    m_selection->getSelectedCells(r0, c0, r1, c1);
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    for (int i = r0; i <= r1; i++) xsh->setCell(i, c0, m_cell);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override {
    return QObject::tr("Fill In Empty Cells");
  }

  int getHistoryType() override { return HistoryType::Xsheet; }
  //-----------------------------------------------------------------------------
};

//=============================================================================
// Duplicate in XSheet Undo
//-----------------------------------------------------------------------------

class DuplicateInXSheetUndo final : public TUndo {
  int m_r0, m_r1, m_c;
  TXshCell m_oldCell;
  TXshCell m_newCell;
  bool m_goForward;

public:
  DuplicateInXSheetUndo(int r0, int r1, int c, TXshCell oldCell,
                        TXshCell newCell, bool goForward)
      : m_r0(r0)
      , m_r1(r1)
      , m_c(c)
      , m_oldCell(oldCell)
      , m_newCell(newCell)
      , m_goForward(goForward) {}

  ~DuplicateInXSheetUndo() {}

  void undo() const override {
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    if (m_r1 > m_r0) {
      for (int i = m_r0; i <= m_r1; i++) {
        xsh->setCell(i, m_c, m_oldCell);
      }
    } else
      xsh->setCell(m_r0, m_c, m_oldCell);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    if (m_goForward) CommandManager::instance()->execute(MI_PrevFrame);
  }

  void redo() const override {
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    if (m_r1 > m_r0) {
      for (int i = m_r0; i <= m_r1; i++) {
        xsh->setCell(i, m_c, m_newCell);
      }
    } else
      xsh->setCell(m_r0, m_c, m_newCell);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    if (m_goForward) CommandManager::instance()->execute(MI_NextFrame);
  }

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override {
    return QObject::tr("Duplicate Frame in XSheet");
  }

  int getHistoryType() override { return HistoryType::Xsheet; }
  //-----------------------------------------------------------------------------
};

//-----------------------------------------------------------------------------
// Obtain level set contained in the cellData
// It is used for checking and updating the scene cast when pasting
void getLevelSetFromData(const TCellData *cellData,
                         std::set<TXshLevel *> &levelSet) {
  for (int c = 0; c < cellData->getCellCount(); c++) {
    TXshCell cell = cellData->getCell(c);
    if (cell.isEmpty()) continue;
    TXshLevelP tmpLevel = cell.m_level;
    if (levelSet.count(tmpLevel.getPointer()) == 0) {
      // gather levels in subxsheet
      if (tmpLevel->getChildLevel()) {
        TXsheet *childXsh = tmpLevel->getChildLevel()->getXsheet();
        childXsh->getUsedLevels(levelSet);
      }
      levelSet.insert(tmpLevel.getPointer());
    }
  }
}

}  // namespace
//-----------------------------------------------------------------------------

//=============================================================================
// TCellSelection::Range
//-----------------------------------------------------------------------------

TCellSelection::Range::Range() : m_c0(0), m_r0(0), m_c1(-1), m_r1(-1) {}

bool TCellSelection::Range::isEmpty() const {
  return m_c0 > m_c1 || m_r0 > m_r1;
}

bool TCellSelection::Range::contains(int r, int c) const {
  return m_r0 <= r && r <= m_r1 && m_c0 <= c && c <= m_c1;
}

int TCellSelection::Range::getRowCount() const { return m_r1 - m_r0 + 1; }

int TCellSelection::Range::getColCount() const { return m_c1 - m_c0 + 1; }

//=============================================================================
// TCellSelection
//-----------------------------------------------------------------------------

TCellSelection::TCellSelection()
    : m_timeStretchPopup(0), m_reframePopup(0), m_resizePivotRow(-1) {
  setAlternativeCommandNames();
}

//-----------------------------------------------------------------------------

TCellSelection::~TCellSelection() {}

//-----------------------------------------------------------------------------

void TCellSelection::enableCommands() {
  // enableCommand(this, MI_PasteNew,     &TCellSelection::dPasteCells);
  enableCommand(this, MI_Autorenumber, &TCellSelection::dRenumberCells);

  enableCommand(this, MI_Reverse, &TCellSelection::reverseCells);
  enableCommand(this, MI_Swing, &TCellSelection::swingCells);
  enableCommand(this, MI_Random, &TCellSelection::randomCells);
  enableCommand(this, MI_Increment, &TCellSelection::incrementCells);
  enableCommand(this, MI_ResetStep, &TCellSelection::resetStepCells);
  enableCommand(this, MI_IncreaseStep, &TCellSelection::increaseStepCells);
  enableCommand(this, MI_DecreaseStep, &TCellSelection::decreaseStepCells);
  enableCommand(this, MI_Step2, &TCellSelection::step2Cells);
  enableCommand(this, MI_Step3, &TCellSelection::step3Cells);
  enableCommand(this, MI_Step4, &TCellSelection::step4Cells);
  enableCommand(this, MI_Each2, &TCellSelection::each2Cells);
  enableCommand(this, MI_Each3, &TCellSelection::each3Cells);
  enableCommand(this, MI_Each4, &TCellSelection::each4Cells);

  enableCommand(this, MI_Rollup, &TCellSelection::rollupCells);
  enableCommand(this, MI_Rolldown, &TCellSelection::rolldownCells);

  enableCommand(this, MI_TimeStretch, &TCellSelection::openTimeStretchPopup);
  enableCommand(this, MI_CloneLevel, &TCellSelection::cloneLevel);
  enableCommand(this, MI_SetKeyframes, &TCellSelection::setKeyframes);

  enableCommand(this, MI_ShiftKeyframesDown,
                &TCellSelection::shiftKeyframesDown);
  enableCommand(this, MI_ShiftKeyframesUp, &TCellSelection::shiftKeyframesUp);

  enableCommand(this, MI_Copy, &TCellSelection::copyCells);
  enableCommand(this, MI_PasteAs, &TCellSelection::pasteAs);
  enableCommand(this, MI_PasteAbove, &TCellSelection::shiftPaste);
  enableCommand(this, MI_Paste,
                &TCellSelection::doPaste);  // choose pasting behavior by
                                            // preference option

  if (dynamic_cast<const TKeyframeData *>(
          QApplication::clipboard()->mimeData()))
    enableCommand(this, MI_PasteInto, &TCellSelection::pasteKeyframesInto);

  enableCommand(this, MI_Cut, &TCellSelection::cutCells);
  enableCommand(this, MI_Clear, &TCellSelection::deleteCells);
  enableCommand(this, MI_Insert, &TCellSelection::insertCells);

  enableCommand(this, MI_PasteInto, &TCellSelection::overWritePasteCells);

  enableCommand(this, MI_FillEmptyCell, &TCellSelection::fillEmptyCell);
  enableCommand(this, MI_Reframe1, &TCellSelection::reframe1Cells);
  enableCommand(this, MI_Reframe2, &TCellSelection::reframe2Cells);
  enableCommand(this, MI_Reframe3, &TCellSelection::reframe3Cells);
  enableCommand(this, MI_Reframe4, &TCellSelection::reframe4Cells);
  enableCommand(this, MI_ConvertVectorToVector,
                &TCellSelection::convertVectortoVector);
  enableCommand(this, MI_ReframeWithEmptyInbetweens,
                &TCellSelection::reframeWithEmptyInbetweens);

  enableCommand(this, MI_PasteNumbers, &TCellSelection::overwritePasteNumbers);
  enableCommand(this, MI_PasteCellContent, &TCellSelection::pasteCells);
  enableCommand(this, MI_CreateBlankDrawing,
                &TCellSelection::createBlankDrawings);
  enableCommand(this, MI_Duplicate, &TCellSelection::duplicateFrames);
  enableCommand(this, MI_PasteDuplicate, &TCellSelection::pasteDuplicateCells);
}

//-----------------------------------------------------------------------------

void TCellSelection::setAlternativeCommandNames() {
  m_alternativeCommandNames = {
      {MI_Copy, QObject::tr("Copy Cells", "TCellSelection")},
      {MI_Paste, QObject::tr("Paste Cells", "TCellSelection")},
      {MI_PasteAbove, QObject::tr("Paste As...", "TCellSelection")},
      {MI_PasteInto, QObject::tr("Overwrite Paste Cells", "TCellSelection")},
      {MI_Cut, QObject::tr("Cut Cells", "TCellSelection")},
      {MI_Clear, QObject::tr("Delete Cells", "TCellSelection")},
      {MI_Insert, QObject::tr("Insert Cells", "TCellSelection")}};
}

//-----------------------------------------------------------------------------
// Used in RenameCellField::eventFilter()

bool TCellSelection::isEnabledCommand(
    std::string commandId) {  // static function
  static QList<std::string> commands = {MI_Autorenumber,
                                        MI_Reverse,
                                        MI_Swing,
                                        MI_Random,
                                        MI_Increment,
                                        MI_ResetStep,
                                        MI_IncreaseStep,
                                        MI_DecreaseStep,
                                        MI_Step2,
                                        MI_Step3,
                                        MI_Step4,
                                        MI_Each2,
                                        MI_Each3,
                                        MI_Each4,
                                        MI_Rollup,
                                        MI_Rolldown,
                                        MI_TimeStretch,
                                        MI_CloneLevel,
                                        MI_SetKeyframes,
                                        MI_ShiftKeyframesDown,
                                        MI_ShiftKeyframesUp,
                                        MI_Copy,
                                        MI_Paste,
                                        MI_PasteAs,
                                        MI_PasteAbove,
                                        MI_PasteInto,
                                        MI_Cut,
                                        MI_Clear,
                                        MI_Insert,
                                        MI_Reframe1,
                                        MI_Reframe2,
                                        MI_Reframe3,
                                        MI_Reframe4,
                                        MI_ReframeWithEmptyInbetweens,
                                        MI_Undo,
                                        MI_Redo,
                                        MI_PasteNumbers,
                                        MI_PasteCellContent,
                                        MI_ConvertVectorToVector,
                                        MI_CreateBlankDrawing,
                                        MI_FillEmptyCell};
  return commands.contains(commandId);
}

//-----------------------------------------------------------------------------

bool TCellSelection::isEmpty() const { return m_range.isEmpty(); }

//-----------------------------------------------------------------------------

void TCellSelection::selectCells(int r0, int c0, int r1, int c1) {
  if (r0 > r1) std::swap(r0, r1);
  if (c0 > c1) std::swap(c0, c1);
  m_range.m_r0 = r0;
  m_range.m_c0 = c0;
  m_range.m_r1 = r1;
  m_range.m_c1 = c1;
  // cell selection won't contain the camera column
  if (m_range.m_c0 < 0) m_range.m_c0 = 0;

  // set the nearest row
  m_resizePivotRow =
      (std::abs(r0 - m_resizePivotRow) < std::abs(r1 - m_resizePivotRow)) ? r0
                                                                          : r1;
  updateCanvasSizeCommandEnabled();
}

//-----------------------------------------------------------------------------

void TCellSelection::selectCell(int row, int col) {
  m_range.m_r0     = row;
  m_range.m_c0     = col;
  m_range.m_r1     = row;
  m_range.m_c1     = col;
  m_resizePivotRow = row;
  updateCanvasSizeCommandEnabled();
}

//-----------------------------------------------------------------------------

void TCellSelection::selectNone() {
  m_range          = Range();
  m_resizePivotRow = -1;
  updateCanvasSizeCommandEnabled();
}

//-----------------------------------------------------------------------------

void TCellSelection::getSelectedCells(int &r0, int &c0, int &r1,
                                      int &c1) const {
  r0 = m_range.m_r0;
  r1 = m_range.m_r1;
  c0 = m_range.m_c0;
  c1 = m_range.m_c1;
}

//-----------------------------------------------------------------------------

TCellSelection::Range TCellSelection::getSelectedCells() const {
  return m_range;
}

//-----------------------------------------------------------------------------

bool TCellSelection::isCellSelected(int r, int c) const {
  return m_range.contains(r, c);
}

//-----------------------------------------------------------------------------

bool TCellSelection::isRowSelected(int r) const {
  return m_range.m_r0 <= r && r <= m_range.m_r1;
}

//-----------------------------------------------------------------------------

bool TCellSelection::isColSelected(int c) const {
  return m_range.m_c0 <= c && c >= m_range.m_c1;
}
//-----------------------------------------------------------------------------

bool TCellSelection::areAllColSelectedLocked() const {
  int c;
  for (c = m_range.m_c0; c <= m_range.m_c1; c++) {
    TXsheet *xsh       = TApp::instance()->getCurrentXsheet()->getXsheet();
    TXshColumn *column = xsh->getColumn(c);
    if (column && !xsh->getColumn(c)->isLocked()) return false;
  }
  return true;
}

//-----------------------------------------------------------------------------

void TCellSelection::copyCells() {
  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);
  if (isEmpty()) return;
  copyCellsWithoutUndo(r0, c0, r1, c1);
}

//-----------------------------------------------------------------------------

// Paste strokes in cell with undo support
static bool pasteStrokesInCell(int row, int col, const StrokesData *strokesData,
                               std::vector<int> *pastedIndices = nullptr) {
  PasteStrokesInCellUndo *undo =
      new PasteStrokesInCellUndo(row, col, strokesData);
  std::vector<int> indices;
  bool isPaste = pasteStrokesInCellWithoutUndo(row, col, strokesData, indices);
  if (!isPaste) {
    delete undo;
    return false;
  }
  undo->setIndices(indices);
  TUndoManager::manager()->add(undo);
  if (pastedIndices) *pastedIndices = indices;
  return true;
}

//-----------------------------------------------------------------------------

// Paste raster image in cell with undo support
static bool pasteRasterImageInCell(int row, int col,
                                   const RasterImageData *rasterImageData) {
  // to let the undo to know which frame is edited
  TTool::m_cellsData.clear();

  TXsheet *xsh         = TApp::instance()->getCurrentXsheet()->getXsheet();
  bool createdFrame    = false;
  bool isLevelCreated  = false;
  TPaletteP oldPalette = 0;

  // get the current cell
  TXshCell cell = xsh->getCell(row, col);
  // if the cell doesn't have a level...
  if (!cell.getSimpleLevel()) {
    createdFrame        = true;
    TXshSimpleLevel *sl = drawingPasteReference(xsh, row, col).getSimpleLevel();
    if (sl) oldPalette = sl->getPalette();
  } else {
    TXshSimpleLevel *sl = cell.getSimpleLevel();
    if (sl->getType() == OVL_XSHLEVEL && sl->getPath().isUneditable())
      return false;
    oldPalette = sl->getPalette();
  }
  if (oldPalette) oldPalette = oldPalette->clone();
  TTileSet *tiles = 0;
  bool isPaste    = pasteRasterImageInCellWithoutUndo(row, col, rasterImageData,
                                                      &tiles, isLevelCreated);
  if (isLevelCreated && oldPalette.getPointer()) oldPalette = 0;
  if (!isPaste) {
    delete tiles;
    return false;
  }
  cell = xsh->getCell(row, col);

  // The flag TTool::m_isLevelRenumbererd is evaluated in the undo.
  // We need to reset the flag here as the operation does not call
  // TTool::touchImage(). Currently the flag can be always false as the
  // operation does not renumber cells regardless of the preferences.
  TTool::m_isLevelRenumbererd = false;

  TTileSetCM32 *cm32Tiles           = dynamic_cast<TTileSetCM32 *>(tiles);
  TTileSetFullColor *fullColorTiles = dynamic_cast<TTileSetFullColor *>(tiles);
  if (cm32Tiles) {
    TUndoManager::manager()->add(new PasteToonzImageInCellsUndo(
        row, col, rasterImageData, cm32Tiles, cell.getSimpleLevel(),
        cell.getFrameId(), oldPalette, createdFrame, isLevelCreated));
  } else if (fullColorTiles) {
    TUndoManager::manager()->add(new PasteFullColorImageInCellsUndo(
        rasterImageData, fullColorTiles, cell.getSimpleLevel(),
        cell.getFrameId(), oldPalette, createdFrame, isLevelCreated, col));
  }
  return true;
}

namespace {

bool hasDrawingSelection(const QMimeData *mime) {
  return mime &&
         (dynamic_cast<const StrokesData *>(mime) ||
          dynamic_cast<const RasterImageData *>(mime) || mime->hasImage() ||
          mime->hasFormat("application/x-opentoonz-vector-selection-v2") ||
          mime->hasFormat("application/x-opentoonz-raster-selection-v1"));
}

int drawingLevelType(TXshSimpleLevel *level) {
  if (!level) return NO_XSHLEVEL;
  return level->getType() == TZI_XSHLEVEL ? OVL_XSHLEVEL : level->getType();
}

QString drawingTypeName(int type) {
  switch (type) {
  case PLI_XSHLEVEL:
    return QObject::tr("Toonz Vector");
  case TZP_XSHLEVEL:
    return QObject::tr("Toonz Raster");
  case OVL_XSHLEVEL:
    return QObject::tr("Raster");
  default:
    return QObject::tr("New level");
  }
}

int chooseDrawingPasteType(int sourceType, int destinationType) {
  QDialog dialog(QApplication::activeWindow());
  dialog.setWindowTitle(QObject::tr("Paste As"));
  QVBoxLayout *layout = new QVBoxLayout(&dialog);
  layout->addWidget(new QLabel(
      QObject::tr("Copied selection: %1").arg(drawingTypeName(sourceType))));
  QComboBox *types = new QComboBox(&dialog);
  for (int type : {PLI_XSHLEVEL, TZP_XSHLEVEL, OVL_XSHLEVEL})
    types->addItem(drawingTypeName(type), type);
  int initial = types->findData(destinationType);
  types->setCurrentIndex(initial >= 0 ? initial : types->findData(sourceType));
  layout->addWidget(types);
  QLabel *description = new QLabel(&dialog);
  description->setWordWrap(true);
  description->setMinimumWidth(380);
  layout->addWidget(description);
  auto updateDescription = [&]() {
    int type = types->currentData().toInt();
    QString conversion;
    if (type == sourceType)
      conversion = QObject::tr("Preserve the native drawing data.");
    else if (type == PLI_XSHLEVEL)
      conversion = QObject::tr(
          "Vectorize using the scene's Convert to Vector settings.");
    else if (sourceType == OVL_XSHLEVEL && type == TZP_XSHLEVEL)
      conversion = QObject::tr(
          "Convert to a palette of up to 256 colors, preserving transparency. "
          "Colors become ink styles.");
    else
      conversion = QObject::tr("Rasterize the copied artwork.");
    QString destination =
        destinationType == type
            ? QObject::tr("Paste into the destination level.")
        : destinationType == NO_XSHLEVEL
            ? QObject::tr("Create a new level in the selected column.")
            : QObject::tr(
                  "Create a new level in an empty column. The destination "
                  "level will be kept.");
    description->setText(conversion + "\n\n" + destination);
  };
  QObject::connect(types, QOverload<int>::of(&QComboBox::currentIndexChanged),
                   &dialog, [&](int) { updateDescription(); });
  updateDescription();
  QDialogButtonBox *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Paste"));
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  layout->addWidget(buttons);
  return dialog.exec() == QDialog::Accepted ? types->currentData().toInt()
                                            : NO_XSHLEVEL;
}

struct DrawingPasteData {
  std::unique_ptr<StrokesData> strokes;
  std::unique_ptr<RasterImageData> raster;
  bool external = false;
  QImage externalImage;

  int type() const {
    return strokes                                              ? PLI_XSHLEVEL
           : dynamic_cast<const ToonzImageData *>(raster.get()) ? TZP_XSHLEVEL
                                                                : OVL_XSHLEVEL;
  }

  bool hasRaster() const {
    if (!raster) return false;
    TRasterP pixels;
    double dpiX = 0.0, dpiY = 0.0;
    std::vector<TRectD> rects;
    std::vector<TStroke> outlines, originalOutlines;
    TAffine affine;
    TPaletteP palette = new TPalette;
    raster->getData(pixels, dpiX, dpiY, rects, outlines, originalOutlines,
                    affine, palette.getPointer());
    return pixels && pixels->getLx() > 0 && pixels->getLy() > 0 &&
           (!rects.empty() || !outlines.empty());
  }

  bool setExternalImage(const QImage &image) {
    TRasterP pixels =
        rasterFromQImage(image.convertToFormat(QImage::Format_ARGB32));
    if (!pixels) return false;
    std::vector<TRectD> rects{TRectD(-image.width() / 2.0,
                                     -image.height() / 2.0, image.width() / 2.0,
                                     image.height() / 2.0)};
    raster.reset(new FullColorImageData);
    raster->setData(pixels, new TPalette, 120.0, 120.0, pixels->getSize(),
                    rects, {}, {}, TAffine());
    return true;
  }

  bool fitExternalImage(TXshSimpleLevel *destination) {
    if (!external || !destination) return true;
    TDimension size = destination->getResolution();
    if (size.lx <= 0 || size.ly <= 0) return true;
    if (externalImage.width() <= size.lx && externalImage.height() <= size.ly)
      return true;
    return setExternalImage(
        externalImage.scaled(size.lx, size.ly, Qt::KeepAspectRatio));
  }

  bool read(const QMimeData *mime) {
    if (const StrokesData *data = dynamic_cast<const StrokesData *>(mime))
      strokes.reset(data->clone());
    else if (const RasterImageData *data =
                 dynamic_cast<const RasterImageData *>(mime))
      raster.reset(data->clone());
    else {
      strokes.reset(StrokesData::fromClipboard(mime));
      if (!strokes) raster.reset(RasterImageData::fromClipboard(mime));
    }
    if (strokes)
      return strokes->m_image && strokes->m_image->getStrokeCount() > 0;
    if (raster) return hasRaster();
    if (mime->hasFormat("application/x-opentoonz-vector-selection-v2") ||
        mime->hasFormat("application/x-opentoonz-raster-selection-v1"))
      return false;
    externalImage = qvariant_cast<QImage>(mime->imageData());
    if (externalImage.isNull())
      externalImage = QApplication::clipboard()->image();
    if (externalImage.isNull()) return false;
    external = true;
    return setExternalImage(externalImage);
  }
};

bool convertDrawingPaste(DrawingPasteData &data, int targetType,
                         TXshSimpleLevel *destination, ToonzScene *scene) {
  int sourceType = data.type();
  if (sourceType == targetType) return true;
  if (targetType == PLI_XSHLEVEL) {
    data.strokes.reset(data.raster->toStrokesData(scene));
    return data.strokes && data.strokes->m_image &&
           data.strokes->m_image->getStrokeCount() > 0;
  }
  if (sourceType == PLI_XSHLEVEL) {
    TPointD dpi = destination ? destination->getDpi()
                              : scene->getCurrentCamera()->getDpi();
    if (dpi.x <= 0.0 || dpi.y <= 0.0) dpi = scene->getCurrentCamera()->getDpi();
    if (targetType == TZP_XSHLEVEL) {
      TToonzImageP image(new TToonzImage(TRasterCM32P(1, 1), TRect()));
      image->setDpi(dpi.x, dpi.y);
      data.raster.reset(data.strokes->toToonzImageData(image));
    } else {
      TRasterImageP image(new TRasterImage(TRaster32P(1, 1)));
      image->setDpi(dpi.x, dpi.y);
      data.raster.reset(data.strokes->toFullColorImageData(image));
    }
    data.strokes.reset();
    return data.hasRaster();
  }
  if (targetType == TZP_XSHLEVEL) {
    const FullColorImageData *color =
        dynamic_cast<const FullColorImageData *>(data.raster.get());
    std::unique_ptr<RasterImageData> converted(color ? color->toToonzImageData()
                                                     : nullptr);
    if (!converted) return false;
    data.raster = std::move(converted);
    return true;
  }
  TPaletteP palette = new TPalette;
  TRasterP raster;
  double dpiX = 0.0, dpiY = 0.0;
  std::vector<TRectD> rects;
  std::vector<TStroke> strokes, originalStrokes;
  TAffine affine;
  data.raster->getData(raster, dpiX, dpiY, rects, strokes, originalStrokes,
                       affine, palette.getPointer());
  TRasterCM32P cm = raster;
  if (!cm) return false;
  TRaster32P rgba(cm->getSize());
  TRop::convert(rgba, cm, palette.getPointer());
  std::unique_ptr<RasterImageData> converted(new FullColorImageData);
  converted->setData(rgba, palette, dpiX, dpiY, data.raster->getDim(), rects,
                     strokes, originalStrokes, affine);
  data.raster = std::move(converted);
  return true;
}

void selectPastedDrawing(const DrawingPasteData &data, const TXshCell &cell,
                         const std::vector<int> &indices) {
  TApp *app   = TApp::instance();
  TTool *tool = app->getCurrentTool()->getTool();
  if (!tool || tool->getName() != "T_Selection") return;
  tool->onActivate();
  if (StrokeSelection *selection =
          dynamic_cast<StrokeSelection *>(tool->getSelection())) {
    selection->selectNone();
    for (int index : indices) selection->select(index, true);
    selection->makeCurrent();
    selection->notifyView();
  } else if (RasterSelection *selection =
                 dynamic_cast<RasterSelection *>(tool->getSelection())) {
    if (!data.raster) return;
    selection->selectNone();
    selection->setCurrentImage(cell.getImage(false), cell);
    selection->setFrameId(cell.getFrameId());
    TRasterP raster;
    double dpiX = 0.0, dpiY = 0.0;
    std::vector<TRectD> rects;
    std::vector<TStroke> strokes, originalStrokes;
    TAffine affine;
    TPaletteP palette = new TPalette;
    data.raster->getData(raster, dpiX, dpiY, rects, strokes, originalStrokes,
                         affine, palette.getPointer());
    TPointD destinationDpi;
    TImageP image = cell.getImage(false);
    if (TToonzImageP ti = image)
      ti->getDpi(destinationDpi.x, destinationDpi.y);
    else if (TRasterImageP ri = image)
      ri->getDpi(destinationDpi.x, destinationDpi.y);
    TPointD cameraDpi =
        app->getCurrentScene()->getScene()->getCurrentCamera()->getDpi();
    auto validDpi = [](double dpi) { return std::isfinite(dpi) && dpi > 0.0; };
    if (!validDpi(cameraDpi.x)) cameraDpi.x = 120.0;
    if (!validDpi(cameraDpi.y)) cameraDpi.y = 120.0;
    if (!validDpi(dpiX)) dpiX = cameraDpi.x;
    if (!validDpi(dpiY)) dpiY = cameraDpi.y;
    if (!validDpi(destinationDpi.x)) destinationDpi.x = cameraDpi.x;
    if (!validDpi(destinationDpi.y)) destinationDpi.y = cameraDpi.y;
    affine = TScale(destinationDpi.x / dpiX, destinationDpi.y / dpiY) * affine;
    for (TStroke &stroke : strokes) {
      stroke.transform(affine, true);
      selection->select(stroke);
    }
    if (strokes.empty())
      for (const TRectD &rect : rects) selection->select(affine * rect);
    selection->makeCurrent();
    selection->notify();
  }
  tool->invalidate();
}

bool pasteDrawingSelectionInCell(int row, int col, bool chooseType) {
  const QMimeData *mime = QApplication::clipboard()->mimeData();
  if (!hasDrawingSelection(mime)) return false;
  TApp *app         = TApp::instance();
  TXsheet *xsh      = app->getCurrentXsheet()->getXsheet();
  ToonzScene *scene = app->getCurrentScene()->getScene();
  if (row < 0 || col < 0) {
    DVGui::warning(
        QObject::tr("Cannot paste a drawing into the camera column."));
    return true;
  }
  TXshColumn *column = xsh->getColumn(col);
  if (column && (column->isLocked() || !column->getLevelColumn())) {
    DVGui::warning(QObject::tr("The destination column cannot be edited."));
    return true;
  }
  DrawingPasteData data;
  try {
    if (!data.read(mime)) {
      DVGui::warning(QObject::tr("The clipboard drawing could not be read."));
      return true;
    }
  } catch (...) {
    DVGui::warning(QObject::tr("The clipboard drawing could not be read."));
    return true;
  }
  TXshCell previous            = xsh->getCell(row, col);
  TXshCell reference           = drawingPasteReference(xsh, row, col);
  TXshSimpleLevel *destination = reference.getSimpleLevel();
  int destinationType          = drawingLevelType(destination);
  int targetType               = destination ? destinationType : data.type();
  if (chooseType || (data.external && destinationType != OVL_XSHLEVEL)) {
    targetType = chooseDrawingPasteType(data.type(), destinationType);
    if (targetType == NO_XSHLEVEL) return true;
  }
  if (scene != app->getCurrentScene()->getScene() ||
      xsh != app->getCurrentXsheet()->getXsheet() ||
      previous != xsh->getCell(row, col) ||
      reference != drawingPasteReference(xsh, row, col)) {
    DVGui::warning(QObject::tr("The destination changed. Please paste again."));
    return true;
  }
  if (targetType != PLI_XSHLEVEL && targetType != TZP_XSHLEVEL &&
      targetType != OVL_XSHLEVEL) {
    DVGui::warning(QObject::tr("A drawing cannot be pasted into this level."));
    return true;
  }
  if (destination && targetType != destinationType) {
    do {
      ++col;
      column = xsh->getColumn(col);
    } while (column && (!column->isEmpty() || column->isLocked() ||
                        !column->getLevelColumn()));
    destination = nullptr;
  }
  if (destination &&
      (destination->isReadOnly() || destination->isSubsequence() ||
       destination->getPath().isUneditable() ||
       (!previous.isEmpty() &&
        destination->isFrameReadOnly(previous.getFrameId())))) {
    DVGui::warning(QObject::tr("The destination level cannot be edited."));
    return true;
  }
  // Ordinary full-color pastes retain their existing conversion restriction.
  if (!chooseType && !data.external && data.type() == OVL_XSHLEVEL &&
      targetType != OVL_XSHLEVEL) {
    DVGui::warning(
        QObject::tr("Use Paste As to convert this Raster selection to another "
                    "level type."));
    return true;
  }
  try {
    // Keep the established fit-to-canvas behavior for external images pasted
    // into an existing Raster level. Native selections retain their geometry.
    if ((targetType == OVL_XSHLEVEL && !data.fitExternalImage(destination)) ||
        !convertDrawingPaste(data, targetType, destination, scene)) {
      DVGui::warning(QObject::tr(
          "The selection could not be converted. No drawing was pasted."));
      return true;
    }
  } catch (...) {
    DVGui::warning(QObject::tr(
        "The selection could not be converted. No drawing was pasted."));
    return true;
  }
  TTool *tool = app->getCurrentTool()->getTool();
  if (tool && tool->getName() == "T_Selection") tool->onDeactivate();
  app->getCurrentColumn()->setColumnIndex(col);
  app->getCurrentColumn()->setColumn(xsh->getColumn(col));
  app->getCurrentFrame()->setFrame(row);
  const TXshCell existing = xsh->getCell(row, col);
  if (data.raster && !existing.isEmpty()) {
    app->getCurrentLevel()->setLevel(existing.getSimpleLevel());
    tool = app->getCurrentTool()->getTool();
    if (tool && tool->getName() == "T_Selection") {
      tool->onActivate();
      if (RasterSelection *selection =
              dynamic_cast<RasterSelection *>(tool->getSelection())) {
        // Keep the pasted pixels floating, so moving them cannot lift
        // artwork that was already underneath them in this drawing.
        if (!selection->pasteData(data.raster.get()))
          DVGui::warning(QObject::tr("The drawing could not be pasted."));
        return true;
      }
    }
  }
  std::vector<int> indices;
  bool success =
      data.strokes ? pasteStrokesInCell(row, col, data.strokes.get(), &indices)
                   : pasteRasterImageInCell(row, col, data.raster.get());
  if (!success) {
    DVGui::warning(QObject::tr("The drawing could not be pasted."));
    return true;
  }
  const TXshCell cell = xsh->getCell(row, col);
  app->getCurrentColumn()->setColumn(xsh->getColumn(col));
  app->getCurrentLevel()->setLevel(cell.getSimpleLevel());
  cell.getSimpleLevel()->setDirtyFlag(true);
  app->getCurrentLevel()->notifyLevelChange();
  app->getCurrentXsheet()->notifyXsheetChanged();
  app->getCurrentScene()->setDirtyFlag(true);
  selectPastedDrawing(data, cell, indices);
  return true;
}

}  // namespace

bool TApp::pasteDrawingSelection(bool chooseType) {
  if (getCurrentObject()->isSpline()) return false;
  if (getCurrentFrame()->isEditingLevel()) {
    if (chooseType) {
      DVGui::warning(
          QObject::tr("Select a destination cell in the Xsheet or Timeline to "
                      "use Paste As."));
      return true;
    }
    return false;
  }
  int row = getCurrentFrame()->getFrame();
  int col = getCurrentColumn()->getColumnIndex();
  if (!chooseType &&
      !getCurrentXsheet()->getXsheet()->getCell(row, col).isEmpty())
    return false;
  return pasteDrawingSelectionInCell(row, col, chooseType);
}

void TCellSelection::pasteAs() {
  if (isEmpty()) return;
  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);
  if (!pasteDrawingSelectionInCell(r0, c0, true))
    DVGui::warning(
        QObject::tr("Copy a drawing selection or image to use Paste As."));
}

void TCellSelection::shiftPaste() {
  // The clipboard can change without reselecting a cell. Resolve its kind
  // when the command runs, not when this selection installs its handlers.
  if (hasDrawingSelection(QApplication::clipboard()->mimeData())) pasteAs();
}

//-----------------------------------------------------------------------------
// Choose pasting behavior by preference option
void TCellSelection::doPaste() {
  // The numbers-only preference applies to copied cells, not drawing
  // selections or images on the clipboard.
  const TCellData *cellData =
      dynamic_cast<const TCellData *>(QApplication::clipboard()->mimeData());
  if (Preferences::instance()->getPasteCellsBehavior() == 0 || !cellData) {
    pasteCells();
    return;
  }

  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);

  XsheetViewer *viewer = TApp::instance()->getCurrentXsheetViewer();
  if (viewer && !viewer->orientation()->isVerticalTimeline()) {
    int cAdj = cellData->getColCount() - 1;
    c0 -= cAdj;
    c1 -= cAdj;
  }

  TXsheet *xsh   = TApp::instance()->getCurrentXsheet()->getXsheet();
  int lastColumn = cellData->getColCount() == 1 && c0 < c1
                       ? c1
                       : c0 + cellData->getColCount() - 1;
  for (int c = c0; c <= lastColumn; ++c) {
    TXshColumn *column = xsh->getColumn(c);
    if (!column || column->isEmpty()) {
      pasteCells();
      return;
    }
  }

  overwritePasteNumbers();
}

//-----------------------------------------------------------------------------

void TCellSelection::pasteCells() {
  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);
  QClipboard *clipboard     = QApplication::clipboard();
  const QMimeData *mimeData = clipboard->mimeData();
  if (!isEmpty() &&
      pasteDrawingSelectionInCell(
          r0, c0,
          QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier)))
    return;
  TXsheet *xsh              = TApp::instance()->getCurrentXsheet()->getXsheet();
  XsheetViewer *viewer      = TApp::instance()->getCurrentXsheetViewer();

  bool initUndo = false;
  const TCellKeyframeData *cellKeyframeData =
      dynamic_cast<const TCellKeyframeData *>(mimeData);

  const TCellData *cellData = dynamic_cast<const TCellData *>(mimeData);
  if (!cellData && cellKeyframeData) cellData = cellKeyframeData->getCellData();
  if (cellData) {
    if (isEmpty())  // If the cell selection is empty, return.
      return;

    if (cellData->getCellCount() == 0) {
      DVGui::error(QObject::tr("No data to paste."));
      return;
    }

    if (viewer && !viewer->orientation()->isVerticalTimeline()) {
      int cAdj = cellData->getColCount() - 1;
      c0 -= cAdj;
      c1 -= cAdj;
    }

    int oldR0 = r0;
    int oldC0 = c0;
    int oldR1 = r1;
    int oldC1 = c1;
    if (!cellData->canChange(xsh, c0)) return;

    // Check Circular References
    int i;
    for (i = 0; i < cellData->getCellCount(); i++) {
      if (!xsh->checkCircularReferences(cellData->getCell(i))) continue;
      DVGui::error(
          QObject::tr("It is not possible to paste the cells: there is a "
                      "circular reference."));
      return;
    }

    // pasting cells may change empty column names. So keep old column names for
    // undo
    std::map<int, std::string> emptyColumnNames;
    if (Preferences::instance()->isLinkColumnNameWithLevelEnabled()) {
      for (int c = c0; c < c0 + cellData->getColCount(); c++) {
        TXshColumn *column = xsh->getColumn(c);
        if (!column || column->isEmpty()) {
          if (!xsh->getStageObject(TStageObjectId::ColumnId(c))
                   ->hasSpecifiedName())
            emptyColumnNames[c] = "";
          else
            emptyColumnNames[c] =
                xsh->getStageObject(TStageObjectId::ColumnId(c))->getName();
        }
      }
    }

    bool isPaste = pasteCellsWithoutUndo(cellData, r0, c0, r1, c1);

    // If the current selection is TCellSelection, select the pasted cells
    TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
        TApp::instance()->getCurrentSelection()->getSelection());
    if (cellSelection) cellSelection->selectCells(r0, c0, r1, c1);

    // Check if the columns were empty before the paste
    // n.b. must do this after paste to get the correct column range
    std::vector<bool> areColumnsEmpty;
    bool containsSoundColumn = false;
    int c;
    for (c = c0; c <= c1; c++) {
      TXshColumn *column = xsh->getColumn(c);
      if (!column) {
        areColumnsEmpty.push_back(false);
        continue;
      }
      if (!containsSoundColumn && column->getSoundColumn())
        containsSoundColumn = true;
      int newCr0, newCr1;
      column->getRange(newCr0, newCr1);
      areColumnsEmpty.push_back(!column || column->isEmpty() ||
                                (newCr0 == r0 && newCr1 == r1));
    }
    if (!isPaste) return;

    initUndo = true;
    TUndoManager::manager()->beginBlock();

    // make sure that the pasting levels are registered in the scene cast
    // it may rename the level if there is another level with the same name
    std::set<TXshLevel *> pastedLevels;
    getLevelSetFromData(cellData, pastedLevels);
    LevelCmd::addMissingLevelsToCast(pastedLevels);

    TUndoManager::manager()->add(new PasteCellsUndo(
        r0, c0, r1, c1, oldR0, oldC0, oldR1, oldC1, areColumnsEmpty,
        containsSoundColumn, emptyColumnNames));
    TApp::instance()->getCurrentScene()->setDirtyFlag(true);
    if (containsSoundColumn)
      TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
  }

  const TKeyframeData *keyframeData =
      dynamic_cast<const TKeyframeData *>(mimeData);
  if (!keyframeData && cellKeyframeData)
    keyframeData = cellKeyframeData->getKeyframeData();
  if (keyframeData) {
    if (keyframeData->m_keyData.empty()) {
      DVGui::error(QObject::tr(
          "It is not possible to paste data: there is nothing to paste."));
      return;
    }
    TKeyframeSelection selection;
    if (isEmpty() && TApp::instance()->getCurrentObject()->getObjectId() ==
                         TStageObjectId::CameraId(xsh->getCameraColumnIndex()))
    // If the selection is empty and the objectId is that of the camera, we are
    // in the camera column so we need to select the current row and -1.
    {
      int row = TApp::instance()->getCurrentFrame()->getFrame();
      selection.select(row, -1);
    } else {
      // Retrieves all keyframe positions from mime data and translates them by
      // (r0,c0)
      std::set<TKeyframeSelection::Position> positions;
      int newC0 = c0;
      if (viewer && !viewer->orientation()->isVerticalTimeline() && !cellData)
        newC0 = c0 - keyframeData->getColumnSpanCount() + 1;
      TKeyframeSelection::Position offset(keyframeData->getKeyframesOffset());
      positions.insert(TKeyframeSelection::Position(r0 + offset.first,
                                                    newC0 + offset.second));
      keyframeData->getKeyframes(positions);
      selection.select(positions);

      if (!cellKeyframeData) {
        // Retrieve the keyframes bbox
        r1 = positions.rbegin()->first;
        c1 = c0;

        std::set<TKeyframeSelection::Position>::const_iterator it,
            end = positions.end();
        for (it = positions.begin(); it != end; ++it)
          c1 = std::max(c1, it->second);
      }
    }
    if (!initUndo) {
      initUndo = true;
      TUndoManager::manager()->beginBlock();
    }
    selection.pasteKeyframesWithShift(r0, r1, c0, c1);
  }

  if (const DrawingData *drawingData =
          dynamic_cast<const DrawingData *>(mimeData)) {
    if (isEmpty())  // If the cell selection is empty, return.
      return;

    std::set<TFrameId> frameIds;
    drawingData->getFrames(frameIds);
    TXshSimpleLevel *level = drawingData->getLevel();
    if (level && !frameIds.empty())
      pasteDrawingsInCellWithoutUndo(xsh, level, frameIds, r0, c0);
    if (!initUndo) {
      initUndo = true;
      TUndoManager::manager()->beginBlock();
    }
    TUndoManager::manager()->add(
        new PasteDrawingsInCellUndo(level, frameIds, r0, c0));
  }
  if (!initUndo) {
    DVGui::error(QObject::tr(
        "It is not possible to paste data: there is nothing to paste."));
    return;
  }
  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TUndoManager::manager()->endBlock();
}

//-----------------------------------------------------------------------------

void TCellSelection::pasteDuplicateCells() {
  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);

  QClipboard *clipboard     = QApplication::clipboard();
  const QMimeData *mimeData = clipboard->mimeData();

  TXsheet *xsh         = TApp::instance()->getCurrentXsheet()->getXsheet();
  XsheetViewer *viewer = TApp::instance()->getCurrentXsheetViewer();

  bool initUndo = false;
  const TCellKeyframeData *cellKeyframeData =
      dynamic_cast<const TCellKeyframeData *>(mimeData);

  const TCellData *cellData = dynamic_cast<const TCellData *>(mimeData);
  if (!cellData && cellKeyframeData) cellData = cellKeyframeData->getCellData();
  if (!cellData) {
    DVGui::error(QObject::tr("There are no copied cells to duplicate."));
    return;
  }
  if (cellData) {
    // go through all the reasons this might fail
    if (isEmpty()) return;

    if (cellData->getCellCount() == 0) {
      DVGui::error(QObject::tr("No data to paste."));
      return;
    }

    // xsheet columns increase in number as they go to the right
    // timeline "columns" (visually rows) increase in number as they go up
    // To paste in the frames the user would expect, we need to adjust the
    // "column" placement if using the timeline.

    if (viewer && !viewer->orientation()->isVerticalTimeline()) {
      int cAdj = cellData->getColCount() - 1;
      c0 -= cAdj;
      c1 -= cAdj;
    }

    // make sure we aren't trying to paste anything in locked columns
    for (int i = 0; i < cellData->getColCount(); i++) {
      TXshColumn *column = xsh->getColumn(c0 + i);
      if (column && column->isLocked()) {
        DVGui::warning(QObject::tr("Cannot paste cells on locked layers."));
        return;
      }
    }

    // make sure the camera column isn't included in the selection
    if (c0 < 0) {
      DVGui::warning(QObject::tr("Can't place drawings on the camera column."));
      return;
    }

    int oldR0 = r0;
    int oldC0 = c0;
    int oldR1 = r1;
    int oldC1 = c1;
    if (!cellData->canChange(xsh, c0)) return;

    // find what cells we need
    int cellCount = cellData->getCellCount();

    // use a pair to ensure that only unique cells are duplicated
    std::set<TXshCell> cells;
    std::vector<TXshCell> cellsVector;
    for (int i = 0; i < cellCount; i++) {
      TXshCell cell = cellData->getCell(i);
      cells.insert(cell);
      cellsVector.push_back(cell);
    }

    // now put the set into a vector since it's easier to deal with
    std::vector<TXshCell> uniqueCells;
    std::set<TXshCell>::iterator it = cells.begin();
    while (it != cells.end()) {
      uniqueCells.push_back(*it);
      it++;
    }

    TXshSimpleLevel *sl;
    TXshLevelP level;

    // check that the selection only includes types that can be duplicated and
    // edited
    it = cells.begin();
    while (it != cells.end()) {
      if (it->isEmpty()) {
        it++;
        continue;
      }
      level = it->m_level;
      if (level) {
        int levelType = level->getType();
        if (levelType == ZERARYFX_XSHLEVEL || levelType == PLT_XSHLEVEL ||
            levelType == SND_XSHLEVEL || levelType == SND_TXT_XSHLEVEL ||
            levelType == MESH_XSHLEVEL) {
          DVGui::warning(
              QObject::tr("Cannot duplicate a drawing in the current column"));
          return;
        } else if (level->getSimpleLevel() &&
                   level->getSimpleLevel()->isReadOnly()) {
          DVGui::warning(
              QObject::tr("Cannot duplicate frames in read only levels"));
          return;
        } else if (level->getSimpleLevel() &&
                   it->getFrameId() == TFrameId::NO_FRAME) {
          DVGui::warning(QObject::tr(
              "Can only duplicate frames in image sequence levels."));
          return;
        }
      }
      it++;
    }

    // now we do the work of duplicating cells

    // start a undo block first
    initUndo = true;
    TUndoManager::manager()->beginBlock();

    // Turn on sync level strip changes with xsheet if it is off
    bool syncXsheetOn = true;
    if (!Preferences::instance()->isSyncLevelRenumberWithXsheetEnabled()) {
      syncXsheetOn = false;
      Preferences::instance()->setValue(syncLevelRenumberWithXsheet, true);
    }

    // now actually duplicate
    std::vector<std::pair<TXshCell, TXshCell>> cellPairs;

    for (int i = 0; i < uniqueCells.size(); i++) {
      level = uniqueCells[i].m_level;
      if (level) {
        sl = uniqueCells[i].getSimpleLevel();
        if (sl) {
          std::set<TFrameId> frames;
          TFrameId fid = uniqueCells[i].getFrameId();
          frames.insert(fid);
          FilmstripCmd::duplicate(sl, frames, true);

          // see if we need to increment the frameId of
          // other cells in the same level
          std::set<int> cellsVectorAlreadyIncremented;
          std::set<int> cellsPairsAlreadyIncremented;
          for (int j = 0; j < uniqueCells.size(); j++) {
            if (j == i) continue;
            TXshSimpleLevel *tempSl;
            TXshLevelP tempLevel;
            tempLevel = uniqueCells[j].m_level;
            if (tempLevel) {
              tempSl = uniqueCells[j].getSimpleLevel();
              if (tempSl) {
                if (tempSl == sl) {
                  TFrameId tempId    = uniqueCells[j].getFrameId();
                  TFrameId currentId = uniqueCells[i].getFrameId();

                  if (tempId.getNumber() > currentId.getNumber()) {
                    tempId = TFrameId(tempId.getNumber() + 1, 0);

                    // keep cellsVector in sync with uniqueCells
                    for (int k = 0; k < cellsVector.size(); k++) {
                      if (cellsVector[k] == uniqueCells[j] &&
                          cellsVectorAlreadyIncremented.find(k) ==
                              cellsVectorAlreadyIncremented.end()) {
                        cellsVector[k].m_frameId = tempId;
                        cellsVectorAlreadyIncremented.insert(k);
                      }
                    }
                    // and with cellPairs
                    for (int k = 0; k < cellPairs.size(); k++) {
                      if (cellPairs[k].first == uniqueCells[j] &&
                          cellsPairsAlreadyIncremented.find(k) ==
                              cellsPairsAlreadyIncremented.end()) {
                        cellPairs[k].first.m_frameId = tempId;
                        cellPairs[k].second.m_frameId =
                            cellPairs[k].second.m_frameId + 1;
                        cellsPairsAlreadyIncremented.insert(k);
                      }
                    }
                    uniqueCells[j].m_frameId = tempId;
                  }
                }
              }
            }
          }
          TXshCell newCell;
          newCell.m_level   = sl;
          newCell.m_frameId = fid + 1;
          cellPairs.push_back(std::make_pair(uniqueCells[i], newCell));
        }
      }
    }

    // turn off sync with xsheet if it wasn't on originally
    if (syncXsheetOn == false) {
      Preferences::instance()->setValue(syncLevelRenumberWithXsheet, false);
    }

    // we should now have a vector with all original cells
    // and a pair vector with the original cell and its duplicate
    // let's make a new cellData populated with the new cells when needed.
    std::vector<TXshCell> newCells;
    for (int i = 0; i < cellsVector.size(); i++) {
      std::vector<std::pair<TXshCell, TXshCell>>::iterator pairIt =
          cellPairs.begin();
      bool found = false;
      while (pairIt != cellPairs.end()) {
        if (cellsVector[i] == pairIt->first) {
          found = true;
          newCells.push_back(pairIt->second);
          break;
        }
        pairIt++;
      }
      if (!found) {
        newCells.push_back(cellsVector[i]);
      }
    }

    TCellData *newCellData = new TCellData(cellData);
    newCellData->replaceCells(newCells);

    assert(newCellData->getCellCount() == cellData->getCellCount());

    bool isPaste = pasteCellsWithoutUndo(newCellData, r0, c0, r1, c1);

    // If the current selection is TCellSelection, select the pasted cells
    TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
        TApp::instance()->getCurrentSelection()->getSelection());
    if (cellSelection) cellSelection->selectCells(r0, c0, r1, c1);

    std::vector<bool> areColumnsEmpty;
    bool containsSoundColumn = false;
    int c;
    for (c = c0; c <= c1; c++) {
      TXshColumn *column = xsh->getColumn(c);
      if (!column) {
        areColumnsEmpty.push_back(false);
        continue;
      }
      if (!containsSoundColumn && column->getSoundColumn())
        containsSoundColumn = true;
      int newCr0, newCr1;
      column->getRange(newCr0, newCr1);
      areColumnsEmpty.push_back(!column || column->isEmpty() ||
                                (newCr0 == r0 && newCr1 == r1));
    }

    if (!isPaste) {
      TUndoManager::manager()->endBlock();
      if (cellPairs.size() > 0) {
        TUndoManager::manager()->undo();
        TUndoManager::manager()->popUndo(1);
      }
      return;
    }

    // make sure that the pasting levels are registered in the scene cast
    // it may rename the level if there is another level with the same name
    std::set<TXshLevel *> pastedLevels;
    getLevelSetFromData(newCellData, pastedLevels);
    LevelCmd::addMissingLevelsToCast(pastedLevels);

    TUndoManager::manager()->add(
        new PasteCellsUndo(r0, c0, r1, c1, oldR0, oldC0, oldR1, oldC1,
                           areColumnsEmpty, containsSoundColumn));
    TApp::instance()->getCurrentScene()->setDirtyFlag(true);
  }

  const TKeyframeData *keyframeData =
      dynamic_cast<const TKeyframeData *>(mimeData);
  if (!keyframeData && cellKeyframeData)
    keyframeData = cellKeyframeData->getKeyframeData();
  if (keyframeData) {
    if (keyframeData->m_keyData.empty()) {
      DVGui::error(QObject::tr(
          "It is not possible to paste data: there is nothing to paste."));
      return;
    }
    TKeyframeSelection selection;
    if (isEmpty() && TApp::instance()->getCurrentObject()->getObjectId() ==
                         TStageObjectId::CameraId(xsh->getCameraColumnIndex()))
    // If the selection is empty and the objectId is that of the camera, we are
    // in the camera column so we need to select the current row and -1.
    {
      int row = TApp::instance()->getCurrentFrame()->getFrame();
      selection.select(row, -1);
    } else {
      // Retrieves all keyframe positions from mime data and translates them
      // by (r0,c0)
      std::set<TKeyframeSelection::Position> positions;
      int newC0 = c0;
      if (viewer && !viewer->orientation()->isVerticalTimeline() && !cellData)
        newC0 = c0 - keyframeData->getColumnSpanCount() + 1;
      TKeyframeSelection::Position offset(keyframeData->getKeyframesOffset());
      positions.insert(TKeyframeSelection::Position(r0 + offset.first,
                                                    newC0 + offset.second));
      keyframeData->getKeyframes(positions);
      selection.select(positions);

      if (!cellKeyframeData) {
        // Retrieve the keyframes bbox
        r1 = positions.rbegin()->first;
        c1 = c0;

        std::set<TKeyframeSelection::Position>::const_iterator it,
            end = positions.end();
        for (it = positions.begin(); it != end; ++it)
          c1 = std::max(c1, it->second);
      }
    }
    if (!initUndo) {
      initUndo = true;
      TUndoManager::manager()->beginBlock();
    }
    selection.pasteKeyframesWithShift(r0, r1, c0, c1);
  }

  if (!initUndo) {
    DVGui::error(QObject::tr("There are no copied cells to duplicate."));
    return;
  }
  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TUndoManager::manager()->endBlock();
  clipboard->clear();
}

//-----------------------------------------------------------------------------

void TCellSelection::deleteCells() {
  // choose clear or remove+shift behavior
  // 0: Clear Cell / Frame
  // 1: Remove and Shift Cells / Frames Up
  bool withShift = Preferences::instance()->getDeleteCommandBehaviour() == 1;
  deleteCells(withShift);
}

//-----------------------------------------------------------------------------

void TCellSelection::deleteCells(bool withShift) {
  if (isEmpty()) return;
  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);
  if (c0 < 0) c0 = 0;  // Ignore camera column
  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  // if with "Clear Cell" behavior and all the selected cells are already
  // empty, then do nothing
  if (!withShift &&
      xsh->isRectEmpty(CellPosition(r0, c0), CellPosition(r1, c1)))
    return;

  std::set<int> removedColIds;
  // check if the operation may remove expression reference as column becomes
  // empty and deleted after the operation.
  if (!checkColumnRemoval(r0, c0, r1, c1, removedColIds)) return;

  // Remove implicate cells for the last Frame
  if (c0 == c1 && xsh->getCell(r1, c0).getFrameId() != TFrameId::EMPTY_FRAME &&
      xsh->getCell(r1 - 1, c0) != xsh->getCell(r1, c0))
    for (; xsh->getCell(r1, c0) == xsh->getCell(r1 + 1, c0); ++r1);

  TCellData *data = new TCellData();
  data->setCells(xsh, r0, c0, r1, c1);

  // clear empty column
  if (!removedColIds.empty()) {
    TUndoManager::manager()->beginBlock();
    // remove, then insert empty column
    for (auto colId : removedColIds) {
      // keep column name if specified
      std::string columnName = "";
      if (Preferences::instance()->isLinkColumnNameWithLevelEnabled() &&
          xsh->getStageObject(TStageObjectId::ColumnId(colId))
              ->hasSpecifiedName())
        columnName =
            xsh->getStageObject(TStageObjectId::ColumnId(colId))->getName();

      ColumnCmd::deleteColumn(colId, true);
      ColumnCmd::insertEmptyColumn(colId);

      if (!columnName.empty())
        TStageObjectCmd::rename(TStageObjectId::ColumnId(colId), columnName,
                                TApp::instance()->getCurrentXsheet());
    }
  }

  // Use unique_ptr to prevent memory leak in case of early return
  std::unique_ptr<DeleteCellsUndo> undo(
      new DeleteCellsUndo(new TCellSelection(m_range), data, withShift));

  deleteCellsWithoutUndo(r0, c0, r1, c1, withShift);

  // Transfer ownership to TUndoManager
  TUndoManager::manager()->add(undo.release());

  if (!removedColIds.empty()) {
    TUndoManager::manager()->endBlock();
  }

  // emit selectionChanged() signal so that the rename field will update
  // accordingly
  if (Preferences::instance()->isUseArrowKeyToShiftCellSelectionEnabled())
    TApp::instance()->getCurrentSelection()->notifySelectionChanged();
  else
    selectNone();

  TApp::instance()->getCurrentScene()->setDirtyFlag(true);
  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
}

//-----------------------------------------------------------------------------

void TCellSelection::cutCells() { cutCells(false); }

//-----------------------------------------------------------------------------

void TCellSelection::cutCells(bool withoutCopy) {
  if (isEmpty()) return;

  std::unique_ptr<CutCellsUndo> undo(
      new CutCellsUndo(new TCellSelection(m_range)));

  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);
  if (c0 < 0) c0 = 0;  // Ignore camera column

  std::set<int> removedColIds;
  // check if the operation may remove expression reference as column becomes
  // empty and deleted after the operation.
  if (!checkColumnRemoval(r0, c0, r1, c1, removedColIds)) {
    return;  // unique_ptr will be automatically deleted
  }

  undo->setCurrentData(r0, c0, r1, c1);
  if (!withoutCopy) copyCellsWithoutUndo(r0, c0, r1, c1);

  // clear empty column
  if (!removedColIds.empty()) {
    TUndoManager::manager()->beginBlock();
    // remove, then insert empty column
    for (auto colId : removedColIds) {
      ColumnCmd::deleteColumn(colId, true);
      ColumnCmd::insertEmptyColumn(colId);
    }
  }

  cutCellsWithoutUndo(r0, c0, r1, c1);

  // Transfer ownership to TUndoManager
  TUndoManager::manager()->add(undo.release());

  if (!removedColIds.empty()) {
    TUndoManager::manager()->endBlock();
  }

  // cutCellsWithoutUndo will clear the selection, so select cells again
  if (Preferences::instance()->isUseArrowKeyToShiftCellSelectionEnabled()) {
    selectCells(r0, c0, r1, c1);
    TApp::instance()->getCurrentSelection()->notifySelectionChanged();
  } else
    selectNone();

  TApp::instance()->getCurrentScene()->setDirtyFlag(true);
  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
}

//-----------------------------------------------------------------------------

void TCellSelection::insertCells() {
  if (isEmpty()) return;
  InsertUndo *undo = new InsertUndo(getSelectedCells());
  undo->redo();
  TUndoManager::manager()->add(undo);
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);
}

//-----------------------------------------------------------------------------

void TCellSelection::pasteKeyframesInto() {
  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  const TKeyframeData *keyframeData = dynamic_cast<const TKeyframeData *>(
      QApplication::clipboard()->mimeData());
  if (keyframeData) {
    int r0, c0, r1, c1;
    getSelectedCells(r0, c0, r1, c1);

    TKeyframeSelection selection;
    if (isEmpty() && TApp::instance()->getCurrentObject()->getObjectId() ==
                         TStageObjectId::CameraId(xsh->getCameraColumnIndex()))
    // If the selection is empty and the objectId is that of the camera, we are
    // in the camera column so we need to select the current row and -1.
    {
      int row = TApp::instance()->getCurrentFrame()->getFrame();
      selection.select(row, -1);
    } else {
      // Retrieves all keyframe positions from mime data and translates them
      // by (r0,c0)
      XsheetViewer *viewer = TApp::instance()->getCurrentXsheetViewer();
      std::set<TKeyframeSelection::Position> positions;
      int newC0 = c0;
      if (viewer && !viewer->orientation()->isVerticalTimeline())
        newC0 = c0 - keyframeData->getColumnSpanCount() + 1;
      positions.insert(TKeyframeSelection::Position(r0, newC0));
      keyframeData->getKeyframes(positions);
      selection.select(positions);
    }

    selection.pasteKeyframes();
  }
}

//-----------------------------------------------------------------------------

void TCellSelection::createBlankDrawing(int row, int col, bool multiple) {
  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();

  if (col < 0) {
    if (!multiple)
      DVGui::warning(
          QObject::tr("Unable to create a blank drawing on the camera column"));
    return;
  }

  TXshColumn *column = xsh->getColumn(col);
  if (column && column->isLocked()) {
    if (!multiple) DVGui::warning(QObject::tr("The current column is locked"));
    return;
  }

  TApp::instance()->getCurrentColumn()->setColumnIndex(col);
  TApp::instance()->getCurrentFrame()->setCurrentFrame(row + 1);

  TXshLevel *level = TApp::instance()->getCurrentLevel()->getLevel();
  if (!level && Preferences::instance()->isAutoCreateEnabled() &&
      Preferences::instance()->isAnimationSheetEnabled()) {
    int r0, r1;
    xsh->getCellRange(col, r0, r1);
    for (int r = std::min(r1, row); r > r0; r--) {
      TXshCell cell = xsh->getCell(r, col);
      if (cell.isEmpty()) continue;
      level = cell.m_level.getPointer();
      if (!level) continue;
      break;
    }
  }
  if (level) {
    int levelType = level->getType();
    if (levelType == ZERARYFX_XSHLEVEL || levelType == PLT_XSHLEVEL ||
        levelType == SND_XSHLEVEL || levelType == SND_TXT_XSHLEVEL ||
        levelType == MESH_XSHLEVEL) {
      if (!multiple)
        DVGui::warning(
            QObject::tr("Cannot create a blank drawing on the current column"));
      return;
    } else if (level->getSimpleLevel() &&
               level->getSimpleLevel()->isReadOnly()) {
      if (!multiple)
        DVGui::warning(QObject::tr("The current level is not editable"));
      return;
    }
  }

  ToolHandle *toolHandle = TApp::instance()->getCurrentTool();

  // If autocreate disabled, let's turn it on temporarily
  bool isAutoCreateEnabled = Preferences::instance()->isAutoCreateEnabled();
  if (!isAutoCreateEnabled)
    Preferences::instance()->setValue(EnableAutocreation, true, false);
  // Enable inserting in the hold cells temporarily too.
  bool isCreationInHoldCellsEnabled =
      Preferences::instance()->isCreationInHoldCellsEnabled();
  if (!isCreationInHoldCellsEnabled)
    Preferences::instance()->setValue(EnableCreationInHoldCells, true, false);

  TImage *img = toolHandle->getTool()->touchImage();

  TXshCell cell       = xsh->getCell(row, col);
  TXshSimpleLevel *sl = cell.getSimpleLevel();

  if (!img || !sl) {
    if (!isAutoCreateEnabled)
      Preferences::instance()->setValue(EnableAutocreation, false, false);
    if (!isCreationInHoldCellsEnabled)
      Preferences::instance()->setValue(EnableCreationInHoldCells, false,
                                        false);
    if (!multiple)
      DVGui::warning(QObject::tr(
          "Unable to create a blank drawing on the current column"));
    return;
  }

  if (!toolHandle->getTool()->m_isFrameCreated) {
    if (!isAutoCreateEnabled)
      Preferences::instance()->setValue(EnableAutocreation, false, false);
    if (!isCreationInHoldCellsEnabled)
      Preferences::instance()->setValue(EnableCreationInHoldCells, false,
                                        false);
    if (!multiple)
      DVGui::warning(QObject::tr(
          "Unable to replace the current drawing with a blank drawing"));
    return;
  }

  sl->setDirtyFlag(true);

  TPalette *palette = sl->getPalette();
  TFrameId frame    = cell.getFrameId();

  CreateBlankDrawingUndo *undo = new CreateBlankDrawingUndo(
      sl, frame, toolHandle->getTool()->m_isLevelCreated, palette);
  TUndoManager::manager()->add(undo);

  IconGenerator::instance()->invalidate(sl, frame);

  // Reset back to what these were
  if (!isAutoCreateEnabled)
    Preferences::instance()->setValue(EnableAutocreation, false, false);
  if (!isCreationInHoldCellsEnabled)
    Preferences::instance()->setValue(EnableCreationInHoldCells, false, false);
}

//-----------------------------------------------------------------------------

void TCellSelection::createBlankDrawings() {
  int col = TApp::instance()->getCurrentColumn()->getColumnIndex();
  int row = TApp::instance()->getCurrentFrame()->getFrameIndex();

  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);

  bool multiple = (r1 - r0 > 1) || (c1 - c0 > 1);

  TUndoManager::manager()->beginBlock();
  for (int c = c0; c <= c1; c++) {
    for (int r = r0; r <= r1; r++) {
      createBlankDrawing(r, c, multiple);
    }
  }
  TUndoManager::manager()->endBlock();

  TApp::instance()->getCurrentColumn()->setColumnIndex(col);
  TApp::instance()->getCurrentFrame()->setCurrentFrame(row + 1);
}

//-----------------------------------------------------------------------------

void TCellSelection::duplicateFrame(int row, int col, bool multiple) {
  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();

  if (col < 0) {
    if (!multiple)
      DVGui::warning(QObject::tr(
          "There are no drawings in the camera column to duplicate"));
    return;
  }

  TXshColumn *column = xsh->getColumn(col);
  if (column && column->isLocked()) {
    if (!multiple) DVGui::warning(QObject::tr("The current column is locked"));
    return;
  }

  TApp::instance()->getCurrentColumn()->setColumnIndex(col);
  TApp::instance()->getCurrentFrame()->setCurrentFrame(row + 1);

  TXshLevel *level = TApp::instance()->getCurrentLevel()->getLevel();
  if (!level && Preferences::instance()->isAutoCreateEnabled() &&
      Preferences::instance()->isAnimationSheetEnabled()) {
    int r0, r1;
    xsh->getCellRange(col, r0, r1);
    for (int r = std::min(r1, row); r > r0; r--) {
      TXshCell cell = xsh->getCell(r, col);
      if (cell.isEmpty()) continue;
      level = cell.m_level.getPointer();
      if (!level) continue;
      break;
    }
  }
  if (level) {
    int levelType = level->getType();
    if (levelType == ZERARYFX_XSHLEVEL || levelType == PLT_XSHLEVEL ||
        levelType == SND_XSHLEVEL || levelType == SND_TXT_XSHLEVEL ||
        levelType == MESH_XSHLEVEL) {
      if (!multiple)
        DVGui::warning(
            QObject::tr("Cannot duplicate a drawing in the current column"));
      return;
    } else if (level->getSimpleLevel() &&
               level->getSimpleLevel()->isReadOnly()) {
      if (!multiple)
        DVGui::warning(QObject::tr("The current level is not editable"));
      return;
    }
  }

  TXshCell targetCell = xsh->getCell(row, col);
  TXshCell prevCell   = xsh->getCell(row - 1, col);
  ;

  // check if we use the current cell to duplicate or the previous cell
  if (!targetCell.isEmpty() && targetCell != prevCell) {
    // Current cell has a drawing to duplicate and it's not a hold shift focus
    // to next cell as if they selected that one to duplicate into
    prevCell = targetCell;
    TApp::instance()->getCurrentFrame()->setCurrentFrame(row + 2);
    row++;
  }

  if (prevCell.isEmpty() || !(prevCell.m_level->getSimpleLevel())) return;

  TXshSimpleLevel *sl = prevCell.getSimpleLevel();
  if (!sl || sl->isSubsequence() || sl->isReadOnly()) return;

  ToolHandle *toolHandle = TApp::instance()->getCurrentTool();

  // If autocreate disabled, let's turn it on temporarily
  bool isAutoCreateEnabled = Preferences::instance()->isAutoCreateEnabled();
  if (!isAutoCreateEnabled)
    Preferences::instance()->setValue(EnableAutocreation, true, false);
  // Enable inserting in the hold cells temporarily too.
  bool isCreationInHoldCellsEnabled =
      Preferences::instance()->isCreationInHoldCellsEnabled();
  if (!isCreationInHoldCellsEnabled)
    Preferences::instance()->setValue(EnableCreationInHoldCells, true, false);

  TImage *img = toolHandle->getTool()->touchImage();
  if (!img) {
    if (!isAutoCreateEnabled)
      Preferences::instance()->setValue(EnableAutocreation, false, false);
    if (!isCreationInHoldCellsEnabled)
      Preferences::instance()->setValue(EnableCreationInHoldCells, false,
                                        false);
    if (!multiple)
      DVGui::warning(
          QObject::tr("Unable to duplicate a drawing on the current column"));
    return;
  }

  bool frameCreated = toolHandle->getTool()->m_isFrameCreated;
  if (!frameCreated) {
    if (!isAutoCreateEnabled)
      Preferences::instance()->setValue(EnableAutocreation, false, false);
    if (!isCreationInHoldCellsEnabled)
      Preferences::instance()->setValue(EnableCreationInHoldCells, false,
                                        false);
    if (!multiple)
      DVGui::warning(
          QObject::tr("Unable to replace the current or next drawing with a "
                      "duplicate drawing"));
    return;
  }

  targetCell           = xsh->getCell(row, col);
  TPalette *palette    = sl->getPalette();
  TFrameId srcFrame    = prevCell.getFrameId();
  TFrameId targetFrame = targetCell.getFrameId();
  std::set<TFrameId> frames;

  FilmstripCmd::duplicateFrameWithoutUndo(sl, srcFrame, targetFrame);

  TApp::instance()->getCurrentLevel()->notifyLevelChange();

  DuplicateDrawingUndo *undo =
      new DuplicateDrawingUndo(sl, srcFrame, targetFrame);
  TUndoManager::manager()->add(undo);

  if (!isAutoCreateEnabled)
    Preferences::instance()->setValue(EnableAutocreation, false, false);
  if (!isCreationInHoldCellsEnabled)
    Preferences::instance()->setValue(EnableCreationInHoldCells, false, false);
}

//-----------------------------------------------------------------------------

void TCellSelection::duplicateFrames() {
  int col = TApp::instance()->getCurrentColumn()->getColumnIndex();
  int row = TApp::instance()->getCurrentFrame()->getFrameIndex();

  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);

  bool multiple = (r1 - r0 > 1) || (c1 - c0 > 1);

  TUndoManager::manager()->beginBlock();
  for (int c = c0; c <= c1; c++) {
    for (int r = r0; r <= r1; r++) {
      duplicateFrame(r, c, multiple);
    }
  }
  TUndoManager::manager()->endBlock();

  if (multiple) {
    TApp::instance()->getCurrentColumn()->setColumnIndex(col);
    TApp::instance()->getCurrentFrame()->setCurrentFrame(row + 1);
  }
}

//-----------------------------------------------------------------------------

void TCellSelection::openTimeStretchPopup() {
  if (!m_timeStretchPopup) m_timeStretchPopup = new TimeStretchPopup();
  m_timeStretchPopup->show();
  m_timeStretchPopup->raise();
  m_timeStretchPopup->activateWindow();
}

//-----------------------------------------------------------------------------

static bool operator<(const TXshCell &a, const TXshCell &b) {
  if (a.getSimpleLevel() < b.getSimpleLevel())
    return true;
  else if (a.getSimpleLevel() > b.getSimpleLevel())
    return false;
  return a.m_frameId < b.m_frameId;
}

//-----------------------------------------------------------------------------

// Renumber cells function
static void dRenumberCells(int col, int r0, int r1) {
  typedef std::vector<std::pair<TFrameId, TFrameId>> FramesMap;
  typedef std::map<TXshCell, TXshCell> CellsMap;
  typedef std::map<TXshSimpleLevel *, FramesMap> LevelsTable;

  LevelsTable levelsTable;
  CellsMap cellsMap;

  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  for (int r = r0; r <= r1; ++r) {
    TXshCell cell = xsh->getCell(r, col);
    if (!cell.isEmpty() && cell.getSimpleLevel() &&
        (r <= 0 || xsh->getCell(r - 1, col) != cell)) {
      TXshCell &toCell = cellsMap[cell];
      if (!toCell.isEmpty()) continue;

      TXshSimpleLevel *sl = cell.getSimpleLevel();  // Get raw pointer

      TFrameId oldFid = cell.getFrameId();
      TFrameId newFid =
          TFrameId(r + 1, 0, oldFid.getZeroPadding(), oldFid.getStartSeqInd());

      // Convert TXshSimpleLevel* to TXshLevelP (base class smart pointer)
      toCell.m_level   = TXshLevelP(sl);  // This constructor should exist
      toCell.m_frameId = newFid;

      // Check if sl is not null before using it
      if (sl && sl->isFid(oldFid))
        levelsTable[sl].push_back(std::make_pair(oldFid, newFid));
    }
  }
  auto getNextLetter = [](const QString &letter) {
    if (letter.isEmpty()) return QString('a');
    QByteArray byteArray = letter.toUtf8();
    // return incrementing the last letter
    byteArray.data()[byteArray.size() - 1]++;
    return QString::fromUtf8(byteArray);
  };

  // Ensure renumber consistency in case some destination fid would overwrite
  // some unrenumbered fid in the level
  {
    CellsMap::iterator it, end = cellsMap.end();
    for (it = cellsMap.begin(); it != end; ++it) {
      if (cellsMap.find(it->second) == cellsMap.end() &&
          it->first.getSimpleLevel() &&
          it->first.getSimpleLevel()->isFid(it->second.getFrameId())) {
        TFrameId &fid = it->second.m_frameId;
        fid = TFrameId(fid.getNumber(), getNextLetter(fid.getLetter()),
                       fid.getZeroPadding(), fid.getStartSeqInd());
      }
    }
  }

  // Renumber the level strip of each found level
  {
    LevelsTable::iterator it, end = levelsTable.end();
    for (it = levelsTable.begin(); it != end; ++it)
      FilmstripCmd::renumber(it->first, it->second, true);
  }
}

//-----------------------------------------------------------------------------

void TCellSelection::dRenumberCells() {
  TUndoManager *undoManager = TUndoManager::manager();

  undoManager->beginBlock();
  undoManager->add(new RenumberUndo::UndoNotifier);

  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);
  for (int c = c1; c >= c0; --c) ::dRenumberCells(c, r0, r1);

  undoManager->add(new RenumberUndo::RedoNotifier);
  undoManager->endBlock();

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);
}

//-----------------------------------------------------------------------------

// Paste new cell undo class
class PasteNewCellUndo final : public TUndo {
  TXshCell m_oldCell, m_newCell;
  TImageP m_img;
  int m_row, m_col;
  bool m_levelCreated;

public:
  PasteNewCellUndo(int row, int col, bool levelCreated)
      : m_row(row), m_col(col), m_levelCreated(levelCreated) {
    m_oldCell = getXsheet()->getCell(m_row, m_col);
  }
  void onAdd() override {
    m_newCell   = getXsheet()->getCell(m_row, m_col);
    TImageP img = m_newCell.getImage(false);
    if (img) m_img = img->cloneImage();
  }
  TXsheet *getXsheet() const {
    return TApp::instance()->getCurrentXsheet()->getXsheet();
  }
  TLevelSet *getLevelSet() const {
    return TApp::instance()->getCurrentScene()->getScene()->getLevelSet();
  }

  void undo() const override {
    getXsheet()->setCell(m_row, m_col, m_oldCell);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    TXshSimpleLevel *sl = m_newCell.getSimpleLevel();
    if (sl) {
      sl->eraseFrame(m_newCell.getFrameId());
      if (m_levelCreated) {
        getLevelSet()->removeLevel(sl);
        TApp::instance()->getCurrentScene()->notifyCastChange();
      } else {
        TApp::instance()->getCurrentLevel()->notifyLevelChange();
      }
    }
  }
  void redo() const override {
    getXsheet()->setCell(m_row, m_col, m_newCell);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    TXshSimpleLevel *sl = m_newCell.getSimpleLevel();
    if (sl) {
      TFrameId fid = m_newCell.getFrameId();
      sl->setFrame(fid, m_img);
      if (m_levelCreated) {
        getLevelSet()->insertLevel(sl);
        IconGenerator::instance()->invalidate(sl, fid);
        TApp::instance()->getCurrentScene()->notifyCastChange();
      } else {
        TApp::instance()->getCurrentLevel()->notifyLevelChange();
      }
    }
  }
  int getSize() const override {
    return sizeof(*this) + sizeof(TXshLevel);  // imprecise.
  }
};

//-----------------------------------------------------------------------------

// TODO: move these functions to a more general and reusable place

static int getLevelType(const TImageP &img) {
  if (img->getType() == TImage::RASTER)
    return OVL_XSHLEVEL;
  else if (img->getType() == TImage::VECTOR)
    return PLI_XSHLEVEL;
  else if (img->getType() == TImage::TOONZ_RASTER)
    return TZP_XSHLEVEL;
  else
    return NO_XSHLEVEL;
}

//-----------------------------------------------------------------------------
/*
void setNewDrawing(TXsheet *xsh, int row, int col, const TImageP &img)
{
  // find a level type suitable for the image

  // search for the level
  TXshCell old = xsh->getCell(row,col);
  if(old.isEmpty())
  {
    for(int rr=row-1;rr>=0;rr--)
    {
      old = xsh->getCell(rr,col);
      if(!old.isEmpty())
        break;
    }
  }
  bool levelCreated = false;
  TXshSimpleLevel *sl;
  if(old.isEmpty())
  {
    // no level found. create it
    TXshLevel *xl = xsh->getScene()->createNewLevel(levelType);
    TApp::instance()->getCurrentScene()->notifyCastChange();
    levelCreated = true;
    sl = xl->getSimpleLevel();
  }
  else
  {
    // found a level. check its type
    sl = old.getSimpleLevel();
    if(sl->getType() != levelType)
      return;
  }
  // compute the frameid
  TFrameId fid(row+1);
  if(sl->isFid(fid))
  {
    fid = TFrameId(fid.getNumber(), 'a');
    while(fid.getLetter()<'z' && sl->isFid(fid))
      fid = TFrameId(fid.getNumber(), fid.getLetter()+1);
  }
  // add the new frame
  sl->setFrame(fid, img->cloneImage());
  TApp::instance()->getCurrentLevel()->notifyLevelChange();
  // undo
  TUndo *undo = new PasteNewCellUndo(row,col,levelCreated);
  // set xsheet cell
  xsh->setCell(row,col,TXshCell(sl,fid));
  TUndoManager::manager()->add(undo);
}
*/

//-----------------------------------------------------------------------------

// Create new drawing function
static void createNewDrawing(TXsheet *xsh, int row, int col,
                             int preferredLevelType) {
  // search for the level
  TXshCell old = xsh->getCell(row, col);
  if (old.isEmpty()) {
    for (int rr = row - 1; rr >= 0; rr--) {
      old = xsh->getCell(rr, col);
      if (!old.isEmpty()) break;
    }
  }
  bool levelCreated = false;
  TXshSimpleLevel *sl;
  if (old.isEmpty()) {
    // no level found. create it
    TXshLevel *xl = xsh->getScene()->createNewLevel(preferredLevelType);
    sl            = xl->getSimpleLevel();
    TApp::instance()->getCurrentScene()->notifyCastChange();
    levelCreated = true;
  } else {
    sl = old.getSimpleLevel();
  }
  // compute the frameid
  TFrameId fid(row + 1);
  if (sl->isFid(fid)) {
    fid = TFrameId(fid.getNumber(), 'a');
    while (fid.getLetter().toUtf8().at(0) < 'z' && sl->isFid(fid))
      fid = TFrameId(fid.getNumber(), fid.getLetter().toUtf8().at(0) + 1);
  }
  // modify frameId to be with the same frame format as existing frames
  TFrameId tmplFId =
      xsh->getScene()->getProperties()->formatTemplateFIdForInput();
  sl->formatFId(fid, tmplFId);
  // add the new frame
  sl->setFrame(fid, sl->createEmptyFrame());
  TApp::instance()->getCurrentLevel()->notifyLevelChange();
  // undo
  TUndo *undo = new PasteNewCellUndo(row, col, levelCreated);
  // set xsheet cell
  xsh->setCell(row, col, TXshCell(sl, fid));
  TUndoManager::manager()->add(undo);
}

//-----------------------------------------------------------------------------

#define DYNAMIC_CAST(Type, Var, Data)                                          \
  const Type *Var = dynamic_cast<const Type *>(Data)

void TCellSelection::dPasteCells() {
  if (isEmpty()) return;
  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);
  TUndoManager::manager()->beginBlock();
  TXsheet *xsh              = TApp::instance()->getCurrentXsheet()->getXsheet();
  QClipboard *clipboard     = QApplication::clipboard();
  const QMimeData *mimeData = clipboard->mimeData();
  std::unique_ptr<StrokesData> transferredStrokes;
  if (!dynamic_cast<const StrokesData *>(mimeData))
    transferredStrokes.reset(StrokesData::fromClipboard(mimeData));
  if (DYNAMIC_CAST(TCellData, cellData, mimeData)) {
    if (!cellData->canChange(xsh, c0)) {
      TUndoManager::manager()->endBlock();
      return;
    }
    for (int c = 0; c < cellData->getColCount(); c++) {
      for (int r = 0; r < cellData->getRowCount(); r++) {
        TXshCell src = cellData->getCell(r, c);
        if (src.getSimpleLevel())
          createNewDrawing(xsh, r0 + r, c0 + c,
                           src.getSimpleLevel()->getType());
      }
    }
  } else if (DYNAMIC_CAST(DrawingData, drawingData, mimeData)) {
    TXshSimpleLevel *level = drawingData->getLevel();
    if (level) {
      std::set<TFrameId> frameIds;
      drawingData->getFrames(frameIds);
      for (int i = 0; i < frameIds.size(); ++i)
        createNewDrawing(xsh, r0 + i, c0, level->getType());
    }
  } else if (dynamic_cast<const StrokesData *>(mimeData) ||
             transferredStrokes) {
    createNewDrawing(xsh, r0, c0, PLI_XSHLEVEL);
  } else if (DYNAMIC_CAST(ToonzImageData, toonzImageData, mimeData)) {
    createNewDrawing(xsh, r0, c0, TZP_XSHLEVEL);
  } else if (DYNAMIC_CAST(FullColorImageData, fullColorImageData, mimeData)) {
    createNewDrawing(xsh, r0, c0, OVL_XSHLEVEL);
  } else {
  }
  pasteCells();
  TUndoManager::manager()->endBlock();
}

//-----------------------------------------------------------------------------
/*-- Overwrite Paste Cells --*/
void TCellSelection::overWritePasteCells() {
  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);

  QClipboard *clipboard     = QApplication::clipboard();
  const QMimeData *mimeData = clipboard->mimeData();

  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  if (const TCellData *cellData = dynamic_cast<const TCellData *>(mimeData)) {
    if (isEmpty())  // If the cell selection is empty, return.
      return;

    if (cellData->getCellCount() == 0) {
      DVGui::error(QObject::tr("No data to paste."));
      return;
    }

    XsheetViewer *viewer = TApp::instance()->getCurrentXsheetViewer();
    if (viewer && !viewer->orientation()->isVerticalTimeline()) {
      int cAdj = cellData->getColCount() - 1;
      c0 -= cAdj;
      c1 -= cAdj;
    }

    int oldR0 = r0;
    int oldC0 = c0;
    int oldR1 = r1;
    int oldC1 = c1;

    /*-- Check if each destination column is compatible with the corresponding
     * copied cell --*/
    if (!cellData->canChange(xsh, c0)) return;

    // Check Circular References
    int i;
    for (i = 0; i < cellData->getCellCount(); i++) {
      if (!xsh->checkCircularReferences(cellData->getCell(i))) continue;
      DVGui::error(
          QObject::tr("It is not possible to paste the cells: there is a "
                      "circular reference."));
      return;
    }

    /*---- Save CellData before pasting for Undo --*/
    r1                    = r0 + cellData->getRowCount() - 1;
    c1                    = c0 + cellData->getColCount() - 1;
    TCellData *beforeData = new TCellData();
    beforeData->setCells(xsh, r0, c0, r1, c1);

    /*-- Set Insert to False to perform Overwrite paste
            r1,c1 are resized according to the paste range
    --*/
    bool isPaste = pasteCellsWithoutUndo(r0, c0, r1, c1, false);

    if (!isPaste) {
      delete beforeData;
      return;
    }

    /*-- For each column, check if the column was empty before paste --*/
    std::vector<bool> areColumnsEmpty;
    bool containsSoundColumn = false;
    int c;
    /*-- For each column after pasting --*/
    for (c = c0; c <= c1; c++) {
      TXshColumn *column = xsh->getColumn(c);
      if (!column) {
        areColumnsEmpty.push_back(false);
        continue;
      }
      if (!containsSoundColumn && column->getSoundColumn())
        containsSoundColumn = true;
      int newCr0, newCr1;
      /*-- Check if something was pasted into the new column --*/
      column->getRange(newCr0, newCr1);
      areColumnsEmpty.push_back(!column || column->isEmpty() ||
                                (newCr0 == r0 && newCr1 == r1));
    }
    TUndoManager::manager()->beginBlock();

    // make sure that the pasting levels are registered in the scene cast
    // it may rename the level if there is another level with the same name
    std::set<TXshLevel *> pastedLevels;
    getLevelSetFromData(cellData, pastedLevels);
    LevelCmd::addMissingLevelsToCast(pastedLevels);

    /*-- r0,c0,r1,c1 is the pasted range, old ones are the selection range
     * before paste --*/
    TUndoManager::manager()->add(new OverwritePasteCellsUndo(
        r0, c0, r1, c1, oldR0, oldC0, oldR1, oldC1, areColumnsEmpty, beforeData,
        containsSoundColumn));
    TUndoManager::manager()->endBlock();
    TApp::instance()->getCurrentScene()->setDirtyFlag(true);
    if (containsSoundColumn)
      TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();

    delete beforeData;
  } else
    DVGui::error(QObject::tr("Cannot paste data \n Nothing to paste"));

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
}

//-----------------------------------------------------------------------------
// Special paste command - overwrite paste cell numbers only

void TCellSelection::overwritePasteNumbers() {
  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);
  if (c0 < 0) c0 = 0;  // Ignore camera column

  QClipboard *clipboard     = QApplication::clipboard();
  const QMimeData *mimeData = clipboard->mimeData();

  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  if (const TCellData *cellData = dynamic_cast<const TCellData *>(mimeData)) {
    if (isEmpty()) return;

    if (cellData->getCellCount() == 0) {
      DVGui::error(QObject::tr("No data to paste."));
      return;
    }

    XsheetViewer *viewer = TApp::instance()->getCurrentXsheetViewer();
    if (viewer && !viewer->orientation()->isVerticalTimeline()) {
      int cAdj = cellData->getColCount() - 1;
      c0 -= cAdj;
      c1 -= cAdj;
    }

    int oldR0 = r0;
    int oldC0 = c0;
    int oldR1 = r1;
    int oldC1 = c1;

    // Check if the copied cells are the same type as the target columns
    if (!cellData->canChange(xsh, c0)) {
      DVGui::error(
          QObject::tr("It is not possible to paste the cells: "
                      "Some column is locked or column type is not match."));
      return;
    }

    // Check Circular References
    int i;
    for (i = 0; i < cellData->getCellCount(); i++) {
      if (!xsh->checkCircularReferences(cellData->getCell(i))) continue;
      DVGui::error(
          QObject::tr("It is not possible to paste the cells: there is a "
                      "circular reference."));
      return;
    }

    // store celldata for undo
    r1 = r0 + cellData->getRowCount() - 1;
    if (cellData->getColCount() != 1 || c0 == c1)
      c1 = c0 + cellData->getColCount() - 1;
    TCellData *beforeData = new TCellData();
    beforeData->setCells(xsh, r0, c0, r1, c1);

    bool isPaste = pasteNumbersWithoutUndo(r0, c0, r1, c1);

    if (!isPaste) {
      delete beforeData;
      return;
    }

    // r0,c0,r1,c1 : pasted region  oldR0,oldC0,oldR1,oldC1 : selected area
    // before pasting
    TUndoManager::manager()->add(new OverwritePasteNumbersUndo(
        r0, c0, r1, c1, oldR0, oldC0, oldR1, oldC1, beforeData));
    TApp::instance()->getCurrentScene()->setDirtyFlag(true);

    delete beforeData;
  } else
    DVGui::error(QObject::tr("Cannot paste data \n Nothing to paste"));

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
}

//-----------------------------------------------------------------------------
// Called from RenameCellField::RenameCell

void TCellSelection::renameCells(TXshCell &cell) {
  if (isEmpty()) return;
  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);
  // register undo only if the cell is modified
  if (checkIfCellsHaveTheSameContent(r0, c0, r1, c1, cell)) return;
  TCellData *data = new TCellData();
  TXsheet *xsh    = TApp::instance()->getCurrentXsheet()->getXsheet();
  data->setCells(xsh, r0, c0, r1, c1);
  RenameCellsUndo *undo = new RenameCellsUndo(this, data, cell);
  undo->redo();

  TUndoManager::manager()->add(undo);
}

//-----------------------------------------------------------------------------
// Rename cells for each columns with correspondent item in the list

void TCellSelection::renameMultiCells(QList<TXshCell> &cells) {
  if (isEmpty()) return;
  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);
  assert(cells.size() == c1 - c0 + 1);
  // register undo only if the cell is modified
  bool somethingChanged = false;
  for (int c = c0; c <= c1; c++) {
    somethingChanged =
        !checkIfCellsHaveTheSameContent(r0, c, r1, c, cells[c - c0]);
    if (somethingChanged) break;
  }
  if (!somethingChanged) return;

  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  TUndoManager::manager()->beginBlock();
  for (int c = c0; c <= c1; c++) {
    TCellData *data = new TCellData();
    data->setCells(xsh, r0, c, r1, c);
    RenameCellsUndo *undo =
        new RenameCellsUndo(r0, c, r1, c, data, cells[c - c0]);
    undo->redo();
    TUndoManager::manager()->add(undo);
  }
  TUndoManager::manager()->endBlock();
}

//-----------------------------------------------------------------------------
// Convert selected vector cells to ToonzRaster

bool TCellSelection::areOnlyVectorCellsSelected() {
  // set up basics
  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);
  int i;
  ToonzScene *scene = TApp::instance()->getCurrentScene()->getScene();
  TXsheet *xsh      = scene->getXsheet();

  TXshCell firstCell = xsh->getCell(r0, c0);
  if (firstCell.isEmpty()) {
    DVGui::error(QObject::tr("This command only works on vector cells."));
    return false;
  }

  TXshSimpleLevel *sourceSl = firstCell.getSimpleLevel();
  if (!sourceSl || sourceSl->getType() != PLI_XSHLEVEL) {
    DVGui::error(QObject::tr("This command only works on vector cells."));
    return false;
  }

  if (c0 != c1) {
    DVGui::error(
        QObject::tr("Please select only one column for this command."));
    return false;
  }

  for (i = r0; i <= r1; i++) {
    TXshCell testCell = xsh->getCell(i, c0);
    if (testCell.isEmpty() || testCell.getSimpleLevel() != sourceSl) {
      DVGui::error(
          QObject::tr("All selected cells must belong to the same level."));
      return false;
    }
  }
  return true;
}

// Get new ToonzRaster level
TXshSimpleLevel *TCellSelection::getNewToonzRasterLevel(
    TXshSimpleLevel *sourceSl) {
  ToonzScene *scene       = TApp::instance()->getCurrentScene()->getScene();
  TFilePath sourcePath    = sourceSl->getPath();
  std::wstring sourceName = sourcePath.getWideName();
  TFilePath parentDir     = sourceSl->getPath().getParentDir();
  TFilePath fp            = scene->getDefaultLevelPath(TZP_XSHLEVEL, sourceName)
                     .withParentDir(parentDir);
  TFilePath actualFp = scene->decodeFilePath(fp);

  int i                = 1;
  std::wstring newName = sourceName;
  while (TSystem::doesExistFileOrLevel(actualFp)) {
    newName = sourceName + QString::number(i).toStdWString();
    fp      = scene->getDefaultLevelPath(TZP_XSHLEVEL, newName)
             .withParentDir(parentDir);
    actualFp = scene->decodeFilePath(fp);
    i++;
  }
  parentDir = scene->decodeFilePath(parentDir);

  TXshLevel *level =
      scene->createNewLevel(TZP_XSHLEVEL, newName, TDimension(), 0, fp);
  TXshSimpleLevel *sl = dynamic_cast<TXshSimpleLevel *>(level);
  return sl;
}

//=============================================================================
// VectorToVectorUndo
//-----------------------------------------------------------------------------

class VectorToVectorUndo final : public TUndo {
  TXshSimpleLevelP m_sl;
  std::vector<TFrameId> m_frameIds;
  // std::vector<TFrameId> m_newFrameIds;
  std::vector<TImageP> m_oldImages;
  std::vector<TImageP> m_newImages;
  HookSet *m_oldLevelHooks;

public:
  VectorToVectorUndo(TXshSimpleLevelP sl, std::vector<TFrameId> frameIds,
                     std::vector<TImageP> oldImages,
                     std::vector<TImageP> newImages, HookSet *oldLevelHooks)
      : m_sl(sl)
      , m_frameIds(frameIds)
      , m_oldImages(oldImages)
      , m_newImages(newImages)
      , m_oldLevelHooks(oldLevelHooks) {}

  ~VectorToVectorUndo() {}

  void undo() const override {
    for (int i = 0; i < m_frameIds.size(); i++) {
      m_sl->getSimpleLevel()->setFrame(m_frameIds[i], m_oldImages[i]);
    }
    TApp::instance()->getCurrentLevel()->notifyLevelChange();

    // update all icons
    std::vector<TFrameId> sl_fids;
    m_sl.getPointer()->getFids(sl_fids);
    invalidateIcons(m_sl.getPointer(), sl_fids);

    *m_sl->getHookSet() = *m_oldLevelHooks;
  }

  void redo() const override {
    for (int i = 0; i < m_frameIds.size(); i++) {
      m_sl->getSimpleLevel()->setFrame(m_frameIds[i], m_newImages[i]);
    }
    TApp::instance()->getCurrentLevel()->notifyLevelChange();

    // update all icons
    std::vector<TFrameId> sl_fids;
    m_sl.getPointer()->getFids(sl_fids);
    invalidateIcons(m_sl.getPointer(), m_frameIds);

    *m_sl->getHookSet() = *m_oldLevelHooks;
  }

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override {
    QString str = QObject::tr("Simplify Vectors : Level %1")
                      .arg(QString::fromStdWString(m_sl->getName()));
    return str;
  }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//-----------------------------------------------------------------------------
// Convert selected vector cells to ToonzRaster and back to vector

void TCellSelection::convertVectortoVector() {
  // set up basics
  int r0, c0, r1, c1;
  getSelectedCells(r0, c0, r1, c1);

  TApp *app = TApp::instance();
  int row   = app->getCurrentFrame()->getFrame();
  int col   = app->getCurrentColumn()->getColumnIndex();
  int i;

  ToonzScene *scene = app->getCurrentScene()->getScene();
  TXsheet *xsh      = scene->getXsheet();

  TXshCell firstCell        = xsh->getCell(r0, c0);
  TXshSimpleLevel *sourceSl = firstCell.getSimpleLevel();
  if (!areOnlyVectorCellsSelected()) return;

  // set up new level name
  TXshSimpleLevel *sl = getNewToonzRasterLevel(sourceSl);
  assert(sl);

  // get camera settings
  TCamera *camera = app->getCurrentScene()->getScene()->getCurrentCamera();
  double dpi      = camera->getDpi().x;
  int xres        = camera->getRes().lx;
  int yres        = camera->getRes().ly;

  if (Preferences::instance()->getUseHigherDpiOnVectorSimplify()) {
    dpi *= 2;
    xres *= 2;
    yres *= 2;
  }

  sl->getProperties()->setDpiPolicy(LevelProperties::DP_ImageDpi);
  sl->getProperties()->setDpi(dpi);
  sl->getProperties()->setImageDpi(TPointD(dpi, dpi));
  sl->getProperties()->setImageRes(TDimension(xres, yres));

  // Get the used FrameIds and images
  // The cloned images are used in the undo.
  TFrameId frameId = firstCell.getFrameId();
  std::set<TFrameId> frameIdsSet;
  std::vector<TFrameId> frameIds;
  std::vector<TImageP> oldImages;
  frameIds.push_back(frameId);
  oldImages.push_back(firstCell.getImage(false).getPointer()->cloneImage());
  for (i = r0 + 1; i <= r1; i++) {
    TXshCell newCell    = xsh->getCell(i, c0);
    TFrameId newFrameId = xsh->getCell(i, c0).getFrameId();
    if (newCell.getSimpleLevel() == sourceSl) {
      if (std::find(frameIds.begin(), frameIds.end(), newCell.getFrameId()) ==
          frameIds.end()) {
        frameIds.push_back(newFrameId);
        oldImages.push_back(newCell.getImage(false)->cloneImage());
      }
    }
  }

  int totalImages = frameIds.size();

  for (i = 0; i < totalImages; i++) {
    frameIdsSet.insert(frameIds[i]);
  }

  DrawingData *data = new DrawingData();
  data->setLevelFrames(sourceSl, frameIdsSet);

  // Make empty frames for the new data
  std::set<TFrameId> newFrameIds;
  for (i = 0; i < totalImages; i++) {
    TRasterCM32P raster(xres, yres);
    raster->fill(TPixelCM32());
    TToonzImageP firstImage(raster, TRect());
    firstImage->setDpi(dpi, dpi);
    TFrameId newFrameId = TFrameId(i + 1);
    sl->setFrame(newFrameId, firstImage);
    newFrameIds.insert(newFrameId);
    firstImage->setSavebox(TRect(0, 0, xres - 1, yres - 1));
  }

  // This is where the copying actually happens
  // copy old frames to Toonz Raster
  bool keepOriginalPalette;
  bool success = data->getLevelFrames(
      sl, newFrameIds, DrawingData::OVER_SELECTION, true, keepOriginalPalette,
      true);  // setting is redo = true skips the
  // question about the palette
  // get the new FrameIds
  std::vector<TFrameId> newFids;
  sl->getFids(newFids);

  // copy the Toonz Raster frames onto the old level
  data->clear();
  data->setLevelFrames(sl, newFrameIds);
  if (Preferences::instance()->getKeepFillOnVectorSimplify())
    data->setKeepVectorFills(true);
  success = data->getLevelFrames(
      sourceSl, frameIdsSet, DrawingData::OVER_SELECTION, true,
      keepOriginalPalette, true);  // setting is redo = true skips the
  // question about the palette

  // get clones of the new images for undo
  std::vector<TImageP> newImages;
  for (i = 0; i < totalImages; i++) {
    newImages.push_back(sourceSl->getFrame(frameIds[i], false)->cloneImage());
  }

  HookSet *oldLevelHooks = new HookSet();
  *oldLevelHooks         = *sourceSl->getHookSet();

  TUndoManager::manager()->add(new VectorToVectorUndo(
      sourceSl, frameIds, oldImages, newImages, oldLevelHooks));

  invalidateIcons(sourceSl, frameIdsSet);

  // remove the toonz raster level
  sl->clearFrames();
  app->getCurrentScene()->getScene()->getLevelSet()->removeLevel(sl, true);

  app->getCurrentScene()->notifySceneChanged();
  app->getCurrentScene()->notifyCastChange();
  app->getCurrentXsheet()->notifyXsheetChanged();

  app->getCurrentTool()->onImageChanged(
      (TImage::Type)app->getCurrentImageType());
}

// Fill empty cells function
void TCellSelection::fillEmptyCell() {
  if (isEmpty()) return;

  // set up basics
  bool initUndo = false;
  TXsheet *xsh  = TApp::instance()->getCurrentXsheet()->getXsheet();
  int r, r0, c0, c, r1, c1;

  getSelectedCells(r0, c0, r1, c1);

  for (c = c0; c <= c1; c++) {
    TXshColumn *column = xsh->getColumn(c);
    if (!column || column->isEmpty() || column->isLocked() ||
        column->getSoundColumn())
      continue;

    for (r = r1; r >= r0; r--) {
      int fillCount = 0;
      TXshCell cell = xsh->getCell(r, c);

      if (cell.isEmpty()) fillCount++;

      int prevR = r - 1;
      while (prevR >= 0) {
        cell = xsh->getCell(prevR, c);
        if (!cell.isEmpty()) break;

        prevR--;
        fillCount++;
      }

      // We've gone past the beginning of timeline without finding a cell.
      // Do nothing, and end current column processing immediately.
      if (prevR < 0) break;

      // Adjacent cell is filled.  Move to next one.
      if (!fillCount) continue;

      // Ok, time fill
      int startR = prevR + 1;
      int endR   = startR + fillCount - 1;

      if (!initUndo) {
        initUndo = true;
        TUndoManager::manager()->beginBlock();
      }

      FillEmptyCellUndo *undo = new FillEmptyCellUndo(startR, endR, c, cell);
      TUndoManager::manager()->add(undo);
      undo->redo();

      // Let's skip cells we just filled
      r = prevR;
    }
  }

  if (initUndo) TUndoManager::manager()->endBlock();

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
}
