#define _GNU_SOURCE /* RUSAGE_THREAD */
/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "compat/strl.h"

#include "client.h"
#include "cmd.h"
#include "console.h"
#include "host.h"
#include "net.h"
#include "protocol.h"
#include "quakedef.h"
#include "sys.h"
#include "zone.h"

#include "perf_timing.h"

#include <libretro.h>
#include <streams/file_stream.h>

extern retro_environment_t environ_cb;

/* forward declarations */
int rfclose(RFILE* stream);
int64_t rfread(void* buffer,
   size_t elem_size, size_t elem_count, RFILE* stream);
int64_t rfflush(RFILE * stream);
int64_t rfwrite(void const* buffer,
   size_t elem_size, size_t elem_count, RFILE* stream);
int rfprintf(RFILE * stream, const char * format, ...);
int rfgetc(RFILE* stream);

static void CL_FinishTimeDemo(void);

/* realtime advances a fixed 1/framerate per retro_run, so timedemo's own
 * numbers only count frames; these track the frontend's wall clock. */
cvar_t timedemo_quit = { "timedemo_quit", "0" };
/* timedemo_shots N: every Nth timedemo frame, VID_Update saves the frame the
 * frontend gets as <timedemo_shotdir>/NNNNN.rgb565.  A timedemo replays the
 * same frames, so the shots of two builds compare pixel for pixel
 * (miyoo/tools/shots.sh).  Not for timing runs: saving costs time. */
cvar_t timedemo_shots = { "timedemo_shots", "0" };
cvar_t timedemo_shotdir = { "timedemo_shotdir", "/tmp/tqshots" };
int td_shot_frame = -1;
#define TD_MAX_FRAMES 16384
static float td_frame_ms[TD_MAX_FRAMES];
static int td_wall_frames;
static double td_wall_start, td_wall_last;

/* CPU time used during the timedemo (the whole process: our threads and
 * RetroArch's; and the main thread alone), for runs paced by audio_sync,
 * where the wall time per frame is the refresh rate whatever the load */
#if defined(__linux__)
#include <sys/resource.h>
#define TD_CPU 1
static double td_cpu_proc, td_cpu_main;

static void CL_TimeDemoCpu(double *proc, double *main_thread)
{
   struct rusage ru;
   getrusage(RUSAGE_SELF, &ru);
   *proc = ru.ru_utime.tv_sec * 1000.0 + ru.ru_utime.tv_usec / 1000.0 +
           ru.ru_stime.tv_sec * 1000.0 + ru.ru_stime.tv_usec / 1000.0;
   getrusage(RUSAGE_THREAD, &ru);
   *main_thread = ru.ru_utime.tv_sec * 1000.0 + ru.ru_utime.tv_usec / 1000.0 +
           ru.ru_stime.tv_sec * 1000.0 + ru.ru_stime.tv_usec / 1000.0;
}
#endif

static void CL_TimeDemoWallFrame(void)
{
   double now = perf_timing_now_usec();

   if (host_framecount == cls.td_startframe + 1)
   {
      td_wall_start = td_wall_last = now;
      td_wall_frames = 0;
#ifdef TD_CPU
      CL_TimeDemoCpu(&td_cpu_proc, &td_cpu_main);
#endif
      return;
   }
   if (host_framecount <= cls.td_startframe + 1)
      return;
   if (td_wall_frames < TD_MAX_FRAMES)
      td_frame_ms[td_wall_frames] = (float)((now - td_wall_last) / 1000.0);
   td_wall_frames++;
   td_wall_last = now;
   if (timedemo_shots.value >= 1 && td_wall_frames % (int)timedemo_shots.value == 0)
      td_shot_frame = td_wall_frames;
}

static int CL_CompareFloat(const void *a, const void *b)
{
   float fa = *(const float *)a, fb = *(const float *)b;
   return (fa > fb) - (fa < fb);
}

/*
==============================================================================

DEMO CODE

When a demo is playing back, all NET_SendMessages are skipped, and
NET_GetMessages are read from the demo file.

Whenever cl.time gets past the last received message, another message is
read from the demo file.
==============================================================================
*/

/*
==============
CL_StopPlayback

Called when a demo file runs out, or the user starts a game
==============
*/
void
CL_StopPlayback(void)
{
    if (!cls.demoplayback)
	return;

    rfclose(cls.demofile);
    cls.demoplayback = false;
    cls.demofile     = NULL;
    cls.state        = ca_disconnected;

    if (cls.timedemo)
	CL_FinishTimeDemo();
}

