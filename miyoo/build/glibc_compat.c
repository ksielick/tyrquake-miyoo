/* stat() became a real symbol in glibc 2.33; glibc 2.28 (the Miyoo) only has __xstat.
 * Linked with -Wl,--wrap=stat. */
#include <sys/stat.h>

extern int __xstat(int ver, const char *path, struct stat *buf);
__asm__(".symver __xstat,__xstat@GLIBC_2.4");

int __wrap_stat(const char *path, struct stat *buf)
{
	return __xstat(3, path, buf); /* _STAT_VER_LINUX */
}
