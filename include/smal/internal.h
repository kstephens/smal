#ifndef _smal_INTERNAL_H
#define _smal_INTERNAL_H

#include "smal/smal.h"

#define smal_likely(x)       __builtin_expect((x) != 0,1)
#define smal_unlikely(x)     __builtin_expect((x) != 0,0)

#endif
