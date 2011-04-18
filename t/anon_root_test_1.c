/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/thread.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h> /* getpid() */
#include <assert.h>

typedef void *my_oop;
typedef struct my_cons {
  my_oop car, cdr;
} my_cons;

static smal_type *my_cons_type;

static void my_cons_mark (void *ptr)
{
  smal_mark_ptr(((my_cons *) ptr)->car);
  smal_mark_ptr(((my_cons *) ptr)->cdr);
}

static
void my_print_stats(int lineno)
{
  smal_stats stats = { 0 };
  int i;

  return;
  fprintf(stdout, "\n%s:%d:\n", __FILE__, lineno);
  smal_global_stats(&stats);
  for ( i = 0; i < sizeof(stats)/sizeof(stats.alloc_id); ++ i ) {
    fprintf(stdout, "  %16lu %s\n", ((unsigned long*) &stats)[i], smal_stats_names[i]);
  }
}

static void *bottom_of_stack;

void smal_collect_before_inner(void *top_of_stack)
{
  smal_thread *thr = smal_thread_self();
  thr->top_of_stack = top_of_stack;
  thr->bottom_of_stack = bottom_of_stack;
  // fprintf(stderr, "stack [%p, %p]\n", thr->top_of_stack, thr->bottom_of_stack);
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

#if 0
static my_cons *global;
#endif

static
void run_test()
{
  my_cons *x = 0, *y = 0;
  fprintf(stderr, "  %s: &x = %p, &y = %p\n", __FILE__, &x, &y);

  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);
  
  x = smal_alloc(my_cons_type);
  smal_collect();
  my_print_stats(__LINE__);
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 1);
    assert(stats.free_id == 0);
  }

  y = smal_alloc(my_cons_type);
  smal_collect();
  my_print_stats(__LINE__);
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 0);
  }

  x->cdr = y;
  y = 0;
  smal_collect();
  my_print_stats(__LINE__);
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 0);
  }

  x->cdr = 0;
  y = 0;
  smal_collect();
  my_print_stats(__LINE__);
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 1);
  }

#if 0
  global = x;
  x = 0;
  smal_collect();
  my_print_stats(__LINE__);
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 1);
  }
#endif

  x = y = 0;
  smal_collect();
  my_print_stats(__LINE__);
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 2);
  }
}

int main(int argc, char **argv)
{
  fprintf(stderr, "  %s: &argc = %p, &argv = %p\n", __FILE__, &argc, &argv);
  bottom_of_stack = &argv;
#if 0
  {
    smal_roots_1(global);
    smal_roots_end_global();
  }
#endif

  {
    size_t pagesize = 4096;
    bottom_of_stack += pagesize - 1;
    smal_ALIGN(bottom_of_stack, pagesize);
  }
  run_test();

  smal_collect();

  {
    char cmd[1024];
    sprintf(cmd, "/bin/ps -l -p %d", getpid());
    system(cmd);
  }
  
  my_print_stats(__LINE__);
  smal_shutdown();
    
  return 0;
}

