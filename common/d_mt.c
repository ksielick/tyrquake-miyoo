#define _GNU_SOURCE /* cpu_set_t, sched_setaffinity */
/*
 * d_mt -- two-thread span drawing for the software renderer (see d_mt.h)
 */

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "console.h"
#include "cvar.h"
#include "d_local.h"
#include "d_mt.h"
#include "quakedef.h"
#include "r_local.h"
#include "d_iface.h"
#include "perf_timing.h"

/* 0 = draw everything on the calling thread (the original renderer) */
cvar_t r_threads = { "r_threads", "1", true };

D_TLS int d_band_v0 = 0, d_band_v1 = 0x7fffffff;

/* everything a recorded call needs from the drawing state */
typedef struct {
   int op, color;
   espan_t *spans;
   float sdivzstepu, tdivzstepu, zistepu;
   float sdivzstepv, tdivzstepv, zistepv;
   float sdivzorigin, tdivzorigin, ziorigin;
   fixed16_t sadjust, tadjust, bbextents, bbextentt;
   pixel_t *cacheblock;
   float cache_mipscale;
   int cachewidth, turb_texheight;
   int turb_alpha, turb_blendmode, turb_ztest;
} d_drawcmd_t;

static d_drawcmd_t *cmds;
static int numcmds, maxcmds;

/* surface caches to build (D_CacheSurface allocated them while recording);
 * both threads take jobs until none are left, then wait for all of them,
 * since a band's spans may use a cache the other thread built */
static drawsurf_t *jobs;
static int numjobs, maxjobs;
static volatile int job_next, jobs_done;

/* worker thread */
static pthread_t worker;
static qboolean worker_running;
static pthread_mutex_t job_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t job_cond = PTHREAD_COND_INITIALIZER;
static volatile int job_seq, done_seq;
static volatile qboolean worker_quit;
static int job_v0, job_v1;
static double worker_ms;
/* D_MT_RunBoth's job, instead of caches + spans when set */
static void (*task_fn)(int part, void *arg);
static void *task_arg;

/* share of the rows drawn by the calling thread, per flush of a frame
 * (the flushes of a frame cover different parts of the screen) */
#define MAX_SPLIT_SLOTS 8
static float split[MAX_SPLIT_SLOTS];
static int flush_slot;

