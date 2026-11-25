# One-time driver setup for a Kinect sensor:
#   Kinect for Xbox 360 or Kinect for Windows v1: camera and motor
#   Kinect for Xbox One or Kinect for Windows v2: camera
#
# Xenia talks to the sensor through Microsoft's WinUSB driver (winusb.inf,
# part of Windows), not through the Kinect for Windows SDK or Runtime. This
# script binds WinUSB to the sensor's camera (and the Kinect 1's motor). The
# Kinect 2's microphones keep their normal Windows audio driver.
#
#   Setup-Kinect.cmd           bind WinUSB (asks for administrator rights)
#   Setup-Kinect.cmd -Restore  give the sensor back to its default driver
#   -NoPause                   close without waiting for Enter when it worked
#
# Exit codes: 0 done, 1 failed, 2 no Kinect found, 3 no administrator rights.
#
# Plug the sensor in and power it before running this.

param([switch]$Restore, [switch]$NoPause)

$ErrorActionPreference = 'Stop'

function Finish([int]$code) {
  # A failure always waits, so the message above can be read.
  if (-not $NoPause -or $code -ne 0) { Read-Host 'Press Enter to close' | Out-Null }
  exit $code
}

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  # Changing a device driver needs administrator rights: run again elevated.
  $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
  if ($Restore) { $arguments += '-Restore' }
  if ($NoPause) { $arguments += '-NoPause' }
  try {
    $process = Start-Process powershell -Verb RunAs -Wait -PassThru -ArgumentList $arguments
  } catch {
    Write-Host 'Windows did not give permission to set up the driver.'
    Write-Host 'Run this again and click Yes when Windows asks.'
    Finish 3
  }
  exit $process.ExitCode
}

# Kinect 1: camera 02AE (Xbox 360) or 02BF (Kinect for Windows), motor 02B0.
# Kinect 2: the sensor function of 02C4.
$sensorPattern = '^USB\\VID_045E&PID_(02AE|02B0|02BF|02C4&MI_00)\\'
$sensors = @(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match $sensorPattern })
if ($sensors.Count -eq 0) {
  Write-Host 'No Kinect was found.'
  Write-Host 'Check that it is plugged in to a USB port of this PC and that its power supply is plugged in, then try again.'
  Finish 2
}

