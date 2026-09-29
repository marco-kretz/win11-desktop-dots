# DesktopDots

GNOME-style virtual desktop indicator on the far left of the Windows 11 taskbar.

- **Filled dot**: at least one window is open on that desktop
- **Hollow dot**: the desktop is empty
- **Pill**: the active desktop (with a short animation when switching)

Left-click a dot to switch to that desktop. Right-click opens a menu (start with Windows, exit).

## Requirements

- Windows 11 with a centered taskbar and the Widgets button disabled (otherwise the dots overlap it)
- .NET 10 SDK to build

## Build and install

```powershell
dotnet publish -c Release -o $HOME\Tools\DesktopDots
& $HOME\Tools\DesktopDots\DesktopDots.exe
```

Then enable "Start with Windows" via right-click. Autostart points to the path of the running `.exe`, so publish to a fixed location first.

## How it works

- The app attaches a transparent layered window as a child of the taskbar (`Shell_TrayWnd`). It moves with the taskbar and is visible on every desktop.
- The desktop list and the active desktop are read from `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops`; changes are picked up immediately via `RegNotifyChangeKeyValue`.
- Occupied desktops are determined once per second using `EnumWindows` and the public COM API `IVirtualDesktopManager`.
- Clicking a dot simulates `Ctrl+Win+Left/Right`, since there is no public API for switching desktops.
- After an Explorer restart, the app re-attaches itself automatically.

Only documented APIs are used, no undocumented shell interfaces, so it should survive Windows updates.

## Performance

Measured on my machine while idle:

- **CPU**: ~0.5 % of one core (47 ms of CPU time per 10 s). Switching desktops runs a ~200 ms animation at ~60 fps, then goes idle again.
- **Memory**: ~19 MB private / ~56 MB working set, mostly the .NET runtime baseline.
- **Explorer**: no noticeable impact; the app only enumerates top-level windows once per second.

Possible improvements if it ever matters: only redraw when something actually changed (it currently redraws once per second), or publish with Native AOT to reduce memory and startup time (WinForms support for this is untested).

## Caveats

- **Completely untested.** This is a private tool I built for my own setup. It works on my machine, and that's all I can say.
- **No settings.** Size, spacing, colors and position are hard-coded in `Program.cs`.
- **Only tested with one taskbar configuration**: centered, bottom, Widgets disabled, primary monitor only. Not tested with a left-aligned taskbar, auto-hide, secondary monitors, different scaling or third-party taskbar tweaks that move the taskbar to the top or sides.
- Occupancy updates can lag by up to one second.
- Switching across several desktops plays the Windows switch animation once per step.

Feel free to take it, change it and redistribute it.

## License

[MIT](LICENSE)
