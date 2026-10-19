#define _GNU_SOURCE /* SCHED_IDLE, cpu_set_t */
/*
 * Background music handling for Quakespasm (adapted from uHexen2)
 * Handles streaming music as raw sound samples and runs the midi driver
 *
 * Copyright (C) 1999-2005 Id Software, Inc.
 * Copyright (C) 2010-2012 O.Sezer <sezero@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "quakedef.h"
#include "console.h"
#include "common.h"
#include "cmd.h"
#include "sound.h"
#include "cdaudio.h"

#include "snd_codec.h"
#include "bgmusic.h"
#include "sys.h"

#define MUSIC_DIRNAME	"music"

qboolean	bgmloop;
cvar_t		bgm_extmusic = {"bgm_extmusic", "1", false};

static qboolean	no_extmusic= false;
static float	old_volume = -1.0f;

typedef enum _bgm_player
{
	BGM_NONE = -1,
	BGM_MIDIDRV = 1,
	BGM_STREAMER,
	ENSURE_INT_BGMPLAYER = 0x70000000
} bgm_player_t;

typedef struct music_handler_s
{
	unsigned int	type;	/* 1U << n (see snd_codec.h)	*/
	bgm_player_t	player;	/* Enumerated bgm player type	*/
	int	is_available;	/* -1 means not present		*/
	const char	*ext;	/* Expected file extension	*/
	const char	*dir;	/* Where to look for music file */
	struct music_handler_s	*next;
} music_handler_t;

static music_handler_t wanted_handlers[] =
{
	{ CODECTYPE_VORBIS,BGM_STREAMER,-1,  "ogg", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_OPUS, BGM_STREAMER, -1, "opus", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_MP3,  BGM_STREAMER, -1,  "mp3", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_FLAC, BGM_STREAMER, -1, "flac", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_WAV,  BGM_STREAMER, -1,  "wav", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_MOD,  BGM_STREAMER, -1,  "it",  MUSIC_DIRNAME, NULL },
	{ CODECTYPE_MOD,  BGM_STREAMER, -1,  "s3m", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_MOD,  BGM_STREAMER, -1,  "xm",  MUSIC_DIRNAME, NULL },
	{ CODECTYPE_MOD,  BGM_STREAMER, -1,  "mod", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_UMX,  BGM_STREAMER, -1,  "umx", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_NONE, BGM_NONE,     -1,   NULL,         NULL,  NULL }
};

static music_handler_t *music_handlers = NULL;

#define ANY_CODECTYPE	0xFFFFFFFF
#define CDRIP_TYPES	(CODECTYPE_VORBIS | CODECTYPE_MP3 | CODECTYPE_FLAC | CODECTYPE_WAV)
#define CDRIPTYPE(x)	(((x) & CDRIP_TYPES) != 0)

static snd_stream_t *bgmstream = NULL;

/*
 * Music decoding on its own thread (unix).  Tremor costs ~1 ms per frame
 * on the Miyoo, all of it on the frame's critical path; a decoder thread at
 * idle priority on the second core (idle between the drawing flushes) keeps
 * a few seconds of decoded PCM in a ring, and BGM_UpdateStream only copies
 * from it.  bgmstream is opened and closed on the main thread; the decoder
 * reads it only while it is dec_stream (changed under dec_lock, which the
 * decoder holds while it reads).  Without the thread the stream is decoded
 * in BGM_UpdateStream as before.
 */
#if defined(__unix__) && !defined(__EMSCRIPTEN__) && !defined(NO_BGM_THREAD)
#define BGM_THREAD 1
#include <pthread.h>
#include <sched.h>

#define BGM_RING_BYTES	(512 * 1024)	/* power of two; ~2.7 s at 48 kHz */
#define BGM_DEC_CHUNK	16384

static byte bgm_ring[BGM_RING_BYTES];
static volatile unsigned ring_in, ring_out;	/* bytes written / read */
static volatile int dec_end;	/* 0, 1 = end of a non-looping stream, <0 = error */
static snd_stream_t *volatile dec_stream;
static pthread_t dec_thread;
static pthread_mutex_t dec_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dec_cond = PTHREAD_COND_INITIALIZER;
static qboolean dec_running, dec_failed, dec_busy;
static unsigned dec_behind;	/* frames the ring ran dry (reported at shutdown) */
static volatile qboolean dec_quit;

