/* tqrun: runs the TyrQuake-Miyoo libretro core on the Miyoo Mini Plus without RetroArch.
 *
 * A minimal libretro frontend: it loads the core (dlopen), answers the environment calls the
 * core makes, shows its frames on /dev/fb0 (RGB565 -> 32 bpp, turned 180 degrees for the
 * upside-down panel, page flip on a thread of its own), reads the buttons from evdev and writes
 * the sound to /dev/dsp (libpadsp.so, preloaded as for RetroArch, hands it to Onion's
 * audioserver). The core does everything else, as in RetroArch.
 *
 *   tqrun [options] <content>
 *     -L core.so    the core (default: tyrquake_libretro.so next to tqrun)
 *     -o file       core options, RetroArch's .opt format (key = "value")
 *     -s dir        save directory (default: the content's folder)
 *     -n frames     stop after that many frames
 *     -V 0|1        wait for the panel before the next frame (default 1)
 *     -A 0|1        wait for the sound (audio sync, default 1)
 *     -l ms         sound latency for the OSS fragment size (default 64, as RetroArch)
 *
 * Menu quits. Times per frame (frontend parts) are printed at the end.
 */
/* no _GNU_SOURCE: with it glibc 2.38's headers send strtol & co. to __isoc23_* */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/soundcard.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "libretro.h"

/* the Miyoo has glibc 2.28: dl* moved into libc in 2.34 */
#if defined(__arm__)
__asm__(".symver dlopen,dlopen@GLIBC_2.4");
__asm__(".symver dlsym,dlsym@GLIBC_2.4");
__asm__(".symver dlerror,dlerror@GLIBC_2.4");
__asm__(".symver dlclose,dlclose@GLIBC_2.4");
__asm__(".symver pthread_create,pthread_create@GLIBC_2.4");
__asm__(".symver pthread_join,pthread_join@GLIBC_2.4");
__asm__(".symver __xstat,__xstat@GLIBC_2.4");
#endif
extern int __xstat(int ver, const char *path, struct stat *buf);

#define SCREEN_W 640
#define SCREEN_H 480
#define OUT_RATE 48000

/* ---- the core ------------------------------------------------------------------------------ */

static struct {
   void (*init)(void);
   void (*deinit)(void);
   void (*set_environment)(retro_environment_t);
   void (*set_video_refresh)(retro_video_refresh_t);
   void (*set_audio_sample)(retro_audio_sample_t);
   void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
   void (*set_input_poll)(retro_input_poll_t);
   void (*set_input_state)(retro_input_state_t);
   void (*get_system_av_info)(struct retro_system_av_info *);
   void (*set_controller_port_device)(unsigned, unsigned);
   bool (*load_game)(const struct retro_game_info *);
   void (*unload_game)(void);
   void (*run)(void);
   size_t (*serialize_size)(void);
   bool (*serialize)(void *, size_t);
   bool (*unserialize)(const void *, size_t);
} core;

static volatile sig_atomic_t quit;
static int verbose = 1;

static double now_usec(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec * 1000000.0 + ts.tv_nsec / 1000.0;
}

/* ---- core options -------------------------------------------------------------------------- */

#define MAX_OPTS 256
static struct opt { char key[64]; char value[128]; int from_file; } opts[MAX_OPTS];
static int num_opts;

static struct opt *opt_find(const char *key)
{
   int i;
   for (i = 0; i < num_opts; i++)
      if (!strcmp(opts[i].key, key))
         return &opts[i];
   return NULL;
}

static void opt_set(const char *key, const char *value, int from_file)
{
   struct opt *o = opt_find(key);
   if (!o)
   {
      if (num_opts == MAX_OPTS)
         return;
      o = &opts[num_opts++];
      snprintf(o->key, sizeof(o->key), "%s", key);
   }
   else if (o->from_file && !from_file)
      return;	/* the file wins over the core's default */
   snprintf(o->value, sizeof(o->value), "%s", value);
   o->from_file = from_file;
}

/* key = "value" lines */
static void opts_load(const char *path)
{
   char line[256];
   FILE *f = fopen(path, "r");
   if (!f)
   {
      fprintf(stderr, "tqrun: no options file %s\n", path);
      return;
   }
   while (fgets(line, sizeof(line), f))
   {
      char *eq = strchr(line, '='), *k = line, *v, *e;
      if (!eq || line[0] == '#')
         continue;
      *eq = 0;
      while (*k == ' ' || *k == '\t')
         k++;
      for (e = eq - 1; e >= k && (*e == ' ' || *e == '\t'); e--)
         *e = 0;
      v = strchr(eq + 1, '"');
      if (!v)
         continue;
      v++;
      e = strchr(v, '"');
      if (!e)
         continue;
      *e = 0;
      opt_set(k, v, 1);
   }
   fclose(f);
}

/* ---- video --------------------------------------------------------------------------------- */

static int fb_fd = -1;
static uint32_t *fb_mem;
static size_t fb_page_px;	/* pixels in one page */
static struct fb_var_screeninfo fb_var;
static int fb_shown;	/* the page on screen */

static unsigned frame_w = SCREEN_W, frame_h = SCREEN_H;

/* frames handed to the present thread: two buffers lent to the core in turn */
static uint16_t *lend_buf[2];
static size_t lend_pitch;	/* bytes */
static int present_buf = -1;	/* the buffer the present thread owns, -1 = idle */
static const uint16_t *present_src;
static unsigned present_w, present_h;
static size_t present_pitch;
static int present_quit;
static pthread_mutex_t present_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t present_cond = PTHREAD_COND_INITIALIZER;
static pthread_t present_thread;
static int vsync = 1;

/* statistics */
static double st_convert_usec, st_pan_usec, st_vwait_usec, st_awrite_usec, st_run_usec;
static unsigned long st_presented, st_dropped, st_frames, st_dupes;

