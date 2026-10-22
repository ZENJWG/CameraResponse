from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
cpp="\n".join(p.read_text(encoding="utf-8",errors="replace") for p in
    (root/"source").glob("*.inc")) + (root/"source"/"CameraResponse.cpp").read_text(encoding="utf-8",errors="replace")
ini=(root/"CameraResponse.ini").read_text(encoding="utf-8",errors="replace")
warnings=[]

def value(section,key):
    m=re.search(rf"(?ms)^\[{re.escape(section)}\]\s*(.*?)(?=^\[|\Z)",ini)
    if not m:
        return None
    k=re.search(rf"(?m)^{re.escape(key)}\s*=\s*(.+?)\s*$",m.group(1))
    return k.group(1).split(';', 1)[0].strip() if k else None

if value("DebugAudit","Enabled")=="1":
    warnings.append("DebugAudit.Enabled=1 adds runtime accounting and periodic log I/O.")
if value("Developer","RuntimeReload")=="1":
    warnings.append("Developer.RuntimeReload=1 polls a Windows key every game update.")

scan=value("NearbyGunfire","ScanMs")
try:
    if scan is None or int(scan)<5:
        warnings.append("NearbyGunfire.ScanMs below 5 can add redundant ped-pool work at high refresh rates.")
except ValueError:
    warnings.append("NearbyGunfire.ScanMs is not a valid integer.")

if "ValidateUltimateFpRenderCallsites" in cpp:
    warnings.append("Dead UltimateFP render-callsite validator remains.")
if "weatherNoise_.Update" in cpp:
    warnings.append("Weather channels still share one procedural-noise state.")

if warnings:
    print("PERFORMANCE AUDIT: WARN")
    for item in warnings:
        print(" -",item)
else:
    print("PERFORMANCE AUDIT: OK")
