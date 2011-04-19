/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "my_cons.h"
#include "roots_explicit.h"

static
int count_object(smal_type *type, void *ptr, void *arg)
{
  fprintf(stderr, "  type %p obj %p\n", type, ptr);
  (* (int *) arg) ++;
  return 0;
}

int main(int argc, char **argv)
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
    {
      smal_stats stats = { 0 };
      smal_global_stats(&stats);
      assert(stats.live_n == 1);
    }
    assert(obj_count == 1);
  }

  x = 0;
  smal_collect();

  smal_roots_end();

  return 0;
}