static inline uint32_t rgb565_to_xrgb(uint32_t p)
{
   uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
   return 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

/* the frame into a screen page, turned 180 degrees: written forwards (the mapping is
 * uncached), read backwards */
static void convert_frame(uint32_t *dst, const uint16_t *src, unsigned w, unsigned h, size_t pitch)
{
   unsigned x, y;
   size_t spx = pitch / 2;

   if (w == SCREEN_W && h == SCREEN_H)
   {
      for (y = 0; y < SCREEN_H; y++)
      {
         const uint32_t *s = (const uint32_t *)(src + (SCREEN_H - 1 - y) * spx + SCREEN_W - 2);
         uint32_t *d = dst + y * SCREEN_W;
         for (x = 0; x < SCREEN_W; x += 2)
         {
            uint32_t two = *s--;
            d[x]     = rgb565_to_xrgb(two >> 16);
            d[x + 1] = rgb565_to_xrgb(two & 0xFFFF);
         }
      }
      return;
   }
   /* other sizes: nearest pixel over the whole screen */
   for (y = 0; y < SCREEN_H; y++)
   {
      const uint16_t *s = src + (size_t)((SCREEN_H - 1 - y) * h / SCREEN_H) * spx;
      uint32_t *d = dst + y * SCREEN_W;
      for (x = 0; x < SCREEN_W; x++)
         d[x] = rgb565_to_xrgb(s[(SCREEN_W - 1 - x) * w / SCREEN_W]);
   }
}

/* ---- the SoC's 2D engine (MI_GFX) -----------------------------------------------------------
 * As Onion's RetroArch (and OpenLara's Miyoo port, cacuracaptors/OpenLara-miyoomini) do: the
 * frames are lent to the core in memory the 2D engine reads (MMA), and the engine converts
 * them to 32 bpp and turns them into the screen page, with no work for the cores. The vendor
 * libraries are loaded at run time; the types below follow the SigmaStar SDK's layouts
 * (mi_gfx_datatype.h). Without them, or with TQ_GFX=0, the CPU does it (convert_frame). */
typedef unsigned long long mi_phy;
typedef struct { int32_t x, y; uint32_t w, h; } mi_rect;
typedef struct { mi_phy phys; int fmt; uint32_t w, h, stride; } mi_surface;
typedef struct { uint8_t enable; int op, fmt; uint32_t start, end; } mi_colorkey;
typedef struct
{
   mi_rect clip;
   mi_colorkey src_key, dst_key;
   int src_blend, dst_blend, mirror, rotate, blend_flags;
   uint32_t src_const, dst_const;
} mi_opt;
enum { MI_FMT_RGB565 = 6, MI_FMT_ARGB8888 = 11 };
enum { MI_BLD_ZERO = 0, MI_BLD_ONE = 1 };
enum { MI_MIRROR_NONE = 0, MI_MIRROR_BOTH = 3 };
enum { MI_ROTATE_0 = 0, MI_ROTATE_180 = 2 };

static struct
{
   int32_t (*sys_init)(void);
   int32_t (*mma_alloc)(uint8_t *heap, uint32_t size, mi_phy *phys);
   int32_t (*mmap)(unsigned long long phys, uint32_t size, void **virt, uint8_t cached);
   int32_t (*flush)(void *virt, uint32_t size);
   int32_t (*open)(void);
   int32_t (*blit)(mi_surface *src, mi_rect *src_rect, mi_surface *dst, mi_rect *dst_rect,
         mi_opt *opt, uint16_t *fence);
   int32_t (*wait)(uint8_t all, uint16_t fence);
} mi;
static int use_gfx;
static mi_phy fb_phys, lend_phys[2];

static void *mi_lib(const char *name)
{
   static const char *dirs[] = { "", "/customer/lib/", "/config/lib/", "/lib/", "/usr/lib/" };
   char path[96];
   unsigned i;
   for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
   {
      void *h;
      snprintf(path, sizeof(path), "%s%s", dirs[i], name);
      h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
      if (h)
         return h;
   }
   return NULL;
}

/* src (RGB565, w x h) into a screen page, turned 180 degrees, scaled to fill it */
static int gfx_blit(mi_phy src, unsigned w, unsigned h, size_t pitch, int page)
{
   mi_surface s = { src, MI_FMT_RGB565, w, h, (uint32_t)pitch };
   mi_surface d = { fb_phys + (mi_phy)page * fb_page_px * 4, MI_FMT_ARGB8888, SCREEN_W, SCREEN_H, SCREEN_W * 4 };
   mi_rect sr = { 0, 0, w, h }, dr = { 0, 0, SCREEN_W, SCREEN_H };
   mi_opt o;
   uint16_t fence = 0;
   static int rotate = -1;

   if (rotate < 0)
   {
      /* TQ_GFX_ROTATE=1: the engine's rotation instead of mirroring both ways (same picture) */
      const char *e = getenv("TQ_GFX_ROTATE");
      rotate = e && *e == '1';
   }
   memset(&o, 0, sizeof(o));
   o.clip = dr;
   o.src_blend = MI_BLD_ONE;
   o.dst_blend = MI_BLD_ZERO;
   o.mirror = rotate ? MI_MIRROR_NONE : MI_MIRROR_BOTH;
   o.rotate = rotate ? MI_ROTATE_180 : MI_ROTATE_0;
   if (mi.blit(&s, &sr, &d, &dr, &o, &fence))
      return 0;
   mi.wait(1, fence);
   return 1;
}

static void gfx_init(struct fb_fix_screeninfo *fix)
{
   const char *e = getenv("TQ_GFX");
   void *hs, *hg;
   int i;

   if (e && *e == '0')
      return;
   hs = mi_lib("libmi_sys.so");
   hg = mi_lib("libmi_gfx.so");
   if (!hs || !hg)
   {
      fprintf(stderr, "tqrun: no MI_GFX libraries, the CPU converts the frames\n");
      return;
   }
   *(void **)&mi.sys_init = dlsym(hs, "MI_SYS_Init");
   *(void **)&mi.mma_alloc = dlsym(hs, "MI_SYS_MMA_Alloc");
   *(void **)&mi.mmap = dlsym(hs, "MI_SYS_Mmap");
   *(void **)&mi.flush = dlsym(hs, "MI_SYS_FlushInvCache");
   *(void **)&mi.open = dlsym(hg, "MI_GFX_Open");
   *(void **)&mi.blit = dlsym(hg, "MI_GFX_BitBlit");
   *(void **)&mi.wait = dlsym(hg, "MI_GFX_WaitAllDone");
   if (!mi.sys_init || !mi.mma_alloc || !mi.mmap || !mi.flush || !mi.open || !mi.blit || !mi.wait)
   {
      fprintf(stderr, "tqrun: MI_GFX functions missing, the CPU converts the frames\n");
      return;
   }
   mi.sys_init();
   mi.open();
   for (i = 0; i < 2; i++)
   {
      void *virt = NULL;
      if (mi.mma_alloc(NULL, SCREEN_W * SCREEN_H * 2, &lend_phys[i])
            || mi.mmap(lend_phys[i], SCREEN_W * SCREEN_H * 2, &virt, 1))
      {
         fprintf(stderr, "tqrun: no MMA memory, the CPU converts the frames\n");
         return;
      }
      memset(virt, 0, SCREEN_W * SCREEN_H * 2);
      mi.flush(virt, SCREEN_W * SCREEN_H * 2);
      lend_buf[i] = virt;
   }
   fb_phys = fix->smem_start;
   use_gfx = 1;
   fprintf(stderr, "tqrun: frames shown by the 2D engine (MI_GFX)\n");
}

static void *present_proc(void *arg)
{
   (void)arg;
   pthread_mutex_lock(&present_mutex);
   for (;;)
   {
      int page;
      double t0, t1, t2;

      while (present_buf < 0 && !present_quit)
         pthread_cond_wait(&present_cond, &present_mutex);
      if (present_quit)
         break;
      pthread_mutex_unlock(&present_mutex);

      page = fb_shown ^ 1;
      t0 = now_usec();
      if (use_gfx)
      {
         int k = present_src == lend_buf[0] ? 0 : 1;
         mi.flush((void *)present_src, present_pitch * present_h);
         if (!gfx_blit(lend_phys[k], present_w, present_h, present_pitch, page))
            convert_frame(fb_mem + page * fb_page_px, present_src, present_w, present_h, present_pitch);
      }
      else
         convert_frame(fb_mem + page * fb_page_px, present_src, present_w, present_h, present_pitch);
      t1 = now_usec();
      fb_var.yoffset = page * SCREEN_H;
      ioctl(fb_fd, FBIOPAN_DISPLAY, &fb_var);	/* waits for the panel's next refresh */
      t2 = now_usec();
      fb_shown = page;

      pthread_mutex_lock(&present_mutex);
      st_convert_usec += t1 - t0;
      st_pan_usec += t2 - t1;
      st_presented++;
      present_buf = -1;
      pthread_cond_broadcast(&present_cond);
   }
   pthread_mutex_unlock(&present_mutex);
   return NULL;
}

static int video_init(void)
{
   struct fb_fix_screeninfo fix;
   int i;

   fb_fd = open("/dev/fb0", O_RDWR);
   if (fb_fd < 0 || ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_var) || ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix))
   {
      fprintf(stderr, "tqrun: /dev/fb0: %s\n", strerror(errno));
      return 0;
   }
   if (fb_var.bits_per_pixel != 32 || fb_var.xres != SCREEN_W || fb_var.yres_virtual < 2 * SCREEN_H)
   {
      fprintf(stderr, "tqrun: fb0 is %ux%u (virtual %u) %u bpp, not 640x480x32 with two pages\n",
            fb_var.xres, fb_var.yres, fb_var.yres_virtual, fb_var.bits_per_pixel);
      return 0;
   }
   fb_page_px = (size_t)SCREEN_W * SCREEN_H;
   fb_mem = mmap(NULL, fb_page_px * 4 * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
   if (fb_mem == MAP_FAILED)
   {
      fprintf(stderr, "tqrun: fb0 mmap: %s\n", strerror(errno));
      return 0;
   }
   fb_shown = fb_var.yoffset >= SCREEN_H ? 1 : 0;
   memset(fb_mem + (fb_shown ^ 1) * fb_page_px, 0, fb_page_px * 4);

   lend_pitch = SCREEN_W * 2;
   gfx_init(&fix);
   if (!use_gfx)
      for (i = 0; i < 2; i++)
         lend_buf[i] = calloc(SCREEN_W * SCREEN_H, 2);
   pthread_create(&present_thread, NULL, present_proc, NULL);
   return 1;
}

