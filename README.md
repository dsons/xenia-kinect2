<p align="center">
    <a href="https://github.com/xenia-canary/xenia-canary/tree/canary_experimental/assets/icon">
        <img height="256px" src="https://raw.githubusercontent.com/xenia-canary/xenia/master/assets/icon/256.png" />
    </a>
</p>

<h1 align="center">Xenia with Kinect</h1>

<p align="center">
Play Xbox 360 Kinect games on a Windows PC with a real Kinect.<br>
A fork of the Xenia Canary Xbox 360 emulator.
</p>

<p align="center">
<a href="https://github.com/dsons/xenia-kinect2/releases/latest"><b>Download the latest version</b></a>
</p>

## What this is, and how it differs from Xenia Canary

This is **not** the official Xenia Canary. It is Xenia Canary with Kinect
support added. The official Xenia Canary cannot use a Kinect: Kinect games in it
stop at "connect a Kinect" or run without the player's movements.

| | Xenia with Kinect (this fork) | Xenia Canary |
| --- | --- | --- |
| Kinect for Xbox 360 / Kinect for Windows v1 | Works, used as the Xbox 360 Kinect | Not supported |
| Kinect for Xbox One / Kinect for Windows v2 | Works, its picture converted to what an Xbox 360 Kinect sees | Not supported |
| Kinect microphones (voice commands) | Kinect for Xbox One's microphones, or any PC microphone | Not supported |
| Kinect setup and test window | **HID > Kinect...** | None |
| Settings for Kinect games | Included for the tested games below, applied automatically | You set them yourself |
| Profile | A profile called "Player" is created and signed in | You create one |
| Network | Games stay offline on this PC by default (`network_local_only`) | Games may use the network |
| Settings and saves | Kept in the program's own folder (portable) | Kept in Documents |
| Updates | New versions here, less often | Updated almost daily |

