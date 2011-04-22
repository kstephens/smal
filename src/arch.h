#ifndef _smal_ARCH_H
#define _smal_ARCH_H

#if defined(sparc) || defined(__sparc__)
static inline void
flush_register_windows(void)
{
    asm
#ifdef __GNUC__
        volatile
#endif
# if defined(__sparc_v9__) || defined(__sparcv9) || defined(__arch64__)
        ("flushw")
# else
        ("ta  0x03")
# endif /* trap always to flush register windows if we are on a Sparc system */
        ;
}
#  define smal_FLUSH_REGISTER_WINDOWS flush_register_windows()
#elif defined(__ia64)
void *smal_ia64_bsp(void);
void smal_ia64_flushrs(void);
#  define smal_FLUSH_REGISTER_WINDOWS smal_ia64_flushrs()
#else
#  define smal_FLUSH_REGISTER_WINDOWS ((void)0)
#endif

#endif
