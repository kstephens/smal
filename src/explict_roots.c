/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/thread.h"
#include "smal/explicit_roots.h"

#include <assert.h>
#include <stdlib.h> /* malloc(), free() */
#include <string.h> /* memcpy() */
#include <stdio.h>

static
smal_roots *global_roots;

int smal_root_add_global(smal_roots *roots)
{
  int result = -1;
  smal_roots *r = 0;

  if ( (r = malloc(sizeof(*r))) ) {
    size_t size = sizeof(r->_bindings[0]) * roots->_bindings_n;
    r->_bindings_n = roots->_bindings_n;
    if ( (r->_bindings = malloc(size)) ) {
      memcpy(r->_bindings, roots->_bindings, size);
      r->_next = global_roots;
      global_roots = r;
      result = 0;
    }
  }

  if ( result < 0 ) {
    if ( r ) {
      if ( r->_bindings )
	free(r->_bindings);
      free(r);
    }
  }

  return result;
}

static
void mark_roots(smal_roots *roots)
{
  while ( roots ) {
    int i;
    /* fprintf(stderr, "  roots %p [%d]\n", roots, roots->_n); */
    for ( i = 0; i < roots->_bindings_n; ++ i ) {
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
  mark_roots(global_roots);
  smal_thread_each(mark_thread, 0);
}


