using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace DesktopDots;

static class Program
{
    [STAThread]
    static void Main()
    {
        using var mutex = new Mutex(true, "DesktopDots", out bool first);
        if (!first) return;
        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        Native.SetPreferredAppMode(1); // AllowDark: native menus follow the system dark mode

        // An Explorer restart destroys our child window together with the taskbar; rebuild it on the new one.
        while (true)
        {
            IntPtr tray;
            while ((tray = Native.FindWindow("Shell_TrayWnd", null)) == IntPtr.Zero) Thread.Sleep(1000);
            _ = new Bar(tray);
            Application.Run();
            Thread.Sleep(2000);
        }
    }
}

sealed class Bar : NativeWindow
{
    const string VdKey = @"Software\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops";
    const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    const int WM_APP = 0x8000, WM_DESTROY = 0x2, WM_MOUSEACTIVATE = 0x21, WM_LBUTTONUP = 0x202, WM_RBUTTONUP = 0x205;

    readonly IntPtr tray;
    readonly IVirtualDesktopManager vdm = (IVirtualDesktopManager)new VirtualDesktopManager();
    readonly System.Windows.Forms.Timer timer = new() { Interval = 1000 };
    readonly System.Windows.Forms.Timer anim = new() { Interval = 15 };
    Guid[] desktops = [];
    int current;
    HashSet<Guid> occupied = [];
    float[] grow = []; // 0 = dot, 1 = pill, per desktop
    float[] slotEnds = [];

    public Bar(IntPtr tray)
    {
        this.tray = tray;
        CreateHandle(new CreateParams
        {
            Style = 0x40000000 | 0x10000000 | 0x04000000, // WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS
            ExStyle = 0x80000 | 0x8000000 | 0x80,         // WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW
            Parent = tray,
        });

        var hwnd = Handle;
        new Thread(() =>
        {
            using var key = Registry.CurrentUser.OpenSubKey(VdKey);
            if (key == null) return;
            // REG_NOTIFY_CHANGE_LAST_SET; stops once our window is gone.
            while (Native.RegNotifyChangeKeyValue(key.Handle, true, 4, IntPtr.Zero, false) == 0
                   && Native.PostMessage(hwnd, WM_APP, 0, 0)) { }
        }) { IsBackground = true }.Start();

        // ponytail: 1s polling catches opened/closed/moved windows; a WinEvent hook if that lag bothers.
        timer.Tick += (_, _) => Refresh();
        timer.Start();
        anim.Tick += (_, _) => Animate();
        Refresh();
    }

    protected override void WndProc(ref Message m)
    {
        switch (m.Msg)
        {
            case WM_APP: Refresh(); return;
            case WM_MOUSEACTIVATE: m.Result = 3; return; // MA_NOACTIVATE
            case WM_LBUTTONUP: SwitchTo(HitTest((short)(m.LParam.ToInt64() & 0xFFFF))); return;
            case WM_RBUTTONUP: ShowMenu(); return;
            case WM_DESTROY: timer.Dispose(); anim.Dispose(); Application.ExitThread(); break;
        }
        base.WndProc(ref m);
    }

    void Refresh()
    {
        using (var key = Registry.CurrentUser.OpenSubKey(VdKey))
        {
            var ids = key?.GetValue("VirtualDesktopIDs") as byte[] ?? [];
            desktops = Enumerable.Range(0, ids.Length / 16).Select(i => new Guid(ids.AsSpan(i * 16, 16))).ToArray();
            current = key?.GetValue("CurrentVirtualDesktop") is byte[] { Length: 16 } cur ? Array.IndexOf(desktops, new Guid(cur)) : -1;
        }
        if (desktops.Length == 0) (desktops, current) = ([Guid.Empty], 0);

        var found = new HashSet<Guid>();
        Native.EnumWindows((h, _) =>
        {
            if (IsAppWindow(h) && vdm.GetWindowDesktopId(h, out var id) == 0) found.Add(id);
            return true;
        }, IntPtr.Zero);
        occupied = found;

        // Desktop added/removed: jump straight to the new layout instead of animating.
        if (grow.Length != desktops.Length)
        {
            grow = new float[desktops.Length];
            if (current >= 0) grow[current] = 1;
        }
        anim.Start();
        Animate();
    }

