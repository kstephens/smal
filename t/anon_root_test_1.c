/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "my_cons.h"
#include "roots_conservative.h"

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
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 1);
    assert(stats.free_id == 0);
  }

  y = smal_alloc(my_cons_type);
  smal_collect();
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 0);
  }

  x->cdr = y;
  y = 0;
  smal_collect();
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 0);
  }

  x->cdr = 0;
  y = 0;
  smal_collect();
  my_print_stats();
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
  
  my_print_stats();
  smal_shutdown();
    
  return 0;
}

