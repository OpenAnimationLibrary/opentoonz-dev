"""Apply and validate the click-stamping follow-up only on PR #221's head."""
import hashlib
from pathlib import Path
import re
import shlex
import subprocess

BASE = "ce40147cd6170aba2950605d01baa2053d5c7950"
BRANCH = "feature/trail-cycle-style-settings"
root = Path.cwd()
patch = Path(__file__).with_name("click-stamp.patch").read_bytes()
assert hashlib.sha256(patch).hexdigest() == "3f21ad396485e1621c99ad2f4b005cf771009dc097a8172d482cc034a7ff3c66"
Path("/tmp/click-stamp.patch").write_bytes(patch)

def run(*args):
    print("+", " ".join(args), flush=True)
    subprocess.run(args, check=True)

def out(*args):
    return subprocess.check_output(args, text=True)

live = out("git", "ls-remote", "--exit-code", "origin", "refs/heads/" + BRANCH).split()[0]
assert live == BASE, "Feature branch advanced; do not overwrite concurrent work"
run("git", "fetch", "--depth=1", "origin", BASE)
run("git", "checkout", "-B", BRANCH, BASE)
run("git", "apply", "--index", "--whitespace=error", "/tmp/click-stamp.patch")
paths = out("git", "diff", "--name-only", "HEAD").splitlines()
assert len(paths) == 8 and all(p.startswith(("toonz/", "doc/")) for p in paths)
for attempt in range(6):
    updated = False
    for name in paths:
        if not name.endswith((".cpp", ".h")):
            continue
        file = root / name
        diff = out("git", "diff", "-U0", "HEAD", "--", name)
        ranges = []
        for start, length in re.findall(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", diff, re.M):
            count = int(length) if length else 1
            if count:
                ranges.append(f"-lines={start}:{int(start) + count - 1}")
        if not ranges:
            continue
        data = file.read_bytes()
        formatted = subprocess.check_output(["clang-format-14", "-style=file", "-assume-filename=" + str(root / "toonz/sources/format.cpp"), *ranges], input=data)
        if formatted != data:
            file.write_bytes(formatted)
            updated = True
    if not updated:
        break
else:
    raise RuntimeError("Formatting did not converge")
run("git", "add", "--", *paths)
run("git", "diff", "--cached", "--check")
run("cmake", "-S", "toonz/tests/trail_settings", "-B", "/tmp/trail-click-tests")
run("cmake", "--build", "/tmp/trail-click-tests", "--parallel", "2")
run("ctest", "--test-dir", "/tmp/trail-click-tests", "--output-on-failure")
qt = shlex.split(out("pkg-config", "--cflags", "Qt5Core", "Qt5Gui", "Qt5Widgets", "Qt5OpenGL", "Qt5Multimedia", "Qt5Network", "Qt5Svg", "Qt5Xml", "Qt5PrintSupport", "Qt5Concurrent", "freetype2", "libmypaint"))
flags = ["g++", "-std=c++17", "-fsyntax-only", "-fPIC", "-DNDEBUG", "-DLINUX", "-DTNZ_LITTLE_ENDIAN=1", "-Itoonz/sources/include", "-Itoonz/sources", "-Itoonz/sources/tnztools", *qt]
for name in paths:
    if name.endswith(".cpp"):
        print("Syntax:", name, flush=True)
        subprocess.run([*flags, name], check=True)
Path("/tmp/trail-click-formatted.patch").write_text(out("git", "diff", "--cached", "--binary"))
run("git", "config", "user.name", "Rodney")
run("git", "config", "user.email", "rodney.baker@gmail.com")
message = """Allow single-click Trail stamping without a drag

At gesture completion, promote point-like loaded Trail strokes to an open,
one-stage-unit carrier with two quadratic chunks. Preserve the click anchor
and peak stroke thickness. The extent is nonzero and below both renderers'
minimum stamp interval, so it supplies exactly one placement and survives
PLI coordinate quantization. Apply with Cycle Off and single-frame sources
as well as active cycles; leave drags, ordinary dots, motion paths and Frame
Range alone. Prevent self-snapping from closing the synthesized carrier.

Draw singleton vector stamps instead of their empty thin-line approximation.
Keep current size/Rotation, stroke metadata, undo path and one cycle commit
per gesture. No additional toolbar or Settings controls.

Add dependency-light carrier tests and optional real TStroke, vector source
transformation and PLI round-trip regressions. Update manual acceptance notes.

Validation: two compiled CTests pass; changed-line clang-format 14 and git
diff --check pass; all changed C++ translation units pass GNU C++17 syntax
checks. Full application/OpenGL and core-backed runtime tests remain pending.

ChatGPT (GPT-6 Astra Pro) assisted this follow-up to the #7085 adaptation.
"""
Path("/tmp/trail-click-message.txt").write_text(message)
run("git", "commit", "-F", "/tmp/trail-click-message.txt")
# A normal push fails safely if the feature branch advances while testing.
run("git", "push", "origin", "HEAD:refs/heads/" + BRANCH)
Path("/tmp/trail-click-head.txt").write_text(out("git", "rev-parse", "HEAD"))