static void *BGM_DecoderMain (void *arg)
{
	byte chunk[BGM_DEC_CHUNK];
	qboolean just_rewound = false;
	(void)arg;

#ifdef __linux__
	{
		/* run only when a core is idle, on the drawing worker's core */
		struct sched_param sp;
		cpu_set_t set;
		memset(&sp, 0, sizeof(sp));
		sched_setscheduler(0, SCHED_IDLE, &sp);
		CPU_ZERO(&set);
		CPU_SET(1, &set);
		sched_setaffinity(0, sizeof(set), &set);
	}
#endif

	pthread_mutex_lock(&dec_lock);
	for (;;)
	{
		snd_stream_t *s;
		int frame, room, res;

		while (!dec_quit && (!dec_stream || dec_end ||
		       BGM_RING_BYTES - (ring_in - ring_out) < BGM_DEC_CHUNK))
		{
			just_rewound = false;
			pthread_cond_wait(&dec_cond, &dec_lock);
		}
		if (dec_quit)
			break;

		/* decode without the lock (this thread may wait long for a
		 * core); BGM_DetachDecoder waits for dec_busy to clear */
		s = dec_stream;
		dec_busy = true;
		pthread_mutex_unlock(&dec_lock);
		frame = s->info.width * s->info.channels;
		room = BGM_DEC_CHUNK - BGM_DEC_CHUNK % frame;
		res = S_CodecReadStream(s, room, chunk);
		if (res == 0 && bgmloop && !just_rewound)
		{
			res = S_CodecRewindStream(s);
			if (res == 0)
			{
				just_rewound = true;
				res = S_CodecReadStream(s, room, chunk);
				if (res == 0)
					res = -1000;	/* decodes to nothing */
			}
			else if (res > 0)
				res = -res;
		}
		pthread_mutex_lock(&dec_lock);
		dec_busy = false;
		pthread_cond_broadcast(&dec_cond);
		if (dec_stream != s)
			continue;	/* detached meanwhile: drop it */

		if (res > 0)
		{
			unsigned pos = ring_in & (BGM_RING_BYTES - 1);
			int first = BGM_RING_BYTES - pos;

			res -= res % frame;
			if (first > res)
				first = res;
			memcpy(bgm_ring + pos, chunk, first);
			memcpy(bgm_ring, chunk + first, res - first);
			__sync_synchronize();
			ring_in += res;
			just_rewound = false;
		}
		else if (res == 0)	/* EOF, not looping */
			dec_end = 1;
		else
			dec_end = res;
	}
	pthread_mutex_unlock(&dec_lock);
	return NULL;
}

static qboolean BGM_StartDecoder (void)
{
	if (dec_running)
		return true;
	if (dec_failed)
		return false;
	dec_quit = false;
	if (pthread_create(&dec_thread, NULL, BGM_DecoderMain, NULL) != 0)
	{
		Con_Printf("Music: no decoder thread, decoding in the frame\n");
		dec_failed = true;
		return false;
	}
	dec_running = true;
	return true;
}

/* the decoder lets go of the stream (it may be closed after this) */
static void BGM_DetachDecoder (void)
{
	if (!dec_running)
		return;
	pthread_mutex_lock(&dec_lock);
	dec_stream = NULL;
	while (dec_busy)
		pthread_cond_wait(&dec_cond, &dec_lock);
	pthread_mutex_unlock(&dec_lock);
}

void BGM_StopThread (void)
{
	if (!dec_running)
		return;
	pthread_mutex_lock(&dec_lock);
	dec_stream = NULL;
	dec_quit = true;
	pthread_cond_broadcast(&dec_cond);
	pthread_mutex_unlock(&dec_lock);
	pthread_join(dec_thread, NULL);
	Sys_Printf("Music decoder: ring ran dry in %u frames\n", dec_behind);
	dec_running = false;
}
#else
void BGM_StopThread (void)
{
}
#endif

static void BGM_Play_f (void)
{
	if (Cmd_Argc() == 2)
	{
		BGM_Play (Cmd_Argv(1));
	}
	else
	{
		Con_Printf ("music <musicfile>\n");
		return;
	}
}

static void BGM_Pause_f (void)
{
	BGM_Pause ();
}

static void BGM_Resume_f (void)
{
	BGM_Resume ();
}

static void BGM_Loop_f (void)
{
	if (Cmd_Argc() == 2)
	{
		if (strcasecmp(Cmd_Argv(1),  "0") == 0 ||
		    strcasecmp(Cmd_Argv(1),"off") == 0)
			bgmloop = false;
		else if (strcasecmp(Cmd_Argv(1), "1") == 0 ||
		         strcasecmp(Cmd_Argv(1),"on") == 0)
			bgmloop = true;
		else if (strcasecmp(Cmd_Argv(1),"toggle") == 0)
			bgmloop = !bgmloop;
	}

	if (bgmloop)
		Con_Printf("Music will be looped\n");
	else
		Con_Printf("Music will not be looped\n");
}

