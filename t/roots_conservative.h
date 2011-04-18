/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

static void *bottom_of_stack;

void smal_collect_before_inner(void *top_of_stack)
{
  smal_thread *thr = smal_thread_self();
  thr->top_of_stack = top_of_stack;
  thr->bottom_of_stack = bottom_of_stack;
  setjmp(thr->registers._jb);
}
void smal_collect_before_mark() { }
void smal_collect_after_mark() { }
void smal_collect_before_sweep() { }
void smal_collect_after_sweep() { }
void smal_collect_mark_roots()
{
  smal_thread *thr = smal_thread_self();
  smal_mark_ptr_range(&thr->registers, &thr->registers + 1);
  smal_mark_ptr_range(thr->top_of_stack, thr->bottom_of_stack);
}

