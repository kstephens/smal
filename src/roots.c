/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/thread.h"
#include "smal/roots.h"

#include <assert.h>
#include <stdlib.h> /* malloc(), free() */
#include <string.h> /* memcpy() */
#include <stdio.h>

static smal_roots *global_roots;
static smal_thread_mutex global_roots_mutex;

static int initialized;
static smal_thread_once _initalized = smal_thread_once_INIT;
static void _initialize()
{
  smal_thread_mutex_init(&global_roots_mutex);
}
static void initialize()
{
  initialized = 1;
  smal_thread_do_once(&_initalized, _initialize);
}

int smal_roots_add_global(smal_roots *roots)
{
  int result = -1;
  smal_roots *r = 0;

  if ( ! initialized ) initialize();
  if ( (r = malloc(sizeof(*r))) ) {
    size_t size = sizeof(r->_bindings[0]) * roots->_bindings_n;
    r->_bindings_n = roots->_bindings_n;
    if ( (r->_bindings = malloc(size)) ) {
      memcpy(r->_bindings, roots->_bindings, size);
      smal_thread_mutex_lock(&global_roots_mutex);
      r->_next = global_roots;
      global_roots = r;
      smal_thread_mutex_unlock(&global_roots_mutex);
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
    smal_mark_bindings(roots->_bindings_n, (void ***) roots->_bindings);
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
int mark_thread(smal_thread *t, void *arg)
{
  mark_roots(t->roots);
  return 0;
}

void smal_roots_mark_chain()
{
  mark_roots(global_roots);
  smal_thread_each(mark_thread, 0);
}

