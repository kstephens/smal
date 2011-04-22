#include "smal/smal.h"
#include "smal/thread.h"
#include "arch.h"

extern void _smal_collect_inner();

extern void smal_collect_before_inner(void *top_of_stack);

void smal_collect()
{
  smal_thread *thr = smal_thread_self();
  void *top_of_stack = 0;
  smal_FLUSH_REGISTER_WINDOWS;
  setjmp(thr->registers._jb);
  getcontext(&thr->registers._ucontext);
  smal_collect_before_inner(&top_of_stack);
  _smal_collect_inner();
}
