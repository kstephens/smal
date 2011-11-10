/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/thread.h"
#include "smal/roots.h"
#include "smal/callback.h"
#include "smal/dllist.h"
#include "smal/assert.h"

#include <stdlib.h> /* malloc(), free() */
#include <string.h> /* memcpy() */
#include <stdio.h>

static smal_roots *global_roots;
static smal_thread_mutex global_roots_mutex;
static smal_callbacks callbacks;

static int initialized;
static smal_thread_once _initalized = smal_thread_once_INIT;
static void _initialize()
{
  smal_thread_mutex_init(&global_roots_mutex);
  smal_callbacks_init(&callbacks);
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

int smal_roots_remove_global(smal_roots *roots)
{
  int result = -1;
  smal_roots *r = 0, **rp;

  if ( ! initialized ) initialize();
  smal_thread_mutex_lock(&global_roots_mutex);
  rp = &global_roots;
  while ( (r = *rp) ) {
    int i, j;
    int r_cleared = 0;
    for ( i = 0; i < roots->_bindings_n; ++ i ) {
      for ( j = 0; j < r->_bindings_n; ++ j ) {
	if ( roots->_bindings[i] && roots->_bindings[i] == r->_bindings[j] ) {
	  r->_bindings[j] = 0;
	  ++ r_cleared;
	  result = 0;
	}
      }
    }
    if ( r_cleared == r->_bindings_n ) {
      // fprintf(stderr, "small_roots_remove_global(): free(%p)\n", (void*) r);
      *rp = r->_next;
      free(r->_bindings);
      free(r);
    } else {
      rp = &(r->_next);
    }
  }
  // fprintf(stderr, "small_roots_remove_global(): global_roots = %p\n", (void*) global_roots);

  smal_thread_mutex_unlock(&global_roots_mutex);
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
  if ( ! initialized ) initialize();
  smal_callbacks_invoke(&callbacks);
  mark_roots(global_roots);
  smal_thread_each(mark_thread, 0);
}

void *smal_roots_add_callback(smal_callback_DECL((*func)), void *data)
{
  void *cb;
  if ( ! initialized ) initialize();
  cb = smal_callbacks_add(&callbacks, func, data);
  return cb;
}

void smal_roots_remove_callback(void *cb)
{
  if ( ! initialized ) initialize();
  smal_callbacks_remove(&callbacks, cb);
}

