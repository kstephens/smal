/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "my_cons.h"
#include "smal/roots.h"
#include "smal/finalizer.h"
#include <stdio.h>
#include <assert.h>

void smal_collect_before_inner(void *tos) { }
void smal_collect_before_mark() { }
void smal_collect_after_mark() { }
void smal_collect_before_sweep() 
{
  smal_finalizer_before_sweep();
}
void smal_collect_after_sweep() 
{
  smal_finalizer_after_sweep();
}
void smal_collect_mark_roots()
{
  smal_roots_mark_chain();
}

static void *expected_finalized_reference = 0;
static int finalizer_calls = 0;
static
void my_cons_finalizer(smal_finalizer *finalizer)
{
  if ( expected_finalized_reference ) {
    assert(expected_finalized_reference == finalizer->referred);
    expected_finalized_reference = 0;
  }
  finalizer_calls ++;
}

int main()
{
  my_cons *x = 0, *y = 0;
  smal_finalizer *fin = 0;
  smal_roots_3(x, y, fin);

  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);
  x = smal_alloc(my_cons_type);
  y = smal_alloc(my_cons_type);

  fin = smal_finalizer_create(y, my_cons_finalizer);
  assert(fin->referred == (void*) y);

  x->car = (my_oop) 1;
  x->cdr = 0;
  y->car = (my_oop) 2;
  y->cdr = (my_oop) 0;

  smal_collect();
  assert(finalizer_calls == 0);
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 0);
  }

  // Direct link to y = 0.
  // assert finalizer was called.
  expected_finalized_reference = y;
  y = 0;
  smal_collect();
  assert(finalizer_calls == 1);
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 0);
  }

  smal_collect();
  assert(finalizer_calls == 1);
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 1);
  }

  smal_roots_end();

  return 0;
}