/*
====================
CL_GetMessage

Handles recording and playback of demos, on top of NET_ code
====================
*/
int
CL_GetMessage(void)
{
   int r;

   if (cls.demoplayback)
   {
      int i;
      float f;

      /* decide if it is time to grab the next message */
      /* allways grab until fully connected */
      if (cls.state == ca_active)
      {
         if (cls.timedemo)
         {
            if (host_framecount == cls.td_lastframe)
               return 0;	/* allready read this frame's message */

            cls.td_lastframe = host_framecount;

            /* if this is the second frame, grab the real td_starttime */
            /* so the bogus time on the first frame doesn't count */
            if (host_framecount == cls.td_startframe + 1)
               cls.td_starttime = realtime;
            CL_TimeDemoWallFrame();
         }
         else if (cl.time <= cl.mtime[0])
         {
            /* don't need another message yet */
            return 0;
         }
      }
      /* get the next message */
      rfread(&net_message.cursize, 4, 1, cls.demofile);
      VectorCopy(cl.mviewangles[0], cl.mviewangles[1]);

      for (i = 0; i < 3; i++) {
         rfread(&f, 4, 1, cls.demofile);
         cl.mviewangles[0][i] = LittleFloat(f);
         /* Demo viewangles are loaded raw from the file (bit
          * pattern preserved). A malformed or hostile demo can
          * land NaN/Inf here, which then flows through
          * CL_RelinkEntities's lerp:
          *   d = cl.mviewangles[0][j] - cl.mviewangles[1][j];
          *   cl.viewangles[j] = cl.mviewangles[1][j] + frac * d;
          * NaN propagates into cl.viewangles, feeds AngleVectors
          * and the screen transforms, and the rasterizer takes
          * non-finite inputs the way the MSG_ReadFloat audit
          * already documented. Clamp to 0 -- losing one frame
          * of view-angle continuity from a broken demo is the
          * least-bad recovery. */
         if (IS_NAN(cl.mviewangles[0][i]))
            cl.mviewangles[0][i] = 0.0f;
      }

      net_message.cursize = LittleLong(net_message.cursize);

      if (net_message.cursize > MAX_MSGLEN)
         Sys_Error("Demo message > MAX_MSGLEN");
      /* Defensive: a corrupt or hostile demo can put a negative
       * cursize on the wire.  Without this check, rfread is
       * called with size = (size_t)cursize -- a negative int
       * cast to size_t becomes a huge value and overflows
       * net_message.data[].  The upper bound above already
       * caught absurd positive values; mirror that for negatives. */
      if (net_message.cursize < 0) {
         CL_StopPlayback();
         return 0;
      }
      r = rfread(net_message.data, net_message.cursize, 1, cls.demofile);
      if (r != 1) {
         CL_StopPlayback();
         return 0;
      }

      return 1;
   }

   while (1)
   {
      r = NET_GetMessage(cls.netcon);

      if (r != 1 && r != 2)
         return r;

      /* discard nop keepalive message */
      if (net_message.cursize == 1 && net_message.data[0] == svc_nop)
         Con_Printf("<-- server to client keepalive\n");
      else
         break;
   }

   return r;
}

/*
====================
CL_PlayDemo_f

play [demoname]
====================
*/
void
CL_PlayDemo_f(void)
{
    char name[256];
    int c;
    qboolean neg = false;

    if (cmd_source != src_command)
	return;

    if (Cmd_Argc() != 2) {
	Con_Printf("play <demoname> : plays a demo\n");
	return;
    }
/**/
/* disconnect from server */
/**/
    CL_Disconnect();

/**/
/* open the demo file */
/**/
    strlcpy(name, Cmd_Argv(1), sizeof(name));
    COM_DefaultExtension(name, sizeof(name), ".dem");

    Con_Printf("Playing demo from %s.\n", name);
    COM_FOpenFile(name, &cls.demofile);
    if (!cls.demofile) {
	Con_Printf("ERROR: couldn't open.\n");
	cls.demonum = -1;	/* stop demo loop */
	return;
    }

    cls.demoplayback = true;
    cls.state = ca_connected;
    cls.forcetrack = 0;

    /* Defensive: bound the header parse.  The original loop has
     * no EOF check (rfgetc returns -1, which is not '\n' so the
     * loop spins forever on a truncated file) and no iteration
     * cap (a hostile demo of solid digits could overflow
     * forcetrack into UB and stall the loader).  Cap the read at
     * a generous fixed count and treat EOF / control characters
     * as terminators. */
    {
	int iter;
	for (iter = 0; iter < 32; iter++) {
	    c = rfgetc(cls.demofile);
	    if (c == EOF || c == '\n' || c == '\0')
		break;
	    if (c == '-') {
		neg = true;
	    } else if (c >= '0' && c <= '9') {
		/* Saturate rather than overflow on absurd input. */
		if (cls.forcetrack < 100000)
		    cls.forcetrack = cls.forcetrack * 10 + (c - '0');
	    }
	    /* silently ignore other characters */
	}
    }

    if (neg)
	cls.forcetrack = -cls.forcetrack;
}

