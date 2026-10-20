#!/usr/bin/env python3
"""Execute the actual Full Color commit method against raster harness doubles."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source_root', type=Path)
p.add_argument('--ref')
a = p.parse_args()
path = 'toonz/sources/tnztools/fullcolorbrushtool.cpp'
s = subprocess.check_output(['git', '-C', str(a.source_root), 'show', f'{a.ref}:{path}'], text=True) if a.ref else (a.source_root / path).read_text()
start = s.index('{', s.index('void FullColorBrushTool::commitStroke()'))
i, depth = start + 1, 1
while depth:
    depth += (s[i] == '{') - (s[i] == '}')
    i += 1
body = s[start:i]
prefix = Path(__file__).with_name('raster.cpp.in').read_text().split('void ToonzRasterBrushTool::commitPainting() @COMMIT@')[0]
prefix = prefix.replace('int getP00() const { return 0; }', 'int getP00() const { return 0; } void empty() {}')
cpp = prefix + Path(__file__).with_name('fullcolor.cpp.in').read_text().replace('@COMMIT@', body)
with tempfile.TemporaryDirectory(prefix='brush-fullcolor-') as d:
    src, exe = Path(d) / 'test.cpp', Path(d) / 'test'
    src.write_text(cpp)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(src), '-o', str(exe)], check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)
