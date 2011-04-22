#include "arch.h"

#if defined(__ia64)

void smal_ia64_flushrs(void) 
{ 
  __builtin_ia64_flushrs();
}
void *smal_ia64_bsp(void) 
{ 
  return __builtin_ia64_bsp();
}

#endif
