/* LD_PRELOAD tap: appends everything a program writes to /dev/dsp (RetroArch's oss driver, after
 * its resampler) to $DSP_TAP (raw, the driver's format: int16 stereo at audio_out_rate).
 * Preload it before libpadsp.so, which forwards /dev/dsp to Onion's audioserver.
 *   arm-none-linux-gnueabihf-gcc -O2 -shared -fPIC -o dsp_tap.so dsp_tap.c -ldl */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* the Miyoo has glibc 2.28: dlsym from libdl, not 2.34's libc */
__asm__(".symver dlsym,dlsym@GLIBC_2.4");

static int dsp_fd = -1, tap_fd = -1;

static void tap_open(const char *path, int fd)
{
	static int (*next_open)(const char *, int, ...);
	const char *out;

	if (fd < 0 || strcmp(path, "/dev/dsp") || !(out = getenv("DSP_TAP")))
		return;
	if (!next_open)
		next_open = dlsym(RTLD_NEXT, "open");
	dsp_fd = fd;
	if (tap_fd < 0)
		tap_fd = next_open(out, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

#define OPEN_WRAPPER(name)                                                 \
	int name(const char *path, int flags, ...)                         \
	{                                                                  \
		static int (*next)(const char *, int, ...);                \
		va_list ap;                                                \
		mode_t mode;                                               \
		int fd;                                                    \
		va_start(ap, flags);                                       \
		mode = va_arg(ap, mode_t);                                 \
		va_end(ap);                                                \
		if (!next)                                                 \
			next = dlsym(RTLD_NEXT, #name);                    \
		fd = next(path, flags, mode);                              \
		tap_open(path, fd);                                        \
		return fd;                                                 \
	}
OPEN_WRAPPER(open)
OPEN_WRAPPER(open64)

ssize_t write(int fd, const void *buf, size_t n)
{
	static ssize_t (*next)(int, const void *, size_t);
	ssize_t r;

	if (!next)
		next = dlsym(RTLD_NEXT, "write");
	r = next(fd, buf, n);
	if (fd == dsp_fd && tap_fd >= 0 && r > 0)
		next(tap_fd, buf, r);
	return r;
}

int close(int fd)
{
	static int (*next)(int);

	if (!next)
		next = dlsym(RTLD_NEXT, "close");
	if (fd == dsp_fd)
		dsp_fd = -1;
	return next(fd);
}
