/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "my_cons.h"
#include "roots_explicit.h"

static my_cons *global;

static my_cons *other_global;
static void *cb_handle;
static
void root_callback(void *data)
{
  assert(data == &other_global);
  smal_mark_ptr(0, other_global);
}

static
void run_test()
{
  my_cons *x = 0, *y = 0;
  smal_roots_2(x, y);

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

  global = x;
  x = 0;
  smal_collect();
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 1);
  }

  other_global = global;
  x = y = global = 0;
  smal_collect();
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 1);
  }

  smal_roots_remove_callback(cb_handle);
  cb_handle = 0;
  smal_collect();
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 2);
  }

  smal_roots_end();
}

int main(int argc, char **argv)
{
  cb_handle = smal_roots_add_callback(root_callback, &other_global);
  smal_roots_1(global);
  smal_roots_end_global();

  run_test();

  global = 0;
  smal_collect();
  my_print_stats();

  {
    char cmd[1024];
    sprintf(cmd, "/bin/ps -l -p %d", getpid());
    system(cmd);
  }
  
  smal_shutdown();

  fprintf(stderr, "\n%s OK\n", argv[0]);
  return 0;
}

