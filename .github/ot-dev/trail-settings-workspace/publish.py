"""Publish the scoped OT-Dev testing branch without waiting for package mirrors."""
import base64
import gzip
import hashlib
from pathlib import Path
import re
import subprocess

BASE = "d850472cffdc7c502ba3dfd2ea6fd6293016b81f"
BRANCH = "feature/trail-cycle-style-settings"
root = Path.cwd()
here = Path(__file__).resolve().parent
payload = "".join((here / f"patch-{i}.b64").read_text().strip() for i in range(5))
patch = gzip.decompress(base64.b64decode(payload, validate=True))
assert hashlib.sha256(patch).hexdigest() == "3085028bdefe4bb9f6d9a245a21328605ceb22d30446efe1944e8d727ed60d56"

def run(*args):
    print("+", " ".join(args), flush=True)
    subprocess.run(args, check=True)

def out(*args):
    return subprocess.check_output(args, text=True)

existing = subprocess.run(["git", "ls-remote", "--exit-code", "origin", "refs/heads/" + BRANCH], capture_output=True, text=True)
if existing.returncode == 0:
    print("An existing feature branch will not be overwritten:", existing.stdout)
    raise SystemExit(0)
assert existing.returncode == 2, existing.stderr
Path("/tmp/trail-settings.patch").write_bytes(patch)
run("git", "fetch", "--depth=1", "origin", BASE)
run("git", "checkout", "-b", BRANCH, BASE)
run("git", "apply", "--index", "--whitespace=error", "/tmp/trail-settings.patch")
test = root / "toonz/tests/trail_settings/trailstyle_test.cpp"
test.write_text(test.read_text().replace('#include "tvectorimage.h"', '#include "tvectorimage.h"\n#include "tstroke.h"', 1))
paths = out("git", "diff", "--name-only", "HEAD").splitlines()
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
Path("/tmp/trail-settings-formatted.patch").write_text(out("git", "diff", "--cached", "--binary"))
run("cmake", "-S", "toonz/tests/trail_settings", "-B", "/tmp/trail-settings-tests")
run("cmake", "--build", "/tmp/trail-settings-tests")
run("ctest", "--test-dir", "/tmp/trail-settings-tests", "--output-on-failure")
run("git", "rm", ".github/workflows/trail_settings_tests.yml")
run("git", "config", "user.name", "Rodney")
run("git", "config", "user.email", "rodney.baker@gmail.com")
message = """Add Trail Cycle to Style Editor Settings

Adapt opentoonz/opentoonz#7085 at 2dc7a2ab5a4e. Store the drawing mode per
Trail style beside Distance and Rotation, not as a Brush toolbar property.
Keep the temporary cursor outside the palette and preserve each completed
stroke's frame offset/step. Support copy/clone, palette scrubbing, optional
TPL attributes and bounded PLI style metadata. Preserve legacy raster sizing.

Validation: compiled gesture CTest, changed-line clang-format 14 and git
diff --check pass. All modified application C++ files passed GNU C++17
syntax preflight in run 36927085103; its final new test needed tstroke.h,
which is now included. Full application and core-backed runtime tests remain
pending for this OT-Dev testing PR. No upstream merge approval is implied.

ChatGPT (GPT-6 Astra Pro) assisted implementation and testing.

Co-authored-by: doangelshavewings <216609577+doangelshavewings@users.noreply.github.com>
"""
Path("/tmp/trail-settings-message.txt").write_text(message)
run("git", "commit", "-F", "/tmp/trail-settings-message.txt")
# Never force-push: a concurrent publication can only cause a safe rejection.
run("git", "push", "origin", "HEAD:refs/heads/" + BRANCH)
Path("/tmp/trail-settings-head.txt").write_text(out("git", "rev-parse", "HEAD"))
