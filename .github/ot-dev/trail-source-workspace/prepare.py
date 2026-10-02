"""Validate source support and fast-forward only the existing PR #221 branch."""
import base64
import gzip
import hashlib
from pathlib import Path
import re
import shlex
import subprocess

BASE = '8cbc9ebf0a81834b0153e1ed07dce2902a497352'
BRANCH = 'feature/trail-cycle-style-settings'
root = Path.cwd()
here = Path(__file__).resolve().parent
payload = ''.join((here / f'patch-{i}.b64').read_text().strip() for i in range(2))
patch = gzip.decompress(base64.b64decode(payload, validate=True))
assert hashlib.sha256(patch).hexdigest() == 'f83a2adf6be157daf1bc36df4ee73cca8a2041783ba61c36e016d1cbc70d9453'
report = Path('/tmp/trail-source-report')
report.mkdir(exist_ok=True)

def out(*args):
    return subprocess.check_output(args, text=True)

def run(*args):
    print('+', ' '.join(args), flush=True)
    subprocess.run(args, check=True)

def logged(name, *args):
    print('+', ' '.join(args), flush=True)
    with (report / (name + '.log')).open('w') as stream:
        result = subprocess.run(args, stdout=stream, stderr=subprocess.STDOUT)
    text = (report / (name + '.log')).read_text(errors='replace')
    print('\n'.join(text.splitlines()[-90:]), flush=True)
    result.check_returncode()

assert out('git', 'ls-remote', 'origin', 'refs/heads/' + BRANCH).split()[0] == BASE, 'PR head changed; do not overwrite'
(report / 'sources.patch').write_bytes(patch)
run('git', 'fetch', '--depth=1', 'origin', BASE)
run('git', 'checkout', '-b', 'trail-sources-implementation', BASE)
run('git', 'apply', '--index', '--whitespace=error', str(report / 'sources.patch'))
paths = out('git', 'diff', '--name-only', 'HEAD').splitlines()
for attempt in range(6):
    changed = False
    for name in paths:
        if not name.endswith(('.cpp', '.h')):
            continue
        ranges = []
        diff = out('git', 'diff', '-U0', 'HEAD', '--', name)
        for start, length in re.findall(r'^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@', diff, re.M):
            count = int(length) if length else 1
            if count:
                ranges.append(f'-lines={start}:{int(start) + count - 1}')
        if not ranges:
            continue
        file = root / name
        original = file.read_bytes()
        formatted = subprocess.check_output(['clang-format-14', '-style=file', '-assume-filename=' + str(root / 'toonz/sources/format.cpp'), *ranges], input=original)
        if formatted != original:
            file.write_bytes(formatted)
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
run('cmake', '-S', 'toonz/tests/trail_settings', '-B', '/tmp/trail-source-light')
run('cmake', '--build', '/tmp/trail-source-light')
logged('light-tests', 'ctest', '--test-dir', '/tmp/trail-source-light', '--output-on-failure')
# Build the actual core (not mocked conversion functions), independently of
# unrelated application/FX dependency configuration. Use the checkout's sources.
core = Path('/tmp/trail-source-core')
core.mkdir(exist_ok=True)
(core / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.16)
project(TrailSourceCore LANGUAGES C CXX)
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
set(LZO_INCLUDE_DIR /usr/include)
find_library(LZO_LIBRARY lzo2 REQUIRED)
add_subdirectory("@ROOT@/thirdparty/lzo/driver" lzodriver)
'''.replace('@ROOT@', str(root)))
logged('core-configure', 'cmake', '-S', str(core), '-B', '/tmp/trail-source-core-build', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release')
logged('core-build', 'cmake', '--build', '/tmp/trail-source-core-build', '--parallel', '4')
library = next(Path('/tmp/trail-source-core-build').rglob('libtnzcore.so'))
helpers = next(Path('/tmp/trail-source-core-build').rglob('lzocompress')).parent
logged('source-configure', 'cmake', '-S', 'toonz/tests/trail_settings', '-B', '/tmp/trail-source-tests', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DTNZCORE_LIBRARY=' + str(library), '-DBUILD_TRAIL_SOURCE_TESTS=ON', '-DTRAIL_LZO_BIN_DIR=' + str(helpers))
logged('source-build', 'cmake', '--build', '/tmp/trail-source-tests', '--target', 'trailsource_test', '--parallel', '4')
logged('source-tests', 'ctest', '--test-dir', '/tmp/trail-source-tests', '--output-on-failure', '-R', '^trailsource$')
assert out('git', 'ls-remote', 'origin', 'refs/heads/' + BRANCH).split()[0] == BASE, 'PR changed during testing'
run('git', 'config', 'user.name', 'Rodney')
run('git', 'config', 'user.email', 'rodney.baker@gmail.com')
message = '''Add Toonz Raster Trail sources and recognize raster formats

Load TLV/TPL sources through palette-aware conversion, keeping canvas bounds,
blank sequence frames, antialiasing, opacity and source-frame palette colors.
Normalize grayscale/high-depth raster inputs for the existing texture path.
Share source format filters between the Trail chooser and renderer; recognize
PNG/TIF/TIFF and other supported raster formats case-insensitively, excluding
palette sidecars and backups. Use the same conversion for chooser thumbnails.
Preserve legacy source sizing, existing RGBA blend behavior, click stamping,
cycle defaults and per-stroke metadata. TLV pixels use premultiplied blending.

Validation: changed-line clang-format 14, diff whitespace check, GNU C++17
syntax checks, two compiled gesture/carrier tests, a built matching tnzcore,
and real source IO/pixel regressions pass. Full desktop/OpenGL/Windows
application testing remains pending on PR #221.

ChatGPT (GPT-6 Astra Pro) assisted implementation and testing.
'''
(report / 'commit.txt').write_text(message)
run('git', 'commit', '-F', str(report / 'commit.txt'))
run('git', 'push', 'origin', 'HEAD:refs/heads/' + BRANCH)
(report / 'head.txt').write_text(out('git', 'rev-parse', 'HEAD'))
