/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "my_cons.h"
#include "roots_explicit.h"

static 
int run_test(int argc, char **argv, int alloc_n)
{
  int i;
  int ncollect = 3;
  int alloc_id = 0;
  my_cons *x = 0, *y = 0;
  my_cons *xp = 0, *yp = 0;
  smal_type *my_cons_type_mu; /* mostly_unchanging */
  smal_roots_4(x, y, xp, yp);
  
#if 0
  smal_debug_set_level(smal_debug_remembered_set, 9);
  smal_debug_set_level(smal_debug_object_alloc, 2);
  smal_debug_set_level(smal_debug_object_free, 2);
#endif

  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);
  {
    smal_type_descriptor desc;
    memset(&desc, 0, sizeof(desc));
    desc.object_size = sizeof(my_cons);
    desc.mark_func = my_cons_mark;
    desc.mostly_unchanging = 1;
    desc.collections_per_sweep = ncollect;
    my_cons_type_mu = smal_type_for_desc(&desc);
  }

  fprintf(stderr, "allocing for x list\n");
  for ( i = 0; i < alloc_n; ++ i ) {
    my_cons *c = smal_alloc(my_cons_type); ++ alloc_id;
    c->car = (void*) 1;
    c->cdr = x;
    x = c;
  }
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.buffer_mutations == 0);
  }

  fprintf(stderr, "collecting x after list\n");
  smal_collect();
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.buffer_mutations == 0);
  }

  fprintf(stderr, "allocing for y list (mostly_unchanging)\n");
  for ( i = 0; i < alloc_n; ++ i ) {
    my_cons *c = smal_alloc(my_cons_type_mu); ++ alloc_id;
    c->car = (void*) 2;
    c->cdr = y;
    y = c;
  }
  my_print_stats();

  fprintf(stderr, "collecting after x and y list\n");
  smal_collect();
  my_print_stats();

  fprintf(stderr, "mutating y list, y list cars point to x elements\n");
  xp = x; yp = y; i = 0;
  while ( yp ) {
    yp->car = xp;
    yp = yp->cdr;
    xp = xp->cdr;
    i ++;
  }
  assert(xp == 0);
  assert(i == alloc_n);
  xp = yp = 0;

  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.buffer_mutations == 1);
  }

  fprintf(stderr, "collecting after mutating y list\n");
  smal_collect();
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.buffer_mutations == 1);
  }

  fprintf(stderr, "mutating y list, again\n");
  xp = x; yp = y; i = 0;
  while ( yp ) {
    yp->car = xp;
    yp = yp->cdr;
    xp = xp->cdr;
    i ++;
  }
  assert(xp == 0);
  assert(i == alloc_n);
  xp = yp = 0;

  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.buffer_mutations == 2);
  }

  fprintf(stderr, "collecting after mutating y list, again\n");
  smal_collect();
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.buffer_mutations == 2);
  }

  fprintf(stderr, "allocing more for x list\n");
  for ( i = 0; i < alloc_n; ++ i ) {
    my_cons *c = smal_alloc(my_cons_type); ++ alloc_id;
    c->car = (void*) 2;
    c->cdr = x;
    x = c;
  }
  smal_collect();
  my_print_stats();

  x = y = 0;
  for ( i = 0; i < ncollect * 2; ++ i ) {
    smal_stats stats = { 0 };
    fprintf(stderr, "dereference all %d\n", i);
    smal_collect();
    my_print_stats();
    smal_global_stats(&stats);
    if ( i == 0 )
      assert(stats.free_id == alloc_n);
    if ( i == 2 )
      assert(stats.free_id == alloc_n * 3);
  }
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == alloc_n * 3);
    assert(stats.free_id == stats.alloc_id);
    assert(stats.capacity_n == 0);
    assert(stats.buffer_n == 0);
    assert(stats.buffer_mutations == 2);
  }

  smal_roots_end();
  
  fprintf(stderr, "\n%s OK\n", argv[0]);
  return 0;
}

int main(int argc, char **argv)
{
  int alloc_n = 100;
  if ( argc > 1 )
    alloc_n = atoi(argv[1]);
  return run_test(argc, argv, alloc_n);
}

