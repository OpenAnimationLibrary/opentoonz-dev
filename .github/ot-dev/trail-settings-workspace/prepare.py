"""Apply the Settings adaptation on its pinned base, never on main."""
import base64
import gzip
import hashlib
from pathlib import Path
import re
import shlex
import subprocess

BASE = "d850472cffdc7c502ba3dfd2ea6fd6293016b81f"
BRANCH = "feature/trail-cycle-style-settings"
EXPECTED = "3085028bdefe4bb9f6d9a245a21328605ceb22d30446efe1944e8d727ed60d56"
root = Path.cwd()
here = Path(__file__).resolve().parent
payload = "".join((here / f"patch-{i}.b64").read_text().strip() for i in range(5))
patch = gzip.decompress(base64.b64decode(payload, validate=True))
assert hashlib.sha256(patch).hexdigest() == EXPECTED, "Patch checksum mismatch"
Path("/tmp/trail-settings.patch").write_bytes(patch)

def run(*args):
    print("+", " ".join(str(x) for x in args), flush=True)
    subprocess.run(args, check=True)

def out(*args):
    return subprocess.check_output(args, text=True)

exists = subprocess.run(["git", "ls-remote", "--exit-code", "origin", "refs/heads/" + BRANCH])
assert exists.returncode == 2, "Target branch already exists or cannot be checked"
run("git", "fetch", "--depth=1", "origin", BASE)
run("git", "checkout", "-b", BRANCH, BASE)
run("git", "apply", "--index", "--whitespace=error", "/tmp/trail-settings.patch")
# Syntax preflight identified the test's use of a forward-declared TStroke.
test = root / "toonz/tests/trail_settings/trailstyle_test.cpp"
text = test.read_text()
assert '#include "tstroke.h"' not in text
test.write_text(text.replace('#include "tvectorimage.h"', '#include "tvectorimage.h"\n#include "tstroke.h"', 1))
paths = out("git", "diff", "--name-only", "HEAD").splitlines()
cpp = [p for p in paths if p.endswith((".h", ".cpp"))]
for attempt in range(6):
    updated = False
    for name in cpp:
        file = root / name
        diff = out("git", "diff", "-U0", "HEAD", "--", name)
        ranges = []
        for start, length in re.findall(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", diff, re.M):
            count = int(length) if length else 1
            if count:
                ranges.append(f"-lines={start}:{int(start) + count - 1}")
        if not ranges:
            continue
        original = file.read_bytes()
        formatted = subprocess.check_output(
            ["clang-format-14", "-style=file", "-assume-filename=" + str(root / "toonz/sources/format.cpp"), *ranges],
            input=original)
        if formatted != original:
            file.write_bytes(formatted)
            updated = True
    if not updated:
        break
else:
    raise RuntimeError("Changed-line formatting did not converge")
run("git", "add", "--", *paths)
run("git", "diff", "--cached", "--check")
Path("/tmp/trail-settings-formatted.patch").write_text(out("git", "diff", "--cached", "--binary"))
run("cmake", "-S", "toonz/tests/trail_settings", "-B", "/tmp/trail-settings-tests")
run("cmake", "--build", "/tmp/trail-settings-tests")
run("ctest", "--test-dir", "/tmp/trail-settings-tests", "--output-on-failure")
qt = shlex.split(out("pkg-config", "--cflags", "Qt5Core", "Qt5Gui", "Qt5Widgets", "Qt5OpenGL",
                    "Qt5Multimedia", "Qt5Network", "Qt5Svg", "Qt5Xml", "Qt5PrintSupport", "Qt5Concurrent",
                    "freetype2", "libmypaint"))
flags = ["g++", "-std=c++17", "-fsyntax-only", "-fPIC", "-DNDEBUG", "-DLINUX", "-DTNZ_LITTLE_ENDIAN=1",
         "-I" + str(root / "toonz/sources/include"), "-I" + str(root / "toonz/sources"),
         "-I" + str(root / "toonz/sources/tnztools"), *qt]
for name in paths:
    if name.endswith(".cpp"):
        print("Syntax:", name, flush=True)
        subprocess.run([*flags, name], check=True)
# The GitHub connector will publish the test workflow separately. The Actions
# token is used only for source changes, with no workflow-write permission.
run("git", "rm", ".github/workflows/trail_settings_tests.yml")
run("git", "config", "user.name", "Rodney")
run("git", "config", "user.email", "rodney.baker@gmail.com")
message = """Add Trail Cycle to Style Editor Settings

Adapt the gesture and per-stroke sequence work from opentoonz/opentoonz#7085
at 2dc7a2ab5a4eda2912698a56c6b33cd9d0403754. Store the drawing mode per Trail
style and expose it next to Distance and Rotation without a Brush toolbar
field or a global cycle preference. Preserve committed stroke sequences.

Keep temporary sequence state outside the palette. Preserve the mode across
palette scrubbing, copy/clone, TPL records and bounded PLI style records.
Retain upstream outline-tag persistence and regression checks. Do not port
the incompatible raster sizing/cropping or texture-quality changes.

Validation: changed-line clang-format 14; git diff --check; compiled gesture
CTest; GNU C++ syntax checks for all changed C++ translation units.
Core-backed palette/PLI runtime tests and application testing remain pending.
ChatGPT (GPT-6 Astra Pro) assisted the adaptation; human review is required.

Co-authored-by: doangelshavewings <216609577+doangelshavewings@users.noreply.github.com>
"""
Path("/tmp/trail-settings-commit.txt").write_text(message)
run("git", "commit", "-F", "/tmp/trail-settings-commit.txt")
run("git", "push", "origin", "HEAD:refs/heads/" + BRANCH)
Path("/tmp/trail-settings-head.txt").write_text(out("git", "rev-parse", "HEAD"))
print(out("git", "diff", "--stat", BASE, "HEAD"))
