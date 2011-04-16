/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/explicit_roots.h"
#include "smal/reference.h"
#include <stdio.h>
#include <assert.h>

typedef void *my_oop;
typedef struct my_cons {
  my_oop car, cdr;
} my_cons;

static smal_type *my_cons_type;

static void my_cons_mark (void *ptr)
{
  smal_mark_ptr(((my_cons *) ptr)->car);
  smal_mark_ptr(((my_cons *) ptr)->cdr);
}

void smal_collect_before_inner(void *tos) { }
void smal_collect_before_mark() { }
void smal_collect_after_mark() { }
void smal_collect_before_sweep() 
{
  smal_reference_before_sweep();
}
void smal_collect_after_sweep() { }
void smal_mark_roots()
{
  smal_roots_mark_chain();
}

int main()
{
  my_cons *x = 0, *y = 0;
  smal_reference *y_ref = 0;
  smal_reference_queue *ref_queue = 0;
  smal_roots_4(x, y, y_ref, ref_queue);

  ref_queue = smal_reference_queue_create();
  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);
  x = smal_alloc(my_cons_type);
  y = smal_alloc(my_cons_type);
  y_ref = smal_reference_create_weak(y, ref_queue);
  assert(smal_reference_referred(y_ref) == (void*) y);
  x->car = (my_oop) 1;
  x->cdr = y_ref;
  y->car = (my_oop) 2;
  y->cdr = (my_oop) 0;
  smal_collect();
  assert(smal_reference_referred(y_ref) == (void*) y);
  assert(smal_reference_queue_take(ref_queue) == 0);

  y = 0;
  smal_collect();
  assert(smal_reference_referred(y_ref) == 0);
  assert(smal_reference_queue_take(ref_queue) == y_ref);

  y_ref = 0;
  smal_collect();
  assert(smal_reference_queue_take(ref_queue) == 0);

  smal_roots_end();

  return 0;
}
