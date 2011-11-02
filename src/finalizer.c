/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/finalizer.h"
#include "smal/thread.h"
#include "smal/assert.h"

// #include <stdlib.h> /* malloc(), free() */
// #include <string.h> /* memcpy() */
#include <stdio.h>

#include "hash/voidP_voidP_Table.h"

int _smal_finalizer_debug;

static smal_type *smal_finalizer_type_;
static void *smal_finalizer_mark(smal_finalizer *obj)
{
  extern void * _smal_mark_referrer;
  if ( _smal_finalizer_debug ) fprintf(stderr, "    smal_finalizer_mark(%p) from %p\n", obj, _smal_mark_referrer);
  /* NOTE: obj->referred is NOT MARKED. */
  smal_mark_ptr(obj, obj->data);
  smal_mark_ptr(obj, obj->func); /* function pointer? */
  return obj->next;
}

typedef struct smal_finalized smal_finalized;
struct smal_finalized { /** Represents a referred object that has finalizers. */
  void *referred;
  smal_finalizer *finalizers;
  smal_finalized *next; /** finalized_queue */
  smal_thread_mutex mutex;
};

static smal_type *smal_finalized_type_;
static void *smal_finalized_mark(smal_finalized *obj)
{
  if ( _smal_finalizer_debug ) fprintf(stderr, "    smal_finalized_mark(%p)\n", obj);
  smal_mark_ptr(obj, obj->finalizers);
  smal_mark_ptr(obj, obj->next);
  return obj->referred;
}
static void smal_finalized_free(smal_finalized *obj)
{
  if ( _smal_finalizer_debug ) fprintf(stderr, "    smal_finalized_free(%p)\n", obj);
  smal_thread_mutex_destroy(&obj->mutex);
}

static voidP_voidP_Table referred_table;
static smal_thread_mutex referred_table_mutex;

static smal_finalized *finalized_queue;
static smal_thread_mutex finalized_queue_mutex;

static int initialized; /* fast initialize lock */
static smal_thread_once _initalized = smal_thread_once_INIT;
static void _initialize()
{
  {
    smal_type_descriptor desc = { 0 };
    desc.object_size = sizeof(smal_finalized);
    desc.mark_func = (void*) smal_finalized_mark;
    desc.free_func = (void*) smal_finalized_free;
    smal_finalized_type_ = smal_type_for_desc(&desc);
  }

  {
    smal_type_descriptor desc = { 0 };
    desc.object_size = sizeof(smal_finalizer);
    desc.mark_func = (void*) smal_finalizer_mark;
    // desc.free_func = (void*) smal_finalizer_free;
    smal_finalizer_type_ = smal_type_for_desc(&desc);
  }

  voidP_voidP_TableInit(&referred_table, 8);
  smal_thread_mutex_init(&referred_table_mutex);

  smal_thread_mutex_init(&finalized_queue_mutex);

  initialized = 1;
}
static void initialize()
{
  smal_thread_do_once(&_initalized, _initialize);
}

smal_type *smal_finalizer_type()
{
  if ( ! initialized ) initialize();
  return smal_finalizer_type_;
}

smal_type *smal_finalized_type()
{
  if ( ! initialized ) initialize();
  return smal_finalized_type_;
}

static smal_finalized *find_finalized_by_referred(void *ptr)
{
  void **ptrp, *finalized;
  // referred_table_mutex is held by -> smal_finalizer_created.
  finalized = (ptrp = voidP_voidP_TableGet(&referred_table, ptr)) ? *ptrp : 0;
  return finalized;
}

static void add_finalized(smal_finalized *finalized)
{
  // referred_table_mutex is held by -> smal_finalizer_created.
  voidP_voidP_TableAdd(&referred_table, finalized->referred, finalized);
}

static void remove_finalized(smal_finalized *finalized)
{
  // referred_table_mutex is held by -> referred_sweep -> smal_finalizer_after_mark.
  voidP_voidP_TableRemove(&referred_table, finalized->referred);
}

