"""Verify that a packaged tcomposer answers its early worker probe."""

import json
import os
import subprocess
import sys


def run(executable, *arguments):
    environment = os.environ.copy()
    environment.pop("TOONZROOT", None)
    # The probe should never try to load a Qt GUI platform plugin.
    environment["QT_QPA_PLATFORM"] = "nonexistent_probe_platform"
    return subprocess.run(
        [executable, *arguments],
        capture_output=True,
        text=True,
        timeout=15,
        env=environment,
        check=False,
    )


def main(executable):
    probe = run(executable, "--worker-info-json")
    assert probe.returncode == 0, (probe.returncode, probe.stdout, probe.stderr)
    assert len(probe.stdout.splitlines()) == 1, probe.stdout
    assert not probe.stderr, probe.stderr
    info = json.loads(probe.stdout)
    assert info["schema_version"] == 1, info
    assert info["role"] == "tcomposer", info
    assert info["features"]["scene_render"] is True, info
    assert info["features"]["timing_jsonl"] is True, info
    assert info["build_abi"], info

    invalid = run(executable, "--worker-info-json", "unexpected")
    assert invalid.returncode != 0, (invalid.stdout, invalid.stderr)
    assert not invalid.stdout, invalid.stdout


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: tcomposer_worker_probe_smoke.py path/to/tcomposer")
    main(sys.argv[1])
