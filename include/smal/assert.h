#ifndef _smal_ASSERT_H
#define _smal_ASSERT_H

#ifdef SMAL_UNIT_TEST
#undef NASSERT
#undef smal_NASSERT
#endif

#if NASSERT || smal_NASSERT
#define smal_ASSERT(EXPR, TEST)	(EXPR)
#else
#define smal_ASSERT(EXPR, TEST)						\
  ({									\
    int __result = (EXPR);						\
    if ( ! (__result TEST) ) {						\
      extern const char * const sys_errlist[];				\
      fprintf(stderr, "\nSMAL %s:%d in %s: FAILED: %s %s, returned %d (%s)\n", \
	      __FILE__, __LINE__, __FUNCTION__,				\
	      #EXPR, #TEST, __result, sys_errlist[__result]);		\
      abort();								\
    }									\
    __result;								\
  })

#endif

#endif