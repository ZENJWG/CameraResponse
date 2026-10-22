#!/usr/bin/env python3
"""Aggregate opt-in CameraResponse SCAN PROFILE log entries by subsystem."""
import argparse
import re
from pathlib import Path

PATTERN = re.compile(
    r"SCAN PROFILE name=(\S+) calls=(\d+) totalUs=(\d+) "
    r"avgUs=(\d+) peakUs=(\d+)"
)

def summarize(log_text):
    totals = {}
    for match in PATTERN.finditer(log_text):
        name, calls, total, avg, peak = match.groups()
        stats = totals.setdefault(name, [0, 0, 0, 0])
        stats[0] += int(calls)
        stats[1] += int(total)
        stats[2] = max(stats[2], int(peak))
        stats[3] += 1
    return sorted(totals.items(), key=lambda item: item[1][1], reverse=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="CameraResponse.log")
    args = parser.parse_args()
    summary = summarize(args.log.read_text(encoding="utf-8", errors="replace"))
    for name, (calls, total, peak, windows) in summary:
        print(f"{name:22} calls={calls:6} total_ms={total/1000:9.2f} "
              f"mean_us={total/max(calls,1):8.1f} peak_us={peak:6} windows={windows}")
    if not summary:
        print("No SCAN PROFILE records found. Set Performance.ScanProfiling=1.")

if __name__ == "__main__":
    main()
