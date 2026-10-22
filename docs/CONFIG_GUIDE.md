# Camera Response tuning guide

The shipped `CameraResponse.ini` remains the complete **advanced** configuration.
Every active setting has a short comment directly beside the value. No existing
camera strength, preset, vehicle handling, recoil or FOV default has been changed.

## Everyday tuning (start here)

| Goal | Settings | Notes |
| --- | --- | --- |
| Overall intensity | `[General] Preset`, `Strength` | Choose one preset first, then fine-tune strength |
| Comfort / motion sickness | `[General] Preset=Comfort`, `[Rotation] Enabled`, `[FOV] Enabled` | Comfort preset retains discrete impacts with less persistent motion |
| Movement feel | `[MasterTuning] Movement`, `[ResponseScales]` movement-related values | Multipliers stack; avoid increasing all levels simultaneously |
| Combat feel | `[MasterTuning] Combat`, `[Recoil]`, `[RecoilProfiles]` | Profile and individual weapon overrides are advanced |
| Driving feel | `[MasterTuning] Vehicle`, `[Vehicle]`, `[VehicleDynamics]` | Alter one category per test drive |
| Environmental effects | `[MasterTuning] World`, `[EnvironmentResponse]`, `[WorldResponse]` | Reduce the master World scale first |
| Camera roll / rotation | `[Rotation] Enabled`, `[DirectionalRotation] Enabled` | Directional rotation only works if the main rotation pipeline is active |
| Field of view | `[FOV] Enabled`, `[MasterTuning] FOV` | Keep conservative for comfort |
| Reload while playing | `[Developer] RuntimeReload`, `ReloadKey`, `ReloadDebounceMs` | See commented INI for key syntax |

**How multipliers work:** General Strength, the selected preset, master scales,
category scales and individual effect scales can multiply together. For
predictable results, change a single level at a time and compare before/after.

## Advanced settings and dependencies

- `[Safety]`: source watchdog / spring cleanup, relevant for stuck motion.
- `[ResponseBudget]`: final displacement limits and soft saturation.
- `[FrequencyBands]`, `[ProceduralNoise]`, `[Smoothing]`: response texture.
- `[RecoilProfiles]`, `[WeaponRecoilProfiles]`, `[MeleeWeaponProfiles]`:
  per-weapon behavior; override only when a specific weapon feels wrong.
- `[NearbyGunfire]`, `[NearbyVehicles]`, `[DoorResponse]`, `[WorldResponse]`:
  pool scans and proximity effects; behavior and CPU use depend on population.
- `[Performance]`: scan cadence and **opt-in** timing measurements.
- `[Debug]`, `[DebugAudit]`: logs; leave off for regular play.

For effects gated by `[General]` switches, enabling a subordinate effect alone
does not override the corresponding general switch. Directional rotation also
requires `[Rotation] Enabled=1`.

## Performance profiling

Set:

```ini
[Performance]
ScanProfiling=1  ; Record expensive scan timings (0/1)
ProfileEveryMs=5000  ; Interval for scan timing summaries (ms)
```

Play the same save/route for at least one minute in a populated city and one
minute in a sparse location. `CameraResponse.log` will contain `SCAN PROFILE`
rows with calls, total microseconds, average microseconds, and maximum scan
time. Summarize the log with:

```sh
python tools/profile_report.py CameraResponse.log
```

These samples measure *scan execution time*, not frame time or FPS. Since
profiling adds timing/logging overhead, set `ScanProfiling=0` again after testing.
Only adjust `NearbyVehicleScanMs` and other scan intervals after measuring;
longer intervals trade responsiveness for CPU headroom.

## Live reload behavior

INI reload now constructs a fresh config using compiled defaults before reading
the file. Removing a previously overridden key and reloading restores its
compiled default. If the INI file is missing, the reload is skipped rather than
replacing the live settings. Changes apply on the game's update thread. Do not
edit/replace the INI while it is being read; write a new file then rename it into
place if using an external configuration editor.

## Game/compatibility testing

Supported build target: **GTA San Andreas Classic PC US 1.0 (Win32/x86)**.
The camera-hook signature and executable image layout are checked before
fixed-address patching. Other versions (Steam, Definitive Edition, other EXE
revisions) are unsupported. Third-party mods changing the same CALL may need
manual load-order testing; Camera Response does not repeatedly re-hook an
address after losing ownership.

Manual regression route, each at 30, 60, 120 and 144 FPS if possible:

1. Start on foot, walk / sprint / crouch / climb / jump / land; verify no stale offset.
2. Shoot and reload several weapons, melee attacks, and trigger explosions.
3. Enter and exit multiple vehicles; drive, collide, land, stop and exit.
4. Approach busy traffic / crowds / objects / water and inspect scan profiles.
5. Enter scripted camera/cutscene, return to third-person; verify no camera drift.
6. Enable then disable rotation, FOV and profiling by INI reload; delete one
   temporary override and confirm its value returns to default.
7. Run with Widescreen Fix and other camera-hook mods present/absent, both
   load orders. Verify hook ownership logs and no cumulative movement.
8. Spawn/respawn or load a save with no player temporarily; verify API output
   resets. Test capped/unlocked FPS and pause/resume.

Automated Linux tests exercise the platform-neutral response core; GitHub
Actions includes an unverified Windows-x86 build/test job for contractors to run
in a repository. Actual in-game behavior must still be checked on the target.
