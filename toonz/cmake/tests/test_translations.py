#!/usr/bin/env python3
"""Exercise production extraction and compilation together, without OpenToonz."""

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
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<TS version="2.1" language="es_ES"><context><name>Fixture</name>'
            '<message><source>Label</source><translation>' + word +
            '</translation></message></context></TS>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--lupdate", required=True)
    parser.add_argument("--lrelease", required=True)
    args = parser.parse_args()
    helper = Path(__file__).resolve().parents[1] / "Translations.cmake"
    with tempfile.TemporaryDirectory(prefix="OT traducción ") as temporary:
        root = Path(temporary)
        source, build, output = root / "source files", root / "build", root / "loc"
        translations = source / "translations" / "spanish"
        translations.mkdir(parents=True)
        ts = translations / "fixture.ts"
        ts.write_text(catalog("Antes"), encoding="utf-8")
        cpp = source / "fixture.cpp"
        cpp.write_text('class Fixture { Q_DECLARE_TR_FUNCTIONS(Fixture)\n'
                       'void label() { tr("Label"); } };\n', encoding="utf-8")
        (source / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.14)
project(TranslationFixture LANGUAGES NONE)
option(WITH_TRANSLATION "Translations" ON)
add_custom_target(application ALL)
if(WITH_TRANSLATION)
    set(LANG_CODES spanish)
    set(LANG_DISPLAY_NAMES "Español")
    set(OT_TRANSLATIONS_SOURCE_DIR "${{CMAKE_CURRENT_SOURCE_DIR}}/translations")
    set(OT_TRANSLATIONS_OUTPUT_DIR "{output.as_posix()}")
    set(Qt5_LUPDATE_EXECUTABLE "{Path(args.lupdate).resolve().as_posix()}")
    set(Qt5_LRELEASE_EXECUTABLE "{Path(args.lrelease).resolve().as_posix()}")
    include("{helper.as_posix()}")
    add_translation(fixture fixture.cpp)
    add_compile_translations_command(application fixture)
endif()
''', encoding="utf-8")

        def configure(*extra):
            run(args.cmake, "-S", str(source), "-B", str(build),
                "-G", args.generator, *extra)

        def rebuild(*extra, succeeds=True):
            return run(args.cmake, "--build", str(build), "--config", "Release",
                       *extra, succeeds=succeeds)

        configure()
        original = ts.read_bytes()
        rebuild()
        qm = output / "Español" / "fixture.qm"
        assert qm.is_file()
        assert ts.read_bytes() == original, "Normal build rewrote .ts"
        assert not (build / "loc").exists(), "Normal build invoked extraction"
        stamp = qm.stat().st_mtime_ns
        no_op = rebuild("--verbose", *(["--", "/verbosity:diagnostic"]\n                                        if args.generator.startswith("Visual Studio") else []))
        assert qm.stat().st_mtime_ns == stamp, (
            "No-op build rewrote .qm\n" + no_op.stdout + no_op.stderr)
        before = qm.read_bytes()
        time.sleep(1.1)  # Also cover filesystems with coarse timestamp precision.
        ts.write_text(catalog("Después"), encoding="utf-8")
        rebuild()
        assert qm.read_bytes() != before, ".ts-only edit failed to rebuild"
        qm.unlink()
        rebuild()
        assert qm.is_file(), "Deleted .qm was not rebuilt"
        previous = qm.read_bytes()
        time.sleep(1.1)
        ts.write_text("<invalid", encoding="utf-8")
        rebuild(succeeds=False)
        assert qm.read_bytes() == previous, "Failure damaged last valid .qm"
        ts.write_text(catalog("Recuperado"), encoding="utf-8")
        rebuild()
        assert qm.read_bytes() != previous
        original = ts.read_bytes()
        cpp.write_text(cpp.read_text(encoding="utf-8").replace(
            'tr("Label");', 'tr("Label"); tr("New label");'), encoding="utf-8")
        rebuild()
        assert ts.read_bytes() == original, "C++ edit caused extraction"
        rebuild("--target", "translate_update_fixture")
        assert "New label" in ts.read_text(encoding="utf-8")
        assert (build / "loc/spanish/fixture.qm").is_file()
        rebuild()
        maintained = ts.read_bytes()
        rebuild("--target", "clean")
        assert ts.read_bytes() == maintained, "Clean deleted/changed source catalog"
        assert not qm.exists(), "Clean left the declared runtime output"
        rebuild()
        assert qm.is_file()

        # A separate translation-disabled build must not generate catalogs.
        build = root / "disabled build"
        configure("-DWITH_TRANSLATION=OFF")
        stamp = qm.stat().st_mtime_ns
        rebuild()
        assert not (build / "loc").exists()
        assert qm.stat().st_mtime_ns == stamp
        assert ts.read_bytes() == maintained
        print("Extraction isolation, incremental compilation, recovery, clean, "
              "and translation-disabled builds passed.")


if __name__ == "__main__":
    main()
