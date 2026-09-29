# AGENTS.md

Tiny WinForms (.NET 10) app that draws virtual desktop dots into the Windows 11 taskbar. See README.md for behavior.

## Layout

Everything lives in `Program.cs`: `Program` (restart loop), `Bar` (window, data, rendering, input), COM interface, `Native` P/Invokes. Keep it a single file unless it grows substantially.

## Build and run

```powershell
Stop-Process -Name DesktopDots -ErrorAction Ignore   # single-instance mutex; stop before relaunching
dotnet build -c Release
.\bin\Release\net10.0-windows\DesktopDots.exe
```

No test suite. Verify changes by running the app; screen captures via GDI `CopyFromScreen` do not show the layered taskbar child, so ask the user to confirm visual results.

## Constraints

- Use only documented APIs. Do not use `IVirtualDesktopManagerInternal` or other undocumented shell COM interfaces; their GUIDs change between Windows builds.
- Desktop list/current desktop come from the registry key `HKCU\...\Explorer\VirtualDesktops`; switching is done by simulating `Ctrl+Win+Left/Right`.
- The window is a layered child of `Shell_TrayWnd` and requires the Windows 8+ `supportedOS` entry in `app.manifest`.
- Per-monitor DPI aware: sizes are in physical pixels scaled by `GetDpiForWindow(tray)`.
- Conventional Commits.
