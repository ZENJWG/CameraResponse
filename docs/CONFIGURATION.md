# Camera Response — streamlined configuration

## Objective

Keep the original GTA San Andreas Classic (US PC 1.0 x86) camera-response
functionality without exposing nearly every algorithm coefficient to players.

This edition uses 134 player-facing settings in 36 groups instead of 760 in 52. The added `[Debug] OverlayScale=1.0` controls the automatic resolution/DPI-aware overlay size.
Most users only need `[General]` and `[MasterTuning]`. Advanced players can still
enable or adjust each major subsystem without editing C++.

## What changed

- User-facing: presets, overall and per-category intensity, gameplay effects,
  vehicle families, recoil families, FOV, movement, weather, selected diagnostics.
- Built into C++: safety/watchdog timeouts, thresholds, scan intervals, spring
  limits, per-weapon recoil profiles, per-melee-weapon profiles, physical scaling
  coefficients, response budgeting, directional biases and procedural texture.
- The original 17 gun recoil calibrations are now in a `recoilTuning[17][5]` table.
- The existing melee calibration arrays already matched the shipped INI; their
  redundant INI lookups were removed.
- The original `[Alive] SettleDelay=0.800` value is now compiled in; its previous
  in-code default of 0.55 would otherwise change idle-camera behavior.
- The original `[MasterTuning] Overall=1.0` is not exposed because it adds
  another gain stage. This keeps the shipped baseline unchanged, but General
  Strength is **not** a direct substitute: FOV is still tuned separately using
  `[MasterTuning] FOV`, and `[General] Enabled=0` disables the plugin entirely.
- `[ResponseScales]` contained five 1.0 multipliers; they now use their compiled
  defaults. Subsystem intensity is controlled by MasterTuning and feature gains.

No camera algorithms, hooks, offsets, public API exports, or game-facing entry
points were intentionally changed. Additionally, the core telemetry reset uses
value assignment instead of clearing a non-trivial C++ object with `memset`.

## Recommended tuning workflow

1. Start with `[General] Preset=Default`, then try Subtle/Heavy/MachineGames/Comfort.
2. Use `[General] Strength` for most physical impulses; use `Enabled=0` to switch off all effects.
3. Use `[MasterTuning]` to balance Movement, Combat, Vehicle, World and FOV.
4. If needed, adjust feature-specific options (Recoil, FOV, Vehicle, etc.).
5. Leave `[Rotation] Enabled=0` unless you deliberately want optional camera
   orientation modifications.

## Existing INI migration

**Replace the old INI** with this project's `CameraResponse.ini`; do not just
leave an older 760-option version alongside a new binary. Removed legacy keys
are silently ignored in the new build, not honored as overrides. To migrate
values from an older INI, use:

    python tools/migrate_legacy_ini.py path/to/old/CameraResponse.ini path/to/new/CameraResponse.ini

Only retained user-facing settings are copied. Removed low-level custom tuning
cannot be represented in the new INI. Keep an old copy if you may need to restore
a custom calibration in C++ later.

The plug-in resolves the INI next to the installed `.asi`, not relative to the
working directory. Enabling `[Developer] RuntimeReload=1` permits F10 reload
(with its built-in 350 ms debounce).

## Validation

Run:

    python tools/config_audit.py
    python tools/performance_audit.py
    python tests/config_contract_tests.py
    g++ -std=c++17 -O2 -Wall -Wextra tests/response_core_tests.cpp -o response_core_tests
    ./response_core_tests

The core unit tests do **not** build or exercise the full Windows ASI.
A Win32 Release plugin-sdk build and GTA SA 1.0 in-game regression pass are
still required before shipping. In particular, verify per-weapon recoil,
hand-to-hand, landings, vehicle transitions, indoor attenuation, game camera
modes, FOV fallback, and hot-reload.

## Scope and trade-offs

Reduced configuration loading helps maintainability and shortens startup/hot
reload reads; it is **not** a measured in-game frame-rate improvement. The
camera feature code remains predominantly a single large C++ file. Splitting
into independently tested modules would be a separate, higher-risk refactor.
Preserve the translation-first render-hook safety constraints when refactoring.

Compatibility: plugin-sdk / GTA San Andreas Classic PC US 1.0, Win32 x86.
