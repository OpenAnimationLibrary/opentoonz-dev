import unittest
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from brand_build import (SPLASH_ANCHOR, SPLASH_FILENAME, TEMPLATE_NAME,
                         TITLE_ANCHOR, brand_startup, brand_window, fields_for, render)


class BrandingTest(unittest.TestCase):
    def test_pr_metadata_is_xml_data_and_substituted_once(self):
        event = {"pull_request": {"number": 260, "title": 'A & B <brush> "test"',
                                  "body": "# Fix\n- Keep [fills](https://example.org) & {{SHORT_SHA}}"}}
        fields = fields_for(event, "pull_request", "abcdef123456", "260/merge")
        template = '<svg><title>{{BUILD_LABEL}}</title><desc>{{DESCRIPTION}}</desc></svg>'
        root = ET.fromstring(render(template, fields))
        self.assertEqual(root.findtext("desc"), "Fix Keep fills & {{SHORT_SHA}}")
        self.assertIn("OT-Dev PR #260", root.findtext("title"))
        self.assertIn("<brush>", fields["PR_HEADING"])

    def test_long_text_fits_the_template_fields(self):
        event = {"pull_request": {"number": 260, "title": "W" * 200, "body": "Wide " * 100}}
        fields = fields_for(event, "pull_request", "abcdef123456", "260/merge")
        for prefix, width in (("PR_HEADING", 37), ("DESCRIPTION", 47)):
            for n in (1, 2):
                self.assertLessEqual(len(fields[f"{prefix}_LINE_{n}"]), width)
        self.assertTrue(fields["DESCRIPTION_LINE_2"].endswith("..."))

    def test_empty_pr_description_and_push_have_useful_fallbacks(self):
        fields = fields_for({"pull_request": {"number": 260, "title": "Fix", "body": None}},
                            "pull_request", "abcdef123456", "260/merge")
        self.assertIn("pull-request build", fields["DESCRIPTION"])
        push = fields_for({}, "push", "abcdef123456", "edition")
        self.assertEqual(push["BUILD_LABEL"], "OT-Dev edition @ abcdef1")
        self.assertIn("edition build", push["PR_HEADING"])

    def test_window_overlay_fails_when_the_source_anchor_changes(self):
        self.assertIn('[OT-Dev PR #260]', brand_window(TITLE_ANCHOR, "OT-Dev PR #260"))
        patched = brand_window(TITLE_ANCHOR * 2, "OT-Dev PR #260")
        self.assertEqual(patched.count("[OT-Dev PR #260]"), 2)
        with self.assertRaises(ValueError):
            brand_window("changed source", "OT-Dev PR #260")

    def test_real_svg_template_renders_without_missing_fields(self):
        template = Path(__file__).with_name(TEMPLATE_NAME).read_text(encoding="utf-8")
        for event, kind, ref in (({}, "push", "edition"),
                                ({"pull_request": {"number": 260, "title": "Restore OT-Dev branding",
                                                   "body": "Show the current PR and its build metadata."}},
                                 "pull_request", "260/merge")):
            svg = render(template, fields_for(event, kind, "abcdef123456", ref))
            self.assertNotIn("{{", svg)
            self.assertEqual(ET.fromstring(svg).tag, "{http://www.w3.org/2000/svg}svg")

    def test_legacy_template_remains_usable(self):
        template = Path(__file__).with_name("splash-template-legacy.svg").read_text(encoding="utf-8")
        svg = render(template, fields_for({}, "push", "abcdef123456", "main"))
        self.assertNotIn("{{", svg)
        ET.fromstring(svg)
        with self.assertRaises(ValueError):
            brand_startup("changed startup source")

    def test_cli_uses_new_artwork_despite_existing_legacy_splash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "toonz/sources/toonz"
            (source / "Resources").mkdir(parents=True)
            (source / "mainwindow.cpp").write_text(TITLE_ANCHOR * 2, encoding="utf-8")
            (source / "main.cpp").write_text(SPLASH_ANCHOR, encoding="utf-8")
            compiled = source / "Resources/splash.svg"
            compiled.write_text("old compiled splash", encoding="utf-8")
            legacy = root / "splash.svg"
            legacy.write_text("old external splash", encoding="utf-8")
            event = root / "event.json"
            event.write_text(json.dumps({"pull_request": {
                "number": 260, "title": "Current PR & new artwork", "body": "Live metadata"
            }}), encoding="utf-8")
            output = root / SPLASH_FILENAME
            subprocess.run([sys.executable, str(Path(__file__).with_name("brand_build.py")),
                            "--output", str(output), "--source-root", str(root),
                            "--event-path", str(event), "--event-name", "pull_request",
                            "--sha", "abcdef123456"], check=True, capture_output=True)
            svg = output.read_text(encoding="utf-8")
            self.assertEqual(output.read_bytes(), compiled.read_bytes())
            self.assertEqual(legacy.read_text(encoding="utf-8"), "old external splash")
            self.assertIn(f'filePath("{SPLASH_FILENAME}")',
                          (source / "main.cpp").read_text(encoding="utf-8"))
            self.assertEqual(ET.fromstring(svg).get("data-otdev-artwork"), "assembly-line")
            self.assertIn("PR #260", svg)
            self.assertIn("abcdef1", svg)
            self.assertNotIn("{{", svg)


if __name__ == "__main__":
    unittest.main()
