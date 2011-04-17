/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/finalizer.h"
#include "smal/thread.h"

#include <assert.h>
#include <stdlib.h> /* malloc(), free() */
#include <string.h> /* memcpy() */
#include <stdio.h>

#include "hash/voidP_voidP_Table.h"

typedef struct smal_finalized smal_finalized;
struct smal_finalized {
  void *referred;
  smal_finalizer *finalizer_list;
  smal_finalized *next;
  smal_thread_mutex mutex;
};


static voidP_voidP_Table referred_table;
static smal_thread_mutex referred_table_mutex;

static smal_finalized *finalized_queue;
static smal_thread_mutex finalized_queue_mutex;

static int initialized;
static smal_thread_once _initalized = smal_thread_once_INIT;
static void _initialize()
{
  voidP_voidP_TableInit(&referred_table, 8);
  smal_thread_mutex_init(&referred_table_mutex);

  smal_thread_mutex_init(&finalized_queue_mutex);
}
static void initialize()
{
  smal_thread_do_once(&_initalized, _initialize);
  initialized = 1;
}

static smal_finalized *find_finalized_by_referred(void *ptr)
{
  void **ptrp, *finalized;
  if ( ! initialized ) initialize();
  smal_thread_mutex_lock(&referred_table_mutex);
  finalized = (ptrp = voidP_voidP_TableGet(&referred_table, ptr)) ? *ptrp : 0;
  smal_thread_mutex_unlock(&referred_table_mutex);
  return finalized;
}

static void add_finalized(smal_finalized *finalized)
{
  smal_thread_mutex_lock(&referred_table_mutex);
  voidP_voidP_TableAdd(&referred_table, finalized->referred, finalized);
  smal_thread_mutex_unlock(&referred_table_mutex);
}

static void remove_finalized(smal_finalized *finalized, int with_lock)
{
  if ( with_lock ) smal_thread_mutex_lock(&referred_table_mutex);
  voidP_voidP_TableRemove(&referred_table, finalized->referred);
  if ( with_lock ) smal_thread_mutex_unlock(&referred_table_mutex);
}


smal_finalizer * smal_finalizer_create(void *ptr, void (*func)(smal_finalizer *finalizer))
{
  smal_finalized *finalized;
  smal_finalizer *finalizer;

  if ( ! (finalized = find_finalized_by_referred(ptr)) ) {
    if ( ! (finalized = malloc(sizeof(*finalized))) )
      return 0;
    finalized->referred = ptr;
    finalized->finalizer_list = 0;
    finalized->next = 0;
    smal_thread_mutex_init(&finalized->mutex);
    smal_thread_mutex_lock(&finalized->mutex);
    add_finalized(finalized);
  } else {
    smal_thread_mutex_lock(&finalized->mutex);
  }

  if ( ! (finalizer = malloc(sizeof(*finalizer))) ) {
    smal_thread_mutex_unlock(&finalized->mutex);
    return 0;
  }

  finalizer->referred = ptr;
  finalizer->func = func;
  finalizer->data = 0;

  finalizer->next = finalized->finalizer_list;
  finalized->finalizer_list = finalizer;

  smal_thread_mutex_unlock(&finalized->mutex);
  return finalizer;
}

static
void referred_sweeped(smal_finalized *finalized)
{
  // fprintf(stderr, "    ref %p => %p referred unreachable\n", finalizer, finalizer->referred);
  /* Prevent sweep of referred this time around. */
  smal_mark_ptr_exact(finalized->referred);
  /* Forget all finalizers. */
  remove_finalized(finalized, 0);
  /* Add to finalized queue. */
  smal_thread_mutex_lock(&finalized->mutex);
  smal_thread_mutex_lock(&finalized_queue_mutex);
  finalized->next = finalized_queue;
  finalized_queue = finalized;
  smal_thread_mutex_unlock(&finalized_queue_mutex);
  smal_thread_mutex_unlock(&finalized->mutex);
}

/*
  For all smal_finalizers,
  If finalizer is not reachable, do nothing.
  If finalizer's referred is not reachable, 
  forget referred and add finalizer to its finalizer queues.
*/
void smal_finalizer_before_sweep()
{
  voidP_voidP_TableIterator i;
  if ( ! initialized ) initialize();
  smal_thread_mutex_lock(&referred_table_mutex);
  voidP_voidP_TableIteratorInit(&referred_table, &i);
  while ( voidP_voidP_TableIteratorNext(&referred_table, &i) ) {
    smal_finalized *finalized = *voidP_voidP_TableIteratorData(&referred_table, &i);
    if ( ! smal_object_reachableQ(finalized->referred) )
      referred_sweeped(finalized);
  }
  smal_thread_mutex_unlock(&referred_table_mutex);
}

void smal_finalizer_after_sweep()
{
  smal_finalized *finalized;
  if ( ! initialized ) initialize();
  smal_thread_mutex_lock(&finalized_queue_mutex);
  while ( (finalized = finalized_queue) ) {
    smal_finalizer *finalizer;
    smal_finalized *zed = finalized;
    smal_thread_mutex_unlock(&finalized_queue_mutex);
    smal_thread_mutex_lock(&zed->mutex);
    while ( (finalizer = finalized->finalizer_list) ) {
      finalizer->func(finalizer);
      finalizer = finalized->finalizer_list->next;
      free(finalized->finalizer_list);
      finalized->finalizer_list = finalizer;
    }
    finalized = finalized->next;
    smal_thread_mutex_unlock(&zed->mutex);
    smal_thread_mutex_lock(&finalized_queue_mutex);
    free(finalized_queue);
    finalized_queue = finalized;
  }
  smal_thread_mutex_unlock(&finalized_queue_mutex);
}

