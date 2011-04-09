/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/explicit_roots.h"
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

void smal_mark_roots()
{
  smal_roots_mark_chain(0);
}

static
void count_object(smal_type *type, void *ptr, void *arg)
{
  fprintf(stderr, "  type %p obj %p\n", type, ptr);
  (* (int *) arg) ++; 
}

int main()
{
  my_cons *x, *y;
  smal_roots_2(x, y);

  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);
  x = smal_alloc(my_cons_type);
  y = smal_alloc(my_cons_type);
  x->car = (my_oop) 1;
  x->cdr = y;
  y->car = (my_oop) 2;
  y->cdr = (my_oop) 0;
  smal_collect();

  x->cdr = 0;
  smal_collect();

  y = 0;
  smal_collect();

  {
    int obj_count = 0;
    smal_each_object(count_object, &obj_count);
    assert(obj_count == 1);
  }

  x = 0;
  smal_collect();

  smal_roots_end();

  return 0;
}
