#!/usr/bin/env python3
from pathlib import Path
import importlib.util

path = Path(__file__).resolve().parents[1] / "tools" / "profile_report.py"
spec = importlib.util.spec_from_file_location("camera_profile", path)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

sample = """
[123] SCAN PROFILE name=doors calls=3 totalUs=150 avgUs=50 peakUs=70 windowMs=5000
[123] SCAN PROFILE name=nearby-vehicles calls=2 totalUs=800 avgUs=400 peakUs=750 windowMs=5000
[124] SCAN PROFILE name=doors calls=5 totalUs=250 avgUs=50 peakUs=80 windowMs=5000
"""
stats = dict(mod.summarize(sample))
assert stats["doors"] == [8, 400, 80, 2]
assert stats["nearby-vehicles"] == [2, 800, 750, 1]
assert mod.summarize("nothing collected") == []
print("profile_report_tests: PASS")
