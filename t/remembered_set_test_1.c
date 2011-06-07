/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "my_cons.h"
#include "roots_explicit.h"

int main(int argc, char **argv)
{
  int i;
  int ncollect = 3;
  int alloc_id = 0;
  int alloc_n = 100;
  my_cons *x = 0, *y = 0;
  my_cons *xp = 0, *yp = 0;
  smal_type *my_cons_type_mu; /* mostly_unchanging */
  smal_roots_4(x, y, xp, yp);
  
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
  for ( alloc_id = 0; alloc_id < alloc_n; ++ alloc_id ) {
    my_cons *c = smal_alloc(my_cons_type);
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
  for ( alloc_id = 0; alloc_id < alloc_n; ++ alloc_id ) {
    my_cons *c = smal_alloc(my_cons_type_mu);
    c->car = (void*) 2;
    c->cdr = y;
    y = c;
  }
  my_print_stats();

  fprintf(stderr, "collecting after x and y list\n");
  smal_collect();
  my_print_stats();

  fprintf(stderr, "mutating y list\n");
  xp = x; yp = y;
  while ( yp ) {
    yp->car = xp;
    yp = yp->cdr;
    xp = xp->cdr;
  }

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
  xp = x; yp = y;
  while ( yp ) {
    yp->car = xp;
    yp = yp->cdr;
    xp = xp->cdr;
  }

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

#if 0
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
  
#endif

  x = y = 0;
  // smal_debug_level = 9;
  for ( i = 0; i < ncollect * 2; ++ i ) {
    smal_collect();
    fprintf(stderr, "dereference all %d\n", i);
    my_print_stats();
  }
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == alloc_n * 2);
    assert(stats.free_id == stats.alloc_id);
    assert(stats.capacity_n == 0);
    assert(stats.buffer_n == 0);
    assert(stats.buffer_mutations == 2);
  }

  smal_roots_end();
  
  fprintf(stderr, "\n%s OK\n", argv[0]);
  return 0;
}