static void video_shutdown(void)
{
   pthread_mutex_lock(&present_mutex);
   present_quit = 1;
   pthread_cond_broadcast(&present_cond);
   pthread_mutex_unlock(&present_mutex);
   pthread_join(present_thread, NULL);
   if (fb_mem)
   {
      /* a black screen until Onion's menu draws again */
      memset(fb_mem, 0, fb_page_px * 4 * 2);
      munmap(fb_mem, fb_page_px * 4 * 2);
   }
   if (fb_fd >= 0)
      close(fb_fd);
}

/* the buffer the core may draw into: the one the present thread isn't reading */
static int lend_next;
static bool get_framebuffer(struct retro_framebuffer *fb)
{
   int k;
   if (fb->width > SCREEN_W || fb->height > SCREEN_H)
      return false;
   pthread_mutex_lock(&present_mutex);
   k = lend_next;
   if (k == present_buf)
      k ^= 1;
   pthread_mutex_unlock(&present_mutex);
   fb->data = lend_buf[k];
   fb->pitch = fb->width * 2;
   fb->format = RETRO_PIXEL_FORMAT_RGB565;
   fb->memory_flags = RETRO_MEMORY_TYPE_CACHED;
   return true;
}

static void RETRO_CALLCONV video_refresh(const void *data, unsigned w, unsigned h, size_t pitch)
{
   int k;

   st_frames++;
   if (!data)
   {
      st_dupes++;
      return;
   }
   pthread_mutex_lock(&present_mutex);
   if (present_buf >= 0)
   {
      if (!vsync)
      {
         /* the panel still shows the last one: this frame isn't shown */
         st_dropped++;
         pthread_mutex_unlock(&present_mutex);
         return;
      }
      {
         double t0 = now_usec();
         while (present_buf >= 0)
            pthread_cond_wait(&present_cond, &present_mutex);
         st_vwait_usec += now_usec() - t0;
      }
   }
   if (data == lend_buf[0])
      k = 0;
   else if (data == lend_buf[1])
      k = 1;
   else
   {
      /* not drawn in a lent buffer: copy it into one */
      unsigned y;
      k = lend_next;
      for (y = 0; y < h && y < SCREEN_H; y++)
         memcpy(lend_buf[k] + y * SCREEN_W, (const uint8_t *)data + y * pitch, (w < SCREEN_W ? w : SCREEN_W) * 2);
      pitch = SCREEN_W * 2;
   }
   present_src = lend_buf[k];
   present_w = w;
   present_h = h;
   present_pitch = pitch;
   present_buf = k;
   lend_next = k ^ 1;
   pthread_cond_broadcast(&present_cond);
   pthread_mutex_unlock(&present_mutex);
}

