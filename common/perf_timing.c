/*
 * perf_timing.c -- see perf_timing.h for design notes.
 *
 * Implementation detail: the libretro frontend's
 * retro_perf_callback is fetched lazily on the first
 * timed section call (and once, regardless of success).
 * If the frontend doesn't provide the interface or
 * doesn't populate get_time_usec, the module silently
 * remains in "counters-only" mode -- section_begin /
 * section_end become no-ops and the once-per-second
 * log shows 0.0ms for every section but still reports
 * counters and frame rate (derived from frame count
 * alone is impossible without time, so FPS is also 0.0
 * in that mode -- counters are the meaningful output).
 *
 * All state is single-threaded process-global; libretro
 * cores run retro_run on a single thread by contract,
 * and no other call site for these APIs exists outside
 * that thread.  No locks needed.
 *
 * C89-clean: declarations before statements at every
 * block scope.
 */

#ifdef __linux__
#define _GNU_SOURCE	/* RUSAGE_THREAD */
#include <sys/resource.h>
#endif

#include "perf_timing.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "console.h"
#include "cvar.h"
#include "qtypes.h"

#include <libretro.h>

/* Pulled in from libretro.c.  Set during retro_set_environment
 * before any retro_run / retro_init activity, so it is non-NULL
 * by the time any section_begin can fire. */
extern retro_environment_t environ_cb;

/* The r_perf cvar.  Declared here (not in r_main.c) so the
 * cvar's lifetime is tied to this module.  Registered via
 * perf_timing_register_cvars. */
cvar_t r_perf = { "r_perf", "0" };

static struct retro_perf_callback perf_cb;
static qboolean perf_cb_ready = false;
static qboolean perf_init_attempted = false;

static double section_start_usec[PERF_SECTION_COUNT];
static double section_accum_usec[PERF_SECTION_COUNT];
static uint64_t counter_accum[PERF_COUNTER_COUNT];
static uint32_t frame_count_accum = 0;
static double log_window_start_usec = 0.0;

/* play log (perf_timing_set_log) */
#define PLAY_BINS 256			/* busy-time histogram, 0.25 ms bins */
static FILE *play_log;
static double play_t0_usec;
static struct {
   uint32_t frames, late20, late25, over, realsteps;
   double interval_max, busy_sum, busy_max, wait_sum;
   uint32_t busy_hist[PLAY_BINS];
   int bgm_min_ms;
   unsigned long long samples_start, samples_now;
   char map[32];
} play;
static double play_budget_usec = 1000000.0 / 60.0;
#ifdef __linux__
static double play_cpu_proc, play_cpu_main;	/* usec at the window start */
#endif

#define PERF_ON (r_perf.value != 0.0f || play_log)

static void
ensure_init(void)
{
   if (perf_init_attempted)
      return;
   perf_init_attempted = true;
   memset(&perf_cb, 0, sizeof(perf_cb));
   if (!environ_cb)
      return;
   if (!environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb))
      return;
   if (!perf_cb.get_time_usec)
      return;
   perf_cb_ready = true;
}

double
perf_timing_now_usec(void)
{
   ensure_init();
   if (!perf_cb_ready)
      return 0.0;
   return (double)perf_cb.get_time_usec();
}

