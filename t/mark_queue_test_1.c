/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "my_cons.h"
#include "roots_explicit.h"

static my_cons *global;

static size_t actual_alloc_n;

static
my_cons *build_tree(int depth)
{
  my_cons *x = 0;
  smal_roots_1(x);

  // fprintf(stderr, " %d ", depth);
  if ( depth == 0 )
    return 0;
  x = smal_alloc(my_cons_type);
  ++ actual_alloc_n;
  depth --;
  x->car = build_tree(depth);
  x->cdr = build_tree(depth);

  smal_roots_end();
  return x;
}

static
void run_test()
{
  size_t depth = 16;
  size_t alloc_n;
  my_cons *x = 0, *y = 0;
  smal_roots_2(x, y);

  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);

  x = build_tree(depth);
  fprintf(stderr, "actual_alloc_n = %lu\n", (unsigned long) actual_alloc_n);
  alloc_n = actual_alloc_n;

  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == alloc_n);
    assert(stats.free_id == 0);
  }

  smal_collect();
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == alloc_n);
    assert(stats.free_id == 0);
  }

  x = y = global = 0;
  smal_collect();
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == alloc_n);
    assert(stats.free_id == alloc_n);
  }

  smal_roots_end();
}

int main(int argc, char **argv)
{
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