/* ---- sound --------------------------------------------------------------------------------- */

static int dsp_fd = -1;
static int audio_sync = 1;
static int audio_latency = 64;
static unsigned long st_audio_frames, st_audio_dropped;

static void audio_init(void)
{
   int fmt = AFMT_S16_LE, ch = 2, rate = OUT_RATE;
   int frags, frag;

   dsp_fd = open("/dev/dsp", O_WRONLY);
   if (dsp_fd < 0)
   {
      fprintf(stderr, "tqrun: /dev/dsp: %s (no sound)\n", strerror(errno));
      return;
   }
   /* as RetroArch's OSS driver: fragments of 1 KiB, as many as the latency holds */
   frags = (audio_latency * rate * 4) / (1000 * (1 << 10));
   frag = (frags << 16) | 10;
   if (ioctl(dsp_fd, SNDCTL_DSP_SETFRAGMENT, &frag) < 0)
      fprintf(stderr, "tqrun: SNDCTL_DSP_SETFRAGMENT failed\n");
   if (ioctl(dsp_fd, SNDCTL_DSP_SETFMT, &fmt) < 0 || ioctl(dsp_fd, SNDCTL_DSP_CHANNELS, &ch) < 0
         || ioctl(dsp_fd, SNDCTL_DSP_SPEED, &rate) < 0)
      fprintf(stderr, "tqrun: /dev/dsp setup failed\n");
   if (rate != OUT_RATE)
      fprintf(stderr, "tqrun: /dev/dsp runs at %d Hz, not %d\n", rate, OUT_RATE);
   if (!audio_sync && !vsync)
      fcntl(dsp_fd, F_SETFL, fcntl(dsp_fd, F_GETFL) | O_NONBLOCK);
}

/* With vsync the panel paces the game, and the sound goes through a ring to a thread of its own
 * that writes it to /dev/dsp: a blocking write in the game's thread would pace the game too, and
 * the two clocks fought (frames 11/19/20 ms). The panel and the sound run within ~0.002 % of
 * each other (59.672 fps, 48002 samples/s measured), so the queue drifts very slowly. The ring is
 * nearly empty while all is well (libpadsp's FIFO is only 2 x 1 KiB = 512 frames, and audioserver
 * pulls it into its own buffer); when the ring holds more than RING_TARGET beyond the FIFO, one
 * frame is left out (every few seconds at most), which keeps every other sample exact. */
#define RING_FRAMES 16384	/* stereo frames, 341 ms */
#define RING_TARGET 1600	/* frames kept queued beyond the FIFO: 33 ms, two game frames */
#define RING_CHUNK 512		/* frames per write */
static int16_t ring[RING_FRAMES * 2];
static unsigned long long ring_in, ring_out;	/* frames written / read, ever */
static int ring_running, ring_quit, ring_prebuffer = 1;
static pthread_mutex_t ring_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ring_cond = PTHREAD_COND_INITIALIZER;
static pthread_t ring_thread;
static unsigned long st_ring_dups, st_ring_skips;
static int dsp_space_ok = -1;	/* SNDCTL_DSP_GETOSPACE works: -1 not known yet */
static long dsp_total_frames, dsp_queued_frames;

