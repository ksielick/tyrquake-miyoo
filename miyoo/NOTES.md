# TyrQuake (libretro) on the Miyoo Mini Plus — notes

Goal: Quake through the TyrQuake libretro core in Onion's RetroArch runs poorly at the native
640x480; make it run well there (stock 1200 MHz first; second core and NEON before overclocking).

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
- `tyrquake-0.71/` in the workspace: standalone TyrQuake 0.71 (Kevin Shanahan, 2022), reference only.

## Device facts
- Onion RetroArch 1.22.2 (`retroarch_miyoo354` bind-mounted as `retroarch`), video `sdl_dingux` via
  MI_GFX, vsync off, audio_sync on (paces the game), `load_dummy_on_core_shutdown` on.
- Stock core: `RetroArch/.retroarch/cores/tyrquake_libretro.so`, "v0.62", md5 f2d8ce31...
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

## Next
- Profile where the 20 ms at 640x480 goes (r_perf sections: 3D view vs 2D/HUD vs palette/output vs
  sound), then: second core (split the screen like dethrace/REDRIVER2), NEON for spans/palette pass.
