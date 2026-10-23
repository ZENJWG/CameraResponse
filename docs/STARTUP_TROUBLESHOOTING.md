# Camera Response — Startup Diagnostics

This is **source code**, not a precompiled `.asi`. Run `build.bat` on Windows
(with Visual Studio C++ x86 tooling); install **both** files from `dist/`:

- `CameraResponse.asi`
- `CameraResponse.ini`

Place them together in an ASI-loaded directory (often the GTA SA game directory,
or an ASI-enabled modloader folder). Remove older copies so the loader cannot
load a different version. The target is **GTA San Andreas Classic PC US 1.0 (x86)**.

## Changes in this version

- Write a short `CameraResponse.log` **every time runtime initialization is reached**.
  `[Debug] Enabled=1` is only needed for verbose diagnostic messages.
- When the loaded ASI's directory is read-only, try `%TEMP%\CameraResponse.log`.
- Allow the already-registered drawing callback to initialize the runtime if
  `gameProcessEvent` has not run yet, e.g., while in the menu.
- Record startup event, resolved INI location and presence, enabled/debug flags,
  hook status, and whether the game process callback ran.
- Validate executable code memory before checking camera/water hook signatures;
  do not install the water hook when the camera signature was rejected.
- Emit an `OutputDebugStringA` loader marker for a Windows debugger/DebugView;
  **no filesystem writes are attempted during DLL static construction**.

The INI values and camera behavior algorithms have **not** been changed.

## Diagnosing startup

1. Build the project and use the new `dist/CameraResponse.asi`—the source ZIP
   cannot be installed directly. Ensure the game has a working ASI loader.
2. Start GTA SA (preferably load into gameplay). Search next to the exact `.asi`
   for `CameraResponse.log`, then in Windows Run -> `%TEMP%`.
3. Open the log:
   - `iniPresent=0`: put the INI beside the loaded ASI.
   - `CONFIG enabled=0`: enable `[General] Enabled=1`.
   - `ERROR: unsupported camera callsite`: check US 1.0 classic executable;
     try without camera-mod conflicts. No incompatible hook is installed.
   - `READY cameraHook=0`: plugin loaded but camera output is unavailable.
   - `READY cameraHook=1`, but no `GAME PROCESS callback active`: plugin loaded,
     but the gameplay callback never reached this plugin; check loader/mod conflicts.
   - `READY cameraHook=1` plus `GAME PROCESS callback active`: plugin started;
     set `[Debug] Enabled=1` and `[Debug] OverlayEnabled=1` if camera movement
     is still absent, then inspect state/gate from the overlay and full log.
4. **No log anywhere**: plugin never reached either plugin-sdk callback, is not
   loaded, or could not write to both destinations. Check for the actual ASI
   (not just the `.ini` or source ZIP), other duplicate ASIs, loader support,
   x86 game compatibility, and load errors. In DebugView, the marker
   `CameraResponse: ASI loaded` confirms static construction even if callbacks
   are not delivered.

Note: if an ASI cannot load at all (e.g., missing loader/imports, wrong
architecture), code in the ASI cannot create a log. A no-log condition does
not by itself identify an INI or camera-algorithm error.

## Verification

The config audit, config contract tests, performance source audit, and the
portable response-core unit tests were run outside Windows and passed.
A Windows x86 plugin-sdk compilation and in-game verification could not
be performed here; these are still required before using the plugin.

## Gameplay callback frozen despite a working hook

See [GAMEPLAY_FALLBACK.md](GAMEPLAY_FALLBACK.md). This source revision
adds a guarded drawing-event recovery path and callback heartbeat logging.
