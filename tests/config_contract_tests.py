#!/usr/bin/env python3
"""Regression tests for the streamlined external configuration contract."""
from pathlib import Path
import configparser
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from migrate_legacy_ini import migrate, read  # noqa: E402

def test_migration():
    template = ROOT / "CameraResponse.ini"
    current = read(template)
    assert len(current.sections()) == 36
    assert sum(len(current[sec]) for sec in current.sections()) == 134
    assert current["Debug"]["OverlayScale"] == "1.0"
    assert not current.has_section("Safety")
    assert not current.has_section("WeaponRecoilProfiles")
    assert not current.has_section("MeleeWeaponProfiles")

    with tempfile.TemporaryDirectory() as tmp:
        legacy = Path(tmp) / "old.ini"
        out = Path(tmp) / "new.ini"
        legacy.write_text(
            "[General]\nPreset=Comfort\nStrength=1.500\n"
            "[Safety]\nSourceReleaseMs=10\n"
            "[WeaponRecoilProfiles]\nColt45.Scale=200\n",
            encoding="utf-8",
        )
        migrated, skipped = migrate(legacy, out, template)
        conf = read(out)
        assert migrated == 2
        assert skipped == 2
        assert conf["General"]["Preset"] == "Comfort"
        assert conf["General"]["Strength"] == "1.500"
        assert not conf.has_section("Safety")
        assert not conf.has_section("WeaponRecoilProfiles")

if __name__ == "__main__":
    test_migration()
    print("config_contract_tests: PASS")
