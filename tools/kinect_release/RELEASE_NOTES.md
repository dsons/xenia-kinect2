Play Xbox 360 Kinect games on a Windows PC with a real Kinect for Xbox 360 or Kinect for Xbox One.

**To play, download `xenia-kinect2-windows-x64.zip` below** (the same as the zip with the version number in its name). Unzip it, plug in the Kinect and start `xenia_canary.exe`: the Kinect window opens and sets up the driver with one button. README.txt inside the zip walks you through it, including which adapter your Kinect needs.

- The only driver needed is the WinUSB driver built into Windows; the **Set up Kinect driver** button switches the Kinect to it. No Kinect SDK or other download is needed.
- Check the Kinect under **HID > Kinect...** before starting a game: it must see you from head to feet, with nothing in front of your legs.
- Plays: Kinect Adventures!, Kinect Sports: Season Two, Fruit Ninja Kinect. Gets partway: Kinect Sports, Dance Central 3, Rise of Nightmares; Kinect Star Wars, The Gunstringer, Fantasia: Music Evolved, Child of Eden and Kinect Party reach their Kinect start screen. Just Dance 4 reaches dancer selection once the Kinect sees your whole body, head to feet. Games are not included.

This is a fork of Xenia Canary with Kinect support, not the official Xenia Canary. For games that do not use Kinect, the official [Xenia Canary](https://github.com/xenia-canary/xenia-canary) is newer.

Found a problem? Please [report it](https://github.com/dsons/xenia-kinect2/issues/new/choose).

`Build-Xenia-Kinect2.cmd` builds the same zip from source on your own PC (optional). The `.sha256` file is the zip's checksum.
