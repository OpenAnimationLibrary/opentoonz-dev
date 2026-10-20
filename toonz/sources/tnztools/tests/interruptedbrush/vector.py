#!/usr/bin/env python3
"""Exercise real frame-range completion/callback bodies with synchronous doubles."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source_root', type=Path)
p.add_argument('--ref')
a = p.parse_args()
path = 'toonz/sources/tnztools/toonzvectorbrushtool.cpp'
s = subprocess.check_output(['git', '-C', str(a.source_root), 'show', f'{a.ref}:{path}'], text=True) if a.ref else (a.source_root / path).read_text()

def body(text, marker):
    start = text.index('{', text.index(marker))
    i, depth = start + 1, 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[start:i]

start = s.index('      // paint frame range strokes')
branch = body(s[start-20:], '} else')
cpp = (Path(__file__).with_name('vector.cpp.in')).read_text()
cpp = cpp.replace('@RANGE@', branch)
cpp = cpp.replace('@CHANGED@', body(s, 'void ToonzVectorBrushTool::onImageChanged()'))
cpp = cpp.replace('@RESET@', body(s, 'void ToonzVectorBrushTool::resetFrameRange()'))
with tempfile.TemporaryDirectory(prefix='brush-vector-') as d:
    src, exe = Path(d) / 'test.cpp', Path(d) / 'test'
    src.write_text(cpp)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(src), '-o', str(exe)], check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)
