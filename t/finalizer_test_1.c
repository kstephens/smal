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
void smal_collect_before_mark()
{
  smal_finalizer_before_mark();
}
void smal_collect_after_mark()
{
  smal_finalizer_after_mark(); 
}
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

extern int _smal_debug_mark;

int main(int argc, char **argv)
{
  my_cons *x = 0, *y = 0, *z = 0;
  smal_finalizer *fin = 0;
  smal_roots_3(x, y, fin);
  extern int _smal_finalizer_debug;
  _smal_finalizer_debug = 1;

  my_cons_type = smal_type_for(sizeof(my_cons), my_cons_mark, 0);
  x = smal_alloc(my_cons_type);
  fprintf(stderr, "  x = %p\n", x);
  y = smal_alloc(my_cons_type);
  fprintf(stderr, "  y = %p\n", y);
  z = smal_alloc(my_cons_type);
  fprintf(stderr, "  z = %p\n", z);

  fin = smal_finalizer_create(y, my_cons_finalizer);
  assert(fin->referred == (void*) y);

  x->car = (my_oop) 1;
  x->cdr = 0;
  y->car = (my_oop) 2;
  y->cdr = (my_oop) z;
  z->car = (my_oop) 3;
  z->cdr = 0;

  fprintf(stderr, "First collect; assert no objects freed, finalizer was not called.\n");
  // _smal_debug_mark = 1;
  smal_collect();
  smal_collect_wait_for_sweep();
  assert(finalizer_calls == 0);
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalizer_type(), &stats);
    assert(stats.alloc_id == 1);
    assert(stats.free_id == 0);
  }
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalized_type(), &stats);
    assert(stats.alloc_id == 1);
    assert(stats.free_id == 0);
  }
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 3 + 2); /* 3 my_cons + 2 finalize objects. */
    assert(stats.free_id == 0);
  }

  fprintf(stderr, "Direct link to y = 0; assert finalizer was called.\n");
  // assert y and z were not freed, yet.
  expected_finalized_reference = y;
  y = 0;
  // _smal_debug_mark = 1;
  fprintf(stderr, "  finalized_reference = %p\n", expected_finalized_reference);
  assert(fin->func != 0);
  assert(fin->next == 0); // only one finalizer
  smal_collect();
  smal_collect_wait_for_sweep();
  assert(finalizer_calls == 1);
  assert(fin->func == 0);
  assert(fin->next == 0);
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalized_type(), &stats);
    assert(stats.alloc_id == 1);
    assert(stats.free_id == 0);
  }
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalizer_type(), &stats);
    assert(stats.alloc_id == 1);
    assert(stats.free_id == 0);
  }
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 3 + 2); // 1 my_cons + 2 finalize objects.
    assert(stats.free_id == 0);
  }

  fprintf(stderr, "Collect again: assert y and z; and 1 smal_finalized object were freed.\n");
  smal_collect();
  smal_collect_wait_for_sweep();
  assert(finalizer_calls == 1);
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalized_type(), &stats);
    assert(stats.alloc_id == 1);
    assert(stats.free_id == 1);
  }
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalizer_type(), &stats);
    assert(stats.alloc_id == 1);
    assert(stats.free_id == 0); // to be freed next GC.
  }
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 3 + 2);
    assert(stats.free_id == 3);
  }

  fprintf(stderr, "Collect again: assert y and z were freed.\n");
  smal_collect();
  smal_collect_wait_for_sweep();
  assert(finalizer_calls == 1);
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalized_type(), &stats);
    assert(stats.alloc_id == 1);
    assert(stats.free_id == 1);
  }
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalizer_type(), &stats);
    assert(stats.alloc_id == 1);
    assert(stats.free_id == 0); // BUG?
  }
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 3 + 2);
    assert(stats.free_id == 3);
  }

  fprintf(stderr, "Add finalizer to x; keep reference to x.\n");
  {
    smal_finalizer *f = smal_finalizer_create(x, my_cons_finalizer);
    f->data = (void*) 1;
    assert(f->referred == x);
  }
  {
    smal_finalizer *f = smal_finalizer_create(x, my_cons_finalizer);
    f->data = (void*) 2;
    assert(f->referred == x);
  }
  smal_collect();
  smal_collect_wait_for_sweep();
  assert(finalizer_calls == 1);
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalized_type(), &stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 1);
  }
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalizer_type(), &stats);
    assert(stats.alloc_id == 3);
    assert(stats.free_id == 0);
  }
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 3 + 5);
    assert(stats.free_id == 3);
  }

  fprintf(stderr, "Remove finalizers to x; keep reference to x.\n");
  {
    smal_finalizer *f = smal_finalizer_remove_all(x);
    assert(f != 0);
  }
  smal_collect();
  smal_collect_wait_for_sweep();
  assert(finalizer_calls == 1);
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalized_type(), &stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 2);
  }
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalizer_type(), &stats);
    assert(stats.alloc_id == 3);
    assert(stats.free_id == 2);
  }
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 3 + 5);
    assert(stats.free_id == 3 + 3);
  }

  fprintf(stderr, "Remove reference to x.\n");
  // Collect: assert x was also freed.
  x = 0;
  smal_collect();
  smal_collect_wait_for_sweep();
  assert(finalizer_calls == 1);
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalized_type(), &stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 2);
  }
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalizer_type(), &stats);
    assert(stats.alloc_id == 3);
    assert(stats.free_id == 2);
  }
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 3 + 5);
    assert(stats.free_id == 3 + 4);
  }

  fprintf(stderr, "Final sweep: remove reference to finalizer\n");
  fin = 0;
  smal_collect();
  smal_collect_wait_for_sweep();
  assert(finalizer_calls == 1);
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalized_type(), &stats);
    assert(stats.alloc_id == 2);
    assert(stats.free_id == 2);
  }
  {
    smal_stats stats = { 0 };
    smal_type_stats(smal_finalizer_type(), &stats);
    assert(stats.alloc_id == 3);
    assert(stats.free_id == 3);
  }
  {
    smal_stats stats = { 0 };
    smal_global_stats(&stats);
    assert(stats.alloc_id == 3 + 5);
    assert(stats.free_id == 3 + 5);
  }

  smal_roots_end();

  fprintf(stderr, "\n%s OK\n", argv[0]);

  return 0;
}
