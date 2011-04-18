/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"

#if SMAL_PTHREAD

#include "my_cons.h"

static size_t alloc_n = 4000;

static void* thread_func(void *arg)
{
  int i;

  fprintf(stderr, "thread %s @%p pthread @%p \n", (char*) arg, smal_thread_self(), (void*) pthread_self());

  for ( i = 0; i < alloc_n; ++ i ) {
    my_cons *x;
    // fprintf(stderr, "%s", (char*) arg);
    x = smal_alloc(my_cons_type);
    // fprintf(stderr, "  t@%p alloced %p\n", smal_thread_self(), x);
    usleep(rand() % 100);
    if ( rand() % 2 == 0 ) {
      smal_free(x);
      usleep(rand() % 100);
    }
  }
  my_print_stats();

  fprintf(stderr, "\nthread %s @%p pthread @%p DONE\n", (char*) arg, smal_thread_self(), (void*) pthread_self());

  return arg;
}

int main(int argc, char **argv)
{
  void *thread_result = 0;
  pthread_t child_thread_1;
  pthread_t child_thread_2;

  bottom_of_stack = 0;
  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);

  smal_thread_init();

  pthread_create(&child_thread_1, 0, thread_func, "1");
  pthread_create(&child_thread_2, 0, thread_func, "2");
  thread_func("M");

  fprintf(stderr, "  parent joining\n");

  pthread_join(child_thread_1, &thread_result);
  pthread_join(child_thread_2, &thread_result);

  fprintf(stderr, "  parent exiting\n");

  return 0;
}

#else

#include <stdio.h>

int main(int argc, char **argv)
{
  fprintf(stderr, "skipping %s\n", __FILE__);
  return 0;
}

#endif
