<p align="center">
    <a href="https://github.com/xenia-canary/xenia-canary/tree/canary_experimental/assets/icon">
        <img height="256px" src="https://raw.githubusercontent.com/xenia-canary/xenia/master/assets/icon/256.png" />
    </a>
</p>

<h1 align="center">Xenia with Kinect</h1>

<p align="center">
Play your Xbox 360 Kinect games on a Windows PC, with a real Kinect.<br>
Free. Based on the <a href="https://github.com/xenia-canary/xenia-canary">Xenia Canary</a> Xbox 360 emulator.
</p>

<p align="center">
<a href="https://github.com/dsons/xenia-kinect2/releases/latest/download/xenia-kinect2-windows-x64.zip"><b>⬇ Download for Windows</b></a>
</p>

<p align="center">
<a href="#1-what-you-need">What you need</a> ·
<a href="#2-set-it-up">Set it up</a> ·
<a href="#3-get-in-a-good-position">Get in a good position</a> ·
<a href="#4-play">Play</a> ·
<a href="#something-not-working">Something not working?</a> ·
<a href="#which-games-work">Which games work</a>
</p>

## 1. What you need

- **A Windows 10 or Windows 11 computer** with a graphics card made in roughly the
  last eight years (it needs DirectX 12; most gaming PCs and laptops have it).
  Update the graphics card's driver first, from the NVIDIA, AMD or Intel website.
- **A Kinect**, either kind:
  - **Kinect for Xbox 360** (the older, smaller one with a tilting base), and a
    **Kinect power supply** for it (sold as "Kinect AC adapter for Xbox 360"; it
    has a wall plug and a normal USB plug).
  - **Kinect for Xbox One** (the larger, newer one), and the **Kinect Adapter for
    Windows**. Your computer needs a USB 3.0 port (usually blue inside, or marked
    "SS").
  - A "Kinect for Windows" works the same as the Xbox one it looks like.
