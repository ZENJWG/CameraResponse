#!/usr/bin/env python3
"""Copy supported user settings from a legacy INI into the streamlined INI."""
import configparser
from pathlib import Path
import sys

def read(path):
    conf = configparser.ConfigParser(interpolation=None, strict=False)
    conf.optionxform = str
    conf.read(path, encoding="utf-8-sig")
    return conf

def migrate(src, dest, template):
    incoming = read(src)
    new_lines = []
    section = None
    supported, skipped = 0, 0
    for line in template.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1]
        elif "=" in line and not stripped.startswith(";") and section is not None:
            key, old_value = line.split("=", 1)
            name = key.strip()
            if incoming.has_option(section, name):
                new_value = incoming.get(section, name, raw=True)
                line = f"{key}={new_value}"
                supported += 1
        new_lines.append(line)
    defaults = read(template)
    valid = {(s, k) for s in defaults.sections() for k in defaults[s]}
    for s in incoming.sections():
        for k in incoming[s]:
            if (s, k) not in valid:
                skipped += 1
    # Do not accidentally overwrite input unless user explicitly used a
    # different output path (also handles matching resolved paths).
    if src.resolve() == dest.resolve():
        raise ValueError("Input and output must differ; preserve the old INI.")
    dest.write_text("\n".join(new_lines) + "\n", encoding="utf-8")
    return supported, skipped

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: python tools/migrate_legacy_ini.py OLD.ini NEW.ini")
    root = Path(__file__).resolve().parents[1]
    old, target = Path(sys.argv[1]), Path(sys.argv[2])
    count, skipped = migrate(old, target, root / "CameraResponse.ini")
    print(f"Migrated {count} supported settings; dropped {skipped} low-level or obsolete settings.")
