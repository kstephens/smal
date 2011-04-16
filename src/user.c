#include "smal/smal.h"

extern void _smal_collect_inner();

extern void smal_collect_before_inner(void *top_of_stack);

void smal_collect()
{
  void *top_of_stack = 0;
  smal_collect_before_inner(&top_of_stack);
  _smal_collect_inner();
}
