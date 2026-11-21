param([int]$ProcessId, [string]$Title, [int]$X, [int]$Y, [int]$W, [int]$H)
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class WinMove {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hh, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public static string Move(uint want, string title, int x, int y, int w, int hh) {
    SetProcessDPIAware();
    string done = "no window found";
    EnumWindows((h, l) => {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid != want || !IsWindowVisible(h)) return true;
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      if (!t.ToString().StartsWith(title)) return true;
      SetWindowPos(h, IntPtr.Zero, x, y, w, hh, 0x0004 | 0x0010);
      RECT r; GetWindowRect(h, out r);
      done = String.Format("moved '{0}' to ({1},{2})-({3},{4})", t, r.L, r.T, r.R, r.B);
      return false;
    }, IntPtr.Zero);
    return done;
  }
}
'@
[WinMove]::Move([uint32]$ProcessId, $Title, $X, $Y, $W, $H)
