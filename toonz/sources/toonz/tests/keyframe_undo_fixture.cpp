// Snapshot dependency double; the complete production UndoMoveKeyFrame class is
// inserted by the runner. Verifies redo restores a result rather than
// replaying.
#include <cassert>
#include <map>
#include <memory>
#include <iostream>
using QString = int;
struct QObject {
  static int tr(const char*) { return 0; }
};
struct HistoryType {
  enum { Xsheet };
};
struct TUndo {
  virtual ~TUndo()                   = default;
  virtual void undo() const          = 0;
  virtual void redo() const          = 0;
  virtual int getSize() const        = 0;
  virtual QString getHistoryString() = 0;
  virtual int getHistoryType()       = 0;
};
struct TKeyframeSelection {};
struct KeyframeMover {
  static std::map<int, int> scene;
  std::map<int, int> snapshot;
  void start(TKeyframeSelection*, int) { snapshot = scene; }
  void undoMoveKeyframe() { scene = snapshot; }
};
std::map<int, int> KeyframeMover::scene;
struct Handle {
  void notifyXsheetChanged() {}
};
struct TApp {
  static TApp* instance() {
    static TApp a;
    return &a;
  }
  Handle* getCurrentXsheet() {
    static Handle h;
    return &h;
  }
};
// PRODUCTION_FUNCTIONS
int main() {
  for (bool copy : {false, true}) {
    KeyframeMover::scene = {{10, 100}, {11, 110}, {50, 500}};
    auto before          = KeyframeMover::scene;
    auto* mover          = new KeyframeMover;
    TKeyframeSelection selection;
    mover->start(&selection, 0);
    KeyframeMover::scene =
        copy ? std::map<int, int>{{10, 100}, {11, 100}, {12, 110}, {50, 500}}
             : std::map<int, int>{{11, 100}, {12, 110}, {50, 500}};
    const auto after = KeyframeMover::scene;
    UndoMoveKeyFrame undo(mover, &selection);
    for (int n = 0; n < 3; ++n) {
      undo.undo();
      assert(KeyframeMover::scene == before);
      undo.redo();
      assert(KeyframeMover::scene == after);
    }
  }
  std::cout << "production key Undo/Redo class restores before/after snapshots "
               "x3 for move and copy\n";
}
