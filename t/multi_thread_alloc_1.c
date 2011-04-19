/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"

#if SMAL_PTHREAD

#include "my_cons.h"
#include "roots_explicit.h"

static size_t alloc_n = 100000;

static void* thread_func(void *arg)
{
  int i;

  fprintf(stderr, "thread t@%p pthread pt@%p \n", smal_thread_self(), (void*) pthread_self());

  for ( i = 0; i < alloc_n; ++ i ) {
    my_cons *x;
    x = smal_alloc(my_cons_type);
    // fprintf(stderr, "  t@%p alloced %p\n", smal_thread_self(), x);
    usleep(rand() % 100);
  }
  my_print_stats();

  fprintf(stderr, "\nthread t@%p pthread pt@%p DONE\n", smal_thread_self(), (void*) pthread_self());

  return arg;
}

int main(int argc, char **argv)
{
  int n_threads = 4;
  pthread_t child_threads[4];
  int i;

  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);

  smal_thread_init();

  for ( i = 0; i < n_threads; ++ i ) {
    pthread_create(&child_threads[i], 0, thread_func, (void*) 0);
  }
  thread_func(0);
  fprintf(stderr, "  parent finished\n");
  my_print_stats();

  fprintf(stderr, "  parent joining\n");

  for ( i = 0; i < n_threads; ++ i ) {
    void *thread_result = 0;
    pthread_join(child_threads[i], &thread_result);
  }

  fprintf(stderr, "  parent collecting\n");
  smal_collect();
  my_print_stats();
  smal_buffer_print_all(0, "after collect");

  fprintf(stderr, "  parent exiting\n");
  my_print_stats();

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
