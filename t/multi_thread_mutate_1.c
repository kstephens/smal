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

struct my_thread_data {
  char *name;
  smal_thread *t;
  size_t alloc_n, free_n, discard_n;
};
static struct my_thread_data global_data = { "m" };
static
void my_thread_data_print(struct my_thread_data *d)
{
  fprintf(stderr, 
	  "\n%s thread t@%p DONE \n"
	  "  bottom_of_stack %p \n"
	  "  (alloc %lu, free %lu + discard %lu = %lu) \n", 
	  d->name, d->t,
	  d->t->bottom_of_stack,
	  (unsigned long) d->alloc_n, (unsigned long) d->free_n, (unsigned long) d->discard_n,  
	  (unsigned long) (d->free_n + d->discard_n));
}

static
void* thread_func(void *arg)
{
  int i;
  struct my_thread_data *d = arg;
  my_cons *x = 0, *y = 0;
  smal_thread *t = smal_thread_self();
  smal_roots_2(x, y);

  d->t = t;
  t->bottom_of_stack = &arg;
  t->user_data[0] = arg;

  fprintf(stderr, "%s thread t@%p pthread pt@%p \n", (char*) d->name, d->t, (void*) pthread_self());

  for ( i = 0; i < alloc_n; ++ i ) {
    // printf("%s+", (char*) arg);
    smal_alloc_p(my_cons_type, (void**) &y);
    d->alloc_n ++;
    y->car = ((void*) 0) + 1;
    y->cdr = 0;
    switch ( rand() % 4 ) {
    case 0:
      // printf("%s>", d->name);
      y->car = x ? x->car + 1 : 0;
      y->cdr = x;
      x = y;
      break;

    case 1:
      // printf("%s-", d->name);
      if ( x ) {
	smal_free(x);
	x = x->cdr;
	d->discard_n ++; // y
      } else {
	smal_free(y);
      }
      d->free_n ++;
      break;

    case 2: 
      x = 0;
      d->discard_n += x ? (size_t) x->car : 0; // x...
      d->discard_n ++;
      break;

    default:
      d->discard_n ++; // y
    }

    if ( rand() % 1000 ) {
      //printf("%sG ", d->name);
      smal_collect();
    }

    // fprintf(stderr, "  t@%p alloced %p\n", t, x);
    usleep(rand() % 100);
  }

  d->discard_n += x ? (size_t) x->car : 0; // x...

  // my_print_stats();

  smal_roots_end();

  smal_thread_died(0);

  return arg;
}

int main(int argc, char **argv)
{
  int n_threads = 4;
  pthread_t child_threads[4];
  struct my_thread_data thread_data[4];
  int i;

  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);

  smal_thread_init();

  for ( i = 0; i < n_threads; ++ i ) {
    struct my_thread_data *d = thread_data + i;
    char *thread_name = malloc(3);
    sprintf(thread_name, "%d", i);
    memset(d, 0, sizeof(*d));
    d->name = thread_name;
    pthread_create(&child_threads[i], 0, thread_func, d) ;
  }
  thread_func(&global_data);
  fprintf(stderr, "  main finished\n");
  my_print_stats();

  fprintf(stderr, "  main joining\n");

  for ( i = 0; i < n_threads; ++ i ) {
    void *thread_result = 0;
    pthread_join(child_threads[i], &thread_result);
    global_data.alloc_n += thread_data[i].alloc_n;
    global_data.free_n += thread_data[i].free_n;
    global_data.discard_n += thread_data[i].discard_n;
  }

  fprintf(stderr, "  main collecting\n");
  smal_collect();
  my_print_stats();
  // smal_buffer_print_all(0, "after collect");

  fprintf(stderr, "\n FINAL: \n");
  my_thread_data_print(&global_data);

  fprintf(stderr, "  main exiting\n");

  return 0;
}

#else

#include <stdio.h>

int main(int argc, char **argv)
{
  fprintf(stderr, "no pthread support: skipping %s\n", __FILE__);
  return 0;
}

#endif