static void BGM_Stop_f (void)
{
	BGM_Stop();
}

qboolean BGM_Init (void)
{
	music_handler_t *handlers = NULL;
	int i;

	Cvar_RegisterVariable(&bgm_extmusic);
	Cmd_AddCommand("music", BGM_Play_f);
	Cmd_AddCommand("music_pause", BGM_Pause_f);
	Cmd_AddCommand("music_resume", BGM_Resume_f);
	Cmd_AddCommand("music_loop", BGM_Loop_f);
	Cmd_AddCommand("music_stop", BGM_Stop_f);

	if (COM_CheckParm("-noextmusic") != 0)
		no_extmusic = true;

	bgmloop = true;

	for (i = 0; wanted_handlers[i].type != CODECTYPE_NONE; i++)
	{
		switch (wanted_handlers[i].player)
		{
		case BGM_MIDIDRV:
		/* not supported in quake */
			break;
		case BGM_STREAMER:
			wanted_handlers[i].is_available =
				S_CodecIsAvailable(wanted_handlers[i].type);
			break;
		case BGM_NONE:
		default:
			break;
		}
		if (wanted_handlers[i].is_available != -1)
		{
			if (handlers)
			{
				handlers->next = &wanted_handlers[i];
				handlers = handlers->next;
			}
			else
			{
				music_handlers = &wanted_handlers[i];
				handlers = music_handlers;
			}
		}
	}

	return true;
}

void BGM_Shutdown (void)
{
	BGM_Stop();
	BGM_StopThread();
/* sever our connections to
 * midi_drv and snd_codec */
	music_handlers = NULL;

	/* Reset -noextmusic latch so a fresh argv parse on the next
	 * BGM_Init takes effect.  Without this, removing -noextmusic
	 * between sessions on a statically-linked target leaves
	 * external music silently disabled. */
	no_extmusic = false;
}

static void BGM_Play_noext (const char *filename, unsigned int allowed_types)
{
	char tmp[MAX_QPATH];
	music_handler_t *handler;

	handler = music_handlers;
	while (handler)
	{
		if (! (handler->type & allowed_types))
		{
			handler = handler->next;
			continue;
		}
		if (!handler->is_available)
		{
			handler = handler->next;
			continue;
		}
		snprintf(tmp, sizeof(tmp), "%s/%s.%s",
			   handler->dir, filename, handler->ext);
		switch (handler->player)
		{
		case BGM_MIDIDRV:
		/* not supported in quake */
			break;
		case BGM_STREAMER:
			bgmstream = S_CodecOpenStreamType(tmp, handler->type);
			if (bgmstream)
				return;		/* success */
			break;
		case BGM_NONE:
		default:
			break;
		}
		handler = handler->next;
	}

	Con_Printf("Couldn't handle music file %s\n", filename);
}

void BGM_Play (const char *filename)
{
	char tmp[MAX_QPATH];
	const char *ext;
	music_handler_t *handler;

	BGM_Stop();

	if (music_handlers == NULL)
		return;

	if (!filename || !*filename)
	{
		Con_DPrintf("null music file name\n");
		return;
	}

	ext = COM_FileExtension(filename);
	if (! *ext)	/* try all things */
	{
		BGM_Play_noext(filename, ANY_CODECTYPE);
		return;
	}

	handler = music_handlers;
	while (handler)
	{
		if (handler->is_available &&
		    !strcasecmp(ext, handler->ext))
			break;
		handler = handler->next;
	}
	if (!handler)
	{
		Con_Printf("Unhandled extension for %s\n", filename);
		return;
	}
	snprintf(tmp, sizeof(tmp), "%s/%s", handler->dir, filename);
	switch (handler->player)
	{
	case BGM_MIDIDRV:
	/* not supported in quake */
		break;
	case BGM_STREAMER:
		bgmstream = S_CodecOpenStreamType(tmp, handler->type);
		if (bgmstream)
			return;		/* success */
		break;
	case BGM_NONE:
	default:
		break;
	}

	Con_Printf("Couldn't handle music file %s\n", filename);
}

