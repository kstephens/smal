/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/explicit_roots.h"
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
void my_print_stats()
{
  smal_stats stats = { 0 };
  int i;

  smal_global_stats(&stats);
  fprintf(stderr, "\n");
  for ( i = 0; i < sizeof(stats)/sizeof(stats.alloc_id); ++ i ) {
    fprintf(stdout, "  %lu %-16s\n", ((unsigned long*) &stats)[i], smal_stats_names[i]);
  }
}

static void *bottom_of_stack;


void smal_collect_before_inner(void *top_of_stack)
{
  smal_thread *thr = smal_thread_self();
  thr->top_of_stack = top_of_stack;
  setjmp(thr->registers._jb);
}
void smal_collect_before_mark() { }
void smal_collect_after_mark() { }
void smal_collect_before_sweep() { }
void smal_collect_after_sweep() { }
void smal_mark_roots()
{
  smal_thread *thr = smal_thread_self();
  smal_mark_ptr_range(&thr->registers, &thr->registers + 1);
  smal_roots_mark_chain(0);
}

int main(int argc, char **argv)
{
  int alloc_id = 0;
  int alloc_n = 4000;
  my_cons *x = 0, *y = 0;
  smal_roots_2(x, y);
  bottom_of_stack = &argv;
  
  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);
  
  fprintf(stderr, "allocing for x list\n");
  for ( alloc_id = 0; alloc_id < alloc_n; ++ alloc_id ) {
    my_cons *c = smal_alloc(my_cons_type);
    c->car = (void*) 1;
    c->cdr = x;
    x = c;
  }
  my_print_stats();
  
  fprintf(stderr, "allocing for y list\n");
  for ( alloc_id = 0; alloc_id < alloc_n; ++ alloc_id ) {
    my_cons *c = smal_alloc(my_cons_type);
    c->car = (void*) 2;
    c->cdr = y;
    y = c;
  }

  fprintf(stderr, "dropping some of x\n");
  {
    my_cons *c;
    for ( c = x; c; c = c->cdr ) {
      if ( rand() % 10 > 5 ) {
	c->cdr = c->cdr ? ((my_cons*) c->cdr)->cdr : 0;
      }
    }
  }
  smal_collect();
  my_print_stats();

  fprintf(stderr, "allocing more for y list\n");
  for ( alloc_id = 0; alloc_id < alloc_n; ++ alloc_id ) {
    my_cons *c = smal_alloc(my_cons_type);
    c->car = (void*) 2;
    c->cdr = y;
    y = c;
  }
  smal_collect();
  my_print_stats();
  
  x = y = 0;
  smal_collect();

  smal_roots_end();
  my_print_stats();
  
  smal_shutdown();
  
  fprintf(stdout, "\nOK\n");
  
  return 0;
}

