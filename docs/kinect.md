# Kinect support

This fork emulates the Xbox 360 Kinect sensor with a Kinect connected to the
PC: a Kinect for Xbox 360 / Kinect for Windows v1, whose frames are used as
they are, or a Kinect for Xbox One / Kinect for Windows v2, whose frames are
converted into the frames an Xbox 360 Kinect produces. The player-facing
guide is [tools/kinect_release/README.txt](../tools/kinect_release/README.txt).

## How a title reaches the camera

Kinect titles link the NUI runtime statically. It talks to the hardware
through a few kernel and XAM exports:

| Area | Where | What |
| --- | --- | --- |
| Camera | `xboxkrnl_nui_camera.cc` | `PsCamDeviceRequest`: device state, streams, calibration, colour settings, 640x480 depth (mm) and YUV/Bayer colour frames |
| Microphones | `xboxkrnl_audio.cc`, `xam_voice.cc` | The four-channel 16 kHz microphone array, render driver tic and MEC client |
| Skeleton tracker data | `xboxkrnl_ldi.cc`, `xam_content.cc` | Cabinet LZX block decompression (`LDI*`) and mounting packages stored on the disc (`XamContentOpenFile`) |
| GPU | `d3d12_command_processor.cc`, `draw_util.cc` | Copy command 2 resolves (DXT3A_AS_1_1_1_1 masks), tile-list resolves, `PM4_WAIT_IB_PFD_COMPLETE`, wide 1D textures (`texture_1d_wide_max_rows`) |
| Identity and sign-in | `xam_nui.cc`, `xam_ui.cc`, `profile_manager.cc` | Enrollment slots, the Kinect sign-in UI, `default_profile_gamertag` |
| Tilt motor | `xam_nui.cc` | A virtual motor that answers status and angle requests; the real motor is not moved |

Kernel objects used by Kinect titles are also implemented: guest-allocated
`KTIMER` and mutant objects, and close procedures of guest object types.

The Kinect skeleton tracker needs the D3D12 GPU backend.

## Host sensors

`nui_kinect_win.cc` loads `kinect\xenia_kinect.dll` next to the executable at
runtime (`tools/kinect_runtime`), so the emulator has no build-time
dependency on libfreenect, libfreenect2 or libusb.

- **Kinect for Xbox 360 / Kinect for Windows v1** (libfreenect): 640x480 RGB
  and depth registered onto it, used unchanged.
- **Kinect for Xbox One / Kinect for Windows v2** (libfreenect2): 960x540 colour
  and registered depth. `nui_kinect2_convert.h` remaps both to the Kinect 1's
  640x480 pinhole, removes time-of-flight flying pixels and limits depth to
  the Kinect 1's 0.4-4.5 m range. `nui_kinect2_zoom` and `nui_kinect2_pan` move
  the virtual camera.

