/* Force-included into every unit: bind libm calls to the symbol versions in the Miyoo's
 * glibc 2.28 (the toolchain's newer glibc would pick exp@GLIBC_2.29 and friends). */
/* With LTO (build_core.sh LTO=1) these top-level asm lines are lost: glibc_compat.c (built
 * without LTO) binds them instead and the other units reach it through -Wl,--wrap. */
#if defined(__arm__) && !defined(__ASSEMBLER__) && !defined(GLIBC_COMPAT_WRAP)
__asm__(".symver exp,exp@GLIBC_2.4");
__asm__(".symver log,log@GLIBC_2.4");
__asm__(".symver pow,pow@GLIBC_2.4");
__asm__(".symver exp2,exp2@GLIBC_2.4");
__asm__(".symver log2,log2@GLIBC_2.4");
/* in libpthread.so.0 on the Miyoo (RetroArch has it loaded); the lookup
 * matches the version name, not the file */
__asm__(".symver pthread_create,pthread_create@GLIBC_2.4");
__asm__(".symver pthread_join,pthread_join@GLIBC_2.4");
#endif
