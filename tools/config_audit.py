#!/usr/bin/env python3
"""Ensure every exposed INI setting has exactly one C++ load site.

Internal-only calibration is intentionally absent from the external INI.
"""
from collections import Counter
from pathlib import Path
import configparser
import re
import sys

root = Path(__file__).resolve().parents[1]
cpp = (root / "source" / "CameraResponse.cpp").read_text(encoding="utf-8")
ini = root / "CameraResponse.ini"

cfg = configparser.ConfigParser(interpolation=None, strict=True)
cfg.optionxform = str
cfg.read(ini, encoding="utf-8")
keys = {(section, key) for section in cfg.sections() for key in cfg[section]}

# Match calls rather than literal expressions split across lines.
load_pattern = re.compile(
    r'\bRead(?:Bool|Int|Float|String)\(\s*iniPath\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"'
)
loaded = Counter(load_pattern.findall(cpp))

missing_loader = sorted(keys - loaded.keys())
unexpected_loader = sorted(loaded.keys() - keys)
duplicate_loads = sorted(k for k, n in loaded.items() if n != 1)

for title, items in (
    ("INI settings without a loader", missing_loader),
    ("C++ loaders for missing INI keys", unexpected_loader),
    ("INI settings loaded more than once", duplicate_loads),
):
    if items:
        print(f"ERROR: {title}:")
        for section, key in items:
            print(f"  [{section}] {key}")

deprecated = {
    "Safety", "Performance", "WeaponRecoilProfiles", "MeleeWeaponProfiles",
    "DebugAuditInternals"
}
exposed_internal = deprecated.intersection(cfg.sections())
if exposed_internal:
    print(f"ERROR: internal sections leaked into INI: {sorted(exposed_internal)}")

# Confirm fixed profile calibration has not silently vanished.
profile = re.search(r'recoilTuning\[17\]\[5\]\s*=\s*\{(.*?)\n\s*\};', cpp, re.S)
if profile is None or len(re.findall(r'\{[^{}]+\}', profile.group(1))) != 17:
    print("ERROR: expected 17 built-in weapon recoil profiles")
    sys.exit(1)

if missing_loader or unexpected_loader or duplicate_loads or exposed_internal:
    sys.exit(1)

print(f"CONFIG AUDIT OK: {len(keys)} externally configurable values across {len(cfg.sections())} sections.")
