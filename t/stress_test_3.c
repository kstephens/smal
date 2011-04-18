/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "my_cons.h"
#include "roots_explicit.h"

int main(int argc, char **argv)
{
  int alloc_id = 0;
  int alloc_n = 4000;
  my_cons *x = 0, *y = 0;
  smal_roots_2(x, y);
  
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
  my_print_stats();

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
  fprintf(stderr, "dereference all\n");
  my_print_stats();
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 12000);
    assert(stats.free_id == stats.alloc_id);
    assert(stats.capacity_n == 0);
    assert(stats.buffer_n == 0);
  }

  smal_roots_end();
  
  fprintf(stdout, "\nOK\n");
  
  return 0;
}

