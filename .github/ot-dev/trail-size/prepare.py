"""Validate and publish only the requested Size Multiplier follow-up to PR 221."""
from pathlib import Path
import re
import shlex
import subprocess

BASE = 'c428288a53200c9681c6349ed8d8d7392157934d'
BRANCH = 'feature/trail-cycle-style-settings'
root = Path.cwd()
here = Path(__file__).resolve().parent
report = Path('/tmp/trail-size-report')
report.mkdir(exist_ok=True)
patch = (here / 'size.patch').read_text().replace('\n diff --git ', '\ndiff --git ')
assets = {
    'toonz/sources/include/trailscale.h': (here / 'trailscale.h').read_bytes(),
    'toonz/sources/include/tsimplecolorstyles.h': (here / 'tsimplecolorstyles.h').read_bytes(),
    'toonz/tests/trail_settings/trailscale_test.cpp': (here / 'trailscale_test.cpp').read_bytes(),
    'toonz/tests/trail_settings/trailscalemath_test.cpp': (here / 'trailscalemath_test.cpp').read_bytes(),
}

def out(*args):
    return subprocess.check_output(args, text=True)

def run(*args):
    print('+', ' '.join(args), flush=True)
    subprocess.run(args, check=True)

def logged(name, *args):
    print('+', ' '.join(args), flush=True)
    log = report / (name + '.log')
    with log.open('w') as stream:
        result = subprocess.run(args, stdout=stream, stderr=subprocess.STDOUT)
    print('\n'.join(log.read_text(errors='replace').splitlines()[-65:]), flush=True)
    result.check_returncode()

assert out('git', 'ls-remote', 'origin', 'refs/heads/' + BRANCH).split()[0] == BASE, 'PR head changed'
(report / 'size.patch').write_text(patch)
run('git', 'fetch', '--depth=1', 'origin', BASE)
run('git', 'checkout', '-b', 'validated-trail-size', BASE)
run('git', 'apply', '--index', '--whitespace=error', str(report / 'size.patch'))
for name, content in assets.items():
    (root / name).write_bytes(content)
run('git', 'add', '--', *assets.keys())
paths = out('git', 'diff', '--name-only', 'HEAD').splitlines()
assert all(p.startswith(('toonz/sources/', 'toonz/tests/trail_settings/')) for p in paths)
# Preserve both the committed offset implementation and the click carrier.
assert out('git', 'diff', '--name-only', 'HEAD', '--', 'toonz/sources/tnztools') == ''
for attempt in range(6):
    changed = False
    for name in paths:
        if not name.endswith(('.cpp', '.h')):
            continue
        ranges = []
        for start, length in re.findall(r'^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@', out('git', 'diff', '-U0', 'HEAD', '--', name), re.M):
            count = int(length) if length else 1
            if count:
                ranges.append(f'-lines={start}:{int(start)+count-1}')
        if not ranges:
            continue
        p = root / name
        data = p.read_bytes()
        formatted = subprocess.check_output(['clang-format-14', '-style=file', '-assume-filename=' + str(root / 'toonz/sources/format.cpp'), *ranges], input=data)
        if formatted != data:
            p.write_bytes(formatted)
            changed = True
    if not changed:
        break
else:
    raise RuntimeError('Changed-line formatting did not converge')
run('git', 'add', '--', *paths)
run('git', 'diff', '--cached', '--check')
(report / 'formatted.patch').write_text(out('git', 'diff', '--cached', '--binary'))
qt = shlex.split(out('pkg-config', '--cflags', 'Qt5Core', 'Qt5Gui', 'Qt5Widgets', 'Qt5OpenGL', 'Qt5Multimedia', 'Qt5Network', 'Qt5Svg', 'Qt5Xml', 'Qt5PrintSupport', 'Qt5Concurrent', 'freetype2', 'libmypaint'))
flags = ['g++', '-std=c++17', '-fsyntax-only', '-fPIC', '-DNDEBUG', '-DLINUX', '-DTNZ_LITTLE_ENDIAN=1', '-I' + str(root / 'toonz/sources/include'), '-I' + str(root / 'toonz/sources'), '-I' + str(root / 'toonz/sources/tnztools'), *qt]
for name in paths:
    if name.endswith('.cpp'):
        logged('syntax-' + Path(name).stem, *flags, name)
