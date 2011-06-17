/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
  RAW ALLOC SPEED.
*/
#include "my_cons.h"
#include "roots_conservative.h"

#if 0
static
int my_count_object(smal_type *type, void *ptr, void *arg)
{
  //  fprintf(stderr, "  type %p obj %p\n", type, ptr);
  (* (int *) arg) ++; 
  return 0;
}
#endif

static
  unsigned long 
    smal_alloc_n = 0, 
    smal_each_object_n = 0, 
    smal_collect_n = 0;

static
void run_test()
{
  int alloc_id;
  my_cons *x = 0, *y = 0;

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

#if 0
    if ( rand() % 100 == 0 ) {
      // fprintf(stderr, "\nGC\n");
      smal_collect();
      ++ smal_collect_n;
      // my_print_stats();
    }
    
    if ( rand() % 100 == 0 ) {
      int obj_count = 0;
      smal_each_object(my_count_object, &obj_count);
      ++ smal_each_object_n;
      {
	smal_stats stats = { 0 };
	smal_global_stats(&stats);
	assert(obj_count == stats.live_n);
      }
      // my_print_stats();
      // fprintf(stderr, "  object_count = %d\n", obj_count);
    }
#endif
  }
  
  fprintf(stderr, "\nDONE\n");
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == smal_alloc_n);
    assert(stats.free_id < stats.alloc_id);
  }

  x = y = 0;  
}

int main(int argc, char **argv)
{
  bottom_of_stack = &argv;
  
  run_test();

#if 0
  smal_collect();
#endif
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == smal_alloc_n);
    // assert(stats.free_id == stats.alloc_id);
  }

#if 0
  smal_shutdown();
#endif

  fprintf(stdout, "\nOK\n");
  fprintf(stdout, "%lu smal_alloc\n", smal_alloc_n);
  fprintf(stdout, "%lu smal_each_object\n", smal_each_object_n);
  fprintf(stdout, "%lu smal_collect\n", smal_collect_n);
  
  fprintf(stderr, "\n%s OK\n", argv[0]);
  return 0;
}