static void *ring_proc(void *arg)
{
   int16_t chunk[RING_CHUNK * 2];
   (void)arg;
   for (;;)
   {
      size_t n, i;
      const uint8_t *p;
      size_t left;

      pthread_mutex_lock(&ring_mutex);
      for (;;)
      {
         unsigned long long fill = ring_in - ring_out;
         if (ring_quit)
            break;
         /* the ring is empty most of the time: the sound waits in audioserver's FIFO, which
          * this thread keeps full; only the first start queues RING_TARGET first */
         if (ring_prebuffer ? fill >= RING_TARGET : fill > 0)
            break;
         pthread_cond_wait(&ring_cond, &ring_mutex);
      }
      if (ring_quit)
      {
         pthread_mutex_unlock(&ring_mutex);
         break;
      }
      ring_prebuffer = 0;
      n = ring_in - ring_out;
      if (n > RING_CHUNK)
         n = RING_CHUNK;
      for (i = 0; i < n; i++)
      {
         size_t k = (size_t)((ring_out + i) % RING_FRAMES) * 2;
         chunk[i * 2] = ring[k];
         chunk[i * 2 + 1] = ring[k + 1];
      }
      ring_out += n;
      pthread_cond_broadcast(&ring_cond);
      pthread_mutex_unlock(&ring_mutex);

      p = (const uint8_t *)chunk;
      left = n * 4;
      while (left)
      {
         ssize_t w = write(dsp_fd, p, left);
         if (w < 0)
         {
            if (errno == EINTR)
               continue;
            break;
         }
         p += w;
         left -= w;
      }

      /* how much the FIFO holds now: the queue the drift is judged by */
      if (dsp_space_ok)
      {
         audio_buf_info info;
         if (ioctl(dsp_fd, SNDCTL_DSP_GETOSPACE, &info) == 0 && info.fragstotal > 0 && info.fragsize > 0)
         {
            long total = (long)info.fragstotal * info.fragsize / 4;
            long queued = total - info.bytes / 4;
            pthread_mutex_lock(&ring_mutex);
            if (dsp_space_ok < 0)
               fprintf(stderr, "tqrun: sound FIFO %ld frames (%d x %d bytes)\n", total,
                     info.fragstotal, info.fragsize);
            dsp_space_ok = 1;
            dsp_total_frames = total;
            dsp_queued_frames = queued < 0 ? 0 : queued;
            pthread_mutex_unlock(&ring_mutex);
         }
         else
         {
            fprintf(stderr, "tqrun: SNDCTL_DSP_GETOSPACE unsupported: only a growing queue is trimmed\n");
            dsp_space_ok = 0;
         }
      }
   }
   return NULL;
}

static void ring_start(void)
{
   if (dsp_fd < 0 || ring_running)
      return;
   ring_running = 1;
   pthread_create(&ring_thread, NULL, ring_proc, NULL);
}

static void ring_stop(void)
{
   if (!ring_running)
      return;
   pthread_mutex_lock(&ring_mutex);
   ring_quit = 1;
   pthread_cond_broadcast(&ring_cond);
   pthread_mutex_unlock(&ring_mutex);
   pthread_join(ring_thread, NULL);
   ring_running = 0;
}

static void ring_push(const int16_t *data, size_t frames)
{
   unsigned long long fill;
   size_t i;
   int adjust = 0;	/* +1: repeat the last frame, -1: leave the last frame out */

   pthread_mutex_lock(&ring_mutex);
   fill = ring_in - ring_out;
   if (!ring_prebuffer && frames > 1)
   {
      if (dsp_space_ok > 0)
      {
         /* audioserver pulls the FIFO into a buffer of its own that can't be seen from here, so
          * the ring is nearly empty while all is well; it grows only when the game makes more
          * than is played (48002 a second measured: the panel runs a hair fast) */
         long queued = (long)fill + dsp_queued_frames;
         if (queued > dsp_total_frames + RING_TARGET)
            adjust = -1;
      }
      else if (fill > RING_TARGET * 3)
         adjust = -1;
   }
   if (adjust < 0)
   {
      frames--;
      st_ring_skips++;
   }
   /* full (only without the panel pacing, or if the sound stopped): audio sync waits */
   while (ring_in - ring_out + frames + 1 > RING_FRAMES && !ring_quit)
   {
      if (!audio_sync)
      {
         st_audio_dropped += frames;
         pthread_mutex_unlock(&ring_mutex);
         return;
      }
      pthread_cond_wait(&ring_cond, &ring_mutex);
   }
   for (i = 0; i < frames; i++)
   {
      size_t k = (size_t)((ring_in + i) % RING_FRAMES) * 2;
      ring[k] = data[i * 2];
      ring[k + 1] = data[i * 2 + 1];
   }
   ring_in += frames;
   if (adjust > 0)
   {
      size_t k = (size_t)(ring_in % RING_FRAMES) * 2;
      ring[k] = data[(frames - 1) * 2];
      ring[k + 1] = data[(frames - 1) * 2 + 1];
      ring_in++;
      st_ring_dups++;
   }
   pthread_cond_broadcast(&ring_cond);
   pthread_mutex_unlock(&ring_mutex);
}

static size_t RETRO_CALLCONV audio_batch(const int16_t *data, size_t frames)
{
   const uint8_t *p = (const uint8_t *)data;
   size_t left = frames * 4;
   double t0 = now_usec();

   st_audio_frames += frames;
   if (dsp_fd < 0)
      return frames;
   if (vsync)
   {
      ring_start();
      ring_push(data, frames);
      st_awrite_usec += now_usec() - t0;
      return frames;
   }
   while (left)
   {
      ssize_t n = write(dsp_fd, p, left);
      if (n < 0)
      {
         if (errno == EINTR)
            continue;
         /* without audio sync: what doesn't fit is dropped, as RetroArch does */
         st_audio_dropped += left / 4;
         break;
      }
      p += n;
      left -= n;
   }
   st_awrite_usec += now_usec() - t0;
   return frames;
}

static void RETRO_CALLCONV audio_sample(int16_t l, int16_t r)
{
   int16_t s[2] = { l, r };
   audio_batch(s, 1);
}

/* ---- buttons ------------------------------------------------------------------------------- */

static int input_fd = -1;
static uint16_t pad;	/* RETRO_DEVICE_ID_JOYPAD_* bits */

static int key_to_pad(int code)
{
   switch (code)
   {
   case KEY_UP:        return RETRO_DEVICE_ID_JOYPAD_UP;
   case KEY_DOWN:      return RETRO_DEVICE_ID_JOYPAD_DOWN;
   case KEY_LEFT:      return RETRO_DEVICE_ID_JOYPAD_LEFT;
   case KEY_RIGHT:     return RETRO_DEVICE_ID_JOYPAD_RIGHT;
   case KEY_SPACE:     return RETRO_DEVICE_ID_JOYPAD_A;
   case KEY_LEFTCTRL:  return RETRO_DEVICE_ID_JOYPAD_B;
   case KEY_LEFTSHIFT: return RETRO_DEVICE_ID_JOYPAD_X;
   case KEY_LEFTALT:   return RETRO_DEVICE_ID_JOYPAD_Y;
   case KEY_E:         return RETRO_DEVICE_ID_JOYPAD_L;
   case KEY_T:         return RETRO_DEVICE_ID_JOYPAD_R;
   case KEY_TAB:       return RETRO_DEVICE_ID_JOYPAD_L2;
   case KEY_BACKSPACE: return RETRO_DEVICE_ID_JOYPAD_R2;
   case KEY_RIGHTCTRL: return RETRO_DEVICE_ID_JOYPAD_SELECT;
   case KEY_ENTER:     return RETRO_DEVICE_ID_JOYPAD_START;
   }
   return -1;
}

