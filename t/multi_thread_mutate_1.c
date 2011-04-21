/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"

#if SMAL_PTHREAD

#include "my_cons.h"
#include "roots_explicit.h"

static size_t alloc_n = 100000;
// static size_t alloc_id = 0, free_id = 0;

static void* thread_func(void *arg)
{
  int i;
  size_t alloc_id = 0, free_id = 0;
  my_cons *x = 0, *y = 0;
  smal_roots_2(x, y);

  fprintf(stderr, "thread t@%p pthread pt@%p \n", smal_thread_self(), (void*) pthread_self());

  for ( i = 0; i < alloc_n; ++ i ) {
    y = smal_alloc(my_cons_type);
    ++ alloc_id;
    y->car = 0;
    y->cdr = 0;
    switch ( rand() % 4 ) {
    case 0:
      y->cdr = x;
      x = y;
      break;
    case 1:
      if ( x ) {
	smal_free(x);
	x = x->cdr;
      } else {
	smal_free(y);
      }
      ++ free_id;
      break;
    case 2:
      x = 0;
      break;
    }

    // fprintf(stderr, "  t@%p alloced %p\n", smal_thread_self(), x);
    usleep(rand() % 100);
  }

  fprintf(stderr, 
	  "\nthread t@%p pthread pt@%pDONE \n"
	  "  (alloc %lu, free %lu) \n", 
	  smal_thread_self(), (void*) pthread_self(),
	  (unsigned long) alloc_id, (unsigned long) free_id);

  my_print_stats();

  smal_roots_end();

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
  // smal_buffer_print_all(0, "after collect");

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
