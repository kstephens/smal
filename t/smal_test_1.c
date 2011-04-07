#include "smal/smal.h"

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

static my_cons *x, *y;
void smal_mark_roots()
{
  smal_mark_ptr(x);
  smal_mark_ptr(y);
}

int main()
{
  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);
  x = smal_type_alloc(my_cons_type);
  y = smal_type_alloc(my_cons_type);
  x->car = (my_oop) 1;
  x->cdr = y;
  y->car = (my_oop) 2;
  y->cdr = (my_oop) 0;
  smal_collect();

  x->cdr = 0;
  smal_collect();

  y = 0;
  smal_collect();

  x = 0;
  smal_collect();

  return 0;
}
