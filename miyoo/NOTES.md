# TyrQuake (libretro) on the Miyoo Mini Plus — notes

Goal: Quake through the TyrQuake libretro core in Onion's RetroArch runs poorly at the native
640x480; make it run well there (stock 1200 MHz first; second core and NEON before overclocking).

## IMPORTANT: savegames did not load (fixed 2026-10-19)
Upstream libretro bug (commit 32da12a, 2026-05-04, "hostile-content hardening"): Host_SavegameComment
stopped turning spaces into '_', so a save of a level with a spaced name or with "kills:  0/  0"
gets spaces in its first line. Host_Loadgame_f reads that line as one "%s" word, every later
field shifts, and the map name comes out as garbage ("Couldn't spawn server maps//.bsp", "maps/of.bsp").
Every save made with the current libretro core is affected; Onion's 2020 core (a44247c) is not.
Fix: the writer replaces ' ' again (c <= 0x20), and both readers (Host_Loadgame_f, the load menu's
M_ScanSaves) read the whole comment line ("%[^
]"), so the owner's two broken saves (s0, s1) load too.
Verified on the device: both load ("Loading game from s0.sav/s1.sav", no error), screenshot of s1 shows
the level. Worth reporting upstream.

## Layout
- `G:\Emulacja\tyrquake-miyoo\repos\tyrquake` — clone of github.com/libretro/tyrquake, branch
  `retroarch` (the libretro core; called `miyoo` until 2026-10-20; base: upstream master 67db338,
  2026-10-12). Branch `standalone` (from 811be98, 2026-10-20): reserved for a standalone port.