static void RETRO_CALLCONV input_poll(void)
{
   struct input_event ev[16];
   ssize_t n;

   if (input_fd < 0)
      return;
   while ((n = read(input_fd, ev, sizeof(ev))) > 0)
   {
      int i;
      for (i = 0; i < n / (ssize_t)sizeof(ev[0]); i++)
      {
         int id;
         if (ev[i].type != EV_KEY || ev[i].value == 2)
            continue;
         if (ev[i].code == KEY_ESC && ev[i].value)
         {
            quit = 1;	/* Menu */
            continue;
         }
         id = key_to_pad(ev[i].code);
         if (id < 0)
            continue;
         if (ev[i].value)
            pad |= 1u << id;
         else
            pad &= ~(1u << id);
      }
   }
}

static int16_t RETRO_CALLCONV input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
   (void)index;
   if (port != 0 || device != RETRO_DEVICE_JOYPAD)
      return 0;
   if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
      return (int16_t)pad;
   return id < 16 ? (pad >> id) & 1 : 0;
}

/* ---- environment --------------------------------------------------------------------------- */

static char save_dir[512], system_dir[512];
static int shutdown_req;

static void RETRO_CALLCONV log_printf(enum retro_log_level level, const char *fmt, ...)
{
   static const char *names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
   va_list ap;
   if (level == RETRO_LOG_DEBUG && verbose < 2)
      return;
   fprintf(stderr, "[%s] ", names[level & 3]);
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

static retro_time_t RETRO_CALLCONV perf_time_usec(void) { return (retro_time_t)now_usec(); }
static retro_perf_tick_t RETRO_CALLCONV perf_counter(void) { return (retro_perf_tick_t)now_usec(); }
static uint64_t RETRO_CALLCONV perf_cpu_features(void) { return RETRO_SIMD_NEON | RETRO_SIMD_VFPV4; }
static void RETRO_CALLCONV perf_register(struct retro_perf_counter *c) { c->registered = true; }
static void RETRO_CALLCONV perf_start(struct retro_perf_counter *c) { c->start = perf_counter(); }
static void RETRO_CALLCONV perf_stop(struct retro_perf_counter *c) { c->total += perf_counter() - c->start; c->call_cnt++; }
static void RETRO_CALLCONV perf_log(void) {}

static bool RETRO_CALLCONV environment(unsigned cmd, void *data)
{
   switch (cmd)	/* the labels carry RETRO_ENVIRONMENT_EXPERIMENTAL where the call has it */
   {
   case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      ((struct retro_log_callback *)data)->log = log_printf;
      return true;
   case RETRO_ENVIRONMENT_GET_PERF_INTERFACE:
   {
      struct retro_perf_callback *p = data;
      p->get_time_usec = perf_time_usec;
      p->get_cpu_features = perf_cpu_features;
      p->get_perf_counter = perf_counter;
      p->perf_register = perf_register;
      p->perf_start = perf_start;
      p->perf_stop = perf_stop;
      p->perf_log = perf_log;
      return true;
   }
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
   case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER:
      return get_framebuffer(data);
   case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      *(bool *)data = true;
      return true;
   case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      return true;
   case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
      *(int *)data = RETRO_AV_ENABLE_VIDEO | RETRO_AV_ENABLE_AUDIO;
      return true;
   case RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE:
      *(unsigned *)data = OUT_RATE;
      return true;
   case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE:
      *(float *)data = 59.67f;
      return true;
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = save_dir[0] ? save_dir : NULL;
      return save_dir[0] != 0;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      *(const char **)data = system_dir[0] ? system_dir : NULL;
      return system_dir[0] != 0;
   case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      *(unsigned *)data = 0;	/* the core then sets plain variables, default first */
      return true;
   case RETRO_ENVIRONMENT_SET_VARIABLES:
   {
      const struct retro_variable *v;
      for (v = data; v->key; v++)
      {
         /* "Description; default|other|..." */
         char def[128];
         const char *s = strstr(v->value, "; ");
         size_t n;
         if (!s)
            continue;
         s += 2;
         n = strcspn(s, "|");
         if (n >= sizeof(def))
            n = sizeof(def) - 1;
         memcpy(def, s, n);
         def[n] = 0;
         opt_set(v->key, def, 0);
      }
      return true;
   }
   case RETRO_ENVIRONMENT_GET_VARIABLE:
   {
      struct retro_variable *v = data;
      struct opt *o = opt_find(v->key);
      v->value = o ? o->value : NULL;
      return o != NULL;
   }
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool *)data = false;
      return true;
   case RETRO_ENVIRONMENT_SET_MESSAGE:
      fprintf(stderr, "[MSG] %s\n", ((const struct retro_message *)data)->msg);
      return true;
   case RETRO_ENVIRONMENT_SHUTDOWN:
      shutdown_req = 1;
      return true;
   case RETRO_ENVIRONMENT_SET_GEOMETRY:
   {
      const struct retro_game_geometry *g = data;
      frame_w = g->base_width;
      frame_h = g->base_height;
      return true;
   }
   case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
   case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
   case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
   case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
   case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
   case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
      return true;
   }
   return false;
}

