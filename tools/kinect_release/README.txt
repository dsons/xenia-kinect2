XENIA WITH KINECT
=================

Play Xbox 360 Kinect games on your Windows PC with a real Kinect.

This is the Xenia Canary Xbox 360 emulator with Kinect support added. It
works with a Kinect for Xbox 360 or a Kinect for Xbox One. Games are not
included: use your own copies.

It is not the official Xenia Canary, which cannot use a Kinect. For games
that do not use Kinect, the official Xenia Canary is newer and a better
choice. Report problems with this version at
github.com/dsons/xenia-kinect2/issues.


QUICK SETUP
-----------

Do steps 1 to 4 once. After that, just start xenia_canary.exe and play.


STEP 1. Check what you need

  [ ] A Windows 10 or Windows 11 PC (64-bit) with a graphics card that
      supports DirectX 12. Most gaming PCs and laptops from the last
      eight years do. Update its driver first, from the NVIDIA, AMD or
      Intel website.

  [ ] A Kinect, and the right power adapter for it:

      Kinect for Xbox 360 (the older, smaller one with a tilting base):
        You need a "Kinect power supply" (also sold as "Kinect AC adapter
        for Xbox 360"). It has a wall plug and a normal USB plug. The
        Kinect's own cable does not fit a PC without it.

      Kinect for Xbox One (the larger, newer one):
        You need the "Kinect Adapter for Windows". It has a wall plug and
        a USB 3.0 plug. Your PC needs a USB 3.0 port (usually blue inside,
        or marked "SS").

      Kinect for Windows (v1 or v2) works the same as the Xbox version it
      looks like.


STEP 2. Unzip this download

  Right-click the zip file and choose "Extract All...", then open the
  folder it makes. Put the folder somewhere easy, like Documents or your
  Desktop. Do not run Xenia from inside the zip.


STEP 3. Plug in the Kinect

  Plug the adapter's power plug into the wall and its USB plug straight
  into your PC (not into a USB hub or extension). A light on the Kinect or
  the adapter should come on.


STEP 4. Start Xenia and set up the Kinect

  Double-click "xenia_canary.exe". The Kinect window opens by itself.

  If it says "Its driver needs to be set up once", press the green "Set up
  Kinect driver" button. Windows asks "Do you want to allow this app to
  make changes to your device?": click "Yes". A moment later the window
  connects to the Kinect.

  When it works, you see yourself and a colourful depth picture. Stand
  where you will play until it says "Good distance" and "Whole body in
  view" (see SET UP WHERE YOU STAND below). Then close the Kinect window.

  The Kinect window opens like this every time you start Xenia with a
  Kinect plugged in, so you can check where you stand before choosing a
  game. To stop that, untick "Open this window when Xenia starts". It does
  not open when Xenia is started with a game (from a shortcut or a game
  launcher), and it is always in the "HID" menu under "Kinect...".

  The driver is the WinUSB driver that is part of Windows, so nothing is
  downloaded. You do not need the Kinect for Windows SDK, the Kinect
  Runtime or Zadig. (Instead of the button, you can also double-click
  "Setup-Kinect.cmd" in the kinect folder.)


PLAY

  Open the "File" menu, choose "Open...", and pick your game (a disc image
  such as an .iso file, or a game's default.xex). The game fills the screen;
  press Esc to get the window and its menus back, and F11 to go fullscreen
  again.

  Kinect games want a player profile. Xenia makes one called "Player" and
  signs it in for you.


SET UP WHERE YOU STAND (CALIBRATION)
------------------------------------

Do this the first time, after moving furniture or the Kinect, and whenever a
game does not see you or does not take your moves.

  1. Start xenia_canary.exe. With a Kinect plugged in, the Kinect window
     opens by itself (otherwise: "HID" menu > "Kinect..."; press Esc first
     if a game is fullscreen).
  2. Stand where you will play, facing the Kinect. In the right-hand
     (depth) picture, green is a good distance, red too close, blue too far.
  3. Read the line under the pictures and do what it says:

     "Whole body in view."            You are set. Close it and play.
     "Your feet are out of the        Tilt the Kinect down a little, or
      picture."                       stand further back.
     "Your head is out of the         Tilt the Kinect up a little, or stand
      picture."                       further back.
     "Something is in front of        Move the table or chair, or move the
      your legs."                     Kinect so it sees your feet.
     "Step back." / "Come closer."    Move until it says "Good distance".

  4. Raise both hands above your head: they should stay in the picture.
  5. Kinect for Xbox One only: in a small room raise "Step back"; if you
     cannot stand in the middle, use "Shift sideways".

  Games cannot tilt your Kinect for you, so aim it by hand. Dance games
  compare your whole body to the dancer: if your feet are hidden, they see
  you but do not take your poses.


PLAYING TIPS
------------

- Stand about 2 to 3 metres (6 to 10 feet) from the Kinect, facing it. The
  Kinect must see ALL of you, head to feet, with nothing in front of your
  legs (a coffee table, the arm of a couch). Dance games compare your whole
  body to the dancer on screen: if your feet are hidden or out of view, they
  will not take your poses. Check in HID > Kinect...: your feet should be in
  the depth picture. If they are not, tilt the Kinect down a little or move
  it, rather than stepping closer.
- A bright room helps. Avoid standing right in front of a window.
- Kinect for Xbox One only: if your room is small or your hands leave the
  picture, open HID > Kinect... and raise "Step back". If you cannot stand in
  the middle of the room, use "Shift sideways".
- Xbox Live Arcade games (for example Fruit Ninja Kinect and Kinect Party) run
  as their trial version unless you set license_mask = 1 in the file
  xenia-canary.config.toml, as in any Xenia. Only do that for games you
  bought in full.


IF SOMETHING DOES NOT WORK
--------------------------

"No Kinect found"
  - Is the adapter plugged into the wall, and is its light on?
  - Plug the USB cable straight into the PC, and try another port. For a
    Kinect for Xbox One use a USB 3.0 port.
  - In HID > Kinect..., press "Set up driver again", then "Retry".

"Kinect for Xbox 360 found, but its camera is off"
  The Kinect gets power from USB for its motor only. Plug its power supply
  into the wall.

"The Kinect is in use by another program"
  Close other Kinect programs (for example Kinect Studio or camera apps),
  then press "Retry" in HID > Kinect...

A game sees you but does not take a pose or gesture
  Usually the Kinect cannot see your whole body. Open HID > Kinect... and
  make sure the depth picture shows you from head to feet with nothing in
  front of your legs; tilt or move the Kinect until it does. This is what
  made Just Dance 4 take its "Raise hand!" pose in testing.

The game says to connect a Kinect
  Open HID > Kinect... and check that it says "connected". If it does not,
  see "No Kinect found" above, then restart the game.

The picture is choppy or keeps stopping (Kinect for Xbox One)
  Try a different USB 3.0 port, ideally one on the back of the PC. Some USB
  ports cannot keep up with this Kinect.

Voice commands do not work
  Open HID > Kinect... and check the microphone level moves when you speak.
  A Kinect for Xbox One's own microphone is used automatically. The Kinect
  for Xbox 360's microphone cannot be used: choose another microphone, such
  as a headset or webcam microphone, in the list.

Windows asks whether Xenia may use the network
  It should not: this build keeps games offline, and Kinect games do not need
  the network. If it does ask, click Cancel. Kinect games work either way.

Other Kinect software stopped working
  The driver setup replaces the Kinect's usual driver. To undo it, open the
  kinect folder and double-click "Restore-Kinect.cmd". To play with Xenia
  again, press "Set up Kinect driver" in HID > Kinect... again.

Still stuck, or found a problem? Please report it at

  github.com/dsons/xenia-kinect2/issues

(a free GitHub account is needed). Say which Kinect and game you used and
what happened, and attach the log files. HID > Kinect... > "Show log files"
opens the folder that has them:

  xenia.log               the last time Xenia ran
  xenia.previous.log      the time before that
  kinect\Setup-Kinect.log  the driver setup, if that did not work

The logs list your Kinect, its driver and USB port, and what Xenia did, so
the problem can usually be found without more questions.


TESTED GAMES
------------

How far each game has been tested. Their settings (in the config folder) and
two game fixes (in the patches folder) are applied automatically.

  Plays:
    Kinect Adventures!
    Kinect Sports: Season Two
    Fruit Ninja Kinect

  Gets partway (not tried further yet):
    Kinect Sports              starts a sport
    Dance Central 3            reaches the main menu
    Rise of Nightmares         reaches its menus
    Kinect Star Wars, The Gunstringer, Disney Fantasia: Music Evolved,
    Child of Eden, Kinect Party: reach their Kinect start screen

    Just Dance 4               reaches dancer selection; needs the Kinect to see
                               you head to feet (see PLAYING TIPS)

If you get further in one of these, or another game works, please say so at
github.com/dsons/xenia-kinect2/issues.

Other Kinect games may work too. Settings for a game go in the config
folder, in a file named after the game's title ID (Xenia shows it in its
window title), for example 4D5308ED.config.toml for Kinect Adventures. Copy
one of the files there and rename it.


ADVANCED SETTINGS
-----------------

The settings of HID > Kinect..., and a few more, are in the [NUI] section
of xenia-canary.config.toml:

  nui_camera         auto (default): a Kinect for Xbox 360 if one is plugged
                     in, otherwise a Kinect for Xbox One. kinect1, kinect2:
                     only that kind. synthetic: a moving test figure.
                     none: no Kinect.
  nui_kinect2_zoom   1.3 (default). Step back, 1.0 to 1.6 (Kinect for
                     Xbox One).
  nui_kinect2_pan    0 (default). Shift sideways, -40 to 40 (Kinect for
                     Xbox One).
  nui_camera_mirror  false (default). Mirror the picture games see.
  nui_mic            true (default): emulate the Kinect microphones.
  nui_mic_device     Part of the name of the microphone to use.
  nui_window_on_start  true: open the Kinect window when Xenia starts with
                     a Kinect plugged in (not when started with a game).

In the [Network] section:

  network_local_only true (default): games stay offline, on this PC only.
                     Set to false for system link or online play.


LEGAL: YOUR OWN GAMES ONLY
--------------------------

This is for preserving and playing Xbox 360 Kinect games you own, from
copies you made yourself of your own discs or downloads. No games are
included, and no game files, keys or download links may be shared in this
project's issues. Only set license_mask = 1 for Xbox Live Arcade games you
bought in full.


ABOUT
-----

Xenia is released under the BSD license (LICENSE.txt). The kinect folder
contains libfreenect and libfreenect2 (Apache License 2.0, with the changes
listed in kinect\licenses\SOURCES.txt), libusb (GNU LGPL 2.1, source
included), libjpeg-turbo (BSD-style licenses) and the Microsoft Visual C++
runtime. See kinect\licenses: SOURCES.txt there says where each file comes
from. The Kinect driver is not included: it is the WinUSB driver that comes
with Windows.

This project is not affiliated with or endorsed by Microsoft. Xbox, Xbox
360, Xbox One and Kinect are trademarks of Microsoft Corporation.