- `miyoo/build/build_core.sh` — builds `build/core/tyrquake_libretro.so` (+ `.unstripped.so`) with the
  Arm GNU toolchain 13.3 from `G:\Emulacja\redriver2-miyoo\deps`, -O2 cortex-a7/neon-vfpv4, ~8 s.
  Only .c timestamps are checked: `FORCE=1` after header changes. The script fails if any symbol needs
  glibc > 2.28 (the Miyoo's); `glibc_compat.h` (.symver for exp/log/pow) and `glibc_compat.c`
  (stat → __xstat, `-Wl,--wrap=stat`) keep it there.
- `miyoo/tools/bench.sh <core.so|stock> [WxH] [demo] [max_frames]` — PC side: copies the scripts and
  core over SMB to `Roms/PORTS/Games/TyrQuake-bench/`, runs `miyoo/bench/bench_device.sh` over SSH.
  Also `STATE=save|load`, `PORT=1`, `SHOTS=n` modes (save states, .port as content, fb dumps).
- `miyoo/tools/shots.sh` + `shots_compare.py` — screenshot regression check (see that section).
- `miyoo/tools/package_release.sh` — release zip (core, .info, shortcut) in `<workspace>/build/release/`.
- `miyoo/card/` — files as installed on the SD card (`.info`, `1996 Quake.port` shortcut).
- `miyoo/README.md` — user-facing README (install, Game Switcher, settings).
- GitHub: private https://github.com/ksielick/tyrquake-miyoo, remote `miyoo`. On GitHub the branch
  is still `miyoo` (local `retroarch` pushed there up to 97c6618 on 2026-10-20:
  `git push miyoo refs/heads/retroarch:refs/heads/miyoo`) until the owner has it renamed.
- Releases: v1.0-beta1 (pre-release, 2026-10-20, owner asked): tag on 97c6618, asset
  TyrQuake-Miyoo-v1.0-beta1.zip from package_release.sh with core 7a446a25 (local PGO+LTO build,
  not yet the planned Actions build). `gh release create <tag> --prerelease --verify-tag`; tags
  need `-c user.name=ksielick -c user.email=...` (no global git identity).
- Tooling: `py -3` for the Python tools (not `-I`: numpy/PIL are in the user site-packages);
  `mssh` returns its output filter's exit code (1 when the remote prints nothing) → `|| true` in
  scripts; Git Bash needs `MSYS2_ARG_CONV_EXCL` to pass device paths like `/tmp/...` unchanged.
- `tyrquake-0.71/` in the workspace: standalone TyrQuake 0.71 (Kevin Shanahan, 2022), reference only.

## Device facts
- Onion RetroArch 1.22.2 (`retroarch_miyoo354` bind-mounted as `retroarch`), video `sdl_dingux` via
  MI_GFX, vsync off, audio_sync on (paces the game), `load_dummy_on_core_shutdown` on.
- Stock core: `RetroArch/.retroarch/cores/tyrquake_libretro.so`, "v0.62", md5 f2d8ce31...
- Launching a game stops SSH/SMB (Onion's runtime.sh kills dropbear, smbd...) unless
  `.tmp_update/config/.keepServicesAlive` exists; the owner's is renamed `.keepServicesAlive_`
  (off). Results over SMB can be read only after the game exits.
- One device, several Claude sessions (portmanager-miyoo, ...): check that nobody else uses it
  before a run; an overlapping run gives a half-drawn screen and wrong timings.
- Quake: Ports shortcut → `Emu/PORTS/launch_retroarch.sh`, data `Roms/PORTS/Games/Quake Collection
  (TyrQuake)/{id1,music}`; core options `Saves/CurrentProfile/config/TyrQuake/TyrQuake.opt`; Quake
  config `Saves/CurrentProfile/saves/TyrQuake/id1/config.cfg` (viewsize 110).

## How the timedemo benchmark works
- In the libretro core `realtime` advances a fixed 1/framerate per retro_run, so Quake's own
  "N frames X seconds" is always 60 fps — useless. Branch `miyoo` adds a wall-clock report from the
  frontend perf interface: `timedemo wall: N frames, s, fps, frame ms avg/median/p90/p99/max`
  (cl_demo.c), cvar `timedemo_quit 1` (core asks RetroArch to shut down after the demo), and
  Sys_Printf → libretro log (console text readable in RetroArch's `-v` log).
- `bench_device.sh` builds an isolated setup in /tmp/tqbench: copy of retroarch.cfg + an override
  (own save dirs, own core options file with the tested resolution, log level 0, audio_sync and vsync
  off, no auto states, `load_dummy_on_core_shutdown = false`), a game dir `tqbench` with
  `autoexec.cfg` (`timedemo_quit 1; timedemo demo1`) and an empty `bench.pak` as the content
  (→ `-game tqbench`), symlinks to the player's id1/ and music/, the player's config.cfg.
- **Launch like Onion does**: write `/tmp/cmd_to_run.sh` and `killall MainUI`; runtime.sh moves it to
  `.tmp_update/cmd_to_run.sh`, runs it, then restarts MainUI. Started next to MainUI (running or
  SIGSTOPped) Onion's RetroArch hangs forever in `ioctl(tty3, VT_WAITACTIVE)` (seen via
  `/proc/<pid>/syscall`). Keep `/mnt/SDCARD/Roms` out of the command text: runtime.sh then treats it
  as an app and doesn't kill dropbear/smbd.
- The stock core can't quit: measure it with `max_frames` 300 and 900; (t900 - t300) / 600 = ms per
  frame (frames 300..900 are inside demo1's 969).

## Results (2026-10-19, demo1, stock 1200 MHz performance governor, audio on, MainUI exited)
| core | res | method | ms/frame | fps |
|---|---|---|---|---|
| ours (master 67db338) | 320x240 | timedemo wall | avg 9.25, median 8.79, p99 14.4, max 15.8 | 108 |
| ours | 640x480 | timedemo wall | avg 20.1, median 19.2, p90 25.2, p99 30.5, max 37.2 | 49.8 |
| ours | 320x240 | max_frames 300→900 | 9.02 | 111 |
| ours | 640x480 | max_frames 300→900 | 19.5 | 51 |
| stock Onion v0.62 | 320x240 | max_frames 300→900 | 8.87 | 113 |
| stock Onion v0.62 | 640x480 | max_frames 300→900 | 19.1 | 52 |

- Current master is as fast as Onion's core (differences within noise).
- 640x480 costs ~2.2x of 320x240; at 640x480 most frames miss the 16.7 ms budget of 60 Hz.
- At 640x480 the log shows ~30 "Bad surface extents (416/480)" errors: surfaces too large for the
  surface cache block limit at high resolution → probably missing/skipped polygons. To investigate.
- The first stock 320x240 timedemo check: hasn't been verified visually that the stock core really
  ran the timedemo (no log text from it); the per-frame numbers match ours, so it likely did.

## Current performance (2026-10-19 evening, core 053a3d6c = d5cbb2b)
640x480, 2 threads (surface caches on both), smooth animation/movement on, battery 43→40 % (not
charging), 1200 MHz performance governor, audio on, audio_sync/vsync off. Budget: 16.76 ms
(59.67 Hz panel).

| demo | avg | median | p90 | p99 | max | fps (wall) |
|---|---|---|---|---|---|---|
| demo1 | 15.86 | 15.49 | 19.03 | 21.76 | 68.3 (1st frames) | 63.1 |
| demo1 (PERF=1 run) | 15.78 | 15.40 | 18.99 | 22.81 | 36.3 | 63.4 |
| demo2 | 14.53 | 13.98 | 17.66 | 21.93 | 27.7 | 68.8 |
| demo3 | 16.27 | 16.17 | 19.55 | 22.53 | 29.6 | 61.5 |

- Same as the earlier 2-thread runs (demo1 15.61-15.68, demo2 14.60-14.64, demo3 16.26-16.38): the
  save-state/.port/start-up changes cost nothing per frame. Vs the stock core (one thread):
  demo1 20.0 → 15.8, demo2 18.0 → 14.5, demo3 20.6 → 16.3 ms.
- The averages are at or under the budget, but p90 is 17.7-19.6 ms: at least 10 % of the frames
  miss it, in the heavy scenes (the audio crackle the owner hears there).
- r_perf demo1, per second: frame 13.5-18.3 ms = render 11.4-16.4 + audio 1.9-2.4 (mixing + OGG
  music). Render = 3D view 5.6-11.8 + "2d" 3.6-4.3 (HUD + VID_Update). SW sections: edge drawing
  4.4-8.4 (D_DrawSurfaces 3.6-6.5, of which the parallel cache phase 0.04-1.3), entities (alias
  models, one thread) 1.0-3.6, VID_Update (palette pass + video_cb) 2.7-3.4.
- Same runs on the charger (owner's curiosity; axp "charging":3, battery 40→43 %): demo1 avg 16.01
  (median 15.65, p90 19.34, p99 22.64), demo2 14.84 (14.32, 18.16, 22.13), demo3 16.44 (16.39,
  19.70, 23.59): 1-2 % slower than on battery, about the noise between runs. Charging doesn't
  speed it up (the clock is fixed at 1200 MHz either way).
- Right after a reboot (owner asked; on battery, uptime <1 min): demo1 avg 15.93 (median 15.56,
  p90 19.21, p99 22.24), demo2 14.53 (14.00, 17.69, 21.80), demo3 16.28 (16.29, 19.54, 21.95): the
  same as after hours of use, so sleep/uptime don't skew the bench. /proc/loadavg was 1.4 before
  the first run and climbed with each run (2.8, 3.6): the ~5 seen earlier was our own back-to-back
  runs (two busy threads each), not Onion.
- Stock Onion core vs ours, same method for both (it can't quit or time a timedemo): RetroArch
  --max-frames 300 and 900, ms/frame = run-time difference / 600 (start-up cancels out).
  On the charger, after the reboot, alternated (raw lines in workspace
  `build/bench_stock_vs_ours.txt`):

  | demo | stock v0.62 | ours 053a3d6c | faster by |
  |---|---|---|---|
  | demo1 | 19.85 ms (50.4 fps) | 15.52 ms (64.4 fps) | 22 % (1.28x) |
  | demo2 | 17.23 ms (58.0 fps) | 14.43 ms (69.3 fps) | 16 % (1.19x) |
  | demo3 | 20.88 ms (47.9 fps) | 16.05 ms (62.3 fps) | 23 % (1.30x) |

  Frames 300-900 only, so these differ a little from the whole-demo timedemo-wall averages.
- So the serial work is now about half the frame: VID_Update ~3.2 + entities ~2.8 + audio ~2.1
  + edge lists/setup. These are the next targets (see Next).

## Trivia: vs the PCs of 1996-98 (owner's curiosity, 2026-10-19)
Not the same test: ours is wall time per frame with lerp, OGG music and RetroArch's audio, on two
cores; theirs are single-core DOS/WinQuake timedemos.
- thandor.net (https://thandor.net/benchmark/33), Quake 1.08 DOS, `timedemo demo3`, 320x200,
  default settings: Pentium 75 20.0 fps, P100 26.7, P133 32.7, P166 37.3, P200 41.4 (the fastest
  when Quake came out, June 1996), Pentium Pro 200 49.8, Pentium II 266 64.1, Celeron 300A 67.8.
- AnandTech Pentium II review (https://www.anandtech.com/show/181/5, WinQuake 640x480, demo not
  given; seen only through a search summary, the page now redirects): PII 233 23.2 fps, PII 266
  25.4, PII 300 31.7. Vogons: a Pentium MMX 266 gets ~10 fps at 640x480.
- Miyoo Mini Plus, TyrQuake-Miyoo, demo3 at 640x480: 61 fps (16.3 ms). That is about twice a
  Pentium II 300 at the same resolution, and about what a Celeron 300A did at 320x200 with 4.8x
  fewer pixels. Not measured yet: demo3 at 320x240 on the current core, for a like-for-like line.

## Origins and the standalone 0.71 (2026-10-19)
- Onion's stock core = plain libretro buildbot armhf build of commit a44247c (2020-11-13), GCC 5.4
  (Ubuntu 16.04 Linaro) — no Onion/Miyoo patches. Master is 438 commits later, same speed on demo1.
- No 0.63–0.71 software-renderer changes were backported to the libretro fork (only 2020 "Backport
  disenchant.net" commits in common/zone/cvar/r_light and d_scan.c). Missing in libretro: auto-grow
  of r_maxsurfs/r_maxedges (libretro drops surfaces at the fixed 800/2400), brush clip buffer
  auto-grow, non-64x64 turbulent textures (hard-coded 64), fence textures, entity alpha, lerp bbox
  (`R_AliasGetBBox`: with r_lerpmodels 1 libretro skips trivial accept — r_lerpmodels is 0 here),
  sprite `pspan->v < 0` guard, missing-texture extents clamp (0.66 crash fix).
- "Bad surface extents (416/480)" is from libretro commit 60d1578 (model.c CalcSurfaceExtents
  dropped the TEX_SPECIAL exemption): sky/water faces log it at map load, harmless (Sys_Error returns);
  not resolution dependent (the 320x240 output was just cut by `tail`).
- `r_waterwarp_scale` 0.5 (libretro): above 640x400 the underwater warp runs at half resolution.

## Fixes ported from 0.71 (2026-10-19, built, not yet tested on the device)
- model.c CalcSurfaceExtents: no error for sky/liquid faces; lightmapped faces clamped to 256 after
  the error (Sys_Error returns in libretro); missing-texture faces clamped to 240.
- r_main.c/r_draw.c: surface/edge overflow flags; R_EdgeDrawing grows the (malloc'd) buffers x2 and
  rebuilds the frame's edge list before scanning; grown sizes are kept for later maps.
- r_bsp.c: skip bmodel faces with more edges than the clip buffer.
- d_scan.c/d_edge.c: liquids that aren't 64x64 use a modulo slow path (opaque, z-test, stipple).
- d_sprite.c: skip spans with v < 0.
- r_alias.c: with r_lerpmodels the bbox encloses both frames and trivial accept is used again.
- Tested 2026-10-19 (core 1a4aa794, on the charger): 320x240 avg 9.23 ms, 640x480 avg 19.92 ms
  (p99 30.0) — same speed as before, no "Bad surface extents" lines any more.
- r_perf at 640x480 (`PERF=1`, per-second windows): frame 16.4–24.7 ms = render 15.4–23.5
  (3D view "rv" 10.2–18.5, "2d" 3.9–5.7, particles <0.4) + audio 0.8–1.4. The FRAME section equals
  the wall time per frame, so RetroArch's own share outside retro_run is ~0; its video work runs
  inside "2d" (SCR_UpdateScreen → VID_Update: palette pass + video_cb). The 3D view dominates;
  2d+output (~4 ms) is the most a standalone port could partly save.
- Not yet: entity alpha (FitzQuake U_ALPHA, translucent spans); fence textures postponed by the owner.

## r_perf stage breakdown (2026-10-19, 640x480, demo1, one thread)
Heaviest second: rv 18.4 ms = edge drawing 14.9 (R_ScanEdges 14.3 = D_DrawSurfaces 12.9, of which
lit surface cache building 4.4; span generation ~1.4; edge lists ~0.5) + entities 2.9; 2d 4.2 =
VID_Update (palette pass + video_cb) 3.5 + HUD; audio ~1. Light second: D_DrawSurfaces 7.3
(cache 0.3), entities 1.8, VID_Update 3.3. Music: the player's layout has `music/` next to `id1/`,
the core looks for `music/trackNN.*` inside the game dirs (id1/music) — not found (logged now). Fixed on the card 2026-10-19 (owner asked): `music/` moved to `id1/music/` (10 tracks).

## Two-thread span drawing (d_mt.c, cvar r_threads, default 1)
- D_DrawSurfaces does its per-surface setup (gradients, D_CacheSurface, bmodel transforms) on the
  calling thread as before but records each span call with a copy of the drawing state; then the
  calling thread and a worker (pinned to core 1) replay the record for the upper/lower band of the
  flush's rows (span functions skip spans outside `[d_band_v0, d_band_v1)`). The split follows the
  two threads' drawing times per flush slot. Worker spins 2000 rounds, then sleeps on a condvar
  (no CPU between frames); the caller spins then sched_yield()s. MAXSPANS 3000 → 12000 (fewer flushes).
- The span drawing state is `__thread` (initial-exec, 140 bytes static TLS, flag STATIC_TLS — loads
  fine in Onion's RetroArch). TLS reads in pixel loops cost 2.3 ms/frame until the span functions
  copied the state into same-named locals (D_LOCAL/D_MUTABLE in d_scan.c), which also beat the
  original (byte stores made the compiler reload `cachewidth` per pixel).
- pthread_create/join bound to GLIBC_2.4 (glibc_compat.h).
- Risk not handled yet: surface caches are built in the record pass and used later in the flush; if
  one flush needed more than the whole surface cache (10 MB), an early cache could be evicted.
- A/B 640x480 demo1 (alternated, same session): r_threads 0: avg 19.73 / 19.50 ms (p99 27.8/27.1);
  r_threads 1: avg 16.16 / 15.81 ms, median 15.2-15.5, p90 20.3-20.9, p99 23.3-24.0. Screenshot
  mid-demo: no seam between the bands.
- Target: the panel refreshes at 59.67 Hz → 16.76 ms per frame for every frame, not on average.

## Installed on the card (2026-10-19, owner asked)
- Shortcuts (owner asked, 2026-10-20): `miyoo/card/` has `1997 Quake hipnotic`, `1997 Quake rogue`, `2021 Quake dopa` as .notfound (our core, QuakeDir), all shortcuts with GameDir/GameDataFile for Onion's import.sh (it read our RomDir literally and would have hidden 1996 Quake). Installed on the card; the old ones in `Roms/PORTS/Games/TyrQuake-bench/backup-20261020-shortcuts/`. No pack data on the card or the PC yet.
- **Now (2026-10-20): `RetroArch/.retroarch/cores/tyrquake_miyoo_libretro.so` md5 7a446a25 = library_name "TyrQuake-Miyoo" (own config/saves/states folders, see "Own name"), before it 69294ffb = owner's settings as defaults (640x480, 59.67; TYRQUAKE_MIYOO_MINI) + sound filter + declicker + limiter + load fades (see Sound), before it a38e69e7, 33cb3773, 03a5eeee, 91afb028 =
  3ace1ed (play log) built with PGO + LTO** (pgo.sh; shots 59/59; before it: 4a500e2b = ee6132b
  PGO + LTO, a81e88ab PGO only), and `.info` ("Quake (TyrQuake-Miyoo)", also in `core_info/`). The stock
  `tyrquake_libretro.so` is untouched.
- Card settings for the 59.67 Hz test: `Saves/CurrentProfile/config/TyrQuake/TyrQuake.opt`
  tyrquake_framerate 40 → "59.67" (Slow Frames: default real time); core override
  `config/TyrQuake/TyrQuake.cfg`: audio_sync true, audio_resampler sinc, video_refresh_rate 59.67
  (applies to the stock core too; tried before: vsync + audio_latency 96, and audio_sync false →
  clicks). Old TyrQuake.opt in workspace `backup/20261019-hz/`; revert = restore it and delete
  TyrQuake.cfg.
- 2026-10-20 (owner asked: settings for our core only): the same three lines also as the content
  directory override `config/TyrQuake/FPS - Quake Collection (TyrQuake).cfg` (= the release's
  file, see "RetroArch settings for this core only"). The owner's TyrQuake.cfg left as it was.
- The shortcut on the card is still the one without the core_info.cache refresh.
- History of the core file: md5 26a57e5b (611d543) → c39cf8a5 (cb36289, caches on both threads)
  → 43f87cab (1e7f291, savegame fix, lerp on) → 4e3da96e (00c76b0, save states) → fc33ab4d
  (fc14e41, .port as content; `cores/core_info.cache` deleted; shortcut replaced by
  `miyoo/card/1996 Quake.port`, previous in `backup/20261019-gameswitcher/`) → 1cec45bf (3f56ebe,
  resume fix, palette on both threads, Q_rand, dither fix) → a1637818 (47f32a5, alias models on
  both threads, music thread, 59.67 option) → c3bc85e7 (ee6132b, Slow Frames) → a81e88ab (PGO).
- `Roms/PORTS/Shortcuts/FPS - Quake Collection (TyrQuake)/1996 Quake.port`: `Core="tyrquake_miyoo"`
  (was `tyrquake`; original in the workspace `backup/20261019-before-install/`). Revert = that line.
- library_name stays "TyrQuake": same saves/config.cfg/core options (TyrQuake.opt) as the stock core.

## Surface caches built on both threads (2026-10-19)
- D_CacheSurface still decides and allocates on the recording thread (D_SCAlloc is not thread-safe),
  but with two threads it queues the build (a copy of r_drawsurf); D_MT_Flush first lets both
  threads take build jobs (atomic index), waits until all are built (a band may use a cache the
  other thread built), then replays the spans. r_surf.c state is per thread (static D_TLS, block
  drawers copy it into locals: R_SURFBLOCK_LOCALS), blocklights is a per-thread buffer. A flush is
  forced once a quarter of the surface cache was allocated since the last one (deferred builds
  must not get their block reused). TLS now 232 bytes.
- 640x480 A/B (same session): demo1 1 thread avg 20.01 / 2 threads 15.65-15.68 (p90 18.9, p99
  21.7-22.3); demo2 17.95 / 14.64 (p90 17.8, p99 21.7); demo3 20.61 / 16.38 (p90 19.7, p99 22.9).
  Before this step (spans only) demo1 2 threads was avg 15.8-16.2, p90 20.3-20.9.
- r_perf demo3 2 threads, per second: frame 13.7-19.4 = edge drawing 5.2-8.1 (D_DrawSurfaces
  4.2-6.3 incl. parallel cache phase 0.05-1.8) + entities 1.7-3.5 + VID_Update 3.2-3.5 + audio ~2
  (music decoding now that it is found). Next serial targets: VID_Update, entities, audio.
- One run (demo1, r_threads 1, right after another run) gave no result: runtime.sh never started
  the bench (a crash would still have written a result); two repeats were fine. bench_device.sh
  now waits until MainUI is 5 s old, leaves a 'started' marker and prints diagnostics.

## Smooth animation / movement on by default (2026-10-19, owner asked)
r_lerpmodels and r_lerpmove default to 1 (menu "Smooth Animation" / "Smooth Movement"); a value in
config.cfg still wins (the owner's has both 1). Test with them on. 640x480, 2 threads, both on
(console woken from sleep just before): demo1 avg 15.61 (p90 18.8, p99 21.4), demo2 14.60 (18.2,
21.5), demo3 16.26 (19.4, 21.8) — same as with them off.

## Save states for Onion's GameSwitcher (2026-10-19)
How Onion does it (from keymon's strings): on Menu during RetroArch, keymon dumps the screen to
`Saves/CurrentProfile/romScreens/<hash>.png` (FNV hash of the recent-list rom path; for the Quake
shortcut 3601100754.png, which already worked), sends `SAVE_STATE_SLOT -1` and `QUIT` over
RetroArch's network commands (`.tmp_update/bin/sendUDP` does the same) and touches
`.runGameSwitcher`. Power-off does the same. Resuming starts RetroArch with savestate_auto_load, which
reads `Saves/CurrentProfile/states/TyrQuake/pak0.state.auto` (sorted by library_name, named after the
content file). Before this the core had no states, so Quake always started over.
- `retro_serialize` = the "save" command's text (Host_WriteSavegame, through /tmp/tyrquake_state.sav)
  behind a header: magic `TQSAVE01`, length, game dir name. Every game's content is `pak0.pak`, so
  id1/hipnotic/rogue share one state file name: a state from another game dir is refused.
- Same rules as "save": single player, not in intermission, not dead (else false: the old state stays).
- retro_serialize_size: fixed 2 MB (more for >992 edicts); RetroArch drops states when it is 0, and
  asks before the game runs. The zero tail compresses away (17 KB file).
- `retro_unserialize` checks the header and leaves the load (Host_ReadSavegame, as "load": map reload)
  to the next retro_run, after Cbuf_Execute: the auto state is loaded before quake.rc has run.
- `.info`: `savestate = "true"`, `savestate_features = "basic"` (RetroArch reads support from the
  .info), quirk INCOMPLETE (no run-ahead/rewind/netplay on top).
- "[State] Failed to save state to RAM" at auto-load is RetroArch's undo-load backup taken before the
  game runs: harmless.
- Test (bench_device.sh STATE=save then STATE=load, LOAD=s1): state saved via sendUDP, resumed by
  auto-load; screenshot `build/shots/state_resume.png` = s1 (health 100, 27 shells).

### Owner's first try on the card failed (2026-10-19), two Onion/RetroArch causes
Onion source: github.com/OnionUI/Onion `src/keymon/menuButtonAction.h`, `src/gameSwitcher/gs_overlay.h`.
- RetroArch's `cores/core_info.cache` (core_info_cache_enable) still had the old .info:
  savestate_support_level 0 → RA refused every state. A hand-edited .info needs that cache deleted
  (RA rebuilds it). The bench didn't see it: it uses its own info dir.
- Menu (single press) runs `gameSwitcher --overlay` over the paused RetroArch. It treats the game as
  running only if `GET_STATUS`'s content name equals the recent-list rom name, i.e. the `.port`
  file's name; RA said `pak0`. Otherwise no romScreen is saved and picking the game kills RA and
  relaunches it (the owner landed in the main menu). Long press / power-off don't check the name.
- Fix without an extra file in id1: the content is the shortcut itself. `miyoo/card/1996 Quake.port`
  passes `RomDir="../Shortcuts/${PWD##*/Shortcuts/}"`, `RomFile="$(basename "$0")"` (launch.sh cds
  into the shortcut's dir and runs it by full path) and has `QuakeDir="Quake Collection (TyrQuake)/id1"`;
  the core (resolve_port_content) reads QuakeDir and finds `Games/<QuakeDir>` walking up from the
  shortcut. RA then reports `quake_1,<shortcut name>,crc32=...`, and each shortcut has its own state
  file (`<shortcut name>.state.auto`). launch_retroarch.sh writes its `.game_config/` next to the
  shortcut. `.info` supported_extensions `pak|port`.
- Resuming showed the console and a demo frame with "Loading" first (owner; caught with
  bench SHOTS=n, frame dumps every ~0.2 s, `build/shots/resume_seq_before.png`). RetroArch hands the
  auto state over after the 1st retro_run, which had run quake.rc + the start-up demo; and before
  that it calls retro_serialize for its undo buffer, whose "Not playing a local game." Con_Printf
  redraws the screen (console) outside retro_run. Now: Host_SavegameAllowed(false) is quiet; the
  first 10 retro_runs don't run Host_Frame (black, waiting for a state); state_load hides the screen
  (SCR_HideForLoading: no console slide) and sets demonum -1 before running quake.rc. Result: black
  then the game (`resume_seq_after.png`); a start without a state still shows console + demo.
- A release zip can't delete files, so the shortcut drops `cores/core_info.cache` itself whenever
  the core's .info md5 differs from `cores/.tyrquake_miyoo_info.md5` (then writes it).
- Release: `miyoo/tools/package_release.sh [tag]` → `<workspace>/build/release/TyrQuake-Miyoo-<tag>.zip`
  (core, .info in cores/ and core_info/, the shortcut); user docs in `miyoo/README.md`.
- Bench PORT=1 (a .port in /tmp/tqbench/Shortcuts/Test, QuakeDir under /tmp/tqbench/Games):
  GET_STATUS `quake_1,Bench Quake,crc32=46182fb0`, `Bench Quake.state.auto` saved and resumed
  (`build/shots/state_resume_port.png`). The bench runs click because they set audio_sync off.

## Screenshot regression check (2026-10-19, owner asked before more optimisation)
Like dethrace's miyoo-emu `--compare`, but on the device: a timedemo replays the same frames
(fixed time step per retro_run), so a frame looks the same in every correct build.
- cvar `timedemo_shots N`: every Nth timedemo frame, VID_Update writes the RGB565 frame it hands
  to RetroArch to `timedemo_shotdir` (default /tmp/tqshots) as NNNNN.rgb565 (614400 bytes at
  640x480). Not for timing runs.
- `miyoo/tools/shots.sh <core> <set> [every] [demos]` runs the demos at 640x480 (CFG as for
  bench.sh) and fetches the shots (tar over SMB) to `<workspace>/build/shots/td/<set>/<demo>/`.
- `py -3 miyoo/tools/shots_compare.py <ref set dir> <new set dir>`: identical or not per shot;
  for each difference the pixel count, bounding box, max channel difference and a picture
  reference | new | changed pixels (magenta); contact sheet per demo; exit 1 on any difference.
- Reference = r_threads 0 (the original one-thread renderer); every optimisation must give the
  same pixels (the two-thread split must already).

### What the first comparisons found (2026-10-19)
- A run overlapping another Claude session on the device (portmanager-miyoo) gave a half-drawn
  screen and wrong timings: check ListAgents before device runs.
- shots.sh stopped after one demo: mssh returns its output filter's exit code (1 when the remote
  command prints nothing) → `|| true` and a check for the tar file.
- The timedemo was not repeatable: two runs of the same core differed in particles and dynamic
  light radii (`200 + (rand() & 31)`). Causes: (1) the C library's rand() state is shared with
  RetroArch in the same process; (2) S_StartSound took a rand() only when the same sound had just
  started on another channel and that one's `pos` was still 0, which depends on how much audio the
  frontend took (wall clock). Fixes: quakedef.h `#define rand() Q_rand()` (common.c, 64-bit LCG,
  31-bit results, the game's own state; libc rand no longer imported), and snd_dma.c's offset uses
  its own LCG. Then two runs gave 19/19 identical frames. (`nosound 1` alone was not enough.)
- Real two-thread bug (since the span split): D_DrawSpans16QbDither skips dithering for mipmaps by
  reading `pcurrentcache->mipscale` at draw time; replayed calls saw the last recorded surface, so
  some walls got the wrong dithering. Now `d_cache_mipscale` (D_TLS) is set with cacheblock and
  recorded/replayed with the call.
- Result (Q_rand everywhere): reference = commit f95ef50 + the same RNG fixes (old palette loop,
  r_threads 0, built in a git worktree at `<workspace>/build/wt/tyrquake` → `build/core_ref`) vs
  core 1cec45bf with r_threads 0 and 1: demo1 19/19, demo2 19/19, demo3 21/21 identical.

## Palette pass on both threads (2026-10-19; timed with 5ed39274, pixels checked with 1cec45bf)
- VID_Update's 8 bpp → RGB565 pass: top half on the calling thread, bottom half on the worker
  (D_MT_RunBoth, a generic two-part job on d_mt's worker); 4 pixels per 32-bit load, two 32-bit
  stores; width/rowbytes in locals (the stores made gcc reload `width` every 4 pixels).
- r_perf prints `vidupd X (pal Y)`: pal = the palette pass alone, the rest is video_cb
  (RetroArch's work).
- 640x480, on battery, console alone: demo1 avg 14.86-14.88 (median 14.5, p90 18.1-18.3, p99
  22.3-22.6), demo2 13.58-13.71 (12.9-13.2, 17.0-17.2, 21.1-21.8), demo3 15.38-15.44 (15.3,
  18.8, 22.4-22.6). Before (15.8-15.9 / 14.5 / 16.3): about 1 ms per frame less.
- r_perf demo1: vidupd 1.7-2.2 ms (was 2.7-3.4), of which the palette pass 1.0-1.25 on both
  threads; video_cb ~0.9.
- Shots identical to the one-thread original (see above).
- Final core 1cec45bf (with Q_rand + the dither fix), on battery, alone: demo1 avg 15.23 (median
  14.82, p90 18.78, p99 22.18), demo2 13.87 (13.26, 17.27, 21.88), demo3 15.63 (15.62, 18.89,
  22.34); ~2 % above the 5ed39274 run (noise, or the now correct dithering per surface).

## Alias models on both threads (2026-10-19, core 15755cdd)
- r_perf `ents X (poly Y)`: poly = D_PolysetDraw (triangle rasterizing) was 88 % of the entity
  time (demo1/3 per second: ents 1.8-3.6, poly 1.6-3.2 ms); vertex transform/clipping is small.
- Between D_PolysetBeginRecord/EndRecord (R_RenderView, around the entities and the view model)
  D_PolysetDraw/D_PolysetDrawFinalVerts record each call: triangles, a copy of the vertices they
  use (arena), skin/colormap/lightcolor. D_PolysetFlush draws the list with D_MT_RunBoth: this
  thread the upper rows, the worker the lower, split moved towards equal times. Triangles and
  calls outside a band are skipped, span rows outside it are stepped over. Same call order in
  each band, each pixel/z in one band → same picture. Sprites and r_shadows flush first; Phong
  shading (r_phongshading) draws directly as before.
- The rasterizer state is per thread (D_TLS, TLS now 536 bytes): the edge tables hold indices
  into r_pv[3][6] (TLS addresses aren't constants); ubasestep/errorterm (d_edge.c) too; the
  subdivided (small/far model) path no longer patches shared vertices for the seam (local
  copies); skintable lookups became pskin + t * skinwidth (same texel). D_PolysetDrawSpans8 keeps
  the state in locals.
- Shots: 2 threads demo1-3 59/59 identical to the reference, r_threads 0 demo1/3 40/40.
- 640x480, console alone, on the charger (85 %): demo1 avg 13.88 (median 13.44, p90 17.05, p99
  21.10), demo2 13.08 (12.66, 15.80, 19.54), demo3 14.25 (14.19, 17.17, 20.62); before 15.23 /
  13.87 / 15.63. r_perf demo1: ents 1.5-2.0 (poly 1.1-1.5).

## What RetroArch costs per frame (2026-10-19, owner asked: worth a standalone port?)
r_perf demo1 640x480 (core 7bc25bc): audio 2.25 ms = music decoding (BGM_Update, OGG) ~1.0 +
audio_batch_cb (RetroArch's resampler + driver) 0.6-0.75 + Quake's mixing ~0.5. video_cb
(vidupd minus pal) ~1.0. Outside retro_run (RetroArch's loop, input, menu checks): ~0.1
(wall time per second vs the sum of retro_run times). So RetroArch's own share is ~1.7 ms of
~14; a standalone port would still need its own present (8 bpp → the 32 bpp upside-down fb0,
could be folded into the palette pass on both threads) and audio push (audioserver FIFO, as in
REDRIVER2): net gain maybe 1-1.5 ms (~10 %). It would lose the RetroArch menu, core options,
remapping and the Game Switcher resume we have (Onion only screenshots/kills standalone ports;
resume would need our own save on exit). Cheaper first: music decoding (~1 ms) on the worker.

## Music decoding on its own thread (2026-10-19, core ac3f4af2)
- The core decodes the music itself (id1/music/trackNN.ogg, Tremor: the frontend negotiates
  int16 at 48000 Hz, logged as "Audio: 48000 Hz, int16 output"); RetroArch only gets the mixed
  PCM (audio_driver oss → audioserver, resampler nearest).
- bgmusic.c: a decoder thread (SCHED_IDLE, pinned to core 1) keeps a 512 KB ring of decoded PCM
  filled in 16 KB chunks, decoding outside its lock (an idle-priority thread holding a lock the
  main thread needs would stall it); BGM_UpdateStream only copies from the ring. Streams are
  opened/closed on the main thread; BGM_Stop detaches the decoder first (waits for a chunk in
  progress). Looping/EOF/errors handled by the decoder, reported to the main thread.
  BGM_StopThread in retro_deinit (and BGM_Shutdown). "Music decoder: ring ran dry in N frames"
  at exit: 1-2 (the first frames of a track).
- r_perf: bgm 1.0 → 0.15 ms. But audio_batch_cb rose 0.65 → 1.1 ms in the bench: the timedemo
  runs faster than real time with audio_sync off, so RetroArch's OSS buffer overflows and faster
  frames make that worse; in play (60 fps, audio_sync) this doesn't happen. An unpinned decoder
  measured the same.
- A/B alternated, 640x480 avg: demo1 13.79/13.75 → 13.38/13.56, demo3 14.32/14.36 → 14.01/14.05
  (p90 about -0.3).

## Paced like play: audio_sync on, 60 fps (2026-10-19, owner asked)
- bench: `SYNC=1` keeps audio_sync on (Onion's setting; the sound paces the game), `FPS=60`
  overrides the core framerate option. The timedemo now also prints `timedemo cpu`: process
  CPU per frame (getrusage, all threads incl. RetroArch's) and the main thread alone: with
  pacing the wall time per frame is the refresh whatever the load, so CPU time is the measure.
- **The owner's core options have `tyrquake_framerate = "40"`**: the game runs at 40 fps in play
  (SYNC=1 without FPS: 39.8 fps, 25.1 ms per frame). The bench always copied that file, but
  without audio_sync the framerate only set the game time step.
- FPS=60 SYNC=1, 640x480, alternated: both builds hold ~59.5 fps (avg 16.8 ms; per-frame wall
  times jump with the audio blocks: median ~17, p90 20). Main thread CPU per frame, no music
  thread / music thread: demo1 12.76-12.80 / 12.29-12.57, demo3 13.29-13.39 / 12.75-13.09; other
  threads +0.65 (the decoder); whole process ~19.7-20.8 ms per frame (~120 % of a core).
- r_perf at 60 fps: audio_batch_cb 2.1-4.4 ms per frame is now mostly waiting for the sound
  (the spare time); heaviest second: render 13.7, cb 2.1.

## Native refresh 59.67 Hz (2026-10-19, owner asked)
- Core: framerate option "59.67" (also what "auto" picks when RetroArch's video_refresh_rate is
  59.67); the samples per frame come from an accumulator (804/805 alternating at 48 kHz, the
  same count every frame at whole framerates).
- bench: `VSYNC=1` (video_vsync), `REFRESH=59.67` (RetroArch's video_refresh_rate, Onion: 60).
  The timedemo prints frames 300-359 in order and how many took over 20 / 25 ms.
- demo1 640x480 frames 300-359 (ms):
  - A, Onion's setup (audio_sync on, vsync off), 60 fps: `12 18 20 12 18 20 13 18 20 ...`:
    the sound paces the game in bursts (RetroArch's OSS writes block per audio block), so the
    frames reach the screen unevenly although the average is 16.9 ms.
  - C, audio_sync + vsync, 59.67: the same bursts (`20 20 18 18 14 13 17 20 12 18 20 ...`).
  - D, vsync on, audio_sync off, 59.67: `17 17 17 16 17 17 ...`, 59.67 fps exactly, avg/median
    16.76, p90 17.22, 19 of 969 frames over 20 ms (heavy frames waiting for the next refresh).
  - A/B/C all ~59.0-59.3 fps, ~100 frames over 20 ms, same CPU (main thread 12.4-12.9 ms).
- So vsync works with sdl_dingux, but with audio_sync on the audio writes decide the pacing.
- D on the card: the owner heard clicks (without audio_sync RetroArch drops what doesn't fit).
- WRONG at first: "audio_sync + vsync + audio_latency 96 paces evenly" — those runs had the
  card's core override (`config/TyrQuake/TyrQuake.cfg`, then audio_sync false) applied on top of
  the bench config (RetroArch loads core overrides; the log said "[Override] ... unloaded"). The
  bench now sets auto_overrides_enable/game_specific_options false. With the override changed
  to audio_sync true the owner still heard faint clicks.
- Clean (demo3, frames 300-1089 "steady" fps): audio paced at 60 fps, rate control off: 59.90
  fps → audioserver takes ~47 920 samples/s (0.16 % under 48 kHz); vsync only: 59.60. With
  audio_sync on every combination paces in bursts (vsync, audio_latency 64/96, sinc): steady
  58.95-59.35 fps, 86-121 of 1090 frames over 20 ms — the same as Onion's setup.
- Music ring dry only 1-2 frames per demo (track start): not the clicks.
- Card test: audio_sync true, audio_resampler sinc, video_refresh_rate 59.67, no vsync: the
  owner still heard clicks whenever the game dropped below 60 fps; at 40 fps never.

## Slow frames: real-time steps (2026-10-19)
- Cause of the clicks: the core always stepped 1/framerate and mixed that much sound; in heavy
  scenes at 60 fps the frames take longer, so it handed over less sound than audioserver plays:
  timedemo (fixed steps, audio_sync, 59.67) frames 300-end 47 679-47 710 samples/s vs ~47 920
  played → the buffer runs dry → clicks. At 40 fps (25 ms budget, ~13 ms work) never.
- Core option "Slow Frames" (tyrquake_frame_steps, default realtime): a frame whose own work
  took longer than 1/framerate steps the game and the sound by that time (max 50 ms). The time
  blocked in audio_batch_cb is left out: counting it, waits made longer steps, more sound,
  longer waits, and the game settled at 30-36 fps (seen). Timedemos keep fixed steps unless
  `timedemo_realsteps 1`; the report prints the sound rate from frame 300 on.
- realsteps 1, 59.67, audio_sync: demo1 47 992, demo3 47 966 samples/s (enough), 58.8-58.9 fps
  (the same game time in fewer, longer steps); pacing as bursty as Onion's (audio_sync).

## Compiler: -O3 and PGO (2026-10-19)
- build_core.sh: extra CFLAGS after the out dir (`build_core.sh <out> -O3`), `LDEXTRA` link
  flags; `xargs -r` (with nothing to compile it ran the compiler once without arguments).
- `miyoo/tools/pgo.sh`: instrumented build (-O3 -fprofile-generate -fprofile-update=atomic;
  ~2.7x slower), demo1-3 on the device with bench `PGO=1` (GCOV_PREFIX=/tmp/pgo; the .gcda
  paths are the Windows object paths), .gcda copied next to the objects, rebuilt with
  -fprofile-use -fprofile-correction. Rerun after code changes (the profile is per source).
- A/B 640x480, two rounds (avg / p90 / main thread CPU ms):
  O2 demo1 13.29-13.40 / 16.35-16.43 / 12.85-12.90, demo3 13.81-13.93 / 16.73-16.94 / 13.12-13.22;
  O3 demo1 13.13-13.23 / 15.86-15.97 / 12.66, demo3 13.50-13.61 / 16.09-16.18 / 13.11-13.22;
  PGO demo1 12.91-12.96 / 15.63-15.76 / 12.40-12.54, demo3 13.28-13.60 / 15.93-16.60 / 12.71-12.82.
  PGO ~3 % (trained on the same demos), O3 ~1.5 %. Shots: O3 = O2, PGO = reference (59/59).
- LTO (2026-10-20): `LTO=1 build_core.sh` adds -flto (also on the link); pgo.sh uses it for the
  final build by default (LTO=0: PGO only). Two problems solved:
  - the .symver lines force-included from glibc_compat.h are lost under LTO (exp@GLIBC_2.29,
    pthread_create@GLIBC_2.34 came back): with LTO, glibc_compat.c is built without LTO, binds
    the old versions and the other units reach it via -Wl,--wrap=exp,log,pow,exp2,log2,
    pthread_create,pthread_join.
  - shots: 1 texel differed (demo1 shot 900). -ffp-contract=off made it worse (56 shots: the
    reference relies on GCC's default fused multiply-adds as placed without LTO); bisected to
    r_misc.c (TransformVector & co. inlined into other units fuse differently). `NOLTO`
    (default common/r_misc.c) keeps sources out of LTO: then 59/59 identical.
- pgo.sh had never run whole: with plain -fprofile-generate the .gcda paths are the Windows
  ones (G:/...), the device can't create them and this toolchain's gcov runtime then aborts
  (free(): invalid pointer, exit 134, even a 1-line test program). Yesterday's build had used
  -fprofile-generate=/tmp/pgo by hand; pgo.sh does that now. An instrumented build runs ~2.7x
  slower (demo1 ~38 ms/frame).
- A/B PGO vs PGO+LTO (core 4a500e2b), two rounds alternated, 640x480 (avg / p90 / main thread
  CPU ms): demo1 13.21-13.22 / 16.12-16.25 / 12.0 → 12.65-12.70 / 15.27-15.44 / 11.65,
  demo3 13.71 / 16.42-16.45 / 12.55 → 13.16-13.31 / 15.78 / 12.0-12.2: another ~4 %
  (PGO+LTO ~7 % over -O2). The binary is smaller too (707 KB vs 801 KB).
- shots.sh pins FPS=40 (the reference set's framerate; the owner's option is now 59.67, which
  moves e.g. when notify lines vanish — 34 shots differed in the top-left text only).

## Play log: timing ordinary play (2026-10-20, owner asked)
- Owner: after PGO+LTO, light knocks only with music on; without music fine.
- Core option "Performance Log" (`tyrquake_perf_log`, default off): perf_timing.c appends one
  CSV line per second to `perflog.csv` in the game's save folder (card:
  `Saves/CurrentProfile/saves/TyrQuake/id1/`, SMB `//192.168.0.246/Saves/TyrQuake/id1/`), a
  `# session` line (date, core, framerate, budget) and a header per start. Per second: frames,
  fps, frames >20/>25 ms apart, max interval, the frame's own work (wall minus the wait in
  audio_batch_cb) avg/p90 (0.25 ms histogram)/max, frames over the budget, real-time steps,
  audio wait, samples/s, music decoder dry frames, least music decoded ahead (ms), mixer chunks
  where the music ran out, clipped sample frames (music + effects over full scale, int16 lane),
  CPU ms per frame (main thread, process), the r_perf sections, CPU MHz, temperature (Miyoo:
  `/sys/devices/system/cpu/cpufreq/temp_out`, "Temp=60"). While on, the sections run as with
  r_perf 1. Map = the client's world model (demos have no server).
- `py -3 miyoo/tools/perflog.py [csv] [--all] [--keep]`: fetches the card's file into
  `<ws>/build/perflog/` (and empties it), prints totals, a per-map table, the worst seconds,
  music gaps, the loudest (clipped) seconds, and a 4-panel chart PNG next to the CSV.
- bench `PERFLOG=1` turns it on (file in /tmp/tqbench/saves/tqbench/perflog.csv).
- Card (owner asked, 2026-10-20): core 91afb028 installed, `tyrquake_perf_log = "enabled"` in
  TyrQuake.opt, `bgmvolume "1"` in id1/config.cfg (was 0; volume 0.7 kept); both files backed
  up in `Roms/PORTS/Games/TyrQuake-bench/backup-20261020-perflog/`.
- The owner's Quake config now has `bgmvolume 0` (music off), so benches copy that: use
  `CFG="bgmvolume 1"` for music.
- First findings (SYNC=1 FPS=59.67 timedemo_realsteps 1 bgmvolume 1, core baa6adda):
  - music decoder never behind except 1-2 frames at a track start; ring ~2.86 s ahead all
    the time: not the knocks.
  - **clipping**: demo1 music + effects clipped in 14 of 16 seconds, up to 15 193 sample
    frames in one second (~1/3 of 48 000). The mixer adds the music at unity onto the
    (already clamped) effects and hard-clips the sum (snd_mix.c): the likely knocks.
  - glibc: with _GNU_SOURCE, fscanf/strtol bind __isoc23_* (2.38): perf_timing.c parses sysfs
    numbers by hand.
- Owner's play log (251 s, e1m4/e1m5, core 91afb028): 59.0 fps avg; clipping in 124 s, worst
  ~7900 sample frames/s (113-124 s, grenade fight); music decoder never behind. Knocks at
  save/load/level change: one frame held 87 / 207 / 618 ms → that second's sound 44 831 /
  34 063 / 19 021 samples: RetroArch's ~64 ms ran out, the music stopped mid-wave.

## Sound: limiter and fades around loads (2026-10-20, owner asked)
- snd_mix.c S_LimitChunk (int16 lane, when music is mixed): per 32-frame block the gain that
  keeps the peak at full scale; the gain ramps within a block to the lower of this and the next
  block's (no overshoot), back to unity over ~150 ms. A/B demo1 (same code, `-DNO_SND_LIMITER`,
  fixed steps, bgmvolume 1): frames at full scale 55 914 (7.1 %) → 347 (0.04 %, peaks limited to
  exactly full scale); largest neighbour jumps 39 337 → 28 596 (at the same times: explosion
  onsets in the sounds themselves). The play log's `clip` still counts what would have clipped
  before the limiter.
- libretro.c S_StallBegin/S_StallEnd (sound.h): before a stall the core mixes 30 ms more and
  fades it to silence, pushes silence while stalled, fades back in over 10 ms; gives up after
  ~5 s. Called by SCR_BeginLoadingPlaque (before S_StopAllSounds) / SCR_EndLoadingPlaque
  (signon), around Host_WriteSavegame, and by console "load". Dump at a load: level before the
  silence 3445 → 962 → 562 → 73 (5 ms RMS steps), 14 right after it.
- bench `AUDIODUMP=1`: the core appends what it pushes (raw int16 stereo, env TQ_AUDIO_DUMP) to
  /tmp/tqbench/audio.raw; `py -3 miyoo/tools/audio_check.py <raw>`: full-scale frames, biggest
  jumps, silent stretches with the level at their edges. bench CFG: `\;` = a ';' kept inside an
  alias. Long `wait` alias chains in the autoexec overflow Quake's 8 KB command buffer at
  start-up (crash 139), so a save in a bench wasn't tested; the save path is the same fade.
- A stale PGO profile after a code change is an error by default (`-Werror=coverage-mismatch`,
  "source locations ... have changed"): rerun pgo.sh, or for a release rebuild with an old
  profile add `-Wno-error=coverage-mismatch` (changed functions then lose their profile).
- Installed 2026-10-20: core 03a5eeee (PGO+LTO, shots 59/59); log and music still on.
- Owner: save/load fixed; still crackles when monsters make sounds. Cause: with music on the
  mixer first hard-clamped the effects alone (before the music and the limiter), and the
  limiter's 0.7 ms attack (instant at a chunk start) was itself audible.
- Look-ahead limiter (S_Limit, replaces S_LimitChunk): runs on every int16 mix (music or not;
  the effects-only pre-clamp is now float-lane only), output delayed 256 frames (5.3 ms),
  64-frame blocks, gain falls in a straight line over up to 4 blocks to meet each coming peak,
  never above the current/next block's limit, ~150 ms release. Play log column `lim_min`.
- Measured (demo1, owner's volume 0.7): **the effects alone go up to 5x full scale** (bgmvolume
  0: limiter down to 0.20 = -14 dB, median of the per-second minima 0.57); music adds little;
  volume 0.35 + bgmvolume 0.5: -8 dB at worst, 7 of 16 s. Without the limiter 10.9 % of the
  frames sat at full scale (hard clip); with it 0.04 %, largest neighbour jump 37 136 → 19 403.
- WAVs for the owner: `<ws>/build/perflog/demo1_A_hard_clip.wav` (no limiter) and
  `demo1_B_limiter.wav` (same demo, look-ahead limiter).
- Installed 2026-10-20: core 33cb3773 (PGO+LTO, shots 59/59).
- Owner: still clicks with the nailgun (fast), explosions, and in the menu (up/down). Not
  clipping (the menu sound is quiet), not the music.
- RetroArch's audio buffer status (RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK, logged:
  "Audio: buffer status from the frontend: yes", columns buf_min/underruns) is useless with
  Onion's OSS/audioserver: 0 % and "underrun likely" every frame while the sound plays fine.
- The wait subtracted from a slow frame's real-time step now leaves out the CPU time inside
  audio_batch_cb (CLOCK_THREAD_CPUTIME_ID): with audio_resampler sinc RetroArch resamples in
  there (~1-2 ms a frame), which was counted as waiting. Owner's play log had 47 347 samples/s
  (31 ordinary seconds at 46 000-47 600); benches didn't reproduce it (47 780-48 013).
- Clicks in the engine itself (any port would have them): a new sound on the same entity
  channel cuts the old one mid-wave (every nailgun shot), S_LocalSound (menu) uses entchannel -1,
  which takes ANY of the player's channels, a full list steals the quietest, sounds end on
  non-zero samples, and a moving source's volume steps once a frame (zipper). Fixes:
  - declicker (snd_mix.c): a cut channel's last value (channel_t last_left/right, kept by
    SND_PaintChannelFrom16) is added to the mix and fades over ~2 ms; S_DeclickChannel from
    SND_PickChannel (replace/steal) and S_StopSound, S_DeclickTail where a sound ends in a chunk.
  - volume ramps: a playing channel whose volumes changed since its last paint goes there
    linearly over 128 frames (painted_lvol/rvol); a sound's start (pos 0) stays sharp.
  - ResampleSfx: 16 fractional bits (8 truncated the 11025 → 48000 step 58.8/256 to 58: every
    sound 1.4 % low and its last 1.4 % never played, ending mid-wave).
  - demo1 dump, effects only: second-difference kinks > 16 000: 30 → 1, > 8 000: 694 → 479
    (the rest: linear interpolation corners of the 11 kHz samples). WAVs:
    `<ws>/build/perflog/demo1_C_before_declick.wav`, `demo1_D_declick.wav`.
- Installed 2026-10-20: core a38e69e7 (PGO+LTO, shots 59/59).
- Owner still heard clicks; played Quake in 1996 without them. Why then: DOS Quake mixed at
  11025 Hz (the card's filter removed everything above ~5 kHz, where clicks and the edges of
  the interpolated 11 kHz samples live), the samples played at their own rate, the CD music
  was mixed analogue, and the speakers had no treble.
- Core option "Sound Filter" (`tyrquake_sound_filter`, original / off, default original;
  snd_lowpass): 4th-order Butterworth low-pass at 5 kHz (biquads Q 0.5412 / 1.3066, TDF-II) on
  the effects after painting and declicking, before the music and the limiter; skipped at
  rates <= 11025. demo1 effects only: kinks > 4000 5355 → 22, > 8000 479 → 0, energy above
  6 kHz 0.30 % → 0.02 %. WAV `<ws>/build/perflog/demo1_E_filter.wav`.
- Cost (PGO+LTO, PERF=1 audio section, demo1 with music, 2 rounds): 1.46-1.48 → 1.51 ms per
  frame (~0.04 ms); whole-frame timedemo within noise (12.67-12.87 vs 12.69-12.75 ms).
- Installed 2026-10-20: core 5125416a (PGO+LTO, shots 59/59).
- Owner (2026-10-20): with the filter the sound is "much more pleasant and soft".
- Owner's last play log with core 5125416a (153 s, e1m5/e1m1/start): 59.1 fps avg, 1.2 % of frames over the budget, limiter active in 92 s (lowest 0.31 = -10 dB, median of the per-second minima 0.68), music decoder never behind, 64-70 °C. Play log then turned off on the card (TyrQuake.opt perf_log disabled, the empty perflog.csv deleted).

## Own name: library_name "TyrQuake-Miyoo" (2026-10-20, owner asked, core 7a446a25)
- Sharing the stock core's folders bought nothing: its savegames don't load back and it has no
  states. Now RetroArch uses `config/TyrQuake-Miyoo/` (TyrQuake-Miyoo.opt, core override
  TyrQuake-Miyoo.cfg = the release's `miyoo/card/TyrQuake-Miyoo.cfg`), `saves/TyrQuake-Miyoo/`,
  `states/TyrQuake-Miyoo/`, `lists/playlists/logs/TyrQuake-Miyoo/`. `.info` corename too.
- The shortcuts copy `saves/TyrQuake/.` and `states/TyrQuake/*.state*` over once, when
  `saves/TyrQuake-Miyoo` doesn't exist yet (the old files stay). Not the .opt: the stock core's
  would replace our defaults (640x480, 59.67). bench_device.sh / perflog.py use the new paths.
- Card: backup of saves/states/config TyrQuake in `Roms/PORTS/Games/TyrQuake-bench/
  backup-20261020-rename/saves.tar`; owner's TyrQuake.opt copied to TyrQuake-Miyoo.opt by hand;
  the "FPS - Quake Collection (TyrQuake).cfg" folder override removed; TyrQuake.cfg/pak0.cfg
  (stock core) left. Shots 59/59 identical.
- Launch test as Onion does (`/tmp/cmd_to_run.sh` running launch.sh on the .port from a script
  in /tmp: a command naming /mnt/SDCARD/Roms is taken as a game, ends SSH and didn't run):
  migration done, "Core-specific overrides found at .../TyrQuake-Miyoo/TyrQuake-Miyoo.cfg",
  state auto-loaded from states/TyrQuake-Miyoo (the owner's spot in the game, 640x480),
  SAVE_STATE_SLOT -1 + QUIT saved it there. The "progs.dat: statement ... globals index out of
  range" lines at a load are upstream's bytecode check (seen before, harmless).

## RetroArch settings for this core only; sinc vs nearest (2026-10-20, owner asked)
- Overrides live in `config/<library_name>/`, and both cores are "TyrQuake", so a core override
  (TyrQuake.cfg) is shared. A content directory override, `config/TyrQuake/<parent folder of
  the content>.cfg`, is ours alone: our content is the shortcut, so the folder is
  `FPS - Quake Collection (TyrQuake)` (the owner's runtime log is named that too), the stock core's
  is `id1`. Checked with bench `PORT=1 OVERRIDES=1` and a `Test.cfg`: "Content dir-specific
  overrides found ... stacking on top of previous overrides" (after TyrQuake.cfg). The release
  shipped `miyoo/card/FPS - Quake Collection (TyrQuake).cfg` for a few hours; replaced by "Own
  name" below.
- Bench `DSPTAP=1`: `miyoo/bench/dsp_tap.c` (LD_PRELOAD before libpadsp.so, built into
  `build/tap/`) appends what RetroArch writes to /dev/dsp, i.e. after its resampler, to
  /tmp/tqbench/dsp.raw; with AUDIODUMP=1 the core's own output is next to it. (dlsym pinned to
  GLIBC_2.4: the 13.3 toolchain binds 2.34.)
- demo3 640x480, SYNC=1 REFRESH=59.67 FPS=59.67, core 69294ffb (timedemo: fixed steps, so the
  core hands over 47 710 samples/s and rate control sits at its +0.5 % limit):
  - CPU: main thread 12.30 ms/frame with sinc, 12.14 with nearest (sinc ~0.16 ms; the earlier
    "1-2 ms" guess was wrong). Late frames 127 vs 133 over 20 ms: same.
  - nearest: exact copies of the core's frames, one repeated every 200 (995 in 200 000 = the
    0.5 %): a 240 Hz timing jitter. Against ideal band-limited resampling its error is median
    -33 dB of the signal, sinc's -39 dB (that's the fit's floor; sinc is likely cleaner).
  - No dropouts or extra silence with either; the largest jumps between samples are the core's.
  - sinc overshoots between samples: frames at full scale 424 vs the core's 221 (the limiter's
    ceiling is full scale). True peak of the core's output +0.04 dB (32 931), so the clipped part
    is <= 0.5 % of full scale in single samples: inaudible. A limiter ceiling ~0.1 dB lower would
    remove it.
  - Kept sinc (Onion's quality setting 2).

## Without RetroArch: tqrun (branch standalone, 2026-10-22, owner asked)
- `miyoo/standalone/tqrun.c`: a minimal libretro frontend that runs the same core .so. It answers
  the core's environment calls (options from a RetroArch .opt file, defaults from the core's
  SET_VARIABLES, perf/log interfaces, lent frame buffers), reads the buttons from evdev
  (Menu quits) and writes the sound to /dev/dsp through libpadsp (as RetroArch's OSS driver).
  Frames: the core draws into two MMA buffers lent in turn; a present thread flushes the cache
  and has the **SoC's 2D engine (MI_GFX_BitBlit, mirrored both ways = turned 180 degrees,
  RGB565 -> ARGB8888)** put the frame into the hidden fb0 page, then FBIOPAN_DISPLAY. The
  vendor libs are dlopen'ed (types copied from the SDK's mi_gfx_datatype.h layouts); TQ_GFX=0
  or missing libs → CPU conversion (that took ~6 ms a frame on the present thread and made
  the game slower than in RetroArch: avg 13.87 ms demo1). Idea from cacuracaptors'
  OpenLara-miyoomini v1.0.9; Onion's RetroArch shows its frames through MI_GFX too.
- Build: `miyoo/standalone/build_tqrun.sh` (dynamic, glibc ≤ 2.28: own `start.S` because the
  toolchain's crt1.o needs __libc_start_main@GLIBC_2.34; dl*/pthread_* pinned to GLIBC_2.4 and
  libdl.so.2 / libpthread.so.0 named explicitly; no _GNU_SOURCE, else __isoc23_strtol).
- Bench: `FRONT=tqrun miyoo/tools/bench.sh <core> 640x480 demoN` (same content, options, saves;
  VSYNC/SYNC/LAT/MAXF → -V/-A/-l/-n). Gotcha found: environment cases carry
  RETRO_ENVIRONMENT_EXPERIMENTAL (GET_TARGET_SAMPLE_RATE): switch on the raw cmd, else the
  core falls back to 44100 Hz.
- A/B 640x480, core 7a446a25, two rounds alternated, audio_sync/vsync off (avg ms / main
  thread CPU ms): demo1 RetroArch 12.65-12.79 / 11.95-12.05 → tqrun 10.23-10.26 / 10.12-10.13;
  demo2 12.09-12.15 / 11.30-11.37 → 9.57-9.58 / 9.47-9.53; demo3 13.14-13.15 / 12.46-12.56 →
  10.62-10.72 / 10.51-10.54. **~20 % faster**, ~1.9 ms a frame of the main thread (RetroArch's
  video_cb copy, resampler, loop). The blit takes ~1.1 ms on the present thread (engine time).
- Paced like play (RetroArch: audio_sync, sinc, 59.67 = the owner's override; tqrun: -V 1 -A 1):
  demo3 RetroArch 136 frames over 20 ms (frames 300-359: 13-21 ms, bursts) → tqrun **5**, every
  frame 16-17 ms (steady 59.671 fps, sound 48001/s, nothing dropped). demo1 RetroArch 117 →
  tqrun 49: there the sound write blocked (1.74 ms/frame) and frames went 11/19/20 for a while:
  the sound FIFO and the panel each pace; to do: with vsync, keep the sound from pacing
  (non-blocking writes while the FIFO has room, wait only when it is nearly full).
- Sound with vsync (commit 1d62071): a ring + a thread of its own writing to /dev/dsp, so only
  the panel paces the game. libpadsp's FIFO is only 2 x 1 KiB (512 frames, GETOSPACE works) and
  audioserver pulls it into its own buffer, so the ring is nearly empty while all is well; the
  game makes 48001-48002 samples a second against 48000 played (the panel runs a hair fast), so
  only one correction is needed: one frame left out when the ring holds > 1600 beyond the FIFO.
  demo1/demo3 paced: 4-5 / 3 frames over 20 ms (RetroArch 117 / 136), frames 16-17 ms. The
  played sound (dsp_tap) equals the core's output sample for sample except 4 frames left out at
  0.17 s (start-up). An earlier try that also repeated frames when the ring ran low repeated
  ~1000 (the ring is empty by design): don't.
- Standalone port (commit 8333b0e, after mr-finster's miyoo64 ports, MIT):
  - Menu button (Onion's keymon does nothing on Menu outside RetroArch/DraStic: MODE_APPS):
    tqrun's own menu over the dimmed last frame, 8x8 public-domain font (font8x8.h): RESUME,
    SAVE STATE / LOAD STATE slots 1-9 (left/right), EXIT GAME (LOAD/EXIT ask ARE YOU SURE?).
    Notice "SAVED SLOT n" over the game for 2 s. (Slots removed again in ce8e06a, see below.)
  - Resume state `saves/quake.state`: written on EXIT GAME, SIGTERM
    (Onion's power-off), POWER press (before Onion's sleep, which may end in a power-off) and once
    after 50 s without a button; tmp + fsync + rename; tied to the core file's contents (FNV-1a:
    a reinstalled same build matches; another build → .rejected). Loaded after the first
    retro_run. If the game can't be saved (demo, dead, intermission) the old resume state is
    removed. Quitting from Quake's own menu removes it too (a deliberate quit starts afresh).
    The core's state is the Quake savegame text padded with zeros: trailing zeros aren't written
    (~42 KB, 30 ms).
  - `miyoo/standalone/card/`: `launch.sh` (lower-cases id1 names; data from its own id1/ or from
    `Quake Collection (TyrQuake)/id1`, where the owner's files are PAK0.PAK — the card's fs is
    case-sensitive here, so the name is looked up; first start copies the RetroArch version's
    savegames/config.cfg; libpadsp only with audioserver; performance governor; exec tqrun),
    `Quake (TyrQuake-Miyoo).port` (Onion launch_standalone.sh, GameDataFile tqrun so it always
    shows), `tyrquake.opt.default` (640x480, 59.67), README.txt. `package.sh` → `build/release/
    TyrQuake-Miyoo-Standalone-<tag>.zip` (sdcard-root: Roms/PORTS/Games/TyrQuake-Miyoo +
    Shortcuts/FPS). Port Manager entry `tyrquake-miyoo` in portmanager-miyoo's catalog.local.json
    (needs a release Port Manager can download: the repo is private).
  - Checked on the device: shortcut start → menu → EXIT GAME (screens fine); bench STATE=save
    (LOAD=s0, SIGTERM) then STATE=load: the same picture after the resume.
  - Installed on the card for the owner (2026-10-22): Games/TyrQuake-Miyoo, Shortcuts/FPS
    (commit 8333b0e's tqrun, menu with save/load slots; the later commits aren't on the card).
- Owner's first play (2026-10-22): sound louder than the RetroArch version (plays at Onion volume
  50 %, the speaker can't take more); save/load state slots are too much: Quake's own saves +
  Onion's Game Switcher are enough; asked for graphics options in the game's menu ("old-school"
  feel); Wi-Fi came on during play (not ours: nothing in tqrun/launch.sh touches Wi-Fi; Onion's
  runtime.sh only kills dropbear/smbd/... for games, the radio stays as set — asked the owner
  whether it was off before).
- Loudness: Onion's retroarch.cfg has audio_volume 0 dB, audio_driver oss through the same
  libpadsp → audioserver path, and tqrun's played sound equals the core's output (dsp_tap), so
  the difference must be elsewhere (Quake's volume/bgmvolume in config.cfg, Onion). To do on the
  device: the same demo with DSPTAP=1 through RetroArch and through tqrun, compare levels (RMS,
  peaks); compare saves/TyrQuake-Miyoo/id1/config.cfg with TyrQuake-Miyoo/saves/id1/config.cfg.
- Menu as in Onion's RetroArch games (commit ce8e06a, not on the card yet):
  - Onion's keymon acts on Menu only for RetroArch (MODE_GAME) and DraStic; for anything else
    (MODE_APPS) it does nothing, so tqrun reads Menu itself.
  - Short press: the resume state, a picture for the switcher, `touch
    /mnt/SDCARD/.tmp_update/.runGameSwitcher`, exit; runtime.sh's check_switcher then runs
    gameSwitcher instead of MainUI. The picture: `Saves/CurrentProfile/romScreens/<hash>.png`,
    hash = Onion's FNV1A_Pippip_Yurii (utils/hash.h, reads 8 bytes past the end: pad) of the
    first `"rompath"` in `Roms/recentlist.json` (or recentlist-hidden.json; type 5/17; a
    "search:" prefix is cut at ':'), 640x480. tqrun writes it as a PNG with stored (uncompressed)
    deflate blocks (~0.9 MB, no zlib). Picking the game in the switcher starts the shortcut
    again → the resume state.
  - Held 0.6 s: the port's menu: RESUME, RESOLUTION (640x480 / 320x240: written to
    tyrquake.opt, the game saves and execv's itself, the resume state brings it back), FPS
    (30 = 29.835, half the panel: the present thread waits one more FBIO_WAITFORVSYNC after the
    pan; 60 = 59.67; UNLIMITED = 200 with real-time steps, no waiting for the panel), EXIT GAME.
    The sound ring stays on in every FPS mode (play mode = started with -V 1).
  - Core (commit 04a657e): framerate 29.835 added (option value and supported list); a changed
    tyrquake_framerate is taken while running when the frontend accepts
    RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO (tqrun does; RetroArch would too), else at the next
    start. Resolution stays start-only (VID re-init would be a renderer rework).
  - Not yet done because the console went offline (Wi-Fi/SSH timeouts at 10:05): pgo.sh +
    shots for the changed core, the menu test through the real shortcut (stest3.sh in the
    session scratchpad), pacing at 30 fps. build/core_pgo was restored to the release core
    7a446a25 (backup build/core_pgo_ra_7a446a25) after the aborted PGO run overwrote it.
- Owner's decision (2026-10-22): **remove the overlay menu entirely**; Menu = Game Switcher only;
  RESOLUTION and FPS go into Quake's own menu as below. Proposal: in Quake's own Options →
  "Video Options" (menu.c has the entry, hidden: vid_menudrawfn/vid_menukeyfn are NULL in the
  libretro build), drawn like the 1996 DOS mode list; the core would hand the choice to the
  frontend with RETRO_ENVIRONMENT_SET_VARIABLE (70) and tqrun would save it (and restart for a
  resolution). Then Menu could be the Game Switcher only and the overlay menu could go.
- Done that way (2026-10-22, owner asked; also asked for an FPS counter there):
  - tqrun: overlay menu, hold logic and font8x8.h removed; Menu (on its release) = Game
    Switcher. SET_VARIABLE: NULL → true (supported); known key → written to tyrquake.opt;
    tyrquake_resolution ≠ the running one → after this retro_run quit = 2 (resume state, execv).
  - Core (libretro.c): Video Options (vid_menudrawfn/vid_menukeyfn, set in VID_Init only when
    SET_VARIABLE is supported, so RetroArch 1.22 shows it too): qplaque + p_option + "Video
    Options", rows at y 48/56/64, values at x 220, cursor at 200 as the Options menu.
    Resolution: left/right choose, A/ENTER applies a changed one ("Press ENTER to apply: the
    game starts again where you left it"). Max FPS 30/60/Unlimited = 29.835/59.67/200: written
    with SET_VARIABLE and taken at once (set_running_framerate → SET_SYSTEM_AV_INFO). Show FPS =
    new archived cvar scr_showfps (config.cfg). M_DrawTransPic exported from menu.c.
  - FPS counter (screen.c SCR_DrawFPS): screen updates per second of the frontend's clock
    (perf interface), refreshed every 0.5 s, "NN fps" top right of the view; hidden in menus.
    With Unlimited it counts the game's frames, not the ones the panel shows (≤ 59.67).
    Off by default; shots need it off (the bench copies the owner's config.cfg).
  - Resolution in RetroArch: written to the core options, taken at the next start.
  - PGO core 4bca0392 (tqrun d42a8717) installed on the card 2026-10-22 (old files and saves in
    workspace backup/20261022-vmenu). Shots: all 59 identical to the release core 7a446a25
    taken the same day (set rel_today). Both differ from td/ref by up to 9 per channel over the
    whole picture: the shots copy the owner's config.cfg, and the owner set gamma 0.8 /
    brightness 0.05 in the morning; td/ref was made with an older config (gamma 1 + brightness
    0 gives 33, brightness 0 alone 20: unknown). Until shots.sh forces fixed picture cvars,
    compare against the release core shot on the same day.
  - Owner checked by hand: START → Options → Video → Video Options works; Unlimited shows over
    100 fps in the 640x480 attract demos (the game's frames; the panel shows ≤ 59.67).
  - Scripted test (sendkeys through the shortcut) didn't open Quake's menu with START during the
    attract demo, though Menu (read by tqrun) worked and the events reach event0 (seen with
    cat): cause not found; check by hand.
  - Game Switcher picture missing after the owner opened Tweaks: recentlist.json's first entry
    was the app (type 3, no rompath) and tqrun read only the first line. Now the first entry
    with a rompath (tqrun 2ff68497).
  - Sound: "left out" grew to ~38 frames a second in these runs (841 in 22 s, 5369 in 100 s;
    earlier runs left out only a few): to look into.
- Next for a standalone port: the 8 bpp → colours pass on the 2D engine too
  (MI_GFX_SetPalette + an I8 surface, ~2 ms of CPU now; pixels must match the shots); mission
  pack shortcuts; a downloadable release for Port Manager (owner's decision: the repo is private).

## Next
Done: profile (r_perf sections), second core for spans, surface caches, the palette pass and
alias models. Every optimisation: shots identical to the one-thread reference (shots.sh +
shots_compare.py) before timing. Now 640x480 avg 13.9 / 13.1 / 14.3 ms (demo1/2/3), p90
15.8-17.2: to bring p90 under 16.76 ms (heaviest first):
- Done since: music decoding on its own thread, 59.67 fps option, Slow Frames real-time steps
  (no clicks when a frame is late), bench SYNC/FPS/VSYNC/REFRESH/LAT/RC/RESAMPLER, CPU per frame.
- Done: 1. PGO (~3 %, installed 2026-10-20), LTO (~4 % more, installed 2026-10-20).
- Pacing: with audio_sync on the frames reach the screen in bursts (12/18/20 ms) whatever we do;
  vsync without audio_sync is even (16.76) but drops samples. Not solved; would need a frontend
  whose audio rate control works with Onion's OSS (or a standalone port).
- Ideas, cheapest first (owner chose to start with 1, 2026-10-19):
  1. Compiler: -O3 per file / for all, PGO (-fprofile-generate on the device with timedemos,
     then -fprofile-use), maybe LTO. Usually 3-8 %, no code changes; shots must stay identical.
  2. Quake's mixing (~0.5 ms) on the second core, overlapped with drawing.
  3. The serial part of the world drawing (BSP walk, edge lists, R_ScanEdges' edge sorting,
     ~1.5-2 ms): the biggest piece still on one core.
  4. ~~32-pixel instead of 16-pixel perspective spans~~: rejected by the owner (2026-10-20): no
     change that alters the picture; every optimisation must keep the shots identical.
  5. Overclocking (last).
- Later: entity alpha (0.71), fence textures (owner postponed).
- To check later (owner, 2026-10-22): Fiends can't follow the player through a low tunnel, they
  keep leaping and bouncing off the ceiling instead of running. Likely id's own QuakeC
  (CheckDemonJump looks only at height difference and range, always leaps at ~100-200 units, not
  at the room overhead); not the Q_rand change (same distribution, the leap there isn't random).
  Our real-time steps / 59.67 fps could still change the flight a little. Plan: same save in that
  tunnel on the stock core vs ours (owner to name the map); a fix would change enemy behaviour,
  so only if the owner wants it.
- Release builds (owner's decision 2026-10-20, not done yet): the PGO profile is collected on
  the Miyoo (pgo.sh) and the .gcda files are committed to the repo; a GitHub Actions build uses
  them (-fprofile-use, -Wno-error=coverage-mismatch), no device or emulator in CI. Refresh the
  profile on the device before a release. (Rejected: collecting it under QEMU in CI.)
- Owner's ideas for later (2026-10-20): try the core on an R36 (Rockchip RK3326, ArkOS); a
  separate branch for a standalone port (Onion's GameSwitcher works only with RetroArch cores;
  mr-finster/miyoo64 gives its standalone games their own menu and save states instead).
- Release worry (owner, 2026-10-20): players with other RetroArch settings may get bad sound.
  Ideas: test Onion's defaults without overrides; the frontend's audio buffer status callback
  (fill level, underrun warning) to add samples or warn when audio_sync is off; README
  recommended settings.
