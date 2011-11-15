/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "my_cons.h"
#include "roots_explicit.h"

static my_cons *global;

static
void run_test()
{
  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);

  global = smal_alloc(my_cons_type);
  smal_collect();
  smal_collect_wait_for_sweep();
  // my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 1);
    assert(stats.free_id == 1);
  }

  global = smal_alloc(my_cons_type);
  smal_collect();
  smal_collect_wait_for_sweep();
  // my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 2);
  }

  // Add global bindings.
  {
    void *bindings[] = { &global }; 
    smal_roots r = { bindings, 1 };
    smal_roots_add_global(&r);
  }

  global = smal_alloc(my_cons_type);
  smal_collect();
  smal_collect_wait_for_sweep();
  // my_print_stats(__LINE__);
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 3);
    assert(stats.free_id == 2);
  }

  // Remove global bindings.
  {
    void *bindings[] = { &global }; 
    smal_roots r = { bindings, 1 };
    smal_roots_remove_global(&r);
  }

  global = smal_alloc(my_cons_type);
  assert(global);
  smal_collect();
  smal_collect_wait_for_sweep();
  // my_print_stats(__LINE__);
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 4);
    assert(stats.free_id == 4);
  }
}

int main(int argc, char **argv)
{
  run_test();

  global = 0;
  // smal_collect();
  // my_print_stats();

#if 0
  {
    char cmd[1024];
    sprintf(cmd, "/bin/ps -l -p %d", getpid());
    system(cmd);
  }
#endif

  smal_shutdown();

  fprintf(stderr, "\n%s OK\n", argv[0]);
  return 0;
}