- **Your own Kinect games**, copied from your own discs (see
  [Your own games only](#your-own-games-only)). Games are not included.

Nothing else: no Microsoft Kinect software, no other downloads.

## 2. Set it up

You only do this once.

1. **Download** the zip file with the button at the top of this page
   ([this link](https://github.com/dsons/xenia-kinect2/releases/latest/download/xenia-kinect2-windows-x64.zip)).
2. **Unzip it:** right-click the downloaded file, choose **Extract All...**, then
   **Extract**. Put the folder somewhere easy to find, like your Desktop. Don't
   run anything from inside the zip.
3. **Plug in the Kinect:** its adapter into the wall, and the adapter's USB plug
   straight into your computer (not into a USB hub). A light on the Kinect or the
   adapter comes on.
4. **Open the folder and double-click `xenia_canary.exe`** (Windows may show it
   as just **xenia_canary**, with the Xenia icon). If Windows says "Windows protected your PC", click **More info**, then **Run
   anyway** (Windows says this about programs it doesn't know yet).
5. **The Kinect window opens by itself.** If it says the Kinect's driver needs to
   be set up, click the green **Set up Kinect driver** button, then **Yes** when
   Windows asks for permission. A few seconds later you see yourself in the
   window.

## 3. Get in a good position

The Kinect has to see **all of you, head to feet**, with nothing in front of your
legs (like a coffee table). If it can't, games may see you but not take your
moves. The Kinect window helps you get it right:

1. Stand where you will play, facing the Kinect.
2. Read the lines under the two pictures and do what they say:

   | The window says | What to do |
   | --- | --- |
   | Your feet are out of the picture. | Tilt the Kinect down a little, or stand further back. |
   | Your head is out of the picture. | Tilt the Kinect up a little, or stand further back. |
   | Something is in front of your legs. | Move the table or chair out of the way, or move the Kinect. |
   | Step back. / Come closer. | Move until it says "Good distance" (about 2 to 3 metres, 6 to 10 feet). |

3. When it says **"Good position. You're ready to play!"**, close the Kinect
   window with the **X** in its top-right corner, and play.

**Kinect for Xbox One in a small room?** In the same window, move the **Step
back** slider to the right: games then see you as if you stood further away.

Do this again whenever you move furniture or the Kinect, or when a game doesn't
see you. The Kinect window is always in the **HID** menu at the top of the Xenia
window: **HID > Kinect...**

## 4. Play

1. Click the **File** menu at the top of the Xenia window, then **Open...**
2. Pick your game: a copy of your game disc (usually a file ending in `.iso`).
3. The game fills the screen. Press **Esc** to get the window and its menus back.

Next time, just double-click `xenia_canary.exe` and play.

## Something not working?

| What happens | What to do |
| --- | --- |
| The Kinect window says **No Kinect found** | Check the adapter is plugged into the wall and its light is on. Plug the USB cable straight into the computer, or try another USB port (for a Kinect for Xbox One, a USB 3.0 port). Then click **Retry**. |
| The window says the driver needs to be set up | Click **Set up Kinect driver**, then **Yes**. |
| The window says the Kinect is in use by another program | Close other Kinect or camera programs, then click **Retry**. |
| A game says to connect a Kinect | Open **HID > Kinect...** and fix what it says, then restart the game. |
| A game doesn't notice you, or doesn't take your moves | Open **HID > Kinect...** and move or adjust until it says "Good position" ([step 3](#3-get-in-a-good-position)). |
| The picture is choppy (Kinect for Xbox One) | Try another USB 3.0 port, ideally one on the back of the computer. |
| Voice commands don't work | In the Kinect window, check the microphone level moves when you speak. A Kinect for Xbox 360's microphone can't be used; pick a headset or webcam microphone instead. |
| Windows asks if Xenia may use the network | Click **Cancel**. Kinect games don't need it. |
| Other Kinect software stopped working | Double-click `Restore-Kinect.cmd` in the `kinect` folder. To play here again, click **Set up Kinect driver** in the Kinect window. |

**Still stuck? Please tell us**, even if you're not sure it's a bug:
**[report a problem](https://github.com/dsons/xenia-kinect2/issues/new/choose)**
(you need a free GitHub account). A short form asks which Kinect and game you
used and what happened. Please also attach the log files: in the Kinect window
click **Show log files**, then drag `xenia.log` (and `xenia.previous.log` if the
problem happened the time before) into the form. They tell us exactly what went
wrong, so we don't have to ask you lots of questions.

Reports that a game works, or ideas, are welcome too. Problems with games that
don't use Kinect belong to the official
[Xenia Canary](https://github.com/xenia-canary/xenia-canary), but please don't
report Kinect problems there: this version isn't theirs.

The download also contains a full guide, **README.txt**, with all of this and
more.

## Which games work

| Game | How far it has been tested |
| --- | --- |
| Kinect Adventures! | Plays |
| Kinect Sports: Season Two | Plays |
| Fruit Ninja Kinect | Plays |
| Kinect Sports | Starts a sport |
| Dance Central 3 | Reaches the main menu |
| Rise of Nightmares | Reaches its menus |
| Just Dance 4 | Reaches dancer selection (needs the Kinect to see you head to feet); songs not tried yet |
| Kinect Star Wars | Reaches its start screen; not tried further yet |
| The Gunstringer | Reaches its start screen; not tried further yet |
| Disney Fantasia: Music Evolved | Reaches its start screen; not tried further yet |
| Child of Eden | Reaches its start screen; not tried further yet |
| Kinect Party | Reaches its start screen; not tried further yet |

Other Kinect games may work too. If you get further in one of these, or another
game works, please [tell us](https://github.com/dsons/xenia-kinect2/issues/new/choose).
Settings these games need are included and applied automatically.

## Your own games only

This project is for **preserving and playing Xbox 360 Kinect games you own**,
from copies you made yourself of your own discs or your own console's downloads.
It does not include any games, and it does not help anyone get them:

- No game files, disc images, keys or download links are included, and none may
  be posted in this project's reports or discussions (they will be deleted).
- Questions about where to get games will not be answered.
- Some Xbox Live Arcade games run as their trial unless `license_mask = 1` is set
  in `xenia-canary.config.toml`. Only do that for games you bought in full.

Xenia, Kinect support included, is built from public information and the reverse
engineering of legally purchased hardware and software.

## For technical readers

Everything below is optional reading. Click a heading to open it.

<details>
<summary><b>How this differs from the official Xenia Canary</b></summary>

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

**About this repository:** it is a fork of
[xenia-canary/xenia-canary](https://github.com/xenia-canary/xenia-canary), with
Xenia Canary's full history, credit and license, kept as a standalone
repository rather than a GitHub fork so that it shows up in GitHub search. It
is based on Xenia Canary from October 2026 (commit `b083312b8`) and does not
update itself. **For games that do not use Kinect, use the official
[Xenia Canary](https://github.com/xenia-canary/xenia-canary)**: it is newer. Please
report problems with Kinect games here, not to the Xenia Canary team.

</details>

<details>
<summary><b>Why a separate fork, not a pull request to Xenia Canary?</b></summary>

Because of how it was made: most of the Kinect code in this fork was written
with an AI assistant (Claude, by Anthropic) and tested on real Kinect sensors.
Many open-source projects, emulators included, are wary of AI-written code, and
reviewing a change this large would fall on Xenia's volunteer developers. So it
is offered as its own fork instead of asking them to take it on. If the Xenia
team or anyone else wants to bring parts of it into Xenia Canary, they are
welcome to: it uses the same BSD license.

</details>

<details>
<summary><b>Drivers</b></summary>

The only driver needed is Microsoft's WinUSB driver, which is part of Windows.
The **Set up Kinect driver** button runs `kinect\Setup-Kinect.cmd`, which tells
Windows to use WinUSB for the Kinect (the Kinect for Xbox 360's camera and motor,
the Kinect for Xbox One's sensor). The Kinect for Windows SDK, the Kinect Runtime
and Zadig are not used. `kinect\Restore-Kinect.cmd` undoes it. If Windows Update
or Microsoft's Kinect software later switches the Kinect to another driver, the
Kinect window notices and offers the button again.

</details>

<details>
<summary><b>Where every file in the download comes from</b></summary>

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

</details>

<details>
<summary><b>Log files and settings</b></summary>

Xenia writes `xenia.log` next to `xenia_canary.exe` (the run before is kept as
`xenia.previous.log`). At every start it records the Windows version, the files in
the `kinect` folder, every Kinect USB device with its driver, Device Manager
problem code and the USB hub and controller it is plugged into, the microphones
and the Kinect settings; then how each sensor opened (in detail), the frame rate
every minute, and any drop-out. `kinect\Setup-Kinect.log` records each driver
setup. `xenia-skeleton.csv` records the body the game tracks, for problems with
poses and gestures. Settings are explained in
[README.txt](tools/kinect_release/README.txt) (Advanced settings) and
[docs/kinect.md](docs/kinect.md).

</details>

<details>
<summary><b>Build it yourself</b></summary>

You do not need to build anything to play: the download above is ready to use.
To make your own build anyway, pick one:

- **On your PC:** download
  [For-Developers-Build-From-Source.cmd](https://github.com/dsons/xenia-kinect2/releases/latest/download/For-Developers-Build-From-Source.cmd),
  put it in an empty folder and double-click it. It offers to install what it
  needs (Git, Python, Visual Studio Build Tools and the Vulkan SDK, all free; a few
  gigabytes), downloads the source to `C:\xenia-kinect2-build` and builds it. When
  it finishes, the zip is next to `For-Developers-Build-From-Source.cmd`. The first build takes
  about half an hour; later ones are quicker.
- **On GitHub, with nothing installed:** sign in to GitHub, press **Fork** at the
  top of this page, then open your fork's **Actions** tab. The first time,
  GitHub asks you to turn workflows on: press the green button to allow them.
  Choose **Kinect release** on the left and press **Run workflow**. When the run
  finishes (about 30 minutes), the zip is under **Artifacts** at the bottom of
  that run's page.

Developers: how it works and the build steps are in [docs/kinect.md](docs/kinect.md).

</details>

## About Xenia Canary

This project is built on [Xenia Canary](https://github.com/xenia-canary/xenia-canary),
the Xbox 360 emulator made by the Xenia team, and all the emulation apart from
Kinect is their work. Their [wiki](https://github.com/xenia-canary/xenia-canary/wiki),
[FAQ](https://github.com/xenia-canary/xenia-canary/wiki/FAQ),
[game compatibility list](https://github.com/xenia-canary/game-compatibility/issues)
and Discord are about Xenia Canary itself: they are useful for general
questions about the emulator, but please ask about Kinect here.

For developers: Xenia's own build instructions are in
[docs/building.md](docs/building.md) and its style guide in
[docs/style_guide.md](docs/style_guide.md).

## Disclaimer

The goal of this project is to experiment, research, and educate on the topic
of emulation of modern devices and operating systems. **It is not for enabling
illegal activity**. Games are not included; use copies you own.

This project is not affiliated with or endorsed by Microsoft or the Xenia team.
Xbox, Xbox 360, Xbox One and Kinect are trademarks of Microsoft Corporation.
Xenia is released under the BSD license ([LICENSE](LICENSE)).
