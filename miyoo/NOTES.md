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
- `G:\Emulacja\tyrquake-miyoo\repos\tyrquake` — clone of github.com/libretro/tyrquake, branch `miyoo`
  (base: upstream master 67db338, 2026-10-12).
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
- GitHub: private https://github.com/ksielick/tyrquake-miyoo, remote `miyoo`, branch `miyoo`.
  Push with `git push miyoo refs/heads/miyoo:refs/heads/miyoo` (plain `miyoo miyoo` is ambiguous).
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
- `RetroArch/.retroarch/cores/tyrquake_miyoo_libretro.so` (md5 26a57e5b, commit 611d543; replaced
  2026-10-19 by md5 c39cf8a5 = commit cb36289, caches built on both threads; version
  string; then by md5 43f87cab = commit 1e7f291, savegame fix + lerp on by default; then by md5
  4e3da96e = commit 00c76b0, save states, with the .info saying savestate = "true"; then by md5
  fc33ab4d = commit fc14e41, .port as content, `cores/core_info.cache` deleted, shortcut replaced by
  `miyoo/card/1996 Quake.port`, previous one in workspace `backup/20261019-gameswitcher/`; then
  by md5 1cec45bf = commit 3f56ebe: resume without console flash, palette on both threads, Q_rand,
  dither fix; the shortcut on the card is still the one without the core_info.cache refresh) and `.info` ("Quake (TyrQuake-Miyoo)", also copied to `core_info/`); the stock
  `tyrquake_libretro.so` is untouched.
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

## Next
Done: profile (r_perf sections), second core for spans, surface caches, the palette pass and
alias models. Every optimisation: shots identical to the one-thread reference (shots.sh +
shots_compare.py) before timing. Now 640x480 avg 13.9 / 13.1 / 14.3 ms (demo1/2/3), p90
15.8-17.2: to bring p90 under 16.76 ms (heaviest first):
- Audio (~2.1 ms): mixing + OGG decoding; check what the music costs alone.
- Core framerate override 59.67 to match the panel (each frame then gets 16.76 ms of game time).
- Later: entity alpha (0.71), fence textures (owner postponed).
