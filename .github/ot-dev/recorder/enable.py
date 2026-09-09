"""CI-only source injection. Normal checkouts/builds never include the recorder."""

import argparse
import os
from pathlib import Path


def inject(root: Path, build_id: str):
    if not build_id or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_" for c in build_id):
        raise ValueError("Invalid CI build identity")
    source = root / "toonz/sources/toonz/main.cpp"
    cmake = root / "toonz/sources/toonz/CMakeLists.txt"
    text = source.read_text(encoding="utf-8")
    anchor = "  int ret = a.exec();"
    include = '#include "mainwindow.h"'
    if text.count(anchor) != 1 or text.count(include) != 1 or "OtDevRecorder" in text:
        raise RuntimeError("Recorder injection anchors changed or already patched")
    hook = f'''  // OT-Dev Windows CI only; not present in normal source builds.
  auto *otDevRecorder = new OtDevRecorder(
      &w, (ToonzFolder::getMyModuleDir() + TFilePath("preferences.ini")).getQString(),
      QStringLiteral("{build_id}"),
      QDir(QCoreApplication::applicationDirPath()).filePath("portablestuff"),
      QDir(QCoreApplication::applicationDirPath()).filePath("otdev-recorder/ffmpeg.exe"));
  QTimer::singleShot(0, otDevRecorder, [otDevRecorder] {{ otDevRecorder->initialize(); }});

'''
    source.write_text(text.replace(include, include + '\n#include "otdevrecorder.h"\n#include <QTimer>')
                     .replace(anchor, hook + anchor), encoding="utf-8")
    with cmake.open("a", encoding="utf-8") as stream:
        stream.write('''
# Injected only by .github/ot-dev/recorder/enable.py in OT-Dev Windows CI.
if(NOT WIN32)
    message(FATAL_ERROR "The OT-Dev recorder injection is Windows CI only")
endif()
target_sources(OpenToonz PRIVATE "${CMAKE_SOURCE_DIR}/../../.github/ot-dev/recorder/otdevrecorder.cpp")
target_include_directories(OpenToonz PRIVATE "${CMAKE_SOURCE_DIR}/../../.github/ot-dev/recorder")
target_link_libraries(OpenToonz user32 gdi32)
''')


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--build-id", required=True)
    args = parser.parse_args()
    if os.environ.get("GITHUB_REPOSITORY", "").lower() != "openanimationlibrary/opentoonz-dev" or os.environ.get("RUNNER_OS") != "Windows":
        raise SystemExit("Only the OT-Dev Windows CI workflow may inject this feature")
    inject(args.root, args.build_id)