/* ---- the resume state -----------------------------------------------------------------------
 * As mr-finster's miyoo64 ports do (MIT, docs/SLEEP-AND-WAKE.md): leaving the game (Menu,
 * Onion's SIGTERM at power-off) writes the core's state; the next start loads it. Written to a
 * new file, synced, then renamed over the old one, so a power-off in the middle keeps the last
 * good state. A state from another build of the core isn't loaded: it is kept as .rejected. */
static const char *state_path;
static char state_build[64];	/* the core file's size and time: a rebuilt core rejects old states */

#define STATE_MAGIC "TQRS0001"

static int state_save(void)
{
   size_t size, nb;
   void *buf;
   char tmp[600];
   FILE *f;
   double t0 = now_usec();

   if (!state_path || !core.serialize_size || !core.serialize)
      return 0;
   size = core.serialize_size();
   if (!size || !(buf = malloc(size)))
      return 0;
   if (!core.serialize(buf, size))
   {
      fprintf(stderr, "tqrun: the core wrote no state\n");
      free(buf);
      return 0;
   }
   snprintf(tmp, sizeof(tmp), "%s.tmp", state_path);
   f = fopen(tmp, "wb");
   if (!f)
   {
      fprintf(stderr, "tqrun: %s: %s\n", tmp, strerror(errno));
      free(buf);
      return 0;
   }
   nb = fwrite(STATE_MAGIC, 8, 1, f) + fwrite(state_build, sizeof(state_build), 1, f)
         + fwrite(&size, sizeof(size), 1, f) + fwrite(buf, size, 1, f);
   free(buf);
   if (nb != 4 || fflush(f) || fsync(fileno(f)))
   {
      fprintf(stderr, "tqrun: %s: write failed\n", tmp);
      fclose(f);
      remove(tmp);
      return 0;
   }
   fclose(f);
   if (rename(tmp, state_path))
   {
      fprintf(stderr, "tqrun: %s: %s\n", state_path, strerror(errno));
      return 0;
   }
   fprintf(stderr, "tqrun: state saved (%zu bytes, %.0f ms)\n", size, (now_usec() - t0) / 1000.0);
   return 1;
}

static void state_reject(const char *why)
{
   char rej[600];
   snprintf(rej, sizeof(rej), "%s.rejected", state_path);
   rename(state_path, rej);
   fprintf(stderr, "tqrun: state not loaded: %s; kept as %s\n", why, rej);
}

static void state_load(void)
{
   char magic[8], build[sizeof(state_build)];
   size_t size;
   void *buf;
   FILE *f;

   if (!state_path || !core.unserialize || !(f = fopen(state_path, "rb")))
      return;
   if (fread(magic, 8, 1, f) != 1 || memcmp(magic, STATE_MAGIC, 8)
         || fread(build, sizeof(build), 1, f) != 1 || fread(&size, sizeof(size), 1, f) != 1
         || size == 0 || size > 64u << 20)
   {
      fclose(f);
      state_reject("not a tqrun state");
      return;
   }
   if (memcmp(build, state_build, sizeof(build)))
   {
      fclose(f);
      state_reject("written by another build of the core");
      return;
   }
   buf = malloc(size);
   if (!buf || fread(buf, size, 1, f) != 1)
   {
      free(buf);
      fclose(f);
      state_reject("cut short");
      return;
   }
   fclose(f);
   if (core.unserialize(buf, size))
      fprintf(stderr, "tqrun: state loaded (%zu bytes)\n", size);
   else
      state_reject("the core refused it");
   free(buf);
}

/* ---- main ---------------------------------------------------------------------------------- */

/* start.S hands these to __libc_start_main (no crt1.o / crti.o: see there) */
extern void (*__preinit_array_start[])(int, char **, char **);
extern void (*__preinit_array_end[])(int, char **, char **);
extern void (*__init_array_start[])(int, char **, char **);
extern void (*__init_array_end[])(int, char **, char **);
extern void (*__fini_array_start[])(void);
extern void (*__fini_array_end[])(void);

void tqrun_init(int argc, char **argv, char **envp)
{
   size_t i;
   for (i = 0; i < (size_t)(__preinit_array_end - __preinit_array_start); i++)
      __preinit_array_start[i](argc, argv, envp);
   for (i = 0; i < (size_t)(__init_array_end - __init_array_start); i++)
      __init_array_start[i](argc, argv, envp);
}

void tqrun_fini(void)
{
   size_t i = __fini_array_end - __fini_array_start;
   while (i-- > 0)
      __fini_array_start[i]();
}

static void on_signal(int sig)
{
   (void)sig;
   quit = 1;
}

static void *core_sym(void *h, const char *name)
{
   void *p = dlsym(h, name);
   if (!p)
      fprintf(stderr, "tqrun: the core has no %s\n", name);
   return p;
}

