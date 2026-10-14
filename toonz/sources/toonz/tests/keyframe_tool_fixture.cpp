// Selection/viewer doubles for the real click/preview coordinate handling.
#include <cassert>
#include <iostream>
#include <memory>
#include <set>
namespace Qt {
enum { ControlModifier = 1, ShiftModifier = 2 };
}
struct QPoint {
  int a, b;
  int x() const { return a; }
  int y() const { return b; }
};
struct QMouseEvent {
  QPoint p;
  QPoint pos() const { return p; }
  int modifiers() const { return 0; }
};
struct TPointD {
  double x = 0, y = 0;
  TPointD() = default;
  TPointD(double a, double b) : x(a), y(b) {}
};
struct CellPosition {
  QPoint p;
  int frame() const { return p.y(); }
  int layer() const { return p.x(); }
};
struct TXsheet {};
struct TKeyframeSelection {
  using Position = std::pair<int, int>;
  std::set<Position> m_positions;
  bool isEmpty() const { return m_positions.empty(); }
  int getFirstRow() const {
    return isEmpty() ? -1 : m_positions.begin()->first;
  }
  bool isSelected(int r, int c) { return m_positions.count({r, c}); }
  void selectNone() { m_positions.clear(); }
  void select(int r, int c) { m_positions.insert({r, c}); }
  void makeCurrent() {}
};
struct KeyframeMover {
  enum { eMoveKeyframes = 2, eOverwriteKeyframes = 8 };
  int qualifiers = 2, lastDelta = 0;
  void start(TKeyframeSelection* s, int q) {
    qualifiers = q;
    for (auto i = s->m_positions.begin(); i != s->m_positions.end();)
      if (i->second == 0)
        i = s->m_positions.erase(i);
      else
        ++i;
  }
  int getQualifiers() { return qualifiers; }
  bool moveKeyframes(int d, std::set<TKeyframeSelection::Position>& p,
                     TKeyframeSelection* s) {
    lastDelta = d;
    for (auto i : s->m_positions) p.insert({i.first + d, i.second});
    return true;
  }
};
struct XsheetViewer {
  TXsheet x;
  TXsheet* getXsheet() { return &x; }
  CellPosition xyToPosition(QPoint p) { return {p}; }
  void update() {}
};
struct KeyframeMoverTool {
  XsheetViewer viewer;
  TKeyframeSelection selection, m_startSelection;
  int m_offset = 0, m_firstRow = 0, m_firstCol = 0;
  bool m_firstKeyframeMovement = true, m_selecting = false,
       m_justMovement = true;
  TPointD m_startPos, m_curPos;
  std::unique_ptr<KeyframeMover> m_mover{new KeyframeMover};
  TKeyframeSelection* getSelection() { return &selection; }
  XsheetViewer* getViewer() { return &viewer; }
  void ctrlSelect(int, int) {}
  void shiftSelect(int, int) {}
  void onClick(const QMouseEvent*);
  void onCellChange(int, int);
};
// PRODUCTION_FUNCTIONS
int main() {
  for (bool overwrite : {false, true}) {
    KeyframeMoverTool tool;
    tool.selection.m_positions = {{0, 0}, {10, 1}};
    QMouseEvent event{{1, 10}};
    tool.onClick(&event);
    assert(tool.selection.m_positions.size() == 1 && tool.m_offset == 0);
    if (overwrite)
      tool.m_mover->qualifiers = KeyframeMover::eOverwriteKeyframes;
    tool.onCellChange(11, 1);
    assert(tool.m_mover->lastDelta == 1);
    assert(tool.selection.isSelected(11, 1));
  }
  std::cout << "locked-column filtering preserves click anchor for "
               "ordinary/overwrite previews\n";
}
