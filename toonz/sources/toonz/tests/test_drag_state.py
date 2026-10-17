#!/usr/bin/env python3
"""Compile current production function bodies with explicit dependency doubles.

No Qt/application/Plastic behavior is mocked implicitly: see *_fixture.cpp.
The native xsheet_empty_cells CTest covers real storage/Plastic components.
Run with Python 3 and a C++17 compiler; CXX may select clang++ instead of g++.
"""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SOURCES = HERE.parent.parent


def function(path, signature):
    source = (SOURCES / path).read_text()
    if signature.startswith("void LevelMoverUndo::"):
        source = source[source.index("class LevelMoverUndo final"):]
        original = signature.replace("LevelMoverUndo::", "")
        result = function_body(source, original + " const override")
        return result.replace(original + " const override", signature + " const", 1)
    result = function_body(source, signature)
    return result + ";" if signature.startswith("class ") else result


def function_body(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    suites = {
        "empty_cells": [
            ("toonzlib/txsheet.cpp", "bool TXsheet::setCells("),
            ("toonzlib/txsheet.cpp", "TXshColumn *TXsheet::touchColumn("),
        ],
        "cell_mover": [
            ("toonz/xshcellmover.cpp", "void LevelMoverUndo::undo()"),
            ("toonz/xshcellmover.cpp", "void LevelMoverUndo::redo()"),
            ("toonz/xshcellmover.cpp", "void LevelMoverTool::onCellChange("),
            ("toonz/xshcellmover.cpp", "LevelMoverTool::~LevelMoverTool()"),
        ],
        "keyframe_tool": [
            ("toonz/keyframemover.cpp", "void KeyframeMoverTool::onClick("),
            ("toonz/keyframemover.cpp", "void KeyframeMoverTool::onCellChange("),
        ],
        "keyframe_undo": [
            ("toonz/keyframemover.cpp", "class UndoMoveKeyFrame final"),
        ],
        "keyframe_mover": [
            ("toonz/keyframemover.cpp", "bool KeyframeMover::moveKeyframes("),
        ],
    }
    with tempfile.TemporaryDirectory(prefix="ot-drag-state-") as directory:
        for name, functions in suites.items():
            body = "\n".join(function(*item) for item in functions)
            code = (HERE / (name + "_fixture.cpp")).read_text().replace(
                "// PRODUCTION_FUNCTIONS", body
            )
            source = Path(directory) / (name + ".cpp")
            executable = Path(directory) / name
            source.write_text(code)
            subprocess.run(
                [os.environ.get("CXX", "g++"), "-std=c++17", "-O0", "-g",
                 str(source), "-o", str(executable)], check=True
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
