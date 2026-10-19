/*
 * d_mt -- two-thread span drawing for the software renderer
 *
 * D_DrawSurfaces runs its per-surface setup (gradients, surface cache,
 * bmodel transforms) on the calling thread as before, but instead of
 * drawing it records each span-drawing call with a copy of the drawing
 * state.  The recorded calls are then replayed by the calling thread and
 * one worker thread, each for its own band of screen rows: every span
 * function skips spans outside [d_band_v0, d_band_v1).  Spans of one
 * flush never overlap, so the bands can be drawn in any order.
 *
 * The drawing state the span functions read or write is thread-local
 * (D_TLS), so both threads can run the same functions.
 */

#ifndef D_MT_H
#define D_MT_H

#include "qtypes.h"
#include "r_shared.h"

/* D_TLS (r_shared.h) is initial-exec: the core is dlopen'ed, but the few
 * bytes fit glibc's static TLS surplus, and each access is a load + add
 * instead of a __tls_get_addr call */

/* rows this thread draws: [d_band_v0, d_band_v1) */
extern D_TLS int d_band_v0, d_band_v1;

#define D_SPAN_OUTSIDE_BAND(pspan) \
   ((unsigned)((pspan)->v - d_band_v0) >= (unsigned)(d_band_v1 - d_band_v0))

/* recorded span-drawing calls */
enum {
   D_OP_SPANS,      /* D_DrawSpans + D_DrawZSpans */
   D_OP_SKY,        /* D_DrawSkyScans8 + D_DrawZSpans */
   D_OP_SOLID,      /* D_DrawSolidSurface + D_DrawZSpans */
   D_OP_TURB,       /* Turbulent8 */
   D_OP_TURB_Z      /* Turbulent8 + D_DrawZSpans */
};

/* rows covered by the spans of the current flush (set by R_ScanEdges) */
extern int d_flush_v0, d_flush_v1;

/* R_ScanEdges starts: flushes are counted per scan for the split */
void D_MT_FrameStart(void);

/* true while D_DrawSurfaces should record instead of draw */
qboolean D_MT_Active(void);

/* record one call with the current drawing state (color: D_OP_SOLID) */
void D_MT_Record(int op, espan_t *spans, int color);

/* draw everything recorded since the last flush on both threads; rows
 * [v0, v1) are what the recorded spans cover */
void D_MT_Flush(int v0, int v1);

/* run a call now on this thread (no recording, whole screen) */
void D_MT_Exec(int op, espan_t *spans, int color);

void D_MT_RegisterCvars(void);
void D_MT_Shutdown(void);

#endif /* D_MT_H */
