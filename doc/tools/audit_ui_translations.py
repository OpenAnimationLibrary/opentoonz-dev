#!/usr/bin/env python3
"""Check issue-specific coverage and report the broader translation backlog."""

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


PLACEHOLDER = re.compile(r"%(?:L?[1-9][0-9]*|L?n)")


def placeholders(text):
    return Counter(PLACEHOLDER.findall(text or ""))


def translation_forms(element):
    forms = element.findall("numerusform")
    return ["".join(form.itertext()) for form in forms or [element]]


def requirements(root):
    helper = (root / "toonz/sources/toonzqt/uitranslation.cpp").read_text(
        encoding="utf-8"
    )
    scopes = {
        ("toonzqt", "FontStyleNames"): set(
            re.findall(r'QT_TRANSLATE_NOOP\("FontStyleNames",\s*"([^"]+)"\)', helper)
        ),
        ("toonzqt", "FileBrowserLocations"): set(
            re.findall(
                r'translate\("FileBrowserLocations",\s*"([^"]+)"\)', helper
            )
        ),
        ("toonz", "XsheetGUI::NoteArea"): {
            "Toggle Xsheet/Timeline",
            "Add New Memo",
        },
        ("toonz", "MainWindow"): {"&Timeline"},
        ("qt_ui", "QPlatformTheme"): {
            "OK", "Save", "Save All", "Open", "&Yes", "Yes to &All", "&No",
            "N&o to All", "Abort", "Retry", "Ignore", "Close", "Cancel",
            "Discard", "Help", "Apply", "Reset", "Restore Defaults",
        },
        ("qt_ui", "QDialogButtonBox"): {"OK"},
    }
    source = (root / "toonz/sources/tnztools/toonzrasterbrushtool.cpp").read_text(
        encoding="utf-8"
    )
    body = re.search(
        r"void ToonzRasterBrushTool::updateTranslation\(\) \{(.*?)\n\}",
        source,
        re.DOTALL,
    ).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    scopes[("tnztools", "ToonzRasterBrushTool")] = set(
        re.findall(r'tr\("([^"]+)"\)', body)
    )
    if any(not sources for sources in scopes.values()):
        raise ValueError("Could not discover required translation strings")
    return scopes


def signatures(catalog):
    result = {}
    for context in catalog.findall("context"):
        for message in context.findall("message"):
            translation = message.find("translation")
            if translation is not None:
                key = (context.findtext("name"), message.findtext("source"),
                       message.findtext("comment", ""))
                result[key] = (translation.get("type", ""),
                               tuple(translation_forms(translation)))
    return result


def audit(root, base_ref=None):
    scopes = requirements(root)
    errors, backlog, checked = [], [], []
    base = root / "toonz/sources/translations"
    language_codes = {
        "chinese": "zh", "czech": "cs", "french": "fr", "german": "de",
        "italian": "it", "japanese": "ja", "korean": "ko",
        "norwegian_bokmal": "nb", "portuguese_brazil": "pt",
        "russian": "ru", "spanish": "es",
    }
    required_modules = {module for module, _ in scopes}
    for directory in sorted(base.iterdir()):
        if directory.is_dir() and directory.name in language_codes:
            for module in sorted(required_modules):
                path = directory / (module + ".ts")
                if not path.is_file():
                    errors.append(f"{path.relative_to(root)}: missing catalog")
    for path in sorted(base.glob("*/*.ts")):
        relative = str(path.relative_to(root))
        try:
            catalog = ET.parse(path).getroot()
        except ET.ParseError as exc:
            errors.append(f"{relative}: invalid XML: {exc}")
            continue
        language = catalog.get("language", "").split("_")[0]
        if not language or language != language_codes.get(path.parent.name, language):
            errors.append(f"{relative}: missing or incorrect catalog language")
        baseline = None
        if base_ref:
            previous = subprocess.run(
                ["git", "show", "--end-of-options", f"{base_ref}:{relative}"],
                cwd=root, text=True, capture_output=True,
            )
            baseline = signatures(ET.fromstring(previous.stdout)) if previous.returncode == 0 else {}
        seen = set()
        for context in catalog.findall("context"):
            name = context.findtext("name", "")
            required = scopes.get((path.stem, name), set())
            for message in context.findall("message"):
                source = message.findtext("source", "")
                translation = message.find("translation")
                state = (
                    translation.get("type", "")
                    if translation is not None
                    else "missing"
                )
                if state in {"vanished", "obsolete"}:
                    continue
                key = f"{relative}: {name}: {source}"
                forms = translation_forms(translation) if translation is not None else []
                incomplete = state == "unfinished" or not forms or any(
                    not form.strip() for form in forms
                )
                if source in required:
                    seen.add((name, source))
                    if incomplete:
                        errors.append(f"{key}: unfinished or empty translation")
                if incomplete:
                    backlog.append(key)
                else:
                    for form in forms:
                        if placeholders(source) != placeholders(form):
                            item = f"{key}: placeholder mismatch"
                            changed = baseline is not None and baseline.get(
                                (name, source, message.findtext("comment", ""))
                            ) != (state, tuple(forms))
                            (errors if source in required or changed else backlog).append(item)
                    # & is a menu mnemonic; escaped && means a literal ampersand.
                    if source in required and "&" in source.replace("&&", ""):
                        if not any("&" in form.replace("&&", "") for form in forms):
                            errors.append(f"{key}: missing menu mnemonic")
        for (module, name), sources in scopes.items():
            if module == path.stem:
                for source in sources:
                    if (name, source) not in seen:
                        errors.append(f"{relative}: {name}: {source}: missing entry")
        checked.append(relative)
    if not checked:
        errors.append("No translation catalogs found")
    return {"catalogs": checked, "errors": errors, "backlog": backlog}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--json", type=Path)
    parser.add_argument("--base-ref", help="Also reject newly introduced placeholder errors in other contexts")
    args = parser.parse_args()
    if args.base_ref:
        subprocess.run(["git", "rev-parse", "--verify", args.base_ref + "^{commit}"],
                       cwd=args.root, check=True, stdout=subprocess.DEVNULL)
    result = audit(args.root, args.base_ref)
    if args.json:
        args.json.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Checked {len(result['catalogs'])} catalogs; {len(result['errors'])} issue-scope errors.")
    print(f"Reported {len(result['backlog'])} existing unfinished/placeholder items outside the completed scope.")
    for error in result["errors"]:
        print(error, file=sys.stderr)
    return bool(result["errors"])


if __name__ == "__main__":
    sys.exit(main())