#ifdef __linux__
static double
cpu_usec(int who)
{
   struct rusage ru;
   if (getrusage(who, &ru) != 0)
      return 0.0;
   return (ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000000.0
      + ru.ru_utime.tv_usec + ru.ru_stime.tv_usec;
}
#endif

/* first number in a sysfs file, -1 if there is none */
static long
read_sys(const char *path)
{
   /* by hand: with _GNU_SOURCE fscanf/strtol bind the __isoc23_ versions (glibc 2.38) */
   char line[32];
   const char *p = line;
   long v = -1;
   FILE *f = fopen(path, "r");
   if (!f)
      return -1;
   if (fgets(line, sizeof(line), f))
   {
      while (*p && (*p < '0' || *p > '9'))	/* "Temp=60" */
         p++;
      for (; *p >= '0' && *p <= '9'; p++)
         v = (v < 0 ? 0 : v * 10) + (*p - '0');
   }
   fclose(f);
   return v;
}

static void
play_reset(void)
{
   unsigned long long s = play.samples_now;
   memset(&play, 0, sizeof(play));
   play.samples_start = play.samples_now = s;
   play.bgm_min_ms = -1;
#ifdef __linux__
   play_cpu_proc = cpu_usec(RUSAGE_SELF);
   play_cpu_main = cpu_usec(RUSAGE_THREAD);
#endif
}

void
perf_timing_set_log(const char *path, double fps)
{
   if (fps > 0.0)
      play_budget_usec = 1000000.0 / fps;
   if (play_log && path)
      return;
   if (play_log)
   {
      fclose(play_log);
      play_log = NULL;
      return;
   }
   if (!path)
      return;
   play_log = fopen(path, "a");
   if (!play_log)
   {
      Con_Printf("Performance log: can't open %s\n", path);
      return;
   }
   {
      char date[32];
      time_t t = time(NULL);
      strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&t));
      fprintf(play_log, "# session %s, core " GIT_VERSION ", %.2f fps budget %.2f ms\n",
              date, fps, play_budget_usec / 1000.0);
   }
   /* per second: frame counts, wall time between frames, the frame's own
    * work (busy = wall minus the wait in audio_batch_cb) and its 90th
    * percentile, sound, music, CPU (ms per frame), the r_perf sections
    * (ms per frame), CPU clock and temperature */
   fprintf(play_log, "t,map,frames,fps,late20,late25,interval_max,busy_avg,busy_p90,busy_max,over,"
           "realsteps,audio_wait,samples_s,bgm_dry,bgm_min_ms,mix_short,clip,cpu_main,cpu_proc,"
           "render,view3d,hud2d,edges,ents,vidupd,audio,bgm,audio_cb,mhz,temp\n");
   fflush(play_log);
   play_t0_usec = 0.0;
   play_reset();
   log_window_start_usec = 0.0;
}

void
perf_timing_frame(double interval_usec, double busy_usec, double audio_wait_usec,
                  int realtime_step, unsigned long long samples_total, const char *map)
{
   int bin;

   if (!play_log)
      return;
   play.samples_now = samples_total;
   if (play_t0_usec == 0.0)	/* the first frame has no interval */
   {
      play_t0_usec = perf_timing_now_usec();
      play.samples_start = samples_total;
      return;
   }
   play.frames++;
   if (interval_usec > 20000.0)
      play.late20++;
   if (interval_usec > 25000.0)
      play.late25++;
   if (interval_usec > play.interval_max)
      play.interval_max = interval_usec;
   play.busy_sum += busy_usec;
   if (busy_usec > play.busy_max)
      play.busy_max = busy_usec;
   if (busy_usec > play_budget_usec)
      play.over++;
   bin = (int)(busy_usec / 250.0);
   play.busy_hist[bin < 0 ? 0 : bin >= PLAY_BINS ? PLAY_BINS - 1 : bin]++;
   play.wait_sum += audio_wait_usec;
   if (realtime_step)
      play.realsteps++;
   if (map)
      snprintf(play.map, sizeof(play.map), "%s", map);
}

void
perf_timing_bgm_fill(int ms)
{
   if (play_log && (play.bgm_min_ms < 0 || ms < play.bgm_min_ms))
      play.bgm_min_ms = ms;
}

/* CPU temperature in C: the Miyoo's SigmaStar driver, else the usual thermal zone */
static double
cpu_temp(void)
{
   long v = read_sys("/sys/devices/system/cpu/cpufreq/temp_out");
   if (v >= 0)
      return (double)v;
   v = read_sys("/sys/class/thermal/thermal_zone0/temp");
   return v >= 0 ? v / 1000.0 : -1.0;
}

