#!/usr/bin/env python3
"""Compile real PR method bodies against small in-memory test doubles.
Not an OpenToonz build or GUI/TLV integration test. See README.md.
Usage: python3 regression.py SOURCE_ROOT [--ref GIT_REF]
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import os

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source_root', type=Path)
p.add_argument('--ref')
p.add_argument('--commit-only', action='store_true', help='Negative control: retain upstream redo while fixing commit')
a = p.parse_args()
templates = Path(__file__).resolve().parent
work = tempfile.TemporaryDirectory(prefix='brush-raster-')
out = Path(work.name)

def source(path):
    if a.ref:
        return subprocess.check_output(['git', '-C', str(a.source_root), 'show', f'{a.ref}:{path}'], text=True)
    return (a.source_root / path).read_text()

def body(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    i = opening + 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[opening:i]

brush = source('toonz/sources/tnztools/toonzrasterbrushtool.cpp')
utils = source('toonz/sources/tnztools/toolutils.cpp')
commit = body(brush, 'void ToonzRasterBrushTool::commitPainting()')
redo = body(brush[brush.index('class MyPaintBrushUndo final'):], 'void redo() const override')
undo = body(utils, 'void ToolUtils::TRasterUndo::undo() const')
if a.commit_only:
    redo = redo.replace('ToolUtils::updateSaveBox(m_level, m_frameId);', 'ToolUtils::updateSaveBox();')
cpp = (templates / 'raster.cpp.in').read_text().replace('@COMMIT@', commit).replace('@REDO@', redo).replace('@UNDO@', undo)
label = 'commit-only' if a.commit_only else ('baseline' if a.ref else 'fixed')
generated = out / f'{label}.cpp'
generated.write_text(cpp)
exe = out / f'{label}-regression'
subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(generated), '-o', str(exe)], check=True)
result = subprocess.run([str(exe)])
raise SystemExit(result.returncode)