smal_finalizer * smal_finalizer_create(void *ptr, void (*func)(smal_finalizer *finalizer))
{
  smal_finalized *finalized = 0;
  smal_finalizer *finalizer;

  if ( ! ptr ) return 0;

  if ( ! initialized ) initialize();

  smal_thread_mutex_lock(&referred_table_mutex);
  if ( ! (finalized = find_finalized_by_referred(ptr)) ) {
    if ( ! (finalized = smal_alloc(smal_finalized_type_)) ) {
      smal_thread_mutex_unlock(&referred_table_mutex);
      return 0;
    }
    finalized->referred = ptr;
    finalized->finalizers = 0;
    finalized->next = 0;
    smal_thread_mutex_init(&finalized->mutex);
    add_finalized(finalized);
  }
  smal_thread_mutex_unlock(&referred_table_mutex);

  smal_thread_mutex_lock(&finalized->mutex);

  if ( (finalizer = smal_alloc(smal_finalizer_type_)) ) {
    finalizer->referred = ptr;
    finalizer->func = func;
    finalizer->data = 0;
    
    finalizer->next = finalized->finalizers;
    finalized->finalizers = finalizer;
  }

  smal_thread_mutex_unlock(&finalized->mutex);
  return finalizer;
}

smal_finalizer *smal_finalizer_remove_all(void *ptr)
{
  smal_finalized *finalized = 0;
  smal_finalizer *finalizer = 0;

  if ( ! initialized ) initialize();

  smal_thread_mutex_lock(&referred_table_mutex);
  if ( (finalized = find_finalized_by_referred(ptr)) ) {
    remove_finalized(finalized);
  }
  smal_thread_mutex_unlock(&referred_table_mutex);

  if ( finalized ) {
    smal_thread_mutex_lock(&finalized->mutex);
    finalizer = finalized->finalizers;
    finalized->finalizers = 0;
    smal_thread_mutex_unlock(&finalized->mutex);
  }

  return finalizer;
}

smal_finalizer *smal_finalizer_copy_all(void *ptr, void *to_ptr)
{
  smal_finalized *finalized = 0;
  smal_finalizer *finalizer = 0, *to_finalizer = 0;

  if ( ! ptr || ! to_ptr || to_ptr == ptr )
    return 0;

  if ( ! initialized ) initialize();

  smal_thread_mutex_lock(&referred_table_mutex);
  finalized = find_finalized_by_referred(ptr);
  smal_thread_mutex_unlock(&referred_table_mutex);

  if ( finalized ) {
    smal_thread_mutex_lock(&finalized->mutex);
    for ( finalizer = finalized->finalizers; finalizer; finalizer = finalizer->next ) {
      to_finalizer = smal_finalizer_create(to_ptr, finalizer->func);
      to_finalizer->data = finalizer->data;
    }
    smal_thread_mutex_unlock(&finalized->mutex);
  }

  return to_finalizer;
}

static
void referred_sweeped(smal_finalized *finalized)
{
  if ( _smal_finalizer_debug ) fprintf(stderr, "    finalized %p => referred %p is unreachable\n", finalized, finalized->referred);
  /* Prevent sweep of finalized and referred this time around. */
  smal_mark_ptr(0, finalized);
  smal_mark_ptr(0, finalized->referred);

  /* Add to finalized queue. */
  smal_thread_mutex_lock(&finalized->mutex);
  smal_thread_mutex_lock(&finalized_queue_mutex);
  finalized->next = finalized_queue;
  finalized_queue = finalized;
  smal_thread_mutex_unlock(&finalized_queue_mutex);
  smal_thread_mutex_unlock(&finalized->mutex);

  /* Forget all finalizers for finalized object. */
  remove_finalized(finalized);
}

static int before_mark_called = 0;
/* This is responsible for marking all current finalizers and finalized_queues. */
void smal_finalizer_before_mark()
{
  if ( _smal_finalizer_debug ) fprintf(stderr, "  smal_finalizer_before_mark()\n");
  before_mark_called = 1;
}

static
void smal_finalizer_after_mark_1()
{
  if ( _smal_finalizer_debug ) fprintf(stderr, "  smal_finalizer_after_mark_1()\n");
  assert(before_mark_called);
  if ( ! initialized ) initialize();

  smal_thread_mutex_lock(&referred_table_mutex);
  {
    voidP_voidP_TableIterator i;
    voidP_voidP_TableIteratorInit(&referred_table, &i);
    while ( voidP_voidP_TableIteratorNext(&referred_table, &i) ) {
      smal_finalizer *finalizer = *voidP_voidP_TableIteratorData(&referred_table, &i);
      smal_mark_ptr(0, finalizer);
    }
  }
  smal_thread_mutex_unlock(&referred_table_mutex);
}

