#!/usr/bin/env python3
"""Compile actual modifier-sync/leftover-event guard fragments for all brushes."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source_root', type=Path)
p.add_argument('--ref')
a = p.parse_args()
prefix = r'''
#include <iostream>
enum class TKey { alt, shift, control };
enum MouseEventType { ME_DOWN, ME_DRAG, ME_UP, ME_MOVE };
struct Input {
  struct State {
    bool keys[3] = {};
    bool isKeyPressed(TKey key) { return keys[int(key)]; }
  } state;
  void keyEvent(bool value, TKey key, int, void*) { state.keys[int(key)] = value; }
};
struct Base {
  Input m_inputmanager;
  bool m_skipStrokeUntilDown = true;
  int delivered = 0;
};
int failures = 0, checks = 0;
void check(bool ok, const char* name) {
  ++checks; failures += !ok;
  std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
}
template <class Tool> void test() {
  Tool tool;
  tool.event(ME_DRAG, true, true, true);
  check(tool.delivered == 0, "leftover drag dropped");
  check(tool.m_inputmanager.state.keys[0] && tool.m_inputmanager.state.keys[1] && tool.m_inputmanager.state.keys[2], "modifiers synced even for dropped drag");
  tool.event(ME_UP, false, false, false);
  check(tool.delivered == 0, "leftover release dropped before image touch/track processing");
  check(!tool.m_inputmanager.state.keys[0] && !tool.m_inputmanager.state.keys[1] && !tool.m_inputmanager.state.keys[2], "released modifiers synced");
  tool.event(ME_MOVE, false, false, true);
  check(tool.delivered == 1 && tool.m_skipStrokeUntilDown, "hover allowed without resetting guard");
  tool.event(ME_DOWN, true, false, true);
  check(tool.delivered == 2 && !tool.m_skipStrokeUntilDown, "fresh down resumes");
  tool.event(ME_DRAG, true, false, true);
  tool.event(ME_UP, false, false, false);
  check(tool.delivered == 4, "fresh stroke drag/release delivered");
}
'''
classes = ''
for cls, filename in [('Vector', 'toonzvectorbrushtool.cpp'), ('Raster', 'toonzrasterbrushtool.cpp'), ('FullColor', 'fullcolorbrushtool.cpp')]:
    path = 'toonz/sources/tnztools/' + filename
    s = subprocess.check_output(['git', '-C', str(a.source_root), 'show', f'{a.ref}:{path}'], text=True) if a.ref else (a.source_root / path).read_text()
    s = s[s.index('::handleMouseEvent('):]
    end = s.index('  TPointD snappedPos') if cls == 'Vector' else s.index('  if (type == ME_MOVE)')
    fragment = s[s.index('  if (alt !='):end]
    classes += f'struct {cls} : Base {{ void event(MouseEventType type, bool alt, bool shift, bool control) {{ int t = 0;\n' + fragment + '\n++delivered; } };\n'
cpp = prefix + classes + 'int main() { test<Vector>(); test<Raster>(); test<FullColor>(); std::cout << checks << " checks, " << failures << " failures\\n"; return failures ? 1 : 0; }'
with tempfile.TemporaryDirectory(prefix='brush-events-') as d:
    src, exe = Path(d) / 'test.cpp', Path(d) / 'test'
    src.write_text(cpp)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(src), '-o', str(exe)], check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)
