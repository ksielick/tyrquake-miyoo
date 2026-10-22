# TyrQuake-Miyoo

Quake on the **Miyoo Mini Plus** with [OnionOS](https://github.com/OnionUI/Onion), at the
screen's full **640x480 and a smooth 60 fps**, on the console's stock clock: **no overclocking**.

It's the [TyrQuake](https://github.com/libretro/tyrquake) core, tuned to use both of the
console's CPU cores. The picture stays exactly as Quake drew it.

- **Smooth 640x480 at 60 fps.** No need to drop to 320x240 to keep the game fluid.
- **Easy on the battery.** Without overclocking the console stays cool. In the version without
  RetroArch, a full charge gives about **6 hours of Quake at 640x480 and 60 fps** (Wi-Fi off;
  an estimate from our measurements).
- **Onion's Game Switcher works.** Press Menu, pick another game, come back to where you were,
  also after turning the console off.
- **Your savegames load.** Quake's own saves work as they should.
- **Clean sound.** No crackle when the action gets busy.

## Two versions

- **In RetroArch** (`TyrQuake-Miyoo-<version>.zip`): its own core next to the stock TyrQuake
  core, with all of RetroArch's options.
- **Without RetroArch** (`TyrQuake-Miyoo-Standalone-<version>.zip`, test version): about 20 %
  faster. Every frame lands on the screen right on time, so movement is perfectly even. It's
  also the coolest and most frugal way to play: ~15 % of the battery an hour at 640x480 and
  60 fps. Settings (resolution, 30/60 fps, FPS counter) are in Quake's own menu, under
  Options → Video Options.

You bring the game data: `id1/pak0.pak` (the shareware episode) and, for the full game,
`id1/pak1.pak`.

Installing, the Game Switcher, settings and building: **[miyoo/README.md](miyoo/README.md)**.
Measurements and technical notes: [miyoo/NOTES.md](miyoo/NOTES.md).