static
void smal_finalizer_after_mark_2()
{
  void *ptr;
  if ( _smal_finalizer_debug ) fprintf(stderr, "  smal_finalizer_after_mark_2()\n");
  smal_thread_mutex_lock(&finalized_queue_mutex);
  ptr = finalized_queue;
  smal_mark_ptr(0, ptr);
  smal_thread_mutex_unlock(&finalized_queue_mutex);
}

/*
  For all smal_finalizers,
  If finalizer is not reachable, do nothing.
  If finalizer's referred is not reachable, 
  forget referred and add finalizer to its finalizer queues.
*/
void smal_finalizer_after_mark()
{
  smal_finalizer_after_mark_1();
  if ( _smal_finalizer_debug ) fprintf(stderr, "  smal_finalizer_after_mark()\n");
  if ( ! initialized ) initialize();
  assert(before_mark_called);
  // smal_thread_mutex_lock(&finalized_queue_mutex); // see referred_sweeped().
  smal_thread_mutex_lock(&referred_table_mutex);
  {
    voidP_voidP_TableIterator i;
    voidP_voidP_TableIteratorInit(&referred_table, &i);
    while ( voidP_voidP_TableIteratorNext(&referred_table, &i) ) {
      smal_finalized *finalized = *voidP_voidP_TableIteratorData(&referred_table, &i);
      if ( ! smal_object_reachableQ(finalized->referred) )
	referred_sweeped(finalized);
    }
  }
  smal_thread_mutex_unlock(&referred_table_mutex);

  smal_finalizer_after_mark_2();
  // smal_thread_mutex_unlock(&finalized_queue_mutex); // see referred_sweeped().
}

void smal_finalizer_before_sweep()
{
}

int smal_finalizer_sweep_amount = 0;
int smal_finalizer_sweep_some(int n)
{
  int aborted = 0;
  smal_finalized *finalized;
  if ( ! initialized ) initialize();
  smal_thread_mutex_lock(&finalized_queue_mutex);
  while ( (finalized = finalized_queue) ) {
    smal_finalizer *finalizer;
    smal_thread_mutex_unlock(&finalized_queue_mutex);
    smal_thread_mutex_lock(&finalized->mutex);
    while ( (finalizer = finalized->finalizers) ) {
      smal_finalizer *f = finalizer;
      void (*func)() = f->func;
      finalized->finalizers = f->next;
      f->next = 0;
      f->func = 0;
      // free(f);
      smal_thread_mutex_unlock(&finalized->mutex);
      if ( _smal_finalizer_debug ) fprintf(stderr, "  smal_finalizer_sweep_some(%d): finalized %p: removed finalizer %p\n", 
	      n, finalized, f);

      if ( func ) {
	if ( _smal_finalizer_debug ) fprintf(stderr, "  smal_finalizer_sweep_some(%d): finalized %p: finalizer %p, referred %p, calling %p(%p)\n", 
		n, finalized, f, f->referred, func, f);
	func(f);
      }
      if ( ! -- n ) {
	aborted = 1;
	break;
      }
      smal_thread_mutex_lock(&finalized->mutex);
    }
    if ( aborted ) {
      break;
    } else {
      smal_thread_mutex_unlock(&finalized->mutex);
    }

    smal_thread_mutex_lock(&finalized_queue_mutex);
    if ( _smal_finalizer_debug ) fprintf(stderr, "   smal_finalizer_sweep_some(%d): finalized %p removed\n", n, finalized);
    // Clear finalize->next and move forward.
    {
      smal_finalized *f = finalized;
      finalized = f->next;
      f->next = 0;
      // free(f);
    }
    finalized_queue = finalized;
  }
  if ( ! aborted ) {
    smal_thread_mutex_unlock(&finalized_queue_mutex);
  }

  return aborted;
}

void smal_finalizer_after_sweep()
{
  if ( _smal_finalizer_debug ) fprintf(stderr, "  smal_finalizer_after_sweep()\n");
  assert(before_mark_called);
  smal_finalizer_sweep_some(smal_finalizer_sweep_amount);
}


