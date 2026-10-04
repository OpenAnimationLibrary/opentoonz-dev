import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("enable", Path(__file__).with_name("enable.py"))
enable = importlib.util.module_from_spec(spec)
spec.loader.exec_module(enable)


class InjectionTest(unittest.TestCase):
    def test_inject_once_and_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "toonz/sources/toonz"
            target.mkdir(parents=True)
            source = target / "main.cpp"
            source.write_text('#include "mainwindow.h"\n  int ret = a.exec();', encoding="utf-8")
            (target / "CMakeLists.txt").write_text("# existing build\n", encoding="utf-8")
            enable.inject(root, "test-build-123")
            result = source.read_text(encoding="utf-8")
            self.assertEqual(result.count("new OtDevRecorder("), 1)
            self.assertIn('filePath("portablestuff")', result)
            self.assertIn('TFilePath("preferences.ini")', result)
            self.assertIn('filePath("otdev-recorder/ffmpeg.exe")', result)
            with self.assertRaises(RuntimeError):
                enable.inject(root, "test-build-123")
            self.assertEqual(result, source.read_text(encoding="utf-8"))

    def test_bad_identity_rejected_before_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(ValueError):
                enable.inject(Path(temporary), 'bad"identity')


if __name__ == "__main__":
    unittest.main()