With `nui_camera = auto` a Kinect 1 is preferred when both are connected.
The microphones are captured with WASAPI from the device named by
`nui_mic_device` (the Kinect 2's array by default; the Kinect 1's microphones
need Microsoft's driver and firmware and are not used).

Both sensors use Microsoft's in-box WinUSB driver, bound by
`tools/kinect_release/Setup-Kinect.ps1`; the Kinect for Windows SDK and
Runtime are not used.

## Kinect window

**HID > Kinect...** (`src/xenia/app/kinect_dialog.cc`) chooses the sensor,
shows its status and frame rate, a live colour and depth picture with a
distance hint, the Kinect 2 view controls and the microphone with a level
meter. It works without a game; settings are saved to the configuration file
as they change (through `OverrideConfigValue`, since the file is written from
config values).

It also guides the driver setup: it lists the Kinect devices Windows sees
(SetupAPI) and says whether none is plugged in, a Kinect 1 has no power, or a
sensor is not on WinUSB yet, in which case **Set up Kinect driver** runs
`kinect\Setup-Kinect.cmd -NoPause` and opens the sensor a few seconds after it
succeeds. A sensor that opens but sends no frames for 6 seconds is reopened.

The release's configuration sets `nui_window_on_start`: the window opens at
start when SetupAPI sees a Kinect, and never when Xenia is started with a game
(`target`), so launchers and shortcuts go straight into the game.

## Diagnostics

Kinect problems mostly come from outside Xenia (the USB port, the driver, the
adapter), so the log records them in detail:

- At start, `LogKinectReport()` writes the Windows build, the files in the
  kinect folder (and any missing DLL), every Kinect USB device Windows knows
  with its driver, version, Device Manager problem code, port and the hubs and
  USB controller it hangs from, the Kinect settings and the microphones.
- While a sensor opens, the runtime passes libfreenect's and libfreenect2's
  informational messages (devices found, firmware, interfaces), then only
  warnings and errors. A failed open logs the devices again.
- Streaming logs the time to the first frame, the frames per minute, and any
  reconnect.
- The Kinect window logs device changes, Retry, sensor changes and the driver
  setup's exit code.
- `kinect\Setup-Kinect.ps1` appends to `kinect\Setup-Kinect.log`: Windows,
  administrator rights, the Kinect devices before and after, and each binding.
- The previous run's log is kept as `xenia.previous.log`.

## Building a release

`Build-Xenia-Kinect2.cmd` (repository root) is the one-step build for players: run
from an empty folder it clones this repository to `C:\xenia-kinect2-build`, builds
it and copies the zip next to itself; run from a checkout it builds that
checkout. It calls `tools/kinect_release/build-all.ps1`, which checks for Git,
Python 3, Visual Studio 2022 or later with the C++ workload and the Vulkan SDK
(offering to install missing ones with winget; `-NoInstall` turns that off),
then runs:

```
python xenia-build.py setup
python xenia-build.py build --config=release --target=xenia-app
powershell -ExecutionPolicy Bypass -File tools\kinect_runtime\build.ps1
powershell -ExecutionPolicy Bypass -File tools\kinect_release\package.ps1 -Version <version>
```

`build.ps1` fetches libfreenect (pinned commit), libfreenect2, libusb and
libjpeg-turbo (checked against SHA-256 hashes), applies
`tools/kinect_runtime/patches` to libfreenect2 and builds the runtime into
`build\kinect`. `package.ps1` writes
`build\release\xenia-kinect2-<version>-windows-x64.zip`. The version is the tag on
the commit (`v1.2.3` gives `1.2.3`), or `custom-<commit>`.

The **Kinect release** GitHub Actions workflow (`.github/workflows/Kinect_release.yml`)
runs the same script: started by hand it uploads the zip as an artifact, and a
pushed `v*` tag also publishes a release with the zip, its checksum,
`Build-Xenia-Kinect2.cmd` and `tools/kinect_release/RELEASE_NOTES.md`.

## Settings

| Cvar | Default | |
| --- | --- | --- |
| `nui_camera` | `auto` | `auto`, `kinect1`, `kinect2`, `synthetic` (a test figure) or `none` |
| `nui_camera_mirror` | `false` | Mirror the frames |
| `nui_camera_log` | `false` | Log every camera request |
| `nui_kinect2_zoom` | `1.3` | Kinect 2 virtual step back (1.0-1.6) |
| `nui_kinect2_pan` | `0` | Kinect 2 sideways shift in sensor pixels (-40 to 40) |
| `nui_mic` | `true` | Emulate the microphone array |
| `nui_mic_device` | `Xbox NUI Sensor` | Capture device name to match |
| `nui_kinect_id` | `true` | Enroll a newly tracked player as the signed-in user |
| `nui_window_on_start` | `false` (`true` in the release) | Open the Kinect window at start |
| `default_profile_gamertag` | empty | Create and sign in a profile on boot |
| `texture_1d_wide_max_rows` | `32` | Kinect titles need 96 |
| `resolve_constant_one_threshold` | `128` | Mask threshold for copy command 2 resolves |

`network_local_only` (default `true`) binds the sockets games open to the
loopback interface: Kinect titles open Xbox Live and system link sockets they
do not need for Kinect play, and on all interfaces that makes Windows ask to
let Xenia through its firewall.

Kinect titles also need `readback_resolve = "full"` and
`readback_memexport = true`; the per-title files in
`tools/kinect_release/config` set these.
