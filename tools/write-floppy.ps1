# Writes an image to a physical floppy and verifies its bytes.
param(
    [string]$Image = (Join-Path (Split-Path $PSScriptRoot -Parent) 'built\flopnix.img'),
    [string]$Drive = '\\.\A:'
)
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class RawDisk {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Auto)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share,
        IntPtr sec, uint disp, uint flags, IntPtr tmpl);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle h, uint code, IntPtr inBuf,
        uint inSz, IntPtr outBuf, uint outSz, out uint ret, IntPtr ov);
    const uint GENERIC_READ = 0x80000000, GENERIC_WRITE = 0x40000000;
    const uint FSCTL_LOCK_VOLUME = 0x00090018;
    const uint FSCTL_DISMOUNT_VOLUME = 0x00090020;

    public static void WriteImage(string dev, byte[] data) {
        SafeFileHandle h = CreateFile(dev, GENERIC_READ | GENERIC_WRITE, 0x3,
            IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h.IsInvalid) throw new IOException("open failed, err=" + Marshal.GetLastWin32Error());
        uint ret;
        bool locked = DeviceIoControl(h, FSCTL_LOCK_VOLUME, IntPtr.Zero, 0, IntPtr.Zero, 0, out ret, IntPtr.Zero);
        if (!locked) {
            if (!DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, IntPtr.Zero, 0, IntPtr.Zero, 0, out ret, IntPtr.Zero))
                throw new IOException("dismount failed, err=" + Marshal.GetLastWin32Error());
            for (int i = 0; i < 10 && !locked; i++) {
                System.Threading.Thread.Sleep(300);
                locked = DeviceIoControl(h, FSCTL_LOCK_VOLUME, IntPtr.Zero, 0, IntPtr.Zero, 0, out ret, IntPtr.Zero);
            }
        }
        using (FileStream fs = new FileStream(h, FileAccess.Write, 65536)) {
            fs.Write(data, 0, data.Length);
            fs.Flush();
        }
    }
    public static byte[] ReadStart(string dev, int bytes) {
        SafeFileHandle h = CreateFile(dev, GENERIC_READ, 0x3, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h.IsInvalid) throw new IOException("open failed, err=" + Marshal.GetLastWin32Error());
        byte[] buf = new byte[bytes];
        using (FileStream fs = new FileStream(h, FileAccess.Read, 65536)) {
            int off = 0;
            while (off < bytes) {
                int n = fs.Read(buf, off, bytes - off);
                if (n <= 0) break;
                off += n;
            }
        }
        return buf;
    }
}
"@

$bytes = [IO.File]::ReadAllBytes($Image)
Write-Host "writing $($bytes.Length) bytes from '$Image' to $Drive (a few minutes on USB floppy)..."
[RawDisk]::WriteImage($Drive, $bytes)

Write-Host "verifying FULL disk (reads every sector back - takes a minute)..."
$check = $bytes.Length
$rb = [RawDisk]::ReadStart($Drive, $check)
$diff = 0
$firstbad = -1
for ($i = 0; $i -lt $check; $i++) {
    if ($bytes[$i] -ne $rb[$i]) { $diff++; if ($firstbad -lt 0) { $firstbad = $i } }
}
if ($diff -eq 0) { Write-Host "OK: all $check bytes verified, 0 mismatches" }
else {
    $sector = [int]($firstbad / 512)
    Write-Host "FAILED: $diff mismatched bytes, first at byte $firstbad (sector $sector)"
    Write-Host "-> that sector may be bad on this disk; try another floppy"
    exit 1
}
Write-Host "done - eject the disk. Windows will call it unformatted; do NOT format it."
Read-Host "press Enter to close"
