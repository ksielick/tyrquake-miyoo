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

## Installing

Both zips are on the [Releases page](https://github.com/ksielick/tyrquake-miyoo/releases). You
need OnionOS with its **Ports** list (Apps → Package Manager → Verified → Ports Collection) and
Quake's data, which isn't included: `id1/pak0.pak` (the shareware episode) and, for the full game,
`id1/pak1.pak`. Music is optional: the soundtrack as `id1/music/track02.ogg` … `track11.ogg`.

### Without RetroArch, with Port Manager (easiest)

1. Install [Port Manager](https://github.com/ksielick/portmanager-miyoo) on the Miyoo.
2. In Port Manager pick **Quake (TyrQuake-Miyoo)** and install it.
3. Copy `pak0.pak` (and `pak1.pak`) into `Roms/PORTS/Games/TyrQuake-Miyoo/id1/` on the SD card.
4. Play: *Games → Ports → FPS → Quake (TyrQuake-Miyoo)*.

Port Manager also tells you when there's a new version and installs it, keeping your saves and
settings.

### Without RetroArch, by hand

1. Download `TyrQuake-Miyoo-Standalone-<version>.zip` and unzip it onto the root of the SD card,
   merging its `Roms` folder with the one already there.
2. Copy `pak0.pak` (and `pak1.pak`) into `Roms/PORTS/Games/TyrQuake-Miyoo/id1/`.
3. Play: *Games → Ports → FPS → Quake (TyrQuake-Miyoo)*.

Already playing Quake in Onion's RetroArch? Skip step 2: if the data is in
`Roms/PORTS/Games/Quake Collection (TyrQuake)/id1/`, it's used from there, and the first start
brings over your savegames and settings.

### The core for RetroArch

1. Copy `pak0.pak` (and `pak1.pak`) into
   `Roms/PORTS/Games/Quake Collection (TyrQuake)/id1/` on the SD card.
2. Download `TyrQuake-Miyoo-<version>.zip` and unzip it onto the root of the SD card, merging its
   `RetroArch`, `Roms` and `Saves` folders with the ones already there.
3. Play: *Games → Ports → 1996 Quake*. If it doesn't show, refresh the list from its menu.

Your savegames from Onion's stock TyrQuake core are brought over on the first start. The two
versions can live side by side: each keeps its own saves and settings.

More about the Game Switcher, settings, mission packs and building:
**[miyoo/README.md](miyoo/README.md)**.
Measurements and technical notes: [miyoo/NOTES.md](miyoo/NOTES.md).
