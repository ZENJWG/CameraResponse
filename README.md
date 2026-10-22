# Camera Response — GTA San Andreas

**Created by Zeneric**  
*A responsive, configurable camera-effects plugin for Grand Theft Auto: San Andreas (Classic PC).*

Camera Response adds subtle motion and impact feedback to the game's camera, making movement, combat, driving, and interactions with the world feel more physical. Instead of a constant screen shake, it blends different effects according to what's happening in-game and lets you control their intensity.

## Features

- **Movement:** walking and running sway, footsteps, surface-dependent steps, directional inertia, jumps, and landings.
- **Combat:** weapon recoil, melee reactions, damage feedback, and explosion shocks.
- **Vehicles:** acceleration, braking, turns, collisions, and other driving-related camera responses.
- **Environment:** nearby traffic and objects, environmental interactions, and contextual effects.
- **Camera tuning:** optional rotation and field-of-view effects, spring smoothing, intensity limits, and comfort-focused adjustments.
- **Customization:** five presets plus a fully documented `CameraResponse.ini` with individual effect controls.

## Presets

Select a preset in `CameraResponse.ini` under `[General]`:

| Preset | Feel |
| --- | --- |
| `Subtle` | Lightweight motion for everyday play. |
| `Default` | Balanced movement, combat, and vehicle feedback. |
| `Heavy` | Stronger impacts and a weightier camera. |
| `MachineGames` | Punchy impacts with restrained ambient motion. |
| `Comfort` | Reduced persistent motion, FOV changes, and rotation. |

The plugin is inspired by the camera feel of MachineGames titles; it is an independent project.

## Installation

**Supported target:** GTA San Andreas **Classic PC, US 1.0 (Windows 32-bit/x86)**. Other releases, including the Definitive Edition, are not supported.

1. Make sure your game can load `.asi` plugins (an ASI loader is required).
2. Build the source as described below, or use a separately provided compatible compiled build.
3. Place `CameraResponse.asi` and `CameraResponse.ini` **together** in a directory scanned by your ASI loader (commonly the GTA San Andreas game folder).
4. Start the game. Camera Response uses the supplied settings automatically.

**Note:** This download is a **source package**, not a precompiled `.asi`. Camera mods that modify the same game hooks may conflict; test combinations carefully.

## Quick configuration

Open `CameraResponse.ini` in a text editor. Each setting includes a short inline explanation.

```ini
[General]
Enabled=1       ; Enable or disable camera effects
Preset=Default  ; Subtle, Default, Heavy, MachineGames, Comfort
Strength=1.000  ; Overall response intensity
```

For a gentler experience, try `Preset=Comfort` or lower `Strength`. For a stronger reaction, try `Preset=Heavy`. Change one group of settings at a time to find the balance you prefer.

Advanced controls include per-effect strengths, rotation, FOV, scan cadence, runtime configuration reload, and optional profiling. See [the tuning guide](docs/CONFIG_GUIDE.md) for details.

## Building from source

On Windows, install **Visual Studio 2022** with Desktop development with C++, MSVC x86 tools, and the Windows SDK. Then run `build.bat` from the project folder. The script sets up the pinned plugin-sdk dependency and, if the build succeeds, creates:

- `dist/CameraResponse.asi`
- `dist/CameraResponse.ini`

Replace both installed files with the versions in `dist/`. For source-level changes and validation information, see [IMPROVEMENTS.md](IMPROVEMENTS.md) and [VALIDATION.txt](VALIDATION.txt).

**Build/gameplay status:** Portable tests have passed, but this package has not been verified through a Windows x86 ASI build or an in-game compatibility test.

## Credits

**Created by Zeneric.**  
Inspired by the cinematic camera feel of MachineGames titles. This is an independent fan-made plugin and is not affiliated with Rockstar Games or MachineGames.