static double now_ms(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static void replay(int v0, int v1)
{
   int i;

   d_band_v0 = v0;
   d_band_v1 = v1;
   for (i = 0; i < numcmds; i++) {
      const d_drawcmd_t *c = &cmds[i];
      d_sdivzstepu = c->sdivzstepu;
      d_tdivzstepu = c->tdivzstepu;
      d_zistepu = c->zistepu;
      d_sdivzstepv = c->sdivzstepv;
      d_tdivzstepv = c->tdivzstepv;
      d_zistepv = c->zistepv;
      d_sdivzorigin = c->sdivzorigin;
      d_tdivzorigin = c->tdivzorigin;
      d_ziorigin = c->ziorigin;
      sadjust = c->sadjust;
      tadjust = c->tadjust;
      bbextents = c->bbextents;
      bbextentt = c->bbextentt;
      cacheblock = c->cacheblock;
      cachewidth = c->cachewidth;
      d_cache_mipscale = c->cache_mipscale;
      r_turb_texheight = c->turb_texheight;
      r_turb_alpha = c->turb_alpha;
      r_turb_blendmode = c->turb_blendmode;
      r_turb_ztest = c->turb_ztest;
      D_MT_Exec(c->op, c->spans, c->color);
   }
   d_band_v0 = 0;
   d_band_v1 = 0x7fffffff;
}

static void build_caches(void)
{
   int i, spins = 0;

   while ((i = __sync_fetch_and_add(&job_next, 1)) < numjobs) {
      r_drawsurf = jobs[i];
      R_DrawSurface();
      __sync_fetch_and_add(&jobs_done, 1);
   }
   while (jobs_done < numjobs) {
      if (++spins > 2000)
         sched_yield();
   }
   __sync_synchronize();
}

void D_MT_RecordCacheBuild(void)
{
   if (numjobs == maxjobs) {
      int n = maxjobs ? maxjobs * 2 : 256;
      drawsurf_t *p = realloc(jobs, n * sizeof(*jobs));
      if (!p) {
         R_DrawSurface();	/* out of memory: build it now */
         return;
      }
      jobs = p;
      maxjobs = n;
   }
   jobs[numjobs++] = r_drawsurf;
}

static void *worker_main(void *arg)
{
   int seen = 0;
   (void)arg;

   R_SurfWorkerInit();

#ifdef __linux__
   {
      /* the calling thread mostly runs on core 0 */
      cpu_set_t set;
      CPU_ZERO(&set);
      CPU_SET(1, &set);
      sched_setaffinity(0, sizeof(set), &set);
   }
#endif

   for (;;) {
      int spins = 0;
      double t0;

      /* spin briefly (the next flush of a frame comes soon), then sleep
       * so an idle worker costs no CPU between frames */
      while (job_seq == seen && !worker_quit) {
         if (++spins < 2000)
            continue;
         pthread_mutex_lock(&job_lock);
         while (job_seq == seen && !worker_quit)
            pthread_cond_wait(&job_cond, &job_lock);
         pthread_mutex_unlock(&job_lock);
      }
      if (worker_quit)
         break;
      seen = job_seq;
      __sync_synchronize();

      if (task_fn) {
         task_fn(1, task_arg);
      } else {
         build_caches();
         t0 = now_ms();
         replay(job_v0, job_v1);
         worker_ms = now_ms() - t0;
      }

      __sync_synchronize();
      done_seq = seen;
   }
   return NULL;
}

static qboolean start_worker(void)
{
   if (worker_running)
      return true;
   worker_quit = false;
   job_seq = done_seq = 0;
   if (pthread_create(&worker, NULL, worker_main, NULL) != 0) {
      Con_Printf("r_threads: can't start the drawing thread\n");
      Cvar_SetValue("r_threads", 0);
      return false;
   }
   worker_running = true;
   return true;
}

void D_MT_Shutdown(void)
{
   if (!worker_running)
      return;
   pthread_mutex_lock(&job_lock);
   worker_quit = true;
   pthread_cond_broadcast(&job_cond);
   pthread_mutex_unlock(&job_lock);
   pthread_join(worker, NULL);
   worker_running = false;
}

qboolean D_MT_Active(void)
{
   if (!r_threads.value) {
      D_MT_Shutdown();
      return false;
   }
   return start_worker();
}

void D_MT_Record(int op, espan_t *spans, int color)
{
   d_drawcmd_t *c;

   if (numcmds == maxcmds) {
      int n = maxcmds ? maxcmds * 2 : 1024;
      d_drawcmd_t *p = realloc(cmds, n * sizeof(*cmds));
      if (!p) {
         /* out of memory: draw it now, single-threaded */
         D_MT_Exec(op, spans, color);
         return;
      }
      cmds = p;
      maxcmds = n;
   }
   c = &cmds[numcmds++];
   c->op = op;
   c->color = color;
   c->spans = spans;
   c->sdivzstepu = d_sdivzstepu;
   c->tdivzstepu = d_tdivzstepu;
   c->zistepu = d_zistepu;
   c->sdivzstepv = d_sdivzstepv;
   c->tdivzstepv = d_tdivzstepv;
   c->zistepv = d_zistepv;
   c->sdivzorigin = d_sdivzorigin;
   c->tdivzorigin = d_tdivzorigin;
   c->ziorigin = d_ziorigin;
   c->sadjust = sadjust;
   c->tadjust = tadjust;
   c->bbextents = bbextents;
   c->bbextentt = bbextentt;
   c->cacheblock = cacheblock;
   c->cachewidth = cachewidth;
   c->cache_mipscale = d_cache_mipscale;
   c->turb_texheight = r_turb_texheight;
   c->turb_alpha = r_turb_alpha;
   c->turb_blendmode = r_turb_blendmode;
   c->turb_ztest = r_turb_ztest;
}

void D_MT_FrameStart(void)
{
   flush_slot = 0;
}

void D_MT_Flush(int v0, int v1)
{
   int slot, mid, spins;
   float f, fi, cm, cw;
   double t0, main_ms;

   if (!numcmds && !numjobs)
      return;

   slot = flush_slot < MAX_SPLIT_SLOTS ? flush_slot : MAX_SPLIT_SLOTS - 1;
   flush_slot++;
   f = split[slot];
   if (f <= 0.0f)
      f = 0.5f;
   mid = v0 + (int)((v1 - v0) * f + 0.5f);

   /* worker takes the lower rows */
   job_v0 = mid;
   job_v1 = v1;
   job_next = 0;
   jobs_done = 0;
   __sync_synchronize();
   pthread_mutex_lock(&job_lock);
   job_seq++;
   pthread_cond_signal(&job_cond);
   pthread_mutex_unlock(&job_lock);

   perf_timing_section_begin(PERF_SECTION_SW_CACHE);
   build_caches();
   perf_timing_section_end(PERF_SECTION_SW_CACHE);
   t0 = now_ms();
   replay(v0, mid);
   main_ms = now_ms() - t0;

   /* the worker's part is about as long: spin, yielding so a preempted
    * worker gets the core back */
   spins = 0;
   while (done_seq != job_seq) {
      if (++spins > 2000)
         sched_yield();
   }
   __sync_synchronize();
   numcmds = 0;
   numjobs = 0;
   d_sc_alloc_since_flush = 0;

   /* move the split towards equal drawing times (per-row costs of the
    * two parts from this flush) */
   if (main_ms > 0.05 && worker_ms > 0.05 && f > 0.0f && f < 1.0f) {
      cm = (float)(main_ms / f);
      cw = (float)(worker_ms / (1.0f - f));
      fi = cw / (cm + cw);
      f = 0.75f * f + 0.25f * fi;
      if (f < 0.1f)
         f = 0.1f;
      else if (f > 0.9f)
         f = 0.9f;
   }
   split[slot] = f;
}

qboolean D_MT_RunBoth(void (*fn)(int part, void *arg), void *arg)
{
   int spins = 0;

   if (!r_threads.value || !worker_running)
      return false;

   task_fn = fn;
   task_arg = arg;
   __sync_synchronize();
   pthread_mutex_lock(&job_lock);
   job_seq++;
   pthread_cond_signal(&job_cond);
   pthread_mutex_unlock(&job_lock);

   fn(0, arg);

   while (done_seq != job_seq) {
      if (++spins > 2000)
         sched_yield();
   }
   __sync_synchronize();
   task_fn = NULL;
   return true;
}

void D_MT_RegisterCvars(void)
{
   Cvar_RegisterVariable(&r_threads);
}