struct stree_root *
CL_Demo_Arg_f(const char *arg)
{
    struct stree_root *root;

    root = (struct stree_root*)Z_Malloc(sizeof(struct stree_root));
    if (root)
    {
    root->entries = 0;
    root->maxlen = 0;
    root->minlen = -1;
    /* root->root = NULL; */
    root->stack = NULL;
	STree_AllocInit();
	COM_ScanDir(root, "", arg, ".dem", true);
    }

    return root;
}

/*
====================
CL_FinishTimeDemo

====================
*/
static void CL_FinishTimeDemo(void)
{
    int frames;
    float time;

    cls.timedemo = false;

/* the first frame didn't count */
    frames = (host_framecount - cls.td_startframe) - 1;
    time = realtime - cls.td_starttime;
    if (!time)
	time = 1;
    Con_Printf("%i frames %5.1f seconds %5.1f fps\n", frames, time,
	       frames / time);

    if (td_wall_frames > 0 && td_wall_last > td_wall_start) {
	int n = td_wall_frames < TD_MAX_FRAMES ? td_wall_frames : TD_MAX_FRAMES;
	double wall = (td_wall_last - td_wall_start) / 1000000.0;

	if (n >= 360) {
	    /* the pacing: frames 300-359 in order (bursts show here) */
	    char line[60 * 4 + 1];
	    int i, len = 0;
	    for (i = 300; i < 360; i++)
		len += snprintf(line + len, sizeof(line) - len, " %.0f",
				td_frame_ms[i]);
	    Con_Printf("timedemo frames 300-359 ms:%s\n", line);
	}
	qsort(td_frame_ms, n, sizeof(td_frame_ms[0]), CL_CompareFloat);
	Con_Printf("timedemo wall: %i frames %.3f s %.2f fps, frame ms "
		   "avg %.2f median %.2f p90 %.2f p99 %.2f max %.2f\n",
		   td_wall_frames, wall, td_wall_frames / wall,
		   wall * 1000.0 / td_wall_frames, td_frame_ms[n / 2],
		   td_frame_ms[n * 9 / 10], td_frame_ms[n * 99 / 100],
		   td_frame_ms[n - 1]);
	{
	    /* with vsync a frame over ~1.5 refreshes missed one */
	    int i, over20 = 0, over25 = 0;
	    for (i = 0; i < n; i++) {
		if (td_frame_ms[i] > 20.0f)
		    over20++;
		if (td_frame_ms[i] > 25.0f)
		    over25++;
	    }
	    Con_Printf("timedemo late: %i frames over 20 ms, %i over 25 ms\n",
		       over20, over25);
	}
#ifdef TD_CPU
	{
	    double proc, main_thread;
	    CL_TimeDemoCpu(&proc, &main_thread);
	    Con_Printf("timedemo cpu: process %.2f ms/frame (%.0f %% of a core), "
		       "main thread %.2f ms/frame, other threads %.2f\n",
		       (proc - td_cpu_proc) / td_wall_frames,
		       (proc - td_cpu_proc) / (wall * 10.0),
		       (main_thread - td_cpu_main) / td_wall_frames,
		       (proc - td_cpu_proc - (main_thread - td_cpu_main)) / td_wall_frames);
	}
#endif
    }
    td_wall_frames = 0;

    if (timedemo_quit.value) {
	extern bool shutdown_core;
	shutdown_core = true;
	environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
    }
}

/*
====================
CL_TimeDemo_f

timedemo [demoname]
====================
*/
void
CL_TimeDemo_f(void)
{
    if (cmd_source != src_command)
	return;

    if (Cmd_Argc() != 2) {
	Con_Printf("timedemo <demoname> : gets demo speeds\n");
	return;
    }

    CL_PlayDemo_f();

/* cls.td_starttime will be grabbed at the second frame of the demo, so */
/* all the loading time doesn't get counted */

    cls.timedemo = true;
    cls.td_startframe = host_framecount;
    cls.td_lastframe = -1;	/* get a new message this frame */
}
