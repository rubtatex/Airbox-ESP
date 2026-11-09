#!/usr/bin/env python3
"""Reads CHANGELOG.md, the single place where firmware changes are written.

    changelog.py notes <version>   release text for GitHub (English then French); exit 3 if the version is missing
    changelog.py json              changelog.json for the USB installer page (newest first)
    changelog.py check             fail if an entry is malformed

Format: one "## X.Y.Z - YYYY-MM-DD" section per version, with "### English" and "### Français" lists of "- " items.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADING = re.compile(r"^## (\d+\.\d+\.\d+)(?:\s+-\s+(\S+))?\s*$")
LANGS = {"english": "en", "français": "fr", "francais": "fr"}


def parse(text):
    versions, current, lang = [], None, None
    for raw in text.splitlines():
        line = raw.rstrip()
        m = HEADING.match(line)
        if m:
            current = {"version": m.group(1), "date": m.group(2) or "", "en": [], "fr": []}
            versions.append(current)
            lang = None
        elif line.startswith("### ") and current is not None:
            lang = LANGS.get(line[4:].strip().lower())
        elif line.startswith("- ") and current is not None and lang:
            current[lang].append(line[2:].strip())
    return versions


def load():
    return parse((ROOT / "CHANGELOG.md").read_text(encoding="utf-8"))


def notes(version):
    for v in load():
        if v["version"] == version:
            parts = []
            for title, key in (("English", "en"), ("Français", "fr")):
                if v[key]:
                    parts.append(f"## {title}\n" + "\n".join(f"- {item}" for item in v[key]))
            if not parts:
                return None
            return "\n\n".join(parts) + "\n"
    return None


def check():
    versions = load()
    errors = []
    if not versions:
        errors.append("CHANGELOG.md has no '## X.Y.Z' section")
    seen = set()
    for v in versions:
        if v["version"] in seen:
            errors.append(f"{v['version']}: listed twice")
        seen.add(v["version"])
        if not v["en"] or not v["fr"]:
            errors.append(f"{v['version']}: needs both an English and a Français list")
    return errors


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "notes" and len(sys.argv) == 3:
        body = notes(sys.argv[2])
        if body is None:
            return 3
        sys.stdout.write(body)
        return 0
    if cmd == "json":
        json.dump({"versions": load()}, sys.stdout, ensure_ascii=False, indent=2)
        sys.stdout.write("\n")
        return 0
    if cmd == "check":
        errors = check()
        if errors:
            print("\n".join(errors), file=sys.stderr)
            return 1
        print("CHANGELOG.md is valid")
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
