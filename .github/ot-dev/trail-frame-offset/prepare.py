"""Validate and fast-forward the existing PR #221, never main or another head."""
import base64
import gzip
import hashlib
from pathlib import Path
import re
import shlex
import subprocess

BASE = 'e5fac895f988ce80304dae275486183c26ce9b86'
BRANCH = 'feature/trail-cycle-style-settings'
root = Path.cwd()
here = Path(__file__).resolve().parent
patch = gzip.decompress(base64.b64decode(''.join((here / f'patch-{i}.b64').read_text().strip() for i in range(2)), validate=True))
assert hashlib.sha256(patch).hexdigest() == '7531ecaba15a4bb8f88449dc6f1d6396e87406edb01d4cc498694f76d9688c8a'
report = Path('/tmp/trail-offset-report')
report.mkdir(exist_ok=True)

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
    print('\n'.join(log.read_text(errors='replace').splitlines()[-70:]), flush=True)
    result.check_returncode()

assert out('git', 'ls-remote', 'origin', 'refs/heads/' + BRANCH).split()[0] == BASE, 'PR head changed'
(report / 'offset.patch').write_bytes(patch)
run('git', 'fetch', '--depth=1', 'origin', BASE)
run('git', 'checkout', '-b', 'trail-offset-implementation', BASE)
run('git', 'apply', '--index', '--whitespace=error', str(report / 'offset.patch'))
paths = out('git', 'diff', '--name-only', 'HEAD').splitlines()
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
core = Path('/tmp/trail-offset-core')
core.mkdir(exist_ok=True)
(core / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.16)
project(TrailOffsetCore LANGUAGES C CXX)
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
logged('core-configure', 'cmake', '-S', str(core), '-B', '/tmp/trail-offset-core-build', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release')
logged('core-build', 'cmake', '--build', '/tmp/trail-offset-core-build', '--parallel', '4')
library = next(Path('/tmp/trail-offset-core-build').rglob('libtnzcore.so'))
logged('tests-configure', 'cmake', '-S', 'toonz/tests/trail_settings', '-B', '/tmp/trail-offset-tests', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DTNZCORE_LIBRARY=' + str(library))
logged('tests-build', 'cmake', '--build', '/tmp/trail-offset-tests', '--parallel', '4')
logged('tests', 'ctest', '--test-dir', '/tmp/trail-offset-tests', '--output-on-failure')
assert out('git', 'ls-remote', 'origin', 'refs/heads/' + BRANCH).split()[0] == BASE, 'PR changed during validation'
run('git', 'config', 'user.name', 'Rodney')
run('git', 'config', 'user.email', 'rodney.baker@gmail.com')
message = '''Add Frame Offset below Trail Cycle in Style Editor Settings

Select a starting/repeating source drawing per Trail style. Zero preserves
the existing automatic/shared-cursor behavior; positive values resolve exact
numeric source frame IDs, not ordinal positions or a guessed 1..count range.
Ignore unavailable drawings without wrapping or resetting an existing cycle.
Repeat holds a valid chosen drawing; Forward/Backward advance after starting.
Off can set a stroke start without replacing the retained cursor/palette.

Persist the non-animated default through TPL attributes and bounded PLI style
metadata, copy/clone and palette scrubbing. Preserve already drawn stroke
sequences, click carriers, Frame Range behavior and legacy raster sizing.

Validation: changed-line clang-format 14, git diff --check, changed C++ syntax
checks, rebuilt matching tnzcore and all five Trail CTests pass. New coverage
includes sparse BMP/PLI IDs, gaps/letter suffixes, explicit starts, Repeat,
cancellation, integer boundaries, and palette/PLI persistence with Cycle Off.
Full Windows/macOS UI and OpenGL application testing remains pending.

ChatGPT (GPT-6 Astra Pro) assisted implementation and testing.
'''
(report / 'commit.txt').write_text(message)
run('git', 'commit', '-F', str(report / 'commit.txt'))
run('git', 'push', 'origin', 'HEAD:refs/heads/' + BRANCH)
(report / 'head.txt').write_text(out('git', 'rev-parse', 'HEAD'))
