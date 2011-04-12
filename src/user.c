#include "smal/smal.h"

extern void _smal_collect_inner();

extern void smal_before_collect_inner(void *top_of_stack);

void smal_collect()
{
  auto void *top_of_stack = &top_of_stack;
  smal_before_collect_inner(top_of_stack);
  _smal_collect_inner();
}
