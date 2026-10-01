# AGENTS.md

Windhawk mod (C++, runs in `explorer.exe`) that draws virtual desktop dots into the Windows 11 taskbar. See README.md for behavior.

## Layout

Everything lives in `desktop-dots.wh.cpp`: Windhawk metadata/readme header, data (registry + `IVirtualDesktopManager`), rendering (GDI+), input, and a worker thread owning the window. Keep it a single file.

## Build and run

Compiled by Windhawk: paste the file into a mod in the Windhawk editor and compile. No test suite. Screen captures via GDI `CopyFromScreen` do not show the layered taskbar child, so ask the user to confirm visual results.

## Constraints

- Use only documented APIs. Do not use `IVirtualDesktopManagerInternal` or other undocumented shell COM interfaces; their GUIDs change between Windows builds. Sole exception: the uxtheme ordinal `#133` (`AllowDarkModeForWindow`) for the dark context menu (stable since Windows 10 1903).
- Desktop list/current desktop come from the registry key `HKCU\...\Explorer\VirtualDesktops`; switching is done by simulating `Ctrl+Win+Left/Right`.
- The window is a layered child of `Shell_TrayWnd`.
- `Wh_ModUninit` must stop the worker thread and destroy the window before the DLL unloads.
- Per-monitor DPI aware: sizes are in physical pixels scaled by `GetDpiForWindow(tray)`.
- Conventional Commits.