void BGM_PlayCDtrack (byte track, qboolean looping)
{
/* instead of searching by the order of music_handlers, do so by
 * the order of searchpath priority: the file from the searchpath
 * with the highest path_id is most likely from our own gamedir
 * itself. This way, if a mod has track02 as a *.mp3 file, which
 * is below *.ogg in the music_handler order, the mp3 will still
 * have priority over track02.ogg from, say, id1.
 */
	char tmp[MAX_QPATH];
	const char *ext;
	unsigned int type;
	music_handler_t *handler;

	BGM_Stop();
	if (CDAudio_Play(track, looping) == 0)
		return;			/* success */
	if (music_handlers == NULL)
		return;
	if (no_extmusic || !bgm_extmusic.value)
		return;

	type = 0;
	ext  = NULL;
	handler = music_handlers;
	while (handler)
	{
		if (! handler->is_available)
			goto _next;
		if (! CDRIPTYPE(handler->type))
			goto _next;
		snprintf(tmp, sizeof(tmp), "%s/track%02d.%s",
				MUSIC_DIRNAME, (int)track, handler->ext);
		if (! COM_FileExists(tmp))
			goto _next;
		type = handler->type;
		ext = handler->ext;
	_next:
		handler = handler->next;
	}
	if (ext)
	{
		snprintf(tmp, sizeof(tmp), "%s/track%02d.%s",
				MUSIC_DIRNAME, (int)track, ext);
		bgmstream = S_CodecOpenStreamType(tmp, type);
		if (! bgmstream)
			Con_Printf("Couldn't handle music file %s\n", tmp);
	}
	else
		Con_Printf("No music for track %d: no %s/track%02d.* in the game dirs (e.g. id1/%s/)\n",
			   (int)track, MUSIC_DIRNAME, (int)track, MUSIC_DIRNAME);
}

void BGM_Stop (void)
{
	if (bgmstream)
	{
#ifdef BGM_THREAD
		BGM_DetachDecoder();
#endif
		bgmstream->status = STREAM_NONE;
		S_CodecCloseStream(bgmstream);
		bgmstream = NULL;
		/* Drop everything still queued for the mixer.  Reset
		 * head too so the next track starts at offset 0 of
		 * s_rawsamples -- cosmetic, not required for
		 * correctness (the FIFO writer would resume at
		 * (s_rawhead + 0) wherever s_rawhead happens to sit),
		 * but easier to reason about. */
		s_rawhead  = 0;
		s_rawavail = 0;
	}
}

void BGM_Pause (void)
{
	if (bgmstream)
	{
		if (bgmstream->status == STREAM_PLAY)
			bgmstream->status = STREAM_PAUSE;
	}
}

void BGM_Resume (void)
{
	if (bgmstream)
	{
		if (bgmstream->status == STREAM_PAUSE)
			bgmstream->status = STREAM_PLAY;
	}
}

