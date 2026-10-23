# Camera Response — code and configuration assessment

## Existing architecture

- Platform: GTA San Andreas Classic PC US 1.0, x86, plugin-sdk with Premake.
- Game-facing code: `source/CameraResponse.cpp` (original: 15,328 source lines).
- Portable response primitives: `source/ResponseCore.h`.
- External tuning: `CameraResponse.ini` (original: 760 keys, 52 sections,
  878 lines).
- Verification utilities: `tools/config_audit.py`, `tools/performance_audit.py`
  and the portable C++ `tests/response_core_tests.cpp`.

The rendering hook has explicit translation-first safety constraints and optional
camera orientation logic. There are many specialized response sources; this
refactor retains those instead of replacing them with generic camera shake.

## Findings, prioritized

**High: Configuration surface is too broad.**
The INI exposes 760 settings, including safety watchdogs, polling cadences,
object collision thresholds, and per-weapon physical coefficients. These are
not normal player preferences and make a safe preset hard to understand.

**High: Weapon-profile duplication.**
The INI supplied 85 recoil fields and 128 melee fields. The latter already
matched source-side default tables exactly; the recoil table supplied distinct
per-weapon axis/recovery multipliers on top of generic fallback values.

**Medium: Duplicate gain stages.**
General Strength, MasterTuning Overall, category gains, and ResponseScales
provide several different multiplication points. The streamlined INI removes
the MasterTuning Overall control and hides unit-valued ResponseScales. These
stages remain in C++ (default=1.0) to preserve default runtime paths. Note
that General Strength and MasterTuning Overall were **not mathematically
identical**: FOV still needs its own explicit gain after this change.

**Medium: Manual INI-loader maintenance.**
Numerous read sites and fallback values duplicated information also present
in the INI. The revised audit checks that every *exposed* key has exactly
one load site and no load sites refer to removed keys.

**Medium: Tight coupling.**
Most integration, context detection, filtering, event routing, FOV, vehicle
mechanics and camera commit logic share one ~15k-line translation unit.
Separating these is worthwhile later, but a simultaneous rewrite would
make camera hook regressions more difficult to diagnose.

**Low: Telemetry initialization warning.**
`MixTelemetry::Reset()` used `memset` on a C++ object with initialized members.
The replacement uses standard C++ value assignment.

## Completed changes

- The initial refactor retained **133 player-facing keys in 36 sections** (82.5% fewer keys); the later 4K overlay revision adds one optional setting (134 total).
- Removed 414 direct scalar INI reads and dynamic per-profile reads; retained
  132 scalar reads plus the preset string.
- Moved the original **17 × 5 gun recoil coefficients** into source.
- Reused the existing **16 × 8 melee calibration arrays** without INI lookup.
- Matched `[Alive] SettleDelay` to the shipped INI value (0.800 rather than
  the previous source-side fallback 0.55).
- Preserved feature toggles, context attenuation, character presets,
  movement, combat, FOV and vehicle/world family controls.
- Added migration script and config contract regression test.
- Kept existing camera integration, memory offsets, render hooks, algorithm
  implementations and runtime feature detection intact.

## Suggested further work (not included)

1. Extract a typed `UserSettings` model and a `BuiltInTuning` model so new
   low-level tuning cannot accidentally become a public INI key.
2. Break up `CameraResponse.cpp` incrementally by event source (Locomotion,
   Combat, Vehicle, World) while leaving the existing camera hook in one place.
3. Add deterministic event-input replay tests for 30/60/120 FPS and for
   transitions, recoil cadence, and camera suppression.
4. Benchmark source pool scans and event mixer CPU time in-game before
   changing scan cadences or caching; never infer FPS savings from fewer INI
   reads alone.

## Validation / known limitations

- Config mapping audit: PASS
- Static performance audit: PASS
- Config migration contract test: PASS
- Portable C++ response core: PASS
- Windows x86 ASI build: not run in this sandbox (missing MSVC/plugin-sdk)
- In-game GTA SA 1.0 compatibility and measured performance: not verified

**Migration note:** custom overrides to eliminated low-level keys will not
survive. Preserve a legacy INI backup before installing the streamlined one.
