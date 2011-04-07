/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/thread.h"
#include <unistd.h>
#include <stdio.h>

void smal_mark_roots()
{
}

static void print_thread(smal_thread *t, void *arg)
{
  fprintf(stderr, "  t %p\n", t);
}

static void* thread_func(void *arg)
{
  fprintf(stderr, "  child thread %p pthread %p \n", smal_thread_self(), (void*) pthread_self());
  smal_thread_each(print_thread, 0);
  fprintf(stderr, "  child exiting\n");
  sleep(1);
  return arg;
}

int main()
{
  void *thread_result;
#if SMAL_PTHREAD
  static pthread_t child_thread;
#endif

  smal_thread_init();

#if SMAL_PTHREAD
  fprintf(stderr, "  parent thread %p = pthread %p\n", smal_thread_self(), (void*) pthread_self());
  pthread_create(&child_thread, 0, thread_func, 0);
  fprintf(stderr, "  parent thread %p forked child pthread %p\n", smal_thread_self(), (void*) child_thread);
#endif

  sleep(1);

  fprintf(stderr, "  parent joining\n");

#if SMAL_PTHREAD
  pthread_join(child_thread, &thread_result);
#endif

  fprintf(stderr, "  parent exiting\n");

  return 0;
}
