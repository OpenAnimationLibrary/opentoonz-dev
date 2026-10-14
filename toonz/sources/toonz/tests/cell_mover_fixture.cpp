// Dependency doubles for executing production onCellChange, Undo/Redo and
// destructor control flow. This is not a GUI/Plastic integration test.
#include <cassert>
#include <iostream>
#include <map>
#include <set>
struct TPoint {
  int x = 0, y = 0;
  TPoint() = default;
  TPoint(int a, int b) : x(a), y(b) {}
  TPoint operator+(TPoint p) const { return {x + p.x, y + p.y}; }
  TPoint operator-(TPoint p) const { return {x - p.x, y - p.y}; }
  bool operator==(TPoint p) const { return x == p.x && y == p.y; }
  bool operator!=(TPoint p) const { return !(*this == p); }
};
struct TDimension {
  int lx = 1, ly = 1;
};
struct TRect {
  int x0, y0, x1, y1;
  TRect(TPoint p, TDimension d)
      : x0(p.x), y0(p.y), x1(p.x + d.lx - 1), y1(p.y + d.ly - 1) {}
};
struct Orientation {
  bool vertical = true;
  bool isVerticalTimeline() const { return vertical; }
};
struct TXshColumn {
  enum { eSoundType };
  bool isEmpty() { return false; }
  bool isLocked() { return false; }
};
struct TXsheet {
  std::map<std::pair<int, int>, int> cells;
  TXshColumn column;
  TXshColumn* getColumn(int) { return &column; }
  int getColumnCount() { return 5; }
  void updateFrameCount() {}
  void clearCells(int r, int c, int n) {
    for (int i = 0; i < n; ++i) cells.erase({r + i, c});
  }
};
struct XsheetHandle {
  TXsheet x;
  TXsheet* getXsheet() { return &x; }
  void notifyXsheetChanged() {}
  void notifyXsheetSoundChanged() {}
};
struct TApp {
  XsheetHandle h;
  static TApp* instance() {
    static TApp a;
    return &a;
  }
  XsheetHandle* getCurrentXsheet() { return &h; }
};
struct CellSelection {
  int row = -1, col = -1;
  void selectCells(int r, int c, int, int) {
    row = r;
    col = c;
  }
};
struct XsheetViewer {
  Orientation o;
  CellSelection s;
  Orientation* orientation() { return &o; }
  TXsheet* getXsheet() { return TApp::instance()->h.getXsheet(); }
  CellSelection* getCellSelection() { return &s; }
};
struct TUndoManager {
  static TUndoManager* manager() {
    static TUndoManager m;
    return &m;
  }
  void endBlock() {}
};
struct CellsMover {
  enum {
    eCopyCells      = 1,
    eInsertCells    = 2,
    eOverwriteCells = 4,
    eMoveColumns    = 8
  };
  Orientation* orientation;
  TPoint start, pos;
  int qualifiers = 4, m_uffa = 0, cell = 7, old = 0;
  const Orientation* getOrientation() const { return orientation; }
  TPoint getStartPos() const { return start; }
  TPoint getPos() const { return pos; }
  int getQualifiers() const { return qualifiers; }
  int getRowCount() const { return 1; }
  int getColumnCount() const { return 1; }
  int getColumnTypeFromCell(int) const { return -1; }
  TPoint rc(TPoint p) const {
    return orientation->vertical ? TPoint(p.y, p.x) : p;
  }
  void setPos(int r, int c) { pos = {c, r}; }
  void saveCells() {
    TPoint p = rc(pos);
    old      = TApp::instance()->h.x.cells[{p.x, p.y}];
  }
  void moveCells(TPoint p) const {
    p                                       = rc(p);
    TApp::instance()->h.x.cells[{p.x, p.y}] = cell;
  }
  void moveCells() const { moveCells(pos); }
  void undoMoveCells(TPoint p) const {
    p           = rc(p);
    auto& cells = TApp::instance()->h.x.cells;
    cells.erase({p.x, p.y});
    if ((qualifiers & eOverwriteCells) && old) cells[{p.x, p.y}] = old;
  }
  void undoMoveCells() const { undoMoveCells(pos); }
  void emptyColumns(int) const {}
  void restoreColumns(int) const {}
};
struct LevelMoverUndo {
  CellsMover m_cellsMover;
  CellsMover* getCellsMover() { return &m_cellsMover; }
  void undo() const;
  void redo() const;
};
struct LevelMoverTool {
  XsheetViewer* viewer;
  TPoint m_grabOffset, m_startPos, m_lastPos, m_aimedPos;
  TDimension m_range;
  int m_columnLimit = 4, m_qualifiers = 4;
  bool m_validPos = true, m_moved = true, m_hasPreview = false, allow = true;
  LevelMoverUndo* m_undo = nullptr;
  XsheetViewer* getViewer() { return viewer; }
  bool canMoveColumns(TPoint) { return true; }
  bool canMove(TPoint) { return allow; }
  bool isTotallyEmptyColumn(int) { return false; }
  void onCellChange(int, int);
  ~LevelMoverTool();
};
// PRODUCTION_FUNCTIONS
int main() {
  for (bool vertical : {false, true})
    for (bool copy : {false, true}) {
      XsheetViewer viewer;
      viewer.o.vertical = vertical;
      auto& cells       = TApp::instance()->h.x.cells;
      cells             = {{{2, 1}, 7}, {{50, 1}, 9}, {{6, 1}, 8}};
      auto original     = cells;
      LevelMoverUndo undo;
      auto& m       = undo.m_cellsMover;
      m.orientation = &viewer.o;
      m.qualifiers =
          CellsMover::eOverwriteCells | (copy ? CellsMover::eCopyCells : 0);
      m.start = vertical ? TPoint(1, 2) : TPoint(2, 1);
      m.pos   = vertical ? TPoint(1, 6) : TPoint(6, 1);
      m.old   = 8;
      // Apply initial preview through the storage double, then real Undo/Redo.
      if (!copy) cells.erase({2, 1});
      m.moveCells();
      auto after = cells;
      for (int i = 0; i < 3; ++i) {
        undo.undo();
        assert(cells == original);
        undo.redo();
        assert(cells == after);
      }
    }
  for (bool vertical : {false, true})
    for (bool valid : {false, true}) {
      XsheetViewer viewer;
      viewer.o.vertical = vertical;
      auto& cells       = TApp::instance()->h.x.cells;
      cells             = {{{2, 1}, 7}, {{50, 1}, 9}};
      auto original     = cells;
      {
        LevelMoverTool tool;
        tool.viewer       = &viewer;
        tool.allow        = valid;
        tool.m_qualifiers = 5;
        tool.m_moved      = false;
        tool.m_startPos = tool.m_lastPos = tool.m_aimedPos =
            vertical ? TPoint(1, 2) : TPoint(2, 1);
        tool.m_undo   = new LevelMoverUndo;
        auto& m       = tool.m_undo->m_cellsMover;
        m.orientation = &viewer.o;
        m.qualifiers  = 5;
        m.start = m.pos = tool.m_startPos;
        // An invalid initial copy (e.g. combined drag to another column) has no
        // preview to roll back. A valid copy preview must be rolled back on
        // dispose.
        tool.onCellChange(6, 1);
        assert(tool.m_hasPreview == valid);
      }
      assert(cells == original);
      if (valid) assert(viewer.s.row == 2 && viewer.s.col == 1);
    }
  std::cout
      << "real cell-mover control flow: both orientations, overwrite/copy "
         "Undo/Redo x3, valid/invalid initial-copy cancellation passed\n";
}
