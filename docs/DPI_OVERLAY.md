# Camera Response — 4K and DPI-aware debug overlay

## Changes

The debug overlay now uses the game RenderWare framebuffer size
(`RsGlobal.maximumWidth` / `RsGlobal.maximumHeight`) to scale text, position,
line spacing, and right-side wrapping. The 4K overlay is approximately twice
the font size it is at 1920×1080, with proportional screen margins.

Windows 10+ per-window DPI is read from the existing GTA window using the
late-bound `GetDpiForWindow` function. Older Windows falls back to window
device-cap DPI. The extra DPI boost applies only when the framebuffer
resolution does not already account for that density, preventing double
scaling on 4K at 200%. Per-window DPI refreshes approximately once per
second, while render dimensions update with each overlay draw.

This is **DPI-aware sizing of the in-game overlay**, not a request to make
the entire GTA San Andreas process DPI-aware. The ASI intentionally **does
not call** `SetProcessDPIAware`, `SetProcessDpiAwareness`, or
`SetProcessDpiAwarenessContext`; changing another program's DPI mode could
alter its rendering, mouse coordinates, or its other plugins.

Long overlay lines have been shortened without removing the telemetry.
Font width is measured and the whole overlay is reduced uniformly if a
line exceeds the current viewport; the three/four rows retain spacing.

## Configuration

`[Debug]`:

```ini
Enabled=0
OverlayEnabled=1
OverlayScale=1.0
```

`OverlayScale=1.0` means automatic resolution and DPI sizing.
Use `1.25` or `1.50` if you want the entire overlay bigger, or `0.80`
if it is too large. Valid range: 0.65–2.5. Other INI settings are
unchanged; now 134 exposed values in 36 sections.

After loading the new ASI, check `CameraResponse.log` for, for example:

```text
OVERLAY resolution=3840x2160 dpi=192 userScale=1.00 font=2.21/3.60
```

The exact numbers depend on Windows' DPI awareness of the game window.
If Windows reports `dpi=96` for a DPI-unaware GTA window, render-resolution
sizing still works. A true 4K framebuffer is not guaranteed if the game is
configured to render at a lower internal resolution or is stretched by a
separate wrapper.

## Test

1. Build the Windows x86 ASI with `build.bat`.
2. Install the **new** `dist/CameraResponse.asi` and matching INI.
3. In the INI, set `OverlayEnabled=1`, and optionally `DetailedHUD=1`
   in `[Developer]`. Restart GTA.
4. Test at 1920×1080 and 3840×2160, including 100% and 200%
   Windows desktop scaling if your monitor allows it.
5. Confirm four aligned rows, no overflowing text, readable size, and
   increasing `tick=`. The gameplay-fallback fix from the previous
   revision is still included.

## Validation

Portable C++ layout tests cover resolutions, DPI scaling without doubling,
manual scaling, fallback values, and clamping. Configuration contract,
source audits, and the response/update portable tests are included.

The full Windows `.asi` compile and in-game rendering still need testing
on GTA SA Classic US 1.0 (x86) with your installed display wrapper/mods.