    // Ease every slot towards its target width; the total stays constant because the shrinking and growing slot move equally.
    void Animate()
    {
        bool done = true;
        for (int i = 0; i < grow.Length; i++)
        {
            float target = i == current ? 1 : 0;
            grow[i] += (target - grow[i]) * 0.3f;
            if (Math.Abs(target - grow[i]) < 0.01f) grow[i] = target;
            else done = false;
        }
        if (done) anim.Stop();
        Render();
    }

    static bool IsAppWindow(IntPtr h)
    {
        if (!Native.IsWindowVisible(h) || Native.GetWindow(h, 4 /*GW_OWNER*/) != IntPtr.Zero) return false;
        if ((Native.GetWindowLongPtr(h, -20 /*GWL_EXSTYLE*/) & 0x80 /*WS_EX_TOOLWINDOW*/) != 0) return false;
        Native.DwmGetWindowAttribute(h, 14 /*DWMWA_CLOAKED*/, out int cloaked, 4);
        // Windows on other desktops are cloaked by the shell; anything else cloaked (e.g. suspended UWP) is not really open.
        return (cloaked & ~2 /*DWM_CLOAKED_SHELL*/) == 0 && Native.GetWindowTextLength(h) > 0;
    }

    void Render()
    {
        Native.GetWindowRect(tray, out var tr);
        float s = Native.GetDpiForWindow(tray) / 96f;
        float dot = 8 * s, pill = 24 * s, gap = 8 * s, pad = 12 * s, pen = 1.5f * s;
        int n = desktops.Length;
        int w = (int)Math.Ceiling(pad * 2 + n * dot + (n - 1) * gap + (pill - dot));
        int h = tr.Bottom - tr.Top;

        bool light = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", "SystemUsesLightTheme", 0) is 1;
        var color = light ? Color.FromArgb(230, 0, 0, 0) : Color.FromArgb(230, 255, 255, 255);

        using var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(bmp))
        using (var brush = new SolidBrush(color))
        using (var outline = new Pen(color, pen))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.FromArgb(1, 0, 0, 0)); // alpha 1 so clicks between dots still reach us
            slotEnds = new float[n];
            float x = pad, y = (h - dot) / 2f;
            for (int i = 0; i < n; i++)
            {
                float dw = dot + (pill - dot) * grow[i];
                if (grow[i] > 0.5f || occupied.Contains(desktops[i]))
                    g.FillPath(brush, Capsule(x, y, dw, dot));
                else
                    g.DrawPath(outline, Capsule(x + pen / 2, y + pen / 2, dw - pen, dot - pen));
                x += dw + gap;
                slotEnds[i] = x - gap / 2;
            }
        }

        Native.SetWindowPos(Handle, IntPtr.Zero /*HWND_TOP*/, 0, 0, w, h, 0x10 /*SWP_NOACTIVATE*/);
        var screen = Native.GetDC(IntPtr.Zero);
        var mem = Native.CreateCompatibleDC(screen);
        var hbmp = bmp.GetHbitmap(Color.FromArgb(0));
        var old = Native.SelectObject(mem, hbmp);
        var size = new Size(w, h);
        var src = Point.Empty;
        var blend = new Native.BLENDFUNCTION { SourceConstantAlpha = 255, AlphaFormat = 1 /*AC_SRC_ALPHA*/ };
        Native.UpdateLayeredWindow(Handle, screen, IntPtr.Zero, ref size, mem, ref src, 0, ref blend, 2 /*ULW_ALPHA*/);
        Native.SelectObject(mem, old);
        Native.DeleteObject(hbmp);
        Native.DeleteDC(mem);
        Native.ReleaseDC(IntPtr.Zero, screen);
    }

    static GraphicsPath Capsule(float x, float y, float w, float d)
    {
        var p = new GraphicsPath();
        p.AddArc(x, y, d, d, 90, 180);
        p.AddArc(x + w - d, y, d, d, 270, 180);
        p.CloseFigure();
        return p;
    }

    int HitTest(int x) => Array.FindIndex(slotEnds, end => x < end);

    // The public API can't switch desktops, so press Ctrl+Win+Left/Right as often as needed.
    void SwitchTo(int target)
    {
        if (target < 0 || current < 0 || target == current) return;
        byte arrow = target > current ? (byte)0x27 : (byte)0x25; // VK_RIGHT : VK_LEFT
        const byte LWIN = 0x5B, CTRL = 0xA2;
        const uint EXT = 1, UP = 2;
        Native.keybd_event(LWIN, 0, EXT, 0);
        Native.keybd_event(CTRL, 0, 0, 0);
        for (int i = 0; i < Math.Abs(target - current); i++)
        {
            Native.keybd_event(arrow, 0, EXT, 0);
            Native.keybd_event(arrow, 0, EXT | UP, 0);
        }
        Native.keybd_event(CTRL, 0, UP, 0);
        Native.keybd_event(LWIN, 0, EXT | UP, 0);
    }

    // Native Win32 menu instead of ContextMenuStrip: gets the Windows 11 look (rounded corners, dark mode).
    static void ShowMenu()
    {
        const uint MF_CHECKED = 0x8, MF_SEPARATOR = 0x800, TPM_RIGHTBUTTON = 0x2, TPM_RETURNCMD = 0x100;
        using var run = Registry.CurrentUser.OpenSubKey(RunKey, true)!;
        bool autostart = run.GetValue("DesktopDots") != null;
        IntPtr menu = Native.CreatePopupMenu();
        Native.AppendMenuW(menu, autostart ? MF_CHECKED : 0, 1, "Start with Windows");
        Native.AppendMenuW(menu, MF_SEPARATOR, 0, null);
        Native.AppendMenuW(menu, 0, 2, "Exit");

        // Our bar is a no-activate child of Explorer's taskbar; a hidden top-level owner in our thread
        // can take the foreground, which the menu needs to close when clicking elsewhere.
        var owner = new NativeWindow();
        owner.CreateHandle(new CreateParams());
        Native.AllowDarkModeForWindow(owner.Handle, true);
        Native.SetForegroundWindow(owner.Handle);
        var pt = Cursor.Position;
        int command = Native.TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.X, pt.Y, owner.Handle, IntPtr.Zero);
        Native.DestroyMenu(menu);
        owner.DestroyHandle();

        if (command == 1)
        {
            if (autostart) run.DeleteValue("DesktopDots");
            else run.SetValue("DesktopDots", $"\"{Environment.ProcessPath}\"");
        }
        else if (command == 2) Environment.Exit(0);
    }
}

