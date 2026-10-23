# Camera Response — frozen overlay / missing game updates

## What the uploaded log proves

`START event=drawingEvent`, `iniPresent=1`, and `READY cameraHook=1`
show that the ASI initialized, loaded its INI and installed the camera commit
hook. The log contained no `GAME PROCESS callback active` or periodic update
records even though the user was in gameplay. A conflicting/modified process
call site is a plausible cause, **not confirmed** by the log.

## Fix

- Keep `Events::gameProcessEvent` as the normal game simulation updater.
- Recover using `Events::drawingEvent` when there has been no process callback
  for more than two `CTimer::m_FrameCounter` frames (or none ever).
- Run at most one camera-response update per frame, whichever callback wins.
- Skip drawing-based recovery in menus, during pauses, without a player,
  with an invalid camera index or with a stopped game clock.
- Switch back to the normal process callback when it resumes.
- Show `src=GAME` or `src=DRAW-FALLBACK` and `tick=` in the in-game overlay.
- Write a compact `DRAW HEARTBEAT` every ~5 seconds, including callback
  counters, gameplay readiness, camera commits, current gate and source.

The calculation of camera effects and hook code are unchanged. The later
4K overlay revision adds only `[Debug] OverlayScale`. When recovered from drawing, the camera output may arrive one
render frame later than with the normal process hook.

## How to install/test

This archive is **source only**; it is not an installable compiled ASI.
On Windows, run `build.bat` with the supported Visual Studio x86/plugin-sdk
toolchain. Install `dist/CameraResponse.asi` and `dist/CameraResponse.ini`
together, remove old/duplicate CameraResponse ASIs, and restart the game.
Classic PC GTA SA US 1.0 x86 is the intended binary.

After starting a saved game, walk, sprint and fire a gun. Check the overlay
(`Debug.OverlayEnabled=1`) and the log beside the loaded ASI:

- `GAME PROCESS callback active ...` and `src=GAME`: original callback works.
- `UPDATE SOURCE drawingEvent fallback enabled ...`, `src=DRAW-FALLBACK`
  and increasing `tick=`: the fallback is running.
- `DRAW HEARTBEAT ... gameplayReady=0`: rendering works but safety checks
  prevent fallback (pause/menu/no player, stopped time step, bad camera index).
- `DRAW HEARTBEAT ... gameplayReady=1` but `updates=0`: investigate frame
  counter behavior and dispatch conditions.
- `updates` increases but `applied=0`: use the heartbeat's `gate`,
  `prepared`, and `commit` values to identify the next camera-output blocker.
- No further `DRAW HEARTBEAT`: the drawing callback also stopped reaching us;
  check other plugins and ASI loader compatibility.
- `READY cameraHook=0`: an incompatible or conflicted camera call site;
  gameplay updates alone cannot produce camera output.

Once diagnosed, turn `DebugAudit.Enabled=0` and `Debug.Enabled=0` to avoid
verbose audit output (short heartbeat messages still remain).

## Validation

Passed on a Linux build host:
- `g++ -std=c++17 -Wall -Wextra -Werror` for portable response-core tests.
- Portable update dispatcher tests covering fallback, duplicate suppression,
  recovery, menu gating and 32-bit frame-counter rollover.
- Configuration audit and config contract tests (134 keys / 36 sections,
  including the later overlay-size setting).
- Performance source audit.

Not verified: Windows `.asi` compilation, runtime hook ordering, in-game
camera movement or compatibility with the user's other installed mods.