This fork is based on Xenia Canary from October 2026 (commit `b083312b8`) and does
not update itself. **For games that do not use Kinect, use the official
[Xenia Canary](https://github.com/xenia-canary/xenia-canary)**: it is newer. Please
report problems with Kinect games here, not to the Xenia Canary team.

## Tested Kinect games

These games start, find the Kinect and follow the player. Their settings are in
[tools/kinect_release/config](tools/kinect_release/config), and two of them also
need a game fix from [tools/kinect_release/patches](tools/kinect_release/patches).
Both are included in the download and applied automatically.

| Game | Title ID | Game fix |
| --- | --- | --- |
| Kinect Adventures! | 4D5308ED | |
| Kinect Sports | 4D5308C9 | |
| Kinect Sports: Season Two | 4D5309D6 | |
| Kinect Star Wars | 4D53090E | |
| The Gunstringer | 4D5309DF | |
| Disney Fantasia: Music Evolved | 42560824 | |
| Child of Eden | 5553084C | |
| Just Dance 4 | 555308B5 | |
| Rise of Nightmares | 53450846 | |
| Dance Central 3 | 373307D9 | Yes |
| Fruit Ninja Kinect | 58410B79 | |
| Kinect Party | 5841125C | Yes |

Other Kinect games may work too, but have not been tried. Games are not
included: use your own copies.

## Quick setup

You need:

- A Windows 10 or 11 PC (64-bit) with a DirectX 12 graphics card. Update your
  graphics driver first (from NVIDIA, AMD or Intel).
- A Kinect and the adapter that connects it to a PC:
  - **Kinect for Xbox 360:** a *Kinect power supply* (wall plug + USB).
  - **Kinect for Xbox One:** the *Kinect Adapter for Windows* and a USB 3.0 port.

Then:

1. **Download** the latest `xenia-kinect2-...-windows-x64.zip` from
   [Releases](https://github.com/dsons/xenia-kinect2/releases/latest), right-click it
   and choose **Extract All...**
2. **Plug in** the adapter's power and its USB cable (straight into the PC).
3. **Start** `xenia_canary.exe`. The Kinect window opens by itself. If it says the
   driver needs to be set up, press **Set up Kinect driver** and click **Yes**.
   When you see yourself in the picture, close the window.
4. **Play:** open your game with **File > Open...**

The Kinect window opens by itself only until the Kinect has worked once (or
untick "Open this window when Xenia starts"). After that it is under
**HID > Kinect...**

**Drivers:** the only driver needed is Microsoft's WinUSB driver, which is
already part of Windows; the **Set up Kinect driver** button (or
`kinect\Setup-Kinect.cmd`) switches the Kinect to it. You do not need the Kinect
for Windows SDK, the Kinect Runtime, Zadig or any other download. The Visual C++
runtime is included in the zip. To give the Kinect back to Microsoft's driver,
run `kinect\Restore-Kinect.cmd`.

The full guide, with troubleshooting, is
[tools/kinect_release/README.txt](tools/kinect_release/README.txt) (included in
the download as README.txt).

## Where everything comes from

Nothing in the download is hidden or comes from an unofficial site. You can
check each part:

| Part | What it is | Where it comes from |
| --- | --- | --- |
| Kinect driver | Microsoft's WinUSB driver | Already part of Windows (`C:\Windows\INF\winusb.inf`). The setup button runs `Setup-Kinect.cmd`, which downloads nothing: it is a short, readable script ([Setup-Kinect.ps1](tools/kinect_release/Setup-Kinect.ps1)) that tells Windows to use that driver for the Kinect. |
| `xenia_canary.exe` | The emulator | Built from this repository's source, which is [Xenia Canary](https://github.com/xenia-canary/xenia-canary) plus the Kinect changes. |
| `kinect\xenia_kinect.dll` | Connects Xenia to the Kinect | Built from [tools/kinect_runtime](tools/kinect_runtime) in this repository. |
| `kinect\freenect.dll` | libfreenect: talks to a Kinect for Xbox 360 | The OpenKinect project, [github.com/OpenKinect/libfreenect](https://github.com/OpenKinect/libfreenect), commit `09a1f09`, unmodified. |
| `kinect\freenect2.dll` | libfreenect2: talks to a Kinect for Xbox One | The OpenKinect project, [github.com/OpenKinect/libfreenect2](https://github.com/OpenKinect/libfreenect2), commit `fd64c5d`, with three small fixes in [tools/kinect_runtime/patches](tools/kinect_runtime/patches). |
| `kinect\libusb-1.0.dll` | libusb: USB access | The libusb project, [release 1.0.30](https://github.com/libusb/libusb/releases/tag/v1.0.30), unmodified. |
| `kinect\turbojpeg.dll` | libjpeg-turbo: decodes the Kinect for Xbox One's colour picture | The libjpeg-turbo project, [release 3.2.0](https://github.com/libjpeg-turbo/libjpeg-turbo/releases/tag/3.2.0), unmodified. |
| `kinect\msvcp140.dll`, `vcruntime140*.dll`, `vcomp140.dll` | Microsoft Visual C++ runtime | Microsoft, copied from Visual Studio's redistributable files. |

The build script ([tools/kinect_runtime/build.ps1](tools/kinect_runtime/build.ps1))
downloads the libraries only from those official addresses and refuses any file
whose SHA-256 checksum differs from the one written in the script. Releases are
built by GitHub Actions from this public source ([build logs](https://github.com/dsons/xenia-kinect2/actions/workflows/Kinect_release.yml)),
and each zip comes with a `.sha256` checksum. The licenses and the exact sources
are in the zip's `kinect\licenses` folder ([SOURCES.txt](tools/kinect_release/SOURCES.txt)).

## Build it yourself

You do not need to build anything to play: the download above is ready to use.
To make your own build anyway, pick one:

- **On your PC:** download
  [Build-Xenia-Kinect2.cmd](https://github.com/dsons/xenia-kinect2/releases/latest/download/Build-Xenia-Kinect2.cmd),
  put it in an empty folder and double-click it. It offers to install what it
  needs (Git, Python, Visual Studio Build Tools and the Vulkan SDK, all free; a few
  gigabytes), downloads the source to `C:\xenia-kinect2-build` and builds it. When
  it finishes, the zip is next to `Build-Xenia-Kinect2.cmd`. The first build takes
  about half an hour; later ones are quicker.
- **On GitHub, with nothing installed:** sign in to GitHub, press **Fork** at the
  top of this page, open your fork's **Actions** tab, choose **Kinect release**
  and press **Run workflow**. After about 30 minutes, the zip is under
  **Artifacts** on that run's page.

Developers: how it works and the build steps are in [docs/kinect.md](docs/kinect.md).


## About Xenia Canary

Xenia Canary is an experimental fork of the Xenia emulator. For more information, see the
[Xenia Canary wiki](https://github.com/xenia-canary/xenia-canary/wiki).

Come chat with us about **emulator-related topics** on [Discord](https://discord.gg/Q9mxZf9).
For developer chat join `#dev` but stay on topic. Lurking is not only fine, but encouraged!
Please check the [FAQ](https://github.com/xenia-canary/xenia-canary/wiki/FAQ) page before asking questions.
We've got jobs/lives/etc, so don't expect instant answers.

Discussing illegal activities will get you banned.

## Status

Buildbot | Status | Releases
-------- | ------ | --------
Canary (🪟, 🐧) | [![CI](https://github.com/xenia-canary/xenia-canary/actions/workflows/Orchestrator.yml/badge.svg?branch=canary_experimental)](https://github.com/xenia-canary/xenia-canary/actions/workflows/Orchestrator.yml/badge.svg?branch=canary_experimental) [![Codacy Badge](https://app.codacy.com/project/badge/Grade/cd506034fd8148309a45034925648499)](https://app.codacy.com/gh/xenia-canary/xenia-canary/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade) | [Latest](https://github.com/xenia-canary/xenia-canary/releases/latest) ◦ [All](https://github.com/xenia-canary/xenia-canary/releases) ◦ [Old](https://github.com/xenia-canary/xenia-canary-releases/releases)

### Experimental Netplay

Buildbot | Status | Releases
-------- | ------ | --------
Windows | [![Codacy Badge](https://app.codacy.com/project/badge/Grade/d814c4b6aa444dcc9c1631e0224b2739)](https://app.codacy.com/gh/AdrianCassar/xenia-canary/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade) | [Latest](https://github.com/AdrianCassar/xenia-canary/releases/latest)

## Quickstart

See the [Quickstart](https://github.com/xenia-canary/xenia-canary/wiki/Quickstart) page.

## FAQ

See the [frequently asked questions](https://github.com/xenia-canary/xenia-canary/wiki/FAQ) page.

## Game Compatibility

See the [Game compatibility list](https://github.com/xenia-canary/game-compatibility/issues)
for currently tracked games, and feel free to contribute your own updates,
screenshots, and information there following the [existing conventions](https://github.com/xenia-canary/game-compatibility/blob/canary/README.md).

## Building

See [building.md](docs/building.md) for setup and information about the
`xb` script. When writing code, check the [style guide](docs/style_guide.md)
and be sure to run clang-format!

## Contributors Wanted!

Have some spare time, know advanced C++, and want to write an emulator?
Contribute! There's a ton of work that needs to be done, a lot of which
is wide open greenfield fun.

**For general rules and guidelines please see [CONTRIBUTING.md](.github/CONTRIBUTING.md).**

Fixes and optimizations are always welcome (please!), but in addition to
that there are some major work areas still untouched:

* Help work through [missing functionality/bugs in games](https://github.com/xenia-canary/xenia-canary/labels/compat)
* Reduce the size of Xenia's [huge log files](https://github.com/xenia-canary/xenia-canary/issues/1526)
* Skilled with Linux? A strong contributor is needed to [help with porting](https://github.com/xenia-canary/xenia-canary/labels/platform-linux)

See more projects [good for contributors](https://github.com/xenia-canary/xenia-canary/labels/good%20first%20issue). It's a good idea to ask on Discord and check the issues page before beginning work on
something.

## Disclaimer

The goal of this project is to experiment, research, and educate on the topic
of emulation of modern devices and operating systems. **It is not for enabling
illegal activity**. All information is obtained via reverse engineering of
legally purchased devices and games and information made public on the internet
(you'd be surprised what's indexed on Google...).