static void
play_write(double now, double window_elapsed, uint32_t fc)
{
   uint32_t n = play.frames, acc = 0;
   double p90 = 0.0, cpu_proc = 0.0, cpu_main = 0.0, secs = window_elapsed / 1000000.0;
   int i;

   if (!n)
      return;
   for (i = 0; i < PLAY_BINS; i++)
   {
      acc += play.busy_hist[i];
      if (acc * 10 >= n * 9)
      {
         p90 = (i + 1) * 0.25;
         break;
      }
   }
#ifdef __linux__
   cpu_proc = (cpu_usec(RUSAGE_SELF) - play_cpu_proc) / 1000.0 / n;
   cpu_main = (cpu_usec(RUSAGE_THREAD) - play_cpu_main) / 1000.0 / n;
#endif
#define MS(s) (section_accum_usec[PERF_SECTION_##s] / 1000.0 / (double)fc)
   fprintf(play_log, "%.1f,%s,%u,%.2f,%u,%u,%.1f,%.2f,%.2f,%.1f,%u,%u,%.2f,%.0f,%u,%d,%u,%u,%.2f,%.2f,"
           "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%ld,%.1f\n",
           (now - play_t0_usec) / 1000000.0, play.map, n, n / secs, play.late20, play.late25,
           play.interval_max / 1000.0, play.busy_sum / 1000.0 / n, p90, play.busy_max / 1000.0,
           play.over, play.realsteps, play.wait_sum / 1000.0 / n,
           (play.samples_now - play.samples_start) / secs,
           (unsigned)counter_accum[PERF_COUNTER_BGM_DRY], play.bgm_min_ms,
           (unsigned)counter_accum[PERF_COUNTER_MIX_SHORT],
           (unsigned)counter_accum[PERF_COUNTER_MIX_CLIP], cpu_main, cpu_proc,
           MS(RENDERVIEW), MS(HOST_RV), MS(HOST_2D), MS(SW_EDGEDRAW), MS(SW_ENTS), MS(SW_VIDUPD),
           MS(AUDIO), MS(AUDIO_BGM), MS(AUDIO_CB),
           read_sys("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq") / 1000,
           cpu_temp());
#undef MS
   fflush(play_log);
   play_reset();
}

void
perf_timing_register_cvars(void)
{
   Cvar_RegisterVariable(&r_perf);
}

void
perf_timing_section_begin(int section_id)
{
   if (!PERF_ON)
      return;
   ensure_init();
   if (!perf_cb_ready)
      return;
   if (section_id < 0 || section_id >= PERF_SECTION_COUNT)
      return;
   section_start_usec[section_id] = (double)perf_cb.get_time_usec();
}

void
perf_timing_section_end(int section_id)
{
   double now;
   if (!PERF_ON)
      return;
   if (!perf_cb_ready)
      return;
   if (section_id < 0 || section_id >= PERF_SECTION_COUNT)
      return;
   now = (double)perf_cb.get_time_usec();
   section_accum_usec[section_id] += (now - section_start_usec[section_id]);
}

void
perf_timing_counter_add(int counter_id, uint64_t n)
{
   if (!PERF_ON)
      return;
   if (counter_id < 0 || counter_id >= PERF_COUNTER_COUNT)
      return;
   counter_accum[counter_id] += n;
}

