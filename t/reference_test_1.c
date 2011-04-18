/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "my_cons.h"
#include "smal/smal.h"
#include "smal/explicit_roots.h"
#include "smal/reference.h"
#include <stdio.h>
#include <assert.h>

void smal_collect_before_inner(void *tos) { }
void smal_collect_before_mark() { }
void smal_collect_after_mark() { }
void smal_collect_before_sweep() 
{
  smal_reference_before_sweep();
}
void smal_collect_after_sweep() { }
void smal_collect_mark_roots()
{
  smal_roots_mark_chain();
}

int main()
{
  my_cons *x = 0, *y = 0;
  smal_reference *ref = 0, *ref_keep = 0;
  smal_reference_queue *ref_queue = 0;
  smal_roots_4(x, y, ref, ref_queue);

  ref_queue = smal_reference_queue_create();

  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);
  x = smal_alloc(my_cons_type);
  y = smal_alloc(my_cons_type);

  ref = smal_reference_create_weak(y, ref_queue);
  assert(smal_reference_referred(ref) == (void*) y);

  x->car = (my_oop) 1;
  x->cdr = ref;
  y->car = (my_oop) 2;
  y->cdr = (my_oop) 0;

  smal_collect();
  assert(smal_reference_referred(ref) == (void*) y);
  assert(smal_reference_queue_take(ref_queue) == 0);

  // Direct link to y = 0, ref link still exists.
  // assert weak reference cleared.
  y = 0;
  smal_collect();
  assert(smal_reference_referred(ref) == 0);

  // Direct link to ref lost
  // Assert reference queue marks queued reference.
  ref_keep = ref;
  ref = 0;
  smal_collect();
  assert(smal_reference_queue_take(ref_queue) == ref_keep);
  ref_keep = 0;

  // Assert reference queue is not empty.
  smal_collect();
  assert(smal_reference_queue_take(ref_queue) == 0);

  // Assert lost reference is immediately swept and not queued.
  ref = smal_reference_create_weak(x, ref_queue);
  ref = 0;
  smal_collect();
  assert(smal_reference_queue_take(ref_queue) == 0);

  smal_roots_end();

  return 0;
}
