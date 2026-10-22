#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
# Loader implementation can live in an included module.
cpp = "\n".join(
    path.read_text(encoding="utf-8", errors="replace")
    for path in sorted((root / "source").glob("*.inc"))
) + "\n" + (root / "source" / "CameraResponse.cpp").read_text(
    encoding="utf-8", errors="replace"
)
ini = (root / "CameraResponse.ini").read_text(encoding="utf-8", errors="replace")

section = None
ini_set = set()
duplicates = []
missing_comments = []
for raw in ini.splitlines():
    line = raw.strip()
    if not line or line.startswith(";"):
        continue
    if line.startswith("[") and line.endswith("]"):
        section = line[1:-1]
        continue
    if "=" in line and section:
        key, value = line.split("=", 1)
        pair = (section, key.strip())
        if pair in ini_set:
            duplicates.append(pair)
        if not value.strip() or " ; " not in value:
            missing_comments.append(pair)
        ini_set.add(pair)

loaded = set(re.findall(
    r'Read(?:Bool|Int|Float)\(\s*iniPath,\s*"([^"]+)",\s*"([^"]+)"',
    cpp
))
loaded.update(re.findall(
    r'ReadString\(\s*iniPath,\s*"([^"]+)",\s*"([^"]+)"',
    cpp
))

recoil_names=["Colt45","Silenced","DesertEagle","Shotgun","Sawnoff","Spas12","MicroUzi","MP5","AK47","M4","Tec9","CountryRifle","SniperRifle","RocketLauncher","HeatSeeker","Flamethrower","Minigun"]
for name in recoil_names:
    for suffix in ("Scale","Vertical","Forward","Side","Recovery"):
        loaded.add(("WeaponRecoilProfiles", f"{name}.{suffix}"))

melee_names=["Unarmed","BrassKnuckle","GolfClub","Nightstick","Knife","BaseballBat","Shovel","PoolCue","Katana","Chainsaw","Dildo1","Dildo2","Vibrator1","Vibrator2","Flowers","Cane"]
for name in melee_names:
    for suffix in ("Scale","SideWeight","VerticalWeight","ReleaseForward","ReleaseSide","ContactScale","LockOnScale","CooldownMs"):
        loaded.add(("MeleeWeaponProfiles", f"{name}.{suffix}"))

missing = sorted(ini_set - loaded)
extra = sorted(loaded - ini_set)

if missing:
    print("ERROR: INI keys without a loader:")
    for section, key in missing:
        print(f"  [{section}] {key}")

if extra:
    print("ERROR: source loads keys absent from CameraResponse.ini:")
    for section, key in extra:
        print(f"  [{section}] {key}")

if duplicates:
    print("ERROR: Duplicate INI settings:", duplicates)
if missing_comments:
    print("ERROR: Missing inline explanations:", missing_comments)

if missing or extra or duplicates or missing_comments:
    sys.exit(1)

print(f"CONFIG AUDIT OK: {len(ini_set)} settings mapped.")