int main(int argc, char **argv)
{
   const char *core_path = NULL, *opt_path = NULL, *content;
   unsigned long max_frames = 0;
   char self_dir[512], default_core[600];
   struct retro_game_info game;
   struct retro_system_av_info av;
   void *h;
   int c;
   double t_start, t_end, t_run0;

   while ((c = getopt(argc, argv, "L:o:s:n:V:A:l:S:v")) != -1)
   {
      switch (c)
      {
      case 'L': core_path = optarg; break;
      case 'o': opt_path = optarg; break;
      case 's': snprintf(save_dir, sizeof(save_dir), "%s", optarg); break;
      case 'n': max_frames = strtoul(optarg, NULL, 10); break;
      case 'V': vsync = atoi(optarg); break;
      case 'A': audio_sync = atoi(optarg); break;
      case 'l': audio_latency = atoi(optarg); break;
      case 'S': state_path = optarg; break;
      case 'v': verbose = 2; break;
      default:
         fprintf(stderr, "usage: tqrun [-L core.so] [-o options] [-s savedir] [-n frames] [-V 0|1] [-A 0|1] [-l ms] content\n");
         return 2;
      }
   }
   if (optind >= argc)
   {
      fprintf(stderr, "tqrun: no content\n");
      return 2;
   }
   content = argv[optind];

   if (!core_path)
   {
      ssize_t n = readlink("/proc/self/exe", self_dir, sizeof(self_dir) - 1);
      char *slash;
      self_dir[n > 0 ? n : 0] = 0;
      slash = strrchr(self_dir, '/');
      if (slash)
         *slash = 0;
      snprintf(default_core, sizeof(default_core), "%s/tyrquake_libretro.so", self_dir[0] ? self_dir : ".");
      core_path = default_core;
   }
   if (opt_path)
      opts_load(opt_path);

   h = dlopen(core_path, RTLD_NOW | RTLD_LOCAL);
   if (!h)
   {
      fprintf(stderr, "tqrun: %s\n", dlerror());
      return 1;
   }
   *(void **)&core.init = core_sym(h, "retro_init");
   *(void **)&core.deinit = core_sym(h, "retro_deinit");
   *(void **)&core.set_environment = core_sym(h, "retro_set_environment");
   *(void **)&core.set_video_refresh = core_sym(h, "retro_set_video_refresh");
   *(void **)&core.set_audio_sample = core_sym(h, "retro_set_audio_sample");
   *(void **)&core.set_audio_sample_batch = core_sym(h, "retro_set_audio_sample_batch");
   *(void **)&core.set_input_poll = core_sym(h, "retro_set_input_poll");
   *(void **)&core.set_input_state = core_sym(h, "retro_set_input_state");
   *(void **)&core.get_system_av_info = core_sym(h, "retro_get_system_av_info");
   *(void **)&core.set_controller_port_device = core_sym(h, "retro_set_controller_port_device");
   *(void **)&core.load_game = core_sym(h, "retro_load_game");
   *(void **)&core.unload_game = core_sym(h, "retro_unload_game");
   *(void **)&core.run = core_sym(h, "retro_run");
   *(void **)&core.serialize_size = core_sym(h, "retro_serialize_size");
   *(void **)&core.serialize = core_sym(h, "retro_serialize");
   *(void **)&core.unserialize = core_sym(h, "retro_unserialize");
   if (!core.init || !core.run || !core.load_game || !core.set_environment)
      return 1;

   signal(SIGTERM, on_signal);
   signal(SIGINT, on_signal);

   if (!video_init())
      return 1;
   audio_init();
   input_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
   if (input_fd < 0)
      fprintf(stderr, "tqrun: /dev/input/event0: %s (no buttons)\n", strerror(errno));

   core.set_environment(environment);
   core.set_video_refresh(video_refresh);
   core.set_audio_sample(audio_sample);
   core.set_audio_sample_batch(audio_batch);
   core.set_input_poll(input_poll);
   core.set_input_state(input_state);
   core.init();

   memset(&game, 0, sizeof(game));
   game.path = content;
   if (!core.load_game(&game))
   {
      fprintf(stderr, "tqrun: the core couldn't load %s\n", content);
      video_shutdown();
      return 1;
   }
   core.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
   core.get_system_av_info(&av);
   fprintf(stderr, "tqrun: %ux%u, %.2f fps, sound %.0f Hz, vsync %d, audio sync %d\n",
         av.geometry.base_width, av.geometry.base_height, av.timing.fps, av.timing.sample_rate,
         vsync, audio_sync);
   if ((int)av.timing.sample_rate != OUT_RATE)
      fprintf(stderr, "tqrun: WARNING: the core makes %.0f Hz, the sound is played at %d Hz\n",
            av.timing.sample_rate, OUT_RATE);

   {
      struct stat st;
      if (!__xstat(3, core_path, &st))	/* stat is a real symbol only since glibc 2.33 */
         snprintf(state_build, sizeof(state_build), "%lld/%lld", (long long)st.st_size, (long long)st.st_mtime);
   }

   t_start = now_usec();
   while (!quit && !shutdown_req && (!max_frames || st_frames < max_frames))
   {
      t_run0 = now_usec();
      core.run();
      st_run_usec += now_usec() - t_run0;
      /* as RetroArch's auto-load: after the first frame (the core waits a few frames for it) */
      if (st_frames == 1)
         state_load();
   }
   t_end = now_usec();

   /* the last frame on screen before the core goes */
   pthread_mutex_lock(&present_mutex);
   while (present_buf >= 0)
      pthread_cond_wait(&present_cond, &present_mutex);
   pthread_mutex_unlock(&present_mutex);

   /* leaving the game (Menu, SIGTERM): the resume state; quitting from Quake's own menu ends
    * the game, and the next start begins afresh */
   if (shutdown_req)
   {
      if (state_path && !remove(state_path))
         fprintf(stderr, "tqrun: quit from the game: resume state removed\n");
   }
   else if (quit)
      state_save();
   ring_stop();

   if (core.unload_game)
      core.unload_game();
   if (core.deinit)
      core.deinit();
   video_shutdown();
   if (dsp_fd >= 0)
      close(dsp_fd);

   if (st_frames)
   {
      double n = st_frames, np = st_presented ? st_presented : 1;
      fprintf(stderr, "tqrun: %lu frames in %.2f s (%.2f fps), retro_run avg %.2f ms "
            "(of which sound write %.2f, waiting for the panel %.2f)\n",
            st_frames, (t_end - t_start) / 1e6, n / ((t_end - t_start) / 1e6),
            st_run_usec / n / 1000.0, st_awrite_usec / n / 1000.0, st_vwait_usec / n / 1000.0);
      fprintf(stderr, "tqrun: sound: %lu frames repeated, %lu left out\n", st_ring_dups, st_ring_skips);
      fprintf(stderr, "tqrun: shown %lu, dropped %lu, dupes %lu; present thread: convert %.2f ms, "
            "pan %.2f ms per shown frame; sound %lu frames, %lu dropped\n",
            st_presented, st_dropped, st_dupes, st_convert_usec / np / 1000.0,
            st_pan_usec / np / 1000.0, st_audio_frames, st_audio_dropped);
   }
   return 0;
}