if ($Restore) {
  foreach ($sensor in $sensors) {
    Write-Host "Removing the WinUSB binding from $($sensor.InstanceId)"
    & pnputil.exe /remove-device $sensor.InstanceId | Out-Host
  }
  & pnputil.exe /scan-devices | Out-Host
  Write-Host 'Done. Windows has re-detected the sensor with its default driver.'
  Finish 0
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

public static class KinectDriver {
    const int DI_ENUMSINGLEINF = 0x00010000;
    const int DI_FLAGSEX_ALLOWEXCLUDEDDRVS = 0x00000800;
    const int SPDIT_CLASSDRIVER = 1;
    const int SPDIT_COMPATDRIVER = 2;
    const int DICS_FLAG_GLOBAL = 1;
    const int DIREG_DEV = 1;
    const int DIIDFLAG_NOFINISHINSTALLUI = 2;
    const int SPDRP_CLASSGUID = 8;
    static readonly IntPtr Invalid = new IntPtr(-1);

    [StructLayout(LayoutKind.Sequential)]
    struct SP_DEVINFO_DATA { public int cbSize; public Guid ClassGuid; public int DevInst; public IntPtr Reserved; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct SP_DEVINSTALL_PARAMS {
        public int cbSize; public int Flags; public int FlagsEx;
        public IntPtr hwndParent; public IntPtr InstallMsgHandler; public IntPtr InstallMsgHandlerContext;
        public IntPtr FileQueue; public UIntPtr ClassInstallReserved; public int Reserved;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string DriverPath;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct SP_DRVINFO_DATA {
        public int cbSize; public int DriverType; public UIntPtr Reserved;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string Description;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string MfgName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string ProviderName;
        public uint DriverDateLow; public uint DriverDateHigh; public ulong DriverVersion;
    }

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern IntPtr SetupDiCreateDeviceInfoList(IntPtr classGuid, IntPtr hwndParent);
    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);
    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool SetupDiOpenDeviceInfoW(IntPtr set, string instanceId, IntPtr hwnd, int flags, ref SP_DEVINFO_DATA data);
    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool SetupDiGetDeviceInstallParamsW(IntPtr set, ref SP_DEVINFO_DATA data, ref SP_DEVINSTALL_PARAMS p);
    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool SetupDiSetDeviceInstallParamsW(IntPtr set, ref SP_DEVINFO_DATA data, ref SP_DEVINSTALL_PARAMS p);
    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiBuildDriverInfoList(IntPtr set, ref SP_DEVINFO_DATA data, int type);
    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool SetupDiEnumDriverInfoW(IntPtr set, ref SP_DEVINFO_DATA data, int type, int index, ref SP_DRVINFO_DATA drv);
    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool SetupDiSetDeviceRegistryPropertyW(IntPtr set, ref SP_DEVINFO_DATA data, int prop, byte[] buf, int size);
    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr SetupDiCreateDevRegKeyW(IntPtr set, ref SP_DEVINFO_DATA data, int scope, int hwProfile, int keyType, IntPtr infHandle, string infSection);
    [DllImport("newdev.dll", SetLastError = true)]
    static extern bool DiInstallDevice(IntPtr hwnd, IntPtr set, ref SP_DEVINFO_DATA data, ref SP_DRVINFO_DATA drv, int flags, out bool needReboot);

    static string Error() { return "error 0x" + Marshal.GetLastWin32Error().ToString("X8"); }

    static IntPtr Open(string instanceId, ref SP_DEVINFO_DATA dev) {
        IntPtr set = SetupDiCreateDeviceInfoList(IntPtr.Zero, IntPtr.Zero);
        if (set == Invalid) throw new Exception("SetupDiCreateDeviceInfoList: " + Error());
        dev = new SP_DEVINFO_DATA();
        dev.cbSize = Marshal.SizeOf(typeof(SP_DEVINFO_DATA));
        if (!SetupDiOpenDeviceInfoW(set, instanceId, IntPtr.Zero, 0, ref dev)) {
            string e = Error();
            SetupDiDestroyDeviceInfoList(set);
            throw new Exception("SetupDiOpenDeviceInfo: " + e);
        }
        return set;
    }

    // Returns null on success, or why it failed.
    public static string Bind(string instanceId, string infPath, string interfaceGuid, string forceClassGuid) {
        SP_DEVINFO_DATA dev = new SP_DEVINFO_DATA();
        if (!String.IsNullOrEmpty(forceClassGuid)) {
            IntPtr s0 = Open(instanceId, ref dev);
            byte[] b = System.Text.Encoding.Unicode.GetBytes(forceClassGuid + "\0");
            SetupDiSetDeviceRegistryPropertyW(s0, ref dev, SPDRP_CLASSGUID, b, b.Length);
            SetupDiDestroyDeviceInfoList(s0);
        }
        IntPtr set = Open(instanceId, ref dev);
        try {
            IntPtr key = SetupDiCreateDevRegKeyW(set, ref dev, DICS_FLAG_GLOBAL, 0, DIREG_DEV, IntPtr.Zero, null);
            if (key == Invalid) return "SetupDiCreateDevRegKey: " + Error();
            using (RegistryKey k = RegistryKey.FromHandle(new SafeRegistryHandle(key, true))) {
                k.SetValue("DeviceInterfaceGUIDs", new string[] { interfaceGuid }, RegistryValueKind.MultiString);
            }

            SP_DEVINSTALL_PARAMS p = new SP_DEVINSTALL_PARAMS();
            p.cbSize = Marshal.SizeOf(typeof(SP_DEVINSTALL_PARAMS));
            if (!SetupDiGetDeviceInstallParamsW(set, ref dev, ref p)) return "SetupDiGetDeviceInstallParams: " + Error();
            p.Flags |= DI_ENUMSINGLEINF;
            p.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
            p.DriverPath = infPath;
            if (!SetupDiSetDeviceInstallParamsW(set, ref dev, ref p)) return "SetupDiSetDeviceInstallParams: " + Error();

            SP_DRVINFO_DATA chosen = new SP_DRVINFO_DATA();
            bool found = false;
            foreach (int type in new int[] { SPDIT_CLASSDRIVER, SPDIT_COMPATDRIVER }) {
                if (!SetupDiBuildDriverInfoList(set, ref dev, type)) continue;
                for (int i = 0; !found; i++) {
                    SP_DRVINFO_DATA d = new SP_DRVINFO_DATA();
                    d.cbSize = Marshal.SizeOf(typeof(SP_DRVINFO_DATA));
                    if (!SetupDiEnumDriverInfoW(set, ref dev, type, i, ref d)) break;
                    if (d.Description.IndexOf("WinUsb", StringComparison.OrdinalIgnoreCase) >= 0) { chosen = d; found = true; }
                }
                if (found) break;
            }
            if (!found) return "NO_WINUSB_DRIVER";

            bool reboot;
            if (!DiInstallDevice(IntPtr.Zero, set, ref dev, ref chosen, DIIDFLAG_NOFINISHINSTALLUI, out reboot)) {
                return "DiInstallDevice: " + Error();
            }
            return null;
        } finally {
            SetupDiDestroyDeviceInfoList(set);
        }
    }
}
'@

$inf = Join-Path $env:SystemRoot 'INF\winusb.inf'
$usbDeviceClass = '{88BAE032-5A81-49F0-BC3D-A4FF138216D6}'
$failed = $false
foreach ($sensor in $sensors) {
  # libusb finds a WinUSB device through a device interface GUID.
  $productId = [regex]::Match($sensor.InstanceId, 'PID_([0-9A-F]{4})').Groups[1].Value
  $interfaceGuid = "{4B696E65-6374-4E55-4900-00000000$productId}"
  $service = (Get-PnpDeviceProperty -InstanceId $sensor.InstanceId -KeyName DEVPKEY_Device_Service).Data
  if ($service -eq 'WINUSB' -and $sensor.Status -eq 'OK') {
    Write-Host "$($sensor.InstanceId) already uses WinUSB."
    continue
  }
  Write-Host "Binding WinUSB to $($sensor.InstanceId) (was: $service)"
  $result = [KinectDriver]::Bind($sensor.InstanceId, $inf, $interfaceGuid, $null)
  if ($result -eq 'NO_WINUSB_DRIVER') {
    $result = [KinectDriver]::Bind($sensor.InstanceId, $inf, $interfaceGuid, $usbDeviceClass)
  }
  if ($result) {
    Write-Host "  Failed: $result"
    $failed = $true
  } else {
    Write-Host '  Done.'
  }
}
if ($failed) {
  Write-Host 'Setup did not finish. Unplug the Kinect, plug it in again and run this again.'
  Finish 1
}
Write-Host ''
Write-Host 'Done. The Kinect is ready for Xenia.'
Finish 0
