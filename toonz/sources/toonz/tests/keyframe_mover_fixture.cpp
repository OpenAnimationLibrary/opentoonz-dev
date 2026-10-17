// Lightweight stage/selection doubles; real KeyframeMover::moveKeyframes is
// inserted.

#include <cassert>
#include <map>
#include <set>
#include <iostream>
struct TKeyframeSelection {
  using Position = std::pair<int, int>;
  std::set<Position> p;
  auto getSelection() { return p; }
};
struct TStageObjectId {
  int v;
  TStageObjectId(int i) : v(i) {}
  operator int() const { return v; }
  static int ColumnId(int c) { return c; }
  static int CameraId(int c) { return c; }
};
struct TStageObject {
  using Keyframe = int;
  std::map<int, int> keys;
  bool isKeyframe(int r) { return keys.count(r); }
  int getKeyframe(int r) { return keys[r]; }
  void removeKeyframeWithoutUndo(int r) { keys.erase(r); }
  void setKeyframeWithoutUndo(int r, int k) { keys[r] = k; }
  bool moveKeyframe(int dst, int src) {
    assert(dst != src);
    if (isKeyframe(dst) || !isKeyframe(src)) return false;
    keys[dst] = keys[src];
    keys.erase(src);
    return true;
  }
  void moveKeyframes(std::set<int>& s, int d) {
    for (auto i = s.rbegin(); i != s.rend(); ++i) moveKeyframe(*i + d, *i);
  }
};
struct TXsheet {
  std::map<int, TStageObject> objs;
  TStageObject* getStageObject(int c) { return &objs[c]; }
  int getCameraColumnIndex() { return -1; }
};
struct KeyframeMover {
  using KeyframePosition = TKeyframeSelection::Position;
  enum {
    eCopyKeyframes      = 1,
    eMoveKeyframes      = 2,
    eInsertKeyframes    = 4,
    eOverwriteKeyframes = 8
  };
  int m_qualifiers = 8;
  std::set<TKeyframeSelection::Position> m_startSelectedKeyframes;
  TXsheet x, snapshot;
  TXsheet* getXsheet() { return &x; }
  void setKeyframes() { x = snapshot; }
  bool moveKeyframes(int, std::set<TKeyframeSelection::Position>&,
                     TKeyframeSelection* selection = nullptr);
};

// PRODUCTION_FUNCTIONS

void checkKeys(KeyframeMover& m, int col, std::map<int, int> expected) {
  assert(m.x.objs[col].keys == expected);
}
int main() {
  for (int columns : {1, 2})
    for (bool copy : {false, true})
      for (int delta : {-1, 1, 10}) {
        KeyframeMover mover;
        mover.m_qualifiers = KeyframeMover::eOverwriteKeyframes |
                             (copy ? KeyframeMover::eCopyKeyframes : 0);
        for (int c = 0; c < columns; ++c) {
          mover.x.objs[c].keys = {
              {10, 100 + c}, {11, 110 + c}, {20, 200 + c}, {50, 500 + c}};
          mover.m_startSelectedKeyframes.insert({10, c});
          mover.m_startSelectedKeyframes.insert({11, c});
        }
        mover.snapshot = mover.x;
        for (int repeat = 0; repeat < 3; ++repeat) {
          std::set<TKeyframeSelection::Position> positions;
          assert(mover.moveKeyframes(delta, positions));
          for (int c = 0; c < columns; ++c) {
            auto expected = mover.snapshot.objs[c].keys;
            if (!copy) {
              expected.erase(10);
              expected.erase(11);
            }
            expected[10 + delta] = 100 + c;
            expected[11 + delta] = 110 + c;
            checkKeys(mover, c, expected);
            assert(positions.count({10 + delta, c}) &&
                   positions.count({11 + delta, c}));
          }
          // Returning to origin is an accepted preview and restores every
          // original.
          positions.clear();
          assert(mover.moveKeyframes(0, positions));
          assert(positions == mover.m_startSelectedKeyframes);
          for (int c = 0; c < columns; ++c)
            checkKeys(mover, c, mover.snapshot.objs[c].keys);
        }
      }
  {
    KeyframeMover m;
    m.m_qualifiers             = KeyframeMover::eMoveKeyframes;
    m.x.objs[0].keys           = {{10, 100}, {11, 110}, {50, 500}};
    m.snapshot                 = m.x;
    m.m_startSelectedKeyframes = {{10, 0}, {11, 0}};
    std::set<TKeyframeSelection::Position> p;
    assert(m.moveKeyframes(1, p));
    checkKeys(m, 0, {{11, 100}, {12, 110}, {50, 500}});
  }
  {
    KeyframeMover m;
    m.x.objs[0].keys           = {{0, 100}, {50, 500}};
    m.snapshot                 = m.x;
    m.m_startSelectedKeyframes = {{0, 0}};
    std::set<TKeyframeSelection::Position> p;
    m.moveKeyframes(5, p);
    p.clear();
    m.moveKeyframes(-1, p);
    checkKeys(m, 0, m.snapshot.objs[0].keys);
    assert(p == m.m_startSelectedKeyframes);
  }
  {
    KeyframeMover m;
    m.m_qualifiers             = KeyframeMover::eInsertKeyframes;
    m.x.objs[0].keys           = {{10, 100}, {11, 110}, {50, 500}};
    m.snapshot                 = m.x;
    m.m_startSelectedKeyframes = {{10, 0}};
    std::set<TKeyframeSelection::Position> p;
    m.moveKeyframes(1, p);
    checkKeys(m, 0, {{11, 100}, {12, 110}, {50, 500}});
    p.clear();
    m.moveKeyframes(0, p);
    checkKeys(m, 0, m.snapshot.objs[0].keys);
  }
  std::cout << "12 overwrite/copy/multi-column cases x 3 preview/return "
               "cycles; ordinary move, clamp and insert controls passed\n";
}
