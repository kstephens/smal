#include "smal/smal.h"
#include "smal/explicit_roots.h"
#include <stdlib.h>
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

void my_count_object(smal_type *type, void *ptr, void *arg)
{
  //  fprintf(stderr, "  type %p obj %p\n", type, ptr);
  (* (int *) arg) ++; 
}

int main(int argc, char **argv)
{
  int alloc_id;
  my_cons *x, *y;
  smal_roots_2(x, y);
  
  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);
  
  for ( alloc_id = 0; alloc_id < 1000000; ++ alloc_id ) {
    int action = rand() % 10;
    x = smal_type_alloc(my_cons_type);
    x->car = x->cdr = 0;
    
#if 1
    switch ( action ) {
    case 0:
      y = x; 
      break;
    case 1:
      if ( y )
	y->car = x;
      else
	y = x;
      break;
    case 2:
      if ( y )
	y->cdr = x;
      else
	y = x;
      break;
    }
#endif
    fprintf(stderr, "%d", action);
    fflush(stderr);
    
#if 1
    if ( alloc_id % 100 == 0 ) {
      // fprintf(stderr, "\nGC\n");
      smal_collect();
    }
#endif
    
#if 1
    if ( alloc_id % 100 == 50 ) {
      int obj_count = 0;
      smal_each_object(my_count_object, &obj_count);
      fprintf(stderr, "  object_count = %d\n", obj_count);
    }
#endif
  }
}

fprintf(stdout, "\nOK\n");

  return 0;
}

