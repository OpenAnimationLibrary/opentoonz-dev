// Lightweight dependency doubles. The runner inserts real TXsheet methods.

#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <vector>
#include <string>
using std::to_string;
enum {
  SND_XSHLEVEL = 1,
  SND_TXT_XSHLEVEL,
  PLT_XSHLEVEL,
  ZERARYFX_XSHLEVEL,
  MESH_XSHLEVEL
};
struct TXshLevel {
  int type;
  int getType() { return type; }
  int getName() { return 1; }
};
struct LevelPtr {
  TXshLevel* p = nullptr;
  TXshLevel* getPointer() const { return p; }
  TXshLevel* operator->() const { return p; }
};
struct TXshCell {
  LevelPtr m_level;
  bool isEmpty() const { return !m_level.p; }
};
struct TFx {
  int getOutputConnectionCount() { return 0; }
};
struct TXsheet;
struct TXshColumn {
  enum ColumnType {
    eLevelType,
    eSoundType,
    eSoundTextType,
    ePaletteType,
    eZeraryFxType,
    eMeshType
  };
  ColumnType type;
  std::map<int, TXshCell> cells;
  bool isEmpty() { return cells.empty(); }
  ColumnType getColumnType() { return type; }
  TXshColumn* getCellColumn() { return this; }
  int getMaxFrame() { return cells.empty() ? -1 : cells.rbegin()->first; }
  bool isLocked() { return false; }
  TFx* getFx() { return nullptr; }
  void setXsheet(TXsheet*) {}
  bool setCells(int r, int n, const TXshCell* p) {
    for (int i = 0; i < n; i++)
      if (p[i].isEmpty())
        cells.erase(r + i);
      else
        cells[r + i] = p[i];
    return true;
  }
};
using TXshCellColumn = TXshColumn;
struct ColPtr {
  TXshColumn* p = nullptr;
  TXshColumn* getPointer() { return p; }
};
struct ColSet {
  std::map<int, ColPtr> cols;
  ColPtr& touchColumn(int i, TXshColumn::ColumnType t) {
    auto& p = cols[i];
    if (!p.p) p.p = new TXshColumn{t, {}};
    return p;
  }
};
struct Stage {
  std::map<int, int> keys{{2, 20}, {9, 90}, {50, 500}};
  bool rig = true;
  void setName(std::string) {}
};
struct TStageObjectId {
  static int ColumnId(int i) { return i; }
};
struct Preferences {
  static Preferences* instance() {
    static Preferences p;
    return &p;
  }
  bool isLinkColumnNameWithLevelEnabled() { return false; }
};
struct Dag {
  void addToXsheet(TFx*) {}
};
struct TXsheet {
  struct Imp {
    ColSet m_columnSet;
    int m_frameCount = 0;
  } imp, *m_imp = &imp;
  std::map<int, Stage> stages;
  int replacements = 0;
  bool isColumnEmpty(int c) {
    auto* p = getColumn(c);
    return !p || p->isEmpty();
  }
  TXshColumn* getColumn(int c) { return imp.m_columnSet.cols[c].p; }
  TXshColumn* touchColumn(int, TXshColumn::ColumnType);
  bool setCells(int, int, int, const TXshCell*);
  void removeColumn(int c) {
    delete imp.m_columnSet.cols[c].p;
    imp.m_columnSet.cols[c].p = nullptr;
    stages.erase(c);
    replacements++;
  }
  void insertColumn(int c, TXshColumn::ColumnType t = TXshColumn::eLevelType) {
    imp.m_columnSet.touchColumn(c, t);
    stages[c] = Stage{{}, false};
  }
  Stage* getStageObject(int c) { return &stages[c]; }
  Dag* getFxDag() {
    static Dag d;
    return &d;
  }
  void updateFrameCount() {}
  void clearCells(int r, int c, int n) {
    TXshCell e;
    for (int i = 0; i < n; i++) getColumn(c)->setCells(r + i, 1, &e);
  }
};

// PRODUCTION_FUNCTIONS

int main() {
  TXshLevel mesh{MESH_XSHLEVEL}, ordinary{0};
  for (bool isMesh : {false, true})
    for (bool partial : {false, true})
      for (bool occupied : {false, true}) {
        TXsheet x;
        TXshCell a{{isMesh ? &mesh : &ordinary}}, empty;
        x.setCells(0, 0, 1, &a);
        x.stages[0] = Stage{};
        if (partial) x.setCells(20, 0, 1, &a);
        if (occupied) x.setCells(10, 0, 1, &a);
        // First LevelMoverTool preview: undoMoveCells(start), initially
        // m_oldCells is all-empty.
        x.clearCells(0, 0, 1);
        x.setCells(0, 0, 1, &empty);
        x.setCells(10, 0, 1, &a);
        bool preview    = x.stages[0].rig;
        int previewKeys = x.stages[0].keys.size();
        // LevelMoverUndo::undo / returning to source preview: clear dest,
        // restore old dest, restore source cells.
        x.clearCells(10, 0, 1);
        x.setCells(10, 0, 1, occupied ? &a : &empty);
        x.setCells(0, 0, 1, &a);
        bool undo = x.stages[0].rig;
        // Redo clears source and moves cells. No same-column stage snapshot
        // restoration.
        x.clearCells(0, 0, 1);
        x.setCells(10, 0, 1, &a);
        std::cout << "cell mesh=" << isMesh << " partial=" << partial
                  << " occupied=" << occupied << " previewRig=" << preview
                  << " previewKeys=" << previewKeys << " undoRig=" << undo
                  << " redoRig=" << x.stages[0].rig
                  << " replacements=" << x.replacements << "\n";
        assert(preview && previewKeys == 3 && undo && x.stages[0].rig);
        assert(x.replacements == 0);
      }
  // Cross-column cells-only mode: source rig lost if clearing empties it;
  // destination rig survives occupied target.
  {
    TXsheet x;
    TXshCell a{{&mesh}}, e;
    x.setCells(0, 0, 1, &a);
    x.setCells(10, 1, 1, &a);
    x.stages[0] = Stage{};
    x.stages[1] = Stage{};
    x.clearCells(0, 0, 1);
    x.setCells(0, 0, 1, &e);
    x.setCells(10, 1, 1, &a);
    assert(x.stages[0].rig && x.stages[1].rig);
    std::cout << "cross-column sourceRig=1 occupiedDestinationRig=1\n";
  }
}
