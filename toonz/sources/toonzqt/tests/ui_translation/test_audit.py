#!/usr/bin/env python3
"""Ensure the catalog gate detects real translation regressions."""

import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[5]
spec = importlib.util.spec_from_file_location(
    "audit_ui_translations", ROOT / "doc/tools/audit_ui_translations.py"
)
auditor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(auditor)


class AuditTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        for relative in [
            "toonz/sources/toonzqt/uitranslation.cpp",
            "toonz/sources/tnztools/toonzrasterbrushtool.cpp",
            "toonz/sources/translations/spanish/toonz.ts",
            "toonz/sources/translations/spanish/toonzqt.ts",
            "toonz/sources/translations/spanish/tnztools.ts",
            "toonz/sources/translations/spanish/qt_ui.ts",
        ]:
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        self.catalog = self.root / "toonz/sources/translations/spanish/toonzqt.ts"

    def change(self, before, after):
        text = self.catalog.read_text(encoding="utf-8")
        self.assertIn(before, text)
        self.catalog.write_text(text.replace(before, after, 1), encoding="utf-8")

    def test_valid_catalogs(self):
        self.assertEqual(auditor.audit(self.root)["errors"], [])

    def test_missing_catalog(self):
        self.catalog.unlink()
        self.assertTrue(any("missing catalog" in e for e in auditor.audit(self.root)["errors"]))

    def test_missing_required_string(self):
        self.change("<source>Project root (%1)</source>", "<source>Changed source</source>")
        self.assertTrue(any("missing entry" in e for e in auditor.audit(self.root)["errors"]))

    def test_invalid_placeholder(self):
        self.change("Raíz del proyecto (%1)", "Raíz del proyecto (%2)")
        self.assertTrue(any("placeholder mismatch" in e for e in auditor.audit(self.root)["errors"]))

    def test_unfinished_required_translation(self):
        self.change("<translation>Raíz del proyecto (%1)</translation>",
                    '<translation type="unfinished">Raíz del proyecto (%1)</translation>')
        self.assertTrue(any("unfinished" in e for e in auditor.audit(self.root)["errors"]))

    def test_wrong_locale(self):
        self.change('language="es_ES"', 'language="pt_BR"')
        self.assertTrue(any("catalog language" in e for e in auditor.audit(self.root)["errors"]))

    def test_menu_mnemonic(self):
        self.catalog = self.root / "toonz/sources/translations/spanish/qt_ui.ts"
        self.change("<translation>&amp;Sí</translation>", "<translation>Sí</translation>")
        self.assertTrue(any("missing menu mnemonic" in e for e in auditor.audit(self.root)["errors"]))

    def test_new_placeholder_error_outside_issue_scope(self):
        text = self.catalog.read_text(encoding="utf-8")
        extra = '<context><name>Fixture</name><message><source>Item %1</source><translation>Elemento %1</translation></message></context>'
        self.catalog.write_text(text.replace("</TS>", extra + "</TS>"), encoding="utf-8")
        for command in [
            ["git", "init", "-q"], ["git", "add", "."],
            ["git", "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
             "commit", "-qm", "Baseline"],
        ]:
            subprocess.run(command, cwd=self.root, check=True, capture_output=True)
        self.change("<translation>Elemento %1</translation>", "<translation>Elemento %2</translation>")
        self.assertEqual(auditor.audit(self.root)["errors"], [])
        self.assertTrue(any("placeholder mismatch" in e for e in auditor.audit(self.root, "HEAD")["errors"]))


if __name__ == "__main__":
    unittest.main()
