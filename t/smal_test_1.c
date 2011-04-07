#include "sgc.h"

typedef void *my_oop;
typedef struct my_cons {
  my_oop car, cdr;
} my_cons;

static sgc_type *my_cons_type;

static void my_cons_mark (void *ptr)
{
  sgc_mark_ptr(((my_cons *) ptr)->car);
  sgc_mark_ptr(((my_cons *) ptr)->cdr);
}

static my_cons *x, *y;
void sgc_mark_roots()
{
  sgc_mark_ptr(x);
  sgc_mark_ptr(y);
}

int main()
{
  my_cons_type = sgc_type_for(sizeof(my_cons), my_cons_mark, 0);
  x = sgc_type_alloc(my_cons_type);
  y = sgc_type_alloc(my_cons_type);
  x->car = (my_oop) 1;
  x->cdr = y;
  y->car = (my_oop) 2;
  y->cdr = (my_oop) 0;
  sgc_collect();

  x->cdr = 0;
  sgc_collect();

  y = 0;
  sgc_collect();

  x = 0;
  sgc_collect();

  return 0;
}
