/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/
#include "smal/explicit_roots.h"

void smal_collect_before_inner(void *top_of_stack)
{
}
void smal_collect_before_mark() { }
void smal_collect_after_mark() { }
void smal_collect_before_sweep() { }
void smal_collect_after_sweep() { }
void smal_collect_mark_roots()
{
   smal_roots_mark_chain();
}

