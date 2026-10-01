# DesktopDots

GNOME-style virtual desktop indicator on the far left of the Windows 11 taskbar, as a [Windhawk](https://windhawk.net/) mod.

<p align="center"><img src="preview.gif" alt="DesktopDots preview"></p>

- **Filled dot**: at least one window is open on that desktop
- **Hollow dot**: the desktop is empty
- **Pill**: the active desktop (with a short animation when switching)

Left-click a dot to switch to that desktop. Right-click toggles hiding trailing empty desktops.

## Requirements

- Windows 11 with a centered taskbar and the Widgets button disabled (otherwise the dots overlap it)
- [Windhawk](https://windhawk.net/)

## Install

In Windhawk, choose *Create a new mod*, paste [`desktop-dots.wh.cpp`](desktop-dots.wh.cpp) and click *Compile*. The mod runs inside `explorer.exe`; enable or disable it in Windhawk.

## Recommended: instant desktop switching

I highly recommend the [Disable Virtual Desktop Transition](https://windhawk.net/mods/disable-virtual-desktop-transition) mod. It disables the Windows slide animation, so switching desktops (including by clicking a dot) is basically instant.

## How it works

- The mod attaches a transparent layered window as a child of the taskbar (`Shell_TrayWnd`). It moves with the taskbar and is visible on every desktop.
- The desktop list and the active desktop are read from `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops`; changes are picked up immediately via `RegNotifyChangeKeyValue`.
- Occupied desktops are determined once per second using `EnumWindows` and the public COM API `IVirtualDesktopManager`.
- Clicking a dot simulates `Ctrl+Win+Left/Right`, since there is no public API for switching desktops.

Only documented APIs are used, no undocumented shell interfaces, so it should survive Windows updates.

## Caveats

- **Completely untested.** This is a private tool I built for my own setup. It works on my machine, and that's all I can say.
- **No settings.** Size, spacing, colors and position are hard-coded in `desktop-dots.wh.cpp`.
- **Only tested with one taskbar configuration**: centered, bottom, Widgets disabled, primary monitor only. Not tested with a left-aligned taskbar, auto-hide, secondary monitors, different scaling or third-party taskbar tweaks that move the taskbar to the top or sides.
- Occupancy updates can lag by up to one second.

Feel free to take it, change it and redistribute it.

## License

[MIT](LICENSE)
