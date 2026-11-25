Play Xbox 360 Kinect games on a Windows PC with a real Kinect for Xbox 360 or Kinect for Xbox One.

**To play, download `xenia-kinect2-...-windows-x64.zip` below.** Unzip it, run `kinect\Setup-Kinect.cmd` once, then start `xenia_canary.exe`. README.txt inside the zip walks you through it, including which adapter your Kinect needs.

- The only driver needed is the WinUSB driver built into Windows; `Setup-Kinect.cmd` switches the Kinect to it. No Kinect SDK or other download is needed.
- Check the Kinect under **HID > Kinect...** before starting a game.
- Tested games: Kinect Adventures!, Kinect Sports, Kinect Sports: Season Two, Kinect Star Wars, The Gunstringer, Disney Fantasia: Music Evolved, Child of Eden, Just Dance 4, Rise of Nightmares, Dance Central 3, Fruit Ninja Kinect and Kinect Party. Games are not included.

This is a fork of Xenia Canary with Kinect support, not the official Xenia Canary. For games that do not use Kinect, the official [Xenia Canary](https://github.com/xenia-canary/xenia-canary) is newer.

`Build-Xenia-Kinect2.cmd` builds the same zip from source on your own PC (optional). The `.sha256` file is the zip's checksum.
