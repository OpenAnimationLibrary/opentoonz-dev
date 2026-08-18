#include "cellselection.h"
#include "historytypes.h"
#include "tapp.h"

#include "toonzqt/menubarcommand.h"
#include "toonzqt/tselectionhandle.h"

#include "toonz/tscenehandle.h"
#include "toonz/txshcolumn.h"
#include "toonz/txshcell.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"

#include "tundo.h"

#include <vector>

namespace {

class TransposeCellsUndo final : public TUndo {
  int m_r0, m_c0, m_r1, m_c1;
  bool m_horizontal;
  std::vector<TXshCell> m_cells;

  void apply(bool forward) const {
    TApp *app    = TApp::instance();
    TXsheet *xsh = app->getCurrentXsheet()->getXsheet();
    int count    = (int)m_cells.size();

    for (int i = 0; i < count; ++i) {
      int r = forward ? (m_horizontal ? m_r0 : m_r0 + i)
                      : (m_horizontal ? m_r0 + i : m_r0);
      int c = forward ? (m_horizontal ? m_c0 + i : m_c0)
                      : (m_horizontal ? m_c0 : m_c0 + i);
      xsh->clearCells(r, c);
    }

    for (int i = 0; i < count; ++i) {
      if (m_cells[i].isEmpty()) continue;
      int r = forward ? (m_horizontal ? m_r0 + i : m_r0)
                      : (m_horizontal ? m_r0 : m_r0 + i);
      int c = forward ? (m_horizontal ? m_c0 : m_c0 + i)
                      : (m_horizontal ? m_c0 + i : m_c0);
      xsh->setCell(r, c, m_cells[i]);
    }

    TCellSelection *selection = dynamic_cast<TCellSelection *>(
        app->getCurrentSelection()->getSelection());
    if (selection) {
      if (forward == m_horizontal)
        selection->selectCells(m_r0, m_c0, m_r0 + count - 1, m_c0);
      else
        selection->selectCells(m_r0, m_c0, m_r0, m_c0 + count - 1);
      app->getCurrentSelection()->notifySelectionChanged();
    }

    app->getCurrentXsheet()->notifyXsheetChanged();
    app->getCurrentScene()->setDirtyFlag(true);
  }

public:
  TransposeCellsUndo(int r0, int c0, int r1, int c1)
      : m_r0(r0)
      , m_c0(c0)
      , m_r1(r1)
      , m_c1(c1)
      , m_horizontal(r0 == r1) {
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    int count    = m_horizontal ? c1 - c0 + 1 : r1 - r0 + 1;
    m_cells.reserve(count);
    for (int i = 0; i < count; ++i)
      m_cells.push_back(
          xsh->getCell(m_horizontal ? r0 : r0 + i,
                       m_horizontal ? c0 + i : c0));
  }

  void undo() const override { apply(false); }
  void redo() const override { apply(true); }

  int getSize() const override {
    return sizeof(*this) + (int)(m_cells.size() * sizeof(TXshCell));
  }
  QString getHistoryString() override { return QObject::tr("Transpose"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

class TransposeCellsCommand final : public MenuItemHandler {
public:
  TransposeCellsCommand() : MenuItemHandler("MI_TransposeCells") {}

  void execute() override {
    TApp *app = TApp::instance();
    TCellSelection *selection = dynamic_cast<TCellSelection *>(
        app->getCurrentSelection()->getSelection());
    if (!selection || selection->isEmpty()) return;

    TCellSelection::Range range = selection->getSelectedCells();
    int rows = range.getRowCount(), cols = range.getColCount();
    if ((rows == 1) == (cols == 1)) return;

    TXsheet *xsh = app->getCurrentXsheet()->getXsheet();
    int count    = rows == 1 ? cols : rows;

    for (int i = 0; i < count; ++i) {
      int sc = rows == 1 ? range.m_c0 + i : range.m_c0;
      int dr = rows == 1 ? range.m_r0 + i : range.m_r0;
      int dc = rows == 1 ? range.m_c0 : range.m_c0 + i;

      TXshColumn *sourceColumn = xsh->getColumn(sc);
      TXshColumn *destColumn   = xsh->getColumn(dc);
      if ((sourceColumn && sourceColumn->isLocked()) ||
          (destColumn &&
           (destColumn->isLocked() || !destColumn->getCellColumn())))
        return;
      if (!range.contains(dr, dc) && !xsh->getCell(dr, dc).isEmpty()) return;
    }

    TransposeCellsUndo *undo = new TransposeCellsUndo(
        range.m_r0, range.m_c0, range.m_r1, range.m_c1);
    TUndoManager::manager()->add(undo);
    undo->redo();
  }
} transposeCellsCommand;

}  // namespace