static void BGM_UpdateStream (void)
{
	int	res;	/* Number of bytes read. */
	int	bufferSamples;
	int	fileSamples;
	int	fileBytes;
	byte	raw[16384];
	/* True iff the most recent S_CodecRewindStream succeeded
	 * and we haven't yet seen a non-empty read from the rewound
	 * stream. Used to break the rewind-then-immediate-EOF spin
	 * for looping streams that decode to zero PCM samples (a
	 * Vorbis/FLAC/Opus file with a valid header but no audio
	 * payload, or a malformed file the underlying library skips
	 * over). Without this flag the outer 'while (s_rawavail <
	 * MAX_RAW_SAMPLES)' loop never advances s_rawavail, so it
	 * rewinds and re-reads forever on the same BGM_UpdateStream
	 * call. WAV streaming rejects zero-sample files at the
	 * header parser already, but every other codec routes
	 * through a third-party library whose validation we don't
	 * own. */
	qboolean just_rewound = false;

	if (bgmstream->status != STREAM_PLAY)
		return;

	/* don't bother playing anything if musicvolume is 0 */
	if (bgmvolume.value <= 0)
		return;

#ifdef BGM_THREAD
	if (BGM_StartDecoder())
	{
		int frame = bgmstream->info.width * bgmstream->info.channels;

		if (dec_stream != bgmstream)
		{
			/* a new stream: the decoder starts on it */
			pthread_mutex_lock(&dec_lock);
			ring_in = ring_out = 0;
			dec_end = 0;
			dec_stream = bgmstream;
			pthread_mutex_unlock(&dec_lock);
		}

		while (s_rawavail < MAX_RAW_SAMPLES)
		{
			unsigned pos, avail = ring_in - ring_out;
			int first;

			bufferSamples = MAX_RAW_SAMPLES - s_rawavail;
			fileSamples = bufferSamples * bgmstream->info.rate / shm->speed;
			if (!fileSamples)
				break;
			fileBytes = fileSamples * frame;
			if (fileBytes > (int) sizeof(raw))
				fileBytes = (int) sizeof(raw) - (int) sizeof(raw) % frame;
			if ((unsigned) fileBytes > avail)
				fileBytes = avail - avail % frame;
			if (!fileBytes)
			{
				if (dec_end > 0)	/* played to the end */
				{
					BGM_Stop();
					return;
				}
				if (dec_end < 0)
				{
					Con_Printf("Stream read error (%i), stopping.\n", dec_end);
					BGM_Stop();
					return;
				}
				dec_behind++;
				break;		/* the decoder is behind */
			}
			__sync_synchronize();
			pos = ring_out & (BGM_RING_BYTES - 1);
			first = BGM_RING_BYTES - pos;
			if (first > fileBytes)
				first = fileBytes;
			memcpy(raw, bgm_ring + pos, first);
			memcpy(raw + first, bgm_ring, fileBytes - first);
			__sync_synchronize();
			ring_out += fileBytes;
			S_RawSamples(fileBytes / frame, bgmstream->info.rate,
					bgmstream->info.width,
					bgmstream->info.channels,
					raw, bgmvolume.value);
		}

		/* room for more: wake the decoder */
		pthread_mutex_lock(&dec_lock);
		pthread_cond_signal(&dec_cond);
		pthread_mutex_unlock(&dec_lock);
		return;
	}
#endif

	/* see how many samples should be copied into the raw buffer */
	while (s_rawavail < MAX_RAW_SAMPLES)
	{
		bufferSamples = MAX_RAW_SAMPLES - s_rawavail;

		/* decide how much data needs to be read from the file */
		fileSamples = bufferSamples * bgmstream->info.rate / shm->speed;
		if (!fileSamples)
			return;

		/* our max buffer size */
		fileBytes = fileSamples * (bgmstream->info.width * bgmstream->info.channels);
		if (fileBytes > (int) sizeof(raw))
		{
			fileBytes = (int) sizeof(raw);
			fileSamples = fileBytes /
					  (bgmstream->info.width * bgmstream->info.channels);
		}

		/* Read */
		res = S_CodecReadStream(bgmstream, fileBytes, raw);
		if (res < fileBytes)
		{
			fileBytes = res;
			fileSamples = res / (bgmstream->info.width * bgmstream->info.channels);
		}

		if (res > 0)	/* data: add to raw buffer */
		{
			just_rewound = false;
			S_RawSamples(fileSamples, bgmstream->info.rate,
							bgmstream->info.width,
							bgmstream->info.channels,
							raw, bgmvolume.value);
		}
		else if (res == 0)	/* EOF */
		{
			if (just_rewound)
			{
				/* Rewound on the previous iteration and the
				 * stream still produced nothing -- the source
				 * decodes to zero PCM samples (empty payload,
				 * malformed file the underlying library is
				 * just skipping past, etc). Stop instead of
				 * spinning the outer while-loop forever. */
				Con_Printf("Stream produced no data after rewind, stopping.\n");
				BGM_Stop();
				return;
			}
			if (bgmloop)
			{
				res = S_CodecRewindStream(bgmstream);
				if (res != 0)
				{
					Con_Printf("Stream seek error (%i), stopping.\n", res);
					BGM_Stop();
					return;
				}
				just_rewound = true;
			}
			else
			{
				BGM_Stop();
				return;
			}
		}
		else	/* res < 0: some read error */
		{
			Con_Printf("Stream read error (%i), stopping.\n", res);
			BGM_Stop();
			return;
		}
	}
}

void BGM_Update (void)
{
	if (old_volume != bgmvolume.value)
	{
		/* NaN compared to any number is false, so the bare
		 * '<0' / '>1' tests both miss it -- a user setting
		 * 'bgmvolume nan' from the console lands here every
		 * frame ('old_volume != bgmvolume.value' fires because
		 * NaN != X is always true), neither branch triggers,
		 * and old_volume is then stamped to NaN as well so the
		 * cvar never gets reset. Worse, BGM_UpdateStream's
		 * '<= 0' early-return also misses NaN and S_RawSamples
		 * ends up evaluating (int)(256 * NaN), which is UB
		 * (typically INT_MIN on x86) and corrupts the music mix.
		 * Treat NaN the same as a negative value -- clamp to 0
		 * -- so the cvar gets sanitised back to a usable range
		 * on the next BGM_Update tick. */
		if (IS_NAN(bgmvolume.value) || bgmvolume.value < 0)
			Cvar_Set ("bgmvolume", "0");
		else if (bgmvolume.value > 1)
			Cvar_Set ("bgmvolume", "1");
		old_volume = bgmvolume.value;
	}
	if (bgmstream)
		BGM_UpdateStream ();
}