void
perf_timing_end_frame(void)
{
   double now;
   double window_elapsed;
   double window_ms;
   double fps;
   double frame_ms_avg;
   double render_ms_avg;
   double host_rv_ms_avg;
   double host_2d_ms_avg;
   double host_pt_ms_avg;
   double upload_vid_ms_avg;
   double upload_zbuf_ms_avg;
   double audio_ms_avg;
   double record_ms_avg;
   double submit_ms_avg;
   double qsubmit_ms_avg;
   double bwait_ms_avg;
   double setimg_ms_avg;
   double vidcb_ms_avg;
   uint32_t fc;
   int i;

   if (!PERF_ON)
      return;
   ensure_init();
   if (!perf_cb_ready) {
      /* No timer.  Bump frame count so counters still
       * average correctly when we hit some other trigger
       * later -- but we never will without a clock.  In
       * practice this path is dead; the libretro frontend
       * always supplies get_time_usec.  Keeping the bump
       * out is fine. */
      return;
   }

   frame_count_accum++;
   now = (double)perf_cb.get_time_usec();

   if (log_window_start_usec == 0.0) {
      log_window_start_usec = now;
      if (play_log)
         play_reset();
      return;
   }

   window_elapsed = now - log_window_start_usec;
   if (window_elapsed < 1000000.0)
      return;

   fc = frame_count_accum;
   if (play_log)
      play_write(now, window_elapsed, fc);
   if (r_perf.value == 0.0f)
      goto reset;	/* only the play log is on */
   window_ms      = window_elapsed / 1000.0;
   fps            = (double)fc / (window_elapsed / 1000000.0);
   frame_ms_avg   = section_accum_usec[PERF_SECTION_FRAME]          / 1000.0 / (double)fc;
   render_ms_avg  = section_accum_usec[PERF_SECTION_RENDERVIEW]     / 1000.0 / (double)fc;
   host_rv_ms_avg = section_accum_usec[PERF_SECTION_HOST_RV]        / 1000.0 / (double)fc;
   host_2d_ms_avg = section_accum_usec[PERF_SECTION_HOST_2D]        / 1000.0 / (double)fc;
   host_pt_ms_avg = section_accum_usec[PERF_SECTION_HOST_PT]        / 1000.0 / (double)fc;
   upload_vid_ms_avg  = section_accum_usec[PERF_SECTION_UPLOAD_VID]  / 1000.0 / (double)fc;
   upload_zbuf_ms_avg = section_accum_usec[PERF_SECTION_UPLOAD_ZBUF] / 1000.0 / (double)fc;
   audio_ms_avg       = section_accum_usec[PERF_SECTION_AUDIO]       / 1000.0 / (double)fc;
   record_ms_avg  = section_accum_usec[PERF_SECTION_RECORD_FRAME]   / 1000.0 / (double)fc;
   submit_ms_avg  = section_accum_usec[PERF_SECTION_SUBMIT_PRESENT] / 1000.0 / (double)fc;
   qsubmit_ms_avg = section_accum_usec[PERF_SECTION_QUEUE_SUBMIT]      / 1000.0 / (double)fc;
   bwait_ms_avg   = section_accum_usec[PERF_SECTION_BEGIN_FRAME_WAIT]  / 1000.0 / (double)fc;
   setimg_ms_avg  = section_accum_usec[PERF_SECTION_SET_IMAGE]       / 1000.0 / (double)fc;
   vidcb_ms_avg   = section_accum_usec[PERF_SECTION_VIDEO_CB]        / 1000.0 / (double)fc;

   Con_Printf("PERF [%u frames %.1fms]: %.1f FPS (%.2fms) | "
              "frame %.2f render %.2f (rv %.2f 2d %.2f pt %.2f) "
              "upload (vb %.2f zb %.2f) "
              "record %.2f submit %.2f "
              "(qs %.2f si %.2f vc %.2f) audio %.2f bw %.2f | "
              "brush=%llu turb=%llu alias=%llu particles=%llu "
              "sprites=%llu dispatches=%llu vidbuf=%llu zbufup=%llu\n",
              (unsigned)fc, window_ms,
              fps, frame_ms_avg,
              frame_ms_avg, render_ms_avg,
              host_rv_ms_avg, host_2d_ms_avg, host_pt_ms_avg,
              upload_vid_ms_avg, upload_zbuf_ms_avg,
              record_ms_avg, submit_ms_avg,
              qsubmit_ms_avg, setimg_ms_avg, vidcb_ms_avg,
              audio_ms_avg, bwait_ms_avg,
              (unsigned long long)(counter_accum[PERF_COUNTER_BRUSH_SURFACES]   / fc),
              (unsigned long long)(counter_accum[PERF_COUNTER_TURB_SURFACES]    / fc),
              (unsigned long long)(counter_accum[PERF_COUNTER_ALIAS_ENTITIES]   / fc),
              (unsigned long long)(counter_accum[PERF_COUNTER_PARTICLES]        / fc),
              (unsigned long long)(counter_accum[PERF_COUNTER_SPRITES]          / fc),
              (unsigned long long)(counter_accum[PERF_COUNTER_DISPATCHES]       / fc),
              (unsigned long long)(counter_accum[PERF_COUNTER_VID_BUFFER_BYTES] / fc),
              (unsigned long long)(counter_accum[PERF_COUNTER_ZBUF_UPLOAD_BYTES]/ fc));

#define SW_MS(s) (section_accum_usec[PERF_SECTION_SW_##s] / 1000.0 / (double)fc)
   Con_Printf("SW: edgedraw %.2f (scan %.2f (dsurf %.2f (cache %.2f))) ents %.2f (poly %.2f) vidupd %.2f (pal %.2f) audio: bgm %.2f cb %.2f\n",
              SW_MS(EDGEDRAW), SW_MS(SCAN), SW_MS(DSURF), SW_MS(CACHE), SW_MS(ENTS), SW_MS(POLY),
              SW_MS(VIDUPD), SW_MS(PAL),
              section_accum_usec[PERF_SECTION_AUDIO_BGM] / 1000.0 / (double)fc,
              section_accum_usec[PERF_SECTION_AUDIO_CB] / 1000.0 / (double)fc);
#undef SW_MS

reset:
   for (i = 0; i < PERF_SECTION_COUNT; i++)
      section_accum_usec[i] = 0.0;
   for (i = 0; i < PERF_COUNTER_COUNT; i++)
      counter_accum[i] = 0;
   frame_count_accum = 0;
   log_window_start_usec = now;
}