[ComImport, Guid("aa509086-5ca9-4c25-8f95-589d3c07b48a")]
class VirtualDesktopManager;

[ComImport, Guid("a5cd92ff-29be-454c-8d04-d82879fb3f1b"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IVirtualDesktopManager
{
    [PreserveSig] int IsWindowOnCurrentVirtualDesktop(IntPtr hwnd, out int onCurrent);
    [PreserveSig] int GetWindowDesktopId(IntPtr hwnd, out Guid desktopId);
    [PreserveSig] int MoveWindowToDesktop(IntPtr hwnd, ref Guid desktopId);
}

static class Native
{
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct BLENDFUNCTION { public byte BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat; }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string? title);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, int msg, nint w, nint l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [DllImport("user32.dll")] public static extern nint GetWindowLongPtr(IntPtr h, int index);
    [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, nuint extra);
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("user32.dll")]
    public static extern bool UpdateLayeredWindow(IntPtr h, IntPtr dst, IntPtr pptDst, ref Size size, IntPtr src, ref Point pptSrc, int key, ref BLENDFUNCTION blend, int flags);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
    [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr obj);
    [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr dc);
    [DllImport("user32.dll")] public static extern IntPtr CreatePopupMenu();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool AppendMenuW(IntPtr menu, uint flags, nint id, string? text);
    [DllImport("user32.dll")] public static extern int TrackPopupMenuEx(IntPtr menu, uint flags, int x, int y, IntPtr h, IntPtr tpm);
    [DllImport("user32.dll")] public static extern bool DestroyMenu(IntPtr menu);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    // Undocumented uxtheme exports (by ordinal, stable since Windows 10 1903), used by Explorer, Notepad++ etc. for dark menus.
    [DllImport("uxtheme.dll", EntryPoint = "#135")] public static extern int SetPreferredAppMode(int mode);
    [DllImport("uxtheme.dll", EntryPoint = "#133")] public static extern bool AllowDarkModeForWindow(IntPtr h, bool allow);
    [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out int value, int size);
    [DllImport("advapi32.dll")]
    public static extern int RegNotifyChangeKeyValue(Microsoft.Win32.SafeHandles.SafeRegistryHandle key, bool subtree, int filter, IntPtr ev, bool async);
}
