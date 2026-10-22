#!/usr/bin/env python3
"""Validate single-translation-unit module wiring and documented INI keys."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
main = (root / "source/CameraResponse.cpp").read_text(encoding="utf-8")
modules = (
    "CameraResponseConfig.inc",
    "CameraResponseWorldScans.inc",
    "CameraResponseCameraIntegration.inc",
)
for module in modules:
    assert main.count(f'#include "{module}"') == 1, module
    assert (root / "source" / module).is_file(), module

ini = (root / "CameraResponse.ini").read_text(encoding="utf-8")
section = None
keys = set()
for line in ini.splitlines():
    if line.startswith("[") and line.endswith("]"):
        section = line[1:-1]
    elif section and line and not line.lstrip().startswith(";") and "=" in line:
        key, value = line.split("=", 1)
        pair = (section, key.strip())
        assert pair not in keys, pair
        keys.add(pair)
        assert " ; " in value, pair

assert ("Performance", "ScanProfiling") in keys
assert ("Performance", "ProfileEveryMs") in keys
assert len(keys) == 762, len(keys)
print("project_integrity_tests: PASS")
