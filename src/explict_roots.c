/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/thread.h"
#include "smal/explicit_roots.h"

#include <assert.h>
#include <stdlib.h> /* malloc(), free() */
#include <stdio.h>

static
void mark_roots(smal_roots *roots)
{
  if ( ! roots )
    roots = smal_roots_current();
  
  while ( roots ) {
    int i;
    /* fprintf(stderr, "  roots %p [%d]\n", roots, roots->_n); */
    for ( i = 0; i < roots->_n; ++ i ) {
      void *ptr = * (void**) roots->_bindings[i];
      /* fprintf(stderr, "  mark %p => %p\n", roots->_bindings[i], ptr); */
      smal_mark_ptr(ptr);
    }
    roots = roots->_next;
  }
}

smal_roots *smal_roots_current()
{
  smal_thread *t = smal_thread_self();
  return t->roots;
}

void smal_roots_set_current(smal_roots *roots)
{
  smal_thread *t = smal_thread_self();
  t->roots = roots;
}

static
void mark_thread(smal_thread *t, void *arg)
{
  mark_roots(t->roots);
}

void smal_roots_mark_chain()
{
  smal_thread_each(mark_thread, 0);
}


