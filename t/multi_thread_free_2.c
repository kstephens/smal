/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"

#if SMAL_PTHREAD

#include "my_cons.h"
#include "roots_explicit.h"

static size_t alloc_n = 8000;

static void* thread_func(void *arg)
{
  int i;
  int ptrs_n = 1000;
  void *ptrs[ptrs_n];
  int ptrs_i = 0;

  memset(ptrs, 0, sizeof(ptrs[0]) * ptrs_n);

  fprintf(stderr, "thread %s @%p pthread @%p START\n", (char*) arg, smal_thread_self(), (void*) pthread_self());

  for ( i = 0; i < alloc_n; ++ i ) {
    my_cons *x;
    // fprintf(stderr, "%s", (char*) arg);
    if ( (x = ptrs[ptrs_i]) )
      smal_free(x);
    x = smal_alloc(my_cons_type);
    ptrs[ptrs_i] = x;
    ptrs_i = (ptrs_i + 1) % ptrs_n;
    // fprintf(stderr, "  t@%p alloced %p\n", smal_thread_self(), x);
    usleep(rand() % 100);
    if ( rand() % 2 == 0 ) {
      int pi = rand() % ptrs_n;
      x = ptrs[pi];
      if ( x ) smal_free(x);
      ptrs[pi] = 0;
      usleep(rand() % 100);
    }
  }

  for ( i = 0; i < ptrs_n; ++ i ) {
    my_cons *x;
    if ( (x = ptrs[i]) )
      smal_free(x);
  }

  fprintf(stderr, "\nthread %s @%p pthread @%p DONE\n", (char*) arg, smal_thread_self(), (void*) pthread_self());
  my_print_stats();

  return arg;
}

int main(int argc, char **argv)
{
  void *thread_result = 0;
  pthread_t child_thread_1;
  pthread_t child_thread_2;

  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);

  smal_thread_init();

  pthread_create(&child_thread_1, 0, thread_func, "1");
  pthread_create(&child_thread_2, 0, thread_func, "2");
  thread_func("M");

  fprintf(stderr, "  parent joining\n");

  pthread_join(child_thread_1, &thread_result);
  pthread_join(child_thread_2, &thread_result);

  fprintf(stderr, "  parent exiting\n");

  my_print_stats();

  fprintf(stderr, "\n%s OK\n", argv[0]);
  return 0;
}

#else

#include <stdio.h>

int main(int argc, char **argv)
{
  fprintf(stderr, "skipping %s\n", __FILE__);

  fprintf(stderr, "\n%s OK\n", argv[0]);
  return 0;
}

#endif
