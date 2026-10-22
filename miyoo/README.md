# TyrQuake-Miyoo

The [TyrQuake libretro core](https://github.com/libretro/tyrquake) (Quake), tuned for the
**Miyoo Mini Plus** with [OnionOS](https://github.com/OnionUI/Onion) at the screen's native
640x480. It runs in Onion's RetroArch as its own core, **Quake (TyrQuake-Miyoo)**, next to the stock
TyrQuake core, and is started from Onion's **Ports** list.

What it changes, compared with the stock core:

- **Faster at 640x480.** The software renderer, the surface caches, the models and the final
  colour conversion run on both CPU cores, the music is decoded on the second core, and the core
  is built with profile-guided and link-time optimisation. In the timedemos at 640x480 an
  average frame takes 12.7-13.3 ms instead of 17.2-20.9 ms with the stock core (demo1-3). The
  screen refreshes at 59.67 Hz, so a frame has 16.76 ms. The picture is pixel-for-pixel the same
  as the original one-thread renderer (checked with timedemo screenshots).
- **Cleaner sound.** No crackle when the game slows down, when many sounds play at once, when a
  sound is cut off (fast weapons, the menu) or when a level loads. See
  [Sound](#sound).
- **Onion's Game Switcher works**, with the picture of where you left off. Menu saves the game and
  resumes it, also after turning the console off. See [Game Switcher](#game-switcher).
- **Savegames load again.** The upstream core (May-October 2026) wrote savegames it could not load
  back ("Couldn't spawn server maps//.bsp"). This core writes them correctly and loads those
  broken ones too.
- **Smooth animation and smooth movement** are on by default.
- Software-renderer fixes taken from standalone TyrQuake 0.71: large maps no longer drop
  surfaces or edges, liquids with non-64x64 textures, and oversized faces.

## Without RetroArch (test version, branch `standalone`)

The same core can also run without RetroArch, through its own small program (`tqrun`, in
`miyoo/standalone/`), as an ordinary Onion port: **Quake (TyrQuake-Miyoo)** in the Ports list's
FPS folder.

- **About 20 % faster** than in RetroArch: an average timedemo frame takes 9.6-10.7 ms instead of
  12.1-13.2 ms. The picture goes to the screen through the console's 2D engine, so the CPU does
  no copying.
- **Every frame shown in time with the screen** (59.67 Hz): frames come every 16-17 ms, where
  RetroArch shows them in bursts of 12, 18 and 20 ms. The sound reaches the speaker exactly as
  the game made it, with no resampling.
- **Menu works as in Onion's RetroArch games:** a short press opens Onion's Game Switcher, and
  picking Quake there brings you back where you were. Your place is also kept when you turn the
  console off or it goes to sleep.
- **Holding Menu** opens the port's menu: RESUME, RESOLUTION (640x480 or 320x240; the game
  restarts and comes back where you were), FPS (30, 60 or UNLIMITED) and EXIT GAME.
- Quake's own savegames work as usual; the first start copies the RetroArch version's
  savegames and settings.

Install: unzip `TyrQuake-Miyoo-Standalone-<version>.zip` onto the SD card (or let Port Manager
install it). Quake's data goes in `Roms/PORTS/Games/TyrQuake-Miyoo/id1/`; if Quake is already
set up for the RetroArch version (`Quake Collection (TyrQuake)/id1`), that data is used.
Building: `miyoo/standalone/build_tqrun.sh`, then `miyoo/standalone/package.sh`.

## Installing

What you need:

- A Miyoo Mini Plus with OnionOS and its **Ports** list. The game goes in Onion's Quake folder,
  `Roms/PORTS/Games/Quake Collection (TyrQuake)/`.
- Quake's data: `id1/pak0.pak` (the shareware episode) and, for the full game, `id1/pak1.pak`.
  It is not included.
- The release zip, `TyrQuake-Miyoo-<version>.zip`.

Steps:

1. Put the data in `Roms/PORTS/Games/Quake Collection (TyrQuake)/id1/` on the SD card.
2. Unzip the release onto the root of the SD card, merging its `RetroArch` and `Roms` folders with
   the ones already there. It adds:
   - the core, `RetroArch/.retroarch/cores/tyrquake_miyoo_libretro.so`;
   - its description, `tyrquake_miyoo_libretro.info`, in `cores/` and `core_info/`;
   - the Ports shortcuts in `Roms/PORTS/Shortcuts/FPS - Quake Collection (TyrQuake)/`:
     `1996 Quake.port`, and `1997 Quake hipnotic`, `1997 Quake rogue` and `2021 Quake dopa` for
     the mission packs and Dimension of the Past (see [Mission packs](#mission-packs)).
     Existing shortcuts with these names are replaced.
   - RetroArch settings for this core,
     `Saves/CurrentProfile/config/TyrQuake-Miyoo/TyrQuake-Miyoo.cfg`: *Audio Sync*
     on, the *sinc* resampler and the screen's 59.67 Hz refresh (see
     [RetroArch settings](#retroarch-settings)).
3. On the Miyoo: *Games → Ports → 1996 Quake*. Refresh the list from its menu if it doesn't show.

The first start takes a few seconds longer: RetroArch rebuilds its list of cores (see below).

Music (optional): the soundtrack as `id1/music/track02.ogg` ... `track11.ogg`. The core looks for it
inside the game's folder.

Saves and settings are kept apart from the stock TyrQuake core's, under the name `TyrQuake-Miyoo`:
Quake's savegames and `config.cfg` in `Saves/CurrentProfile/saves/TyrQuake-Miyoo/id1/` (and
`hipnotic/`, `rogue/`, ...), the core options and RetroArch settings in
`Saves/CurrentProfile/config/TyrQuake-Miyoo/`, the Game Switcher states in
`Saves/CurrentProfile/states/TyrQuake-Miyoo/`.

**Coming from the stock core** (or from a version of this core before 2026-10-20, which used the
stock core's `TyrQuake` folders): the first time a shortcut starts the game it copies
`saves/TyrQuake/` (savegames, `config.cfg`) and the states from `states/TyrQuake/` into the new
folders. The old files stay. The core options are not copied, so this core starts with its own
defaults. If you start the core some other way first, copy those folders yourself.

## Game Switcher

Onion's Menu button works as with the emulators:

- **Menu (short press)**: the Game Switcher opens over the paused game and saves Quake's state and
  picture. Picking Quake goes back to the game; picking another game closes Quake.
- **Menu (held)**, or **turning the console off**: Quake's state is saved and you go back to Onion.
- **Resuming** from the Game Switcher, or after turning the console on: the state is loaded.

A state is a Quake savegame, kept by RetroArch as
`Saves/CurrentProfile/states/TyrQuake-Miyoo/1996 Quake.state.auto`. Resuming from one reloads the level
(1-2 s, with Quake's "Loading" plaque) and puts everything back as it was: you, the monsters, doors
and items. Short-lived effects (flying debris, sounds already playing) are not kept.

A state can be saved only where Quake lets you save: in a single-player game, not on the
end-of-level screen, not while dead, and not in the menus or the demos. Otherwise the previous
state stays.

### Why the shortcut is the content

Onion's Game Switcher resumes a game only when RetroArch's content name equals the shortcut's
name. A plain shortcut runs `id1/pak0.pak`, so RetroArch reports `pak0` and Onion restarts Quake
instead of resuming it. This shortcut hands RetroArch **the shortcut itself** as the content:

```sh
QuakeDir="Quake Collection (TyrQuake)/id1"
RomDir="../Shortcuts/${PWD##*/Shortcuts/}"
RomFile="$(basename "$0")"
```

`RomDir`/`RomFile` point Onion's launcher back at the shortcut, wherever it sits under
`Shortcuts/`. When the core gets a `.port` file, it reads `QuakeDir` (relative to
`Roms/PORTS/Games`, or an absolute path) and loads that game folder. RetroArch then reports
`1996 Quake`, and each shortcut has its own state file. Without this, every Quake game folder would
share one `pak0` state.

The shortcuts also carry `GameDir` and `GameDataFile` for Onion's *Import games (Ports)*, which checks
from them that the game's data is there (without them it would hide the shortcut).

### Mission packs

The release has shortcuts for the two mission packs and for Dimension of the Past:

| shortcut | game folder (next to `id1/`) | game |
|---|---|---|
| `1997 Quake hipnotic` | `hipnotic/` | Scourge of Armagon |
| `1997 Quake rogue` | `rogue/` | Dissolution of Eternity |
| `2021 Quake dopa` | `dopa/` | Dimension of the Past |

They come as `.notfound` files, hidden in the Ports list. Copy the pack's folder (with its
`pak0.pak`) into `Roms/PORTS/Games/Quake Collection (TyrQuake)/` and run Onion's *Import games (Ports)*
(in the Ports list's options): it turns the shortcuts whose data it finds into `.port`. `id1/` must
stay there, the packs use it. For another mod, copy a shortcut under a new name and change
`QuakeDir`, `GameDir` and `GameName`; a folder other than `hipnotic`, `rogue` or `quoth` is loaded
with `-game <folder>`. A state from another game folder is never loaded.

### RetroArch's core list cache

RetroArch keeps the cores' `.info` files in `RetroArch/.retroarch/cores/core_info.cache` and does
not notice when one changes. With an old copy it thinks this core has no save states and the
Game Switcher saves nothing. The shortcut deletes that cache once whenever this core's `.info`
changes, for example after an update, and RetroArch rebuilds it. If you start the core some other
way after updating, delete `core_info.cache` yourself.

## Settings

In RetroArch's quick menu, *Core Options*:

- **Internal resolution** (default `640x480`, the screen's native size): `320x240` is about
  twice as fast.
- **Framerate** (default `59.67fps (Miyoo Mini screen)`, the screen's refresh): `Auto` takes
  RetroArch's refresh rate (60 on Onion), which works too.
- **Slow Frames** (default `Real time`): what happens when a frame takes longer than the
  framerate allows (a heavy fight at 640x480). `Real time`: the game and the sound move on by
  the time that really passed, so the game keeps its speed and RetroArch always gets enough
  sound. `Fixed`: every frame is one framerate step, as in the original; a slow frame slows the
  game down and hands over too little sound, which crackles. Timedemos always use fixed steps.
- **Sound Filter** (default `Original (11 kHz)`): see [Sound](#sound).
- **Performance Log** (default off): see [Sound](#sound).

Resolution and framerate take effect after restarting the game.

### RetroArch settings

The release sets three RetroArch settings for this core only, in
`Saves/CurrentProfile/config/TyrQuake-Miyoo/TyrQuake-Miyoo.cfg`:

```
audio_sync = "true"
audio_resampler = "sinc"
video_refresh_rate = "59.670000"
```

It is a *core override*, read only by this core (the stock core's are in `config/TyrQuake/`).
It replaces an existing file of that name. To change these settings, edit the file, or in
RetroArch's quick menu use *Overrides → Save Core Overrides*.

- **Audio Sync** paces the game by the sound; without it RetroArch drops sound that doesn't fit
  its buffer, which crackles.
- **sinc**: RetroArch adjusts the sound's rate by up to 0.5 % to keep its buffer filled. Onion's
  default *nearest* does that by repeating a sample (one in 200 at 0.5 %), a slight roughness on
  high sounds; *sinc* interpolates. It costs about 0.15 ms a frame (measured in a timedemo).
- **59.67 Hz** is the screen's real refresh rate (Onion's RetroArch assumes 60), which matches the
  core's 59.67 fps.

## Sound

Quake's effects are 11 kHz samples. On DOS, Quake mixed at 11 kHz and the sound card filtered
everything above about 5 kHz, which hid the rough edges. At the Miyoo's 48 kHz they reach the ear
as crackle. The core deals with that and with a few more causes of clicks:

- **Sound Filter: Original (11 kHz)** filters the effects like that sound card (a low-pass at
  5 kHz). The music, which came from the CD, is not filtered. `Off` gives the full range. It
  costs about 0.04 ms a frame.
- **No hard clipping.** Several loud effects, or effects and music, add up to more than the
  loudest sample there is. Instead of cutting the tops off, the core turns the whole sound down
  smoothly for a moment (a limiter that looks 5 ms ahead) and back up within about 0.15 s. If
  you hear it duck in big fights, lower Quake's *Sound Volume* and raise the console's volume.
- **No clicks from cut-off sounds.** Quake stops a sound mid-wave when the same source plays a
  new one (each nailgun shot, each move in the menu) or when it runs out of channels. The core
  fades such a sound out over 2 ms, and sounds whose volume changes as they move change it
  smoothly. The effects are also resampled to 48 kHz at the right pitch (they used to play 1.4 %
  low with their ends cut off).
- **Loading and saving** stop the game for 0.1-0.6 s. The core fades the sound out before and
  back in after, instead of letting it break off.
- **Music** (`.ogg`) is decoded on the second CPU core, a few seconds ahead.

Keep RetroArch's *Audio Sync* on (Onion's default). Without it RetroArch drops sound that
doesn't fit its buffer, which crackles.

**Performance Log** writes one line per second to `perflog.csv` in the game's save folder
(`Saves/CurrentProfile/saves/TyrQuake-Miyoo/id1/` for Quake): frame times, frames over the budget,
how much sound was handed over, the music buffer, how hard the limiter worked, CPU time, CPU
clock and temperature. `miyoo/tools/perflog.py` on a PC fetches it over the network and
summarises it with a chart. Leave it off for normal play.

## Building

From Git Bash on Windows, with the Arm GNU toolchain 13.3 (`arm-none-linux-gnueabihf`, path in
`TC`, see the script):

```sh
miyoo/build/build_core.sh                 # -> <workspace>/build/core/tyrquake_libretro.so
miyoo/tools/package_release.sh v1.0       # -> <workspace>/build/release/TyrQuake-Miyoo-v1.0.zip
```

The build fails if the core needs a glibc newer than OnionOS's 2.28. After changing a header, build
with `FORCE=1`. The release script needs Python 3.

`miyoo/tools/bench.sh` runs a timedemo on the console over SSH, kept apart from the player's setup
in `/tmp/tqbench`. `miyoo/NOTES.md` has the measurements, the renderer changes and the device
details.

## License

GPL v2, as TyrQuake and Quake. Quake's data is not included.
