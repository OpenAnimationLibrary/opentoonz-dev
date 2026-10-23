#!/usr/bin/env python3
import concurrent.futures
import argparse
import pathlib
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parent
repo = root.parents[1]
parser = argparse.ArgumentParser(description="Focused Linux raster palette regression harness")
parser.add_argument("--cxx", default="g++")
parser.add_argument("--include-root", type=pathlib.Path,
                    help="Optional extracted development-header prefix (normally use pkg-config)")
parser.add_argument("--build-dir", type=pathlib.Path)
args = parser.parse_args()
temporary = tempfile.TemporaryDirectory(prefix="raster-palette-tests-")
work = args.build_dir or pathlib.Path(temporary.name)
stubs = work / "stubs"
(stubs / "toonz").mkdir(parents=True, exist_ok=True)
hdr = (repo / "toonz/sources/include/toonz/fullcolorpalette.h").read_text()
(stubs / "toonz/fullcolorpalette.h").write_text(hdr.replace("  Q_OBJECT", "  // No moc needed in the test wrapper."))
(stubs / "toonz/tscenehandle.h").write_text("#pragma once\n")
(stubs / "toonz/tproject.h").write_text('''#pragma once
class TProject { public: bool palettesAlias = true; int getFolderIndex(const char*) const {return palettesAlias ? 0 : -1;} };
''')
(stubs / "toonz/toonzscene.h").write_text('''#pragma once
#include "tfilepath.h"
#include "toonz/tproject.h"
class ToonzScene { public:
 TProject project; TFilePath palettes; int clears = 0;
 explicit ToonzScene(TFilePath p): palettes(p) {}
 TProject* getProject() { return &project; }
 TFilePath getScenePath() { return palettes + "../scenes/test.tnz"; }
 void clear() { ++clears; }
 TFilePath decodeFilePath(const TFilePath& path) {
  QString p=path.getQString();
  if(p.startsWith("+palettes/")) return palettes + TFilePath(p.mid(10));
  return path;
 }
};
''')
(stubs / "toonz/toonzfolders.h").write_text('''#pragma once
#include "tfilepath.h"
namespace ToonzFolder { TFilePath getStudioPaletteFolder(); }
''')

def replace_body(src, signature, replacement):
    start = src.index(signature)
    brace = src.index("{", start)
    depth = 1
    i = brace + 1
    while depth:
        if src[i] == "{": depth += 1
        if src[i] == "}": depth -= 1
        i += 1
    return src[:brace] + "{ " + replacement + " }" + src[i:]

colors = (repo / "toonz/sources/common/tvrender/tcolorstyles.cpp").read_text()
colors = colors.replace(", m_icon(0)", ", m_icon()")
colors = replace_body(colors, "void TColorStyle::makeIcon(", "throw TException(\"Rendering is not part of this test.\");")
colors = replace_body(colors, "const TRaster32P &TColorStyle::getIcon(", "throw TException(\"Rendering is not part of this test.\");")
colors = replace_body(colors, "TRectD TColorStyle::getStrokeBBox(", "throw TException(\"Rendering is not part of this test.\");")
(work / "tcolorstyles_without_rendering.cpp").write_text(colors)
palette = (repo / "toonz/sources/common/tvrender/tpalette.cpp").read_text()
palette = replace_body(palette, "TOutputStreamInterface &operator<<(const TRaster32P &ras)", "throw TException(\"Embedded image styles are excluded from this test.\");")
palette = replace_body(palette, "TInputStreamInterface &operator>>(TRaster32P &x)", "throw TException(\"Embedded image styles are excluded from this test.\");")
(work / "tpalette_without_image_codecs.cpp").write_text(palette)

flags = [args.cxx, "-std=c++17", "-O0", "-g", "-DNDEBUG", "-DLINUX", "-DTNZ_LITTLE_ENDIAN=1", "-fPIC", "-ffunction-sections", "-fdata-sections", "-fmax-errors=3", "-I"+str(stubs), "-I"+str(repo / "toonz/sources/include"), "-I"+str(root)]
if args.include_root:
    include = args.include_root / "usr/include"
    qt = include / "x86_64-linux-gnu/qt5"
    flags += ["-I"+str(p) for p in [include, qt]+list(qt.iterdir()) if p.is_dir()]
    libraries = ["-l:libQt5Core.so.5", "-l:libQt5Gui.so.5", "-l:libQt5Network.so.5", "-l:liblz4.so.1"]
else:
    def pkg_config(*options):
        return shlex.split(subprocess.check_output(["pkg-config", *options, "Qt5Core", "Qt5Gui", "Qt5Network", "liblz4"], text=True))
    flags += pkg_config("--cflags")
    libraries = pkg_config("--libs")

def extract_function(path, signature):
    source = (repo / path).read_text()
    start = source.index(signature)
    # Production methods used here have balanced braces in comments/strings.
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

# Compile the actual routing and save/transition decision bodies. All fixture
# classes below are UI/application boundaries; no decision body is duplicated.
methods = [
    ("toonz/sources/toonzlib/tpalettehandle.cpp", "void TPaletteHandle::setPalette(TPalette *palette, int styleIndex)"),
    ("toonz/sources/toonzlib/tpalettehandle.cpp", "void TPaletteHandle::setStyleIndex(int index, bool forceEmit)"),
    ("toonz/sources/toonz/tapp.cpp", "void TApp::updateXshLevel()"),
    ("toonz/sources/toonz/iocommand.cpp", "bool IoCmd::saveNonSceneFiles(int flags)"),
    ("toonz/sources/toonz/iocommand.cpp", "bool IoCmd::saveSceneIfNeeded(QString msg)"),
]
(work / "application_methods.h").write_text("\n\n".join(extract_function(*m) for m in methods))
handlers = ["OverwritePaletteCommandHandler", "SaveAsDefaultRasterPaletteCommandHandler"]
(work / "palette_commands.h").write_text("\n\n".join(
    extract_function("toonz/sources/toonz/iocommand.cpp", "class " + name + " final") + ";"
    for name in handlers))
flags += ["-I"+str(work)]
sources = [
 "toonz/sources/toonzlib/fullcolorpalette.cpp",
 "toonz/sources/common/tstream/tstream.cpp",
 "toonz/sources/common/tsystem/tfilepath.cpp",
 "toonz/sources/common/tsystem/tfilepath_io.cpp",
 "toonz/sources/common/tsystem/tsystem.cpp",
 "toonz/sources/common/tsystem/tsystempd.cpp",
 "toonz/sources/common/tsystem/tfiletype.cpp",
 "toonz/sources/common/tcore/tstring.cpp",
 "toonz/sources/common/tcore/texception.cpp",
 "toonz/sources/common/tcolor/tpixel.cpp",
]
sources = [repo/p for p in sources] + [work / "tpalette_without_image_codecs.cpp", work / "tcolorstyles_without_rendering.cpp", root / "rendering_stubs.cpp", root / "palette_io_test.cpp", root / "application_test.cpp"]

def compile_file(src):
    obj = work/(src.stem+".o")
    p = subprocess.run(flags+["-c",str(src),"-o",str(obj)],text=True,capture_output=True)
    if p.returncode:
        raise RuntimeError(str(src)+"\n"+p.stdout+p.stderr)
    print("Compiled",src.name,flush=True)
    return obj

objects = list(concurrent.futures.ThreadPoolExecutor(max_workers=4).map(compile_file,sources))
cmd=[args.cxx,"-Wl,--gc-sections"]+[str(p) for p in objects]+libraries+["-o",str(work/"palette_harness")]
subprocess.run(cmd,check=True)
subprocess.run([str(work / "palette_harness")], check=True)
