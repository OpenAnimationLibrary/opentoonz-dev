#!/usr/bin/env python3
"""Exercise the production CMake rules with a translation-only fixture."""

import argparse
from pathlib import Path
import subprocess
import tempfile
import time


def run(*command, succeeds=True):
    result = subprocess.run(command, text=True, capture_output=True)
    if (result.returncode == 0) != succeeds:
        raise AssertionError(result.stdout + result.stderr)
    return result


def catalog(word):
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<TS version="2.1" language="es_ES"><context><name>Fixture</name>'
        '<message><source>Label</source><translation>'
        + word
        + '</translation></message></context></TS>\n'
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--lrelease", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="OT traducción ") as temporary:
        root = Path(temporary)
        source, build, output = root / "source files", root / "build", root / "loc"
        translations = source / "translations" / "spanish"
        translations.mkdir(parents=True)
        ts = translations / "fixture.ts"
        ts.write_text(catalog("Antes"), encoding="utf-8")
        cmake_text = f'''cmake_minimum_required(VERSION 3.14)
project(TranslationFixture LANGUAGES NONE)
set(LANG_CODES spanish)
set(LANG_DISPLAY_NAMES "Español")
set(OT_TRANSLATIONS_SOURCE_DIR "${{CMAKE_CURRENT_SOURCE_DIR}}/translations")
set(OT_TRANSLATIONS_OUTPUT_DIR "{output.as_posix()}")
set(Qt5_LRELEASE_EXECUTABLE "{Path(args.lrelease).as_posix()}")
include("{(args.root / 'toonz/cmake/Translations.cmake').as_posix()}")
add_custom_target(application ALL)
add_compile_translations_command(application fixture)
'''
        (source / "CMakeLists.txt").write_text(cmake_text, encoding="utf-8")
        run(args.cmake, "-S", str(source), "-B", str(build), "-G", "Ninja")

        def rebuild(succeeds=True):
            return run(args.cmake, "--build", str(build), succeeds=succeeds)

        qm = output / "Español" / "fixture.qm"
        rebuild()
        assert qm.is_file()
        before = qm.read_bytes()
        timestamp = qm.stat().st_mtime_ns
        rebuild()
        assert qm.stat().st_mtime_ns == timestamp, "No-op build rewrote the catalog"
        time.sleep(0.05)
        ts.write_text(catalog("Después"), encoding="utf-8")
        rebuild()
        assert qm.read_bytes() != before, "A .ts-only edit did not rebuild the .qm"

        qm.unlink()
        rebuild()
        assert qm.is_file(), "Deleted catalog was not recovered"

        previous = qm.read_bytes()
        ts.write_text("<invalid", encoding="utf-8")
        rebuild(succeeds=False)
        assert qm.read_bytes() == previous, "Failed lrelease damaged the last valid catalog"
        ts.write_text(catalog("Recuperado"), encoding="utf-8")
        rebuild()
        assert qm.read_bytes() != previous, "Build did not recover after an invalid catalog"

        print("Translation-only edits, no-op builds, deletion, and failed compilation passed.")



if __name__ == "__main__":
    main()
