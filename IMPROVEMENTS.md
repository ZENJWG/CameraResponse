# CameraResponse — Improvements Summary

**Target:** GTA San Andreas Classic PC US 1.0 (Windows x86), using the project's pinned plugin-sdk. **Delivery:** optimized source only; no rebuilt `.asi` is included.

## Completed improvements

| Area | Changes |
| --- | --- |
| **INI and reload** | All **762 settings** have short inline descriptions. The original **760 key/value pairs remain unchanged**; only `ScanProfiling` and `ProfileEveryMs` were added. INI parsing now handles inline `;` comments, whitespace, overflow, malformed values, and nonfinite numbers. Live reload starts from compiled defaults, restores defaults for removed keys, and retains the active config if the INI file is missing. |
| **Camera and event safety** | Nonfinite external API inputs and mixer events are rejected; impulses are bounded. The 48-event mixer prioritizes important impacts during overflow, counts displaced events, and resets/recomputes telemetry correctly. Missing-player states clear camera outputs. |
| **Frame-rate consistency** | Source-watchdog magnitude decay now uses elapsed time rather than update count (calibrated to approximately the former 50 FPS behavior). Same-tick updates no longer compound decay; time-zero event handling and dominant-source selection were corrected. Vehicle ambient ducking now updates once per frame. |
| **Hook compatibility** | Fixed-address camera and water-cannon hooks check supported executable layout, memory access, and expected callsite signatures before patching; existing cooperative handling of competing hooks remains. These checks reduce risk but do not establish compatibility with every executable or camera mod. |
| **Performance visibility** | Optional scan timing tracks **eight** world/nearby-query categories (total, mean, and peak microseconds); `tools/profile_report.py` summarizes logged windows. Profiling is **off by default**. Scan frequencies and gameplay tuning were not changed without measurements. |
| **Maintainability and delivery** | Configuration, world scanning, and camera integration were separated into `.inc` fragments within the existing translation unit, preserving the plugin ABI. Added a quick-tuning guide, build documentation, portable regression tests, and a Windows x86 CI workflow. |

## Validation status

**Passed locally:** portable core tests using GCC (C++17), Clang (C++20), and GCC undefined-behavior sanitizer; INI mapping/comment audits; project/performance audits; profiler-log parser checks; and source wiring tests. The GitHub Actions workflow YAML was parsed locally.

**Not yet verified:** Windows/MSVC x86 ASI compilation, plugin-sdk integration, live hook behavior, compatibility with other camera mods, in-game camera feel, or measured FPS gains. The Windows CI workflow was added but **not executed** in this environment.

## Next steps before release

1. **Build on Windows:** run `build.bat` with the pinned plugin-sdk and confirm the generated `dist/CameraResponse.asi` loads in the supported game executable.
2. **Run gameplay regression checks:** test on-foot/vehicle transitions, explosions and heavy event bursts, missing-player/restart states, INI reload (including deleted overrides), Widescreen Fix and camera-mod combinations, and 30/60/120/144 FPS.
3. **Profile before further tuning:** enable `[Performance] ScanProfiling=1` temporarily, inspect `CameraResponse.log` using `tools/profile_report.py`, then optimize only measured hotspots. Turn profiling off afterward.

For tuning details and the manual test matrix, see [`docs/CONFIG_GUIDE.md`](docs/CONFIG_GUIDE.md). For test results, see [`VALIDATION.txt`](VALIDATION.txt).
