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
  for ( i = 0; smal_stats_names[i]; ++ i ) {
    fprintf(stdout, "  %16lu %s\n", (unsigned long) (((size_t*) &stats)[i]), smal_stats_names[i]);
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
void smal_collect_mark_roots()
{
  smal_thread *thr = smal_thread_self();
  smal_mark_ptr_range(&thr->registers, &thr->registers + 1);
  smal_roots_mark_chain(0);
}

void my_count_object(smal_type *type, void *ptr, void *arg)
{
  //  fprintf(stderr, "  type %p obj %p\n", type, ptr);
  (* (int *) arg) ++; 
}

int main(int argc, char **argv)
{
  unsigned long 
    smal_alloc_n = 0, 
    smal_each_object_n = 0, 
    smal_collect_n = 0;

  int alloc_id;
  my_cons *x = 0, *y = 0;
  smal_roots_2(x, y);
  bottom_of_stack = &argv;
  
  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);
  
  for ( alloc_id = 0; alloc_id < 10000000; ++ alloc_id ) {
    int action = rand() % 10;
    x = smal_alloc(my_cons_type);
    ++ smal_alloc_n;
    
    x->car = x->cdr = 0;
    
    switch ( action ) {
    case 0:
      y = x; 
      break;
    case 1:
      if ( y )
	y->car = x;
      else
	y = x;
      break;
    case 2:
      if ( y )
	y->cdr = x;
      else
	y = x;
      break;
    }

#if 0
    fprintf(stderr, "%d", action);
    fflush(stderr);
#endif

    if ( rand() % 100 == 0 ) {
      // fprintf(stderr, "\nGC\n");
      smal_collect();
      ++ smal_collect_n;
      // my_print_stats();
    }
    
    if ( rand() % 100 == 50 ) {
      int obj_count = 0;
      smal_each_object(my_count_object, &obj_count);
      ++ smal_each_object_n;
      // my_print_stats();
      // fprintf(stderr, "  object_count = %d\n", obj_count);
    }
  }

  my_print_stats();
  
  x = y = 0;
  smal_collect();

  smal_roots_end();

  {
    char cmd[1024];
    sprintf(cmd, "/bin/ps -l -p %d", getpid());
    system(cmd);
  }
  
  my_print_stats();
  
  smal_shutdown();
  
  fprintf(stdout, "\nOK\n");
  fprintf(stdout, "%lu smal_alloc\n", smal_alloc_n);
  fprintf(stdout, "%lu smal_each_object\n", smal_each_object_n);
  fprintf(stdout, "%lu smal_collect\n", smal_collect_n);
  
  return 0;
}

