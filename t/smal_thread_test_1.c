/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/thread.h"
#include <unistd.h>
#include <stdio.h>

static int print_thread(smal_thread *t, void *arg)
{
  fprintf(stderr, "  t %p\n", t);
  return 0;
}

static void* thread_func(void *arg)
{
  fprintf(stderr, "  child thread %p pthread %p \n", smal_thread_self(),
#if SMAL_PTHREAD
	  (void*) pthread_self()
#else
	  (void*) 0
#endif
	  );
  {
    void *stack_ptr = 0; 
    size_t stack_size = 0;
    smal_thread_getstack(smal_thread_self(), &stack_ptr, &stack_size);
  }
  smal_thread_each(print_thread, 0);
  fprintf(stderr, "  child exiting\n");
  sleep(1);
  return arg;
}

int main(int argc, char **argv)
{
  void *thread_result = 0;
#if SMAL_PTHREAD
  static pthread_t child_thread;
#endif

  smal_thread_init();

  {
    void *stack_ptr = 0; 
    size_t stack_size = 0;
    smal_thread_getstack(smal_thread_self(), &stack_ptr, &stack_size);
  }

#if SMAL_PTHREAD
  fprintf(stderr, "  parent thread %p = pthread %p\n", smal_thread_self(), (void*) pthread_self());
  pthread_create(&child_thread, 0, thread_func, 0);
  fprintf(stderr, "  parent thread %p forked child pthread %p\n", smal_thread_self(), (void*) child_thread);
#else
  thread_result = thread_func;
#endif

  sleep(1);

  fprintf(stderr, "  parent joining\n");

#if SMAL_PTHREAD
  pthread_join(child_thread, &thread_result);
#endif

  fprintf(stderr, "  parent exiting\n");

  return 0;
}
