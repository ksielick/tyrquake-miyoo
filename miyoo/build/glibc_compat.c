/* stat() became a real symbol in glibc 2.33; glibc 2.28 (the Miyoo) only has __xstat.
 * Linked with -Wl,--wrap=stat. */
#include <sys/stat.h>

extern int __xstat(int ver, const char *path, struct stat *buf);
__asm__(".symver __xstat,__xstat@GLIBC_2.4");

int __wrap_stat(const char *path, struct stat *buf)
{
	return __xstat(3, path, buf); /* _STAT_VER_LINUX */
}

#ifdef GLIBC_COMPAT_WRAP
/* LTO build: -Wl,--wrap=exp,... sends the other units here; this unit is not LTO, so its
 * .symver lines work. The versioned names aren't "exp", so --wrap leaves these calls alone. */
#include <math.h>
#include <pthread.h>
__asm__(".symver exp,exp@GLIBC_2.4");
__asm__(".symver log,log@GLIBC_2.4");
__asm__(".symver pow,pow@GLIBC_2.4");
__asm__(".symver exp2,exp2@GLIBC_2.4");
__asm__(".symver log2,log2@GLIBC_2.4");
__asm__(".symver pthread_create,pthread_create@GLIBC_2.4");
__asm__(".symver pthread_join,pthread_join@GLIBC_2.4");

double __wrap_exp(double x) { return exp(x); }
double __wrap_log(double x) { return log(x); }
double __wrap_pow(double x, double y) { return pow(x, y); }
double __wrap_exp2(double x) { return exp2(x); }
double __wrap_log2(double x) { return log2(x); }

int __wrap_pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *arg)
{
	return pthread_create(t, a, f, arg);
}

int __wrap_pthread_join(pthread_t t, void **ret)
{
	return pthread_join(t, ret);
}
#endif