# The generic Settings page consumes the new DOUBLE parameter at index 4.
logged('syntax-styleeditor', *flags, 'toonz/sources/toonzqt/styleeditor.cpp')
core = Path('/tmp/trail-size-core')
core.mkdir(exist_ok=True)
(core / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.16)
project(TrailSizeCore LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_TARGET_UNIX ON)
set(BUILD_ENV_UNIXLIKE ON)
set(PLATFORM 64)
set(WITH_TRANSLATION OFF)
set(SDKROOT "@ROOT@/thirdparty")
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets OpenGL Multimedia Network)
find_package(JPEG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Freetype REQUIRED)
find_library(GL_LIB GL REQUIRED)
find_library(GLU_LIB GLU REQUIRED)
find_library(GLUT_LIB glut REQUIRED)
find_library(LZ4_LIB lz4 REQUIRED)
set(JPEG_LIB JPEG::JPEG)
set(Z_LIB ZLIB::ZLIB)
add_definitions(-DLINUX -DTNZ_LITTLE_ENDIAN=1 -Dx64)
include_directories("@ROOT@/toonz/sources/include" ${JPEG_INCLUDE_DIRS})
add_subdirectory("@ROOT@/toonz/sources/tnzcore" core)
'''.replace('@ROOT@', str(root)))
logged('core-configure', 'cmake', '-S', str(core), '-B', '/tmp/trail-size-core-build', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release')
logged('core-build', 'cmake', '--build', '/tmp/trail-size-core-build', '--parallel', '4')
library = next(Path('/tmp/trail-size-core-build').rglob('libtnzcore.so'))
logged('tests-configure', 'cmake', '-S', 'toonz/tests/trail_settings', '-B', '/tmp/trail-size-tests', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DTNZCORE_LIBRARY=' + str(library))
logged('tests-build', 'cmake', '--build', '/tmp/trail-size-tests', '--parallel', '4')
logged('tests', 'ctest', '--test-dir', '/tmp/trail-size-tests', '--output-on-failure')
assert out('git', 'ls-remote', 'origin', 'refs/heads/' + BRANCH).split()[0] == BASE, 'PR changed during validation'
run('git', 'config', 'user.name', 'Rodney')
run('git', 'config', 'user.email', 'rodney.baker@gmail.com')
message = '''Add Trail Size Multiplier below Frame Offset

Expose a live per-style 0.25-10.0 multiplier, default 1.0, through the existing
Style Editor Settings page. Scale raster, converted TLV and vector artwork and
spacing without changing global brush size, source pixels or click carriers.
Preserve Frame Offset and committed stroke sequences. Include scaled rotated
bounds and stamp-aware early culling. Save optional locale-independent size
metadata in palettes and PLI; copy/clone and palette animation retain the value.

Validation: changed-line clang-format 14, whitespace and changed-source syntax
checks, matching tnzcore build, and all seven Trail CTests pass. Coverage adds
fractional scaling, real transforms/bounds, copy/clone, palette/PLI round trips,
palette animation and preservation of Frame Offset. Full Windows/macOS UI and
OpenGL application testing remains pending.

ChatGPT (GPT-6 Astra Pro) assisted this follow-up to PR #221.
'''
(report / 'commit.txt').write_text(message)
run('git', 'commit', '-F', str(report / 'commit.txt'))
# This one explicit branch update is the requested PR publication; never force.
run('git', 'push', 'origin', 'HEAD:refs/heads/' + BRANCH)
(report / 'head.txt').write_text(out('git', 'rev-parse', 'HEAD'))
