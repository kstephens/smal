/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/reference.h"
#include "smal/thread.h"

#include <assert.h>
#include <stdlib.h> /* malloc(), free() */
#include <string.h> /* memcpy() */
#include <stdio.h>

#include "hash/voidP_voidP_Table.h"

static voidP_voidP_Table referred_table;
static smal_thread_mutex referred_table_mutex;

static smal_type *reference_type;
static void reference_type_mark(void *object);
static void reference_type_free(void *object);

static smal_type *reference_queue_type;
static void ref_queue_mark(void *ptr);

static int initialized;
static smal_thread_once _initalized = smal_thread_once_INIT;
static void _initialize()
{
  voidP_voidP_TableInit(&referred_table, 8);
  smal_thread_mutex_init(&referred_table_mutex);

  reference_type = smal_type_for(sizeof(smal_reference), reference_type_mark, reference_type_free);
  reference_queue_type = smal_type_for(sizeof(smal_reference_queue), ref_queue_mark, 0);

  initialized = 1;
}
static void initialize()
{
  smal_thread_do_once(&_initalized, _initialize);
}

static smal_reference *find_reference_by_referred(void *ptr)
{
  void **ptrp, *reference;
  if ( ! initialized ) initialize();
  reference = (ptrp = voidP_voidP_TableGet(&referred_table, ptr)) ? *ptrp : 0;
  return reference;
}

static void add_reference(smal_reference *reference)
{
  // Assume reference->mutex is already locked.
  voidP_voidP_TableAdd(&referred_table, reference->referred, reference);
}

static void remove_reference(smal_reference *reference)
{
  // Assume reference->mutex is already locked.
  voidP_voidP_TableRemove(&referred_table, reference->referred);
}

static void reference_type_mark(void *object)
{
  smal_reference *reference = object;
  smal_reference_queue_list *ref_queue_list;

  smal_thread_mutex_lock(&reference->mutex);
  ref_queue_list = reference->reference_queue_list;
  while ( ref_queue_list ) {
    smal_mark_ptr_exact(ref_queue_list->reference_queue);
    ref_queue_list = ref_queue_list->next;
  }
  smal_thread_mutex_unlock(&reference->mutex);
}
static void reference_type_free(void *object)
{
  smal_reference *reference = object;
  smal_reference_queue_list *ref_queue_list;

  smal_thread_mutex_lock(&reference->mutex);
  while ( (ref_queue_list = reference->reference_queue_list) ) {
    ref_queue_list = ref_queue_list->next;
    free(reference->reference_queue_list);
    reference->reference_queue_list = ref_queue_list;
  }  
  smal_thread_mutex_unlock(&reference->mutex);

  smal_thread_mutex_lock(&referred_table_mutex);
  remove_reference(object);
  smal_thread_mutex_unlock(&referred_table_mutex);
}


smal_reference * smal_reference_create_weak(void *ptr, smal_reference_queue *ref_queue)
{
  int error = 0;
  smal_reference *reference;

  smal_thread_mutex_lock(&referred_table_mutex);
  if ( ! (reference = find_reference_by_referred(ptr)) ) {
    if ( ! (reference = smal_alloc(reference_type)) ) {
      smal_thread_mutex_unlock(&referred_table_mutex);
      return 0;
    }
    reference->data = 0;
    reference->referred = ptr;
    reference->reference_queue_list = 0;
    smal_thread_mutex_init(&reference->mutex);
    add_reference(reference);
  }
  smal_thread_mutex_unlock(&referred_table_mutex);

  smal_thread_mutex_lock(&reference->mutex);

  if ( ref_queue ) {
    smal_reference_queue_list *ref_queue_list = malloc(sizeof(*ref_queue_list));
    if ( ref_queue_list ) {
      smal_thread_mutex_lock(&ref_queue->mutex);
      ref_queue_list->reference_queue = ref_queue;
      ref_queue_list->next = reference->reference_queue_list;
      reference->reference_queue_list = ref_queue_list;
      smal_thread_mutex_unlock(&ref_queue->mutex);
    } else {
      error = 1;
    }
  }

  smal_thread_mutex_unlock(&reference->mutex);
  return error ? 0 : reference;
}

void* smal_reference_referred(smal_reference *reference)
{
  void *ptr;
  smal_thread_mutex_lock(&reference->mutex);
  ptr = reference->referred;
  smal_thread_mutex_unlock(&reference->mutex);
  return ptr;
}

static void ref_queue_mark(void *ptr)
{
  smal_reference_queue *ref_queue = ptr;
  smal_reference_list *list;
  smal_thread_mutex_lock(&ref_queue->mutex);
  list = ref_queue->reference_list;
  while ( list ) {
    // fprintf(stderr, "ref_queue %p marking %p\n", ref_queue, list->reference);
    smal_mark_ptr_exact(list->reference);
    list = list->next;
  }
  smal_thread_mutex_unlock(&ref_queue->mutex);
}

smal_reference_queue *smal_reference_queue_create()
{
  smal_reference_queue *ref_queue;
  if ( ! initialized ) initialize();
  ref_queue = smal_alloc(reference_queue_type);
  memset(ref_queue, 0, sizeof(*ref_queue));
  ref_queue->reference_list = 0;
  smal_thread_mutex_init(&ref_queue->mutex);
  return ref_queue;
}

static
void queue_reference(smal_reference_queue *ref_queue, smal_reference *reference)
{
  smal_reference_list *list;
  if ( ! (list = malloc(sizeof(*list))) )
    return;
  list->reference = reference;
  smal_thread_mutex_lock(&ref_queue->mutex);
  list->next = ref_queue->reference_list;
  ref_queue->reference_list = list;
  smal_thread_mutex_unlock(&ref_queue->mutex);
  // fprintf(stderr, "  ref %p queued into %p\n", reference, ref_queue);
}

smal_reference * smal_reference_queue_take(smal_reference_queue *ref_queue)
{
  smal_reference_list *list;
  smal_reference *reference;

  smal_thread_mutex_lock(&ref_queue->mutex);
  if ( (list = ref_queue->reference_list) ) {
    reference = list->reference;
    ref_queue->reference_list = list->next;
    free(list);
  } else
    reference = 0;
  smal_thread_mutex_unlock(&ref_queue->mutex);
  return reference;
}


static
void referred_sweeped(smal_reference *reference)
{
  // fprintf(stderr, "    ref %p => %p referred unreachable\n", reference, reference->referred);
  smal_thread_mutex_lock(&reference->mutex);
  remove_reference(reference);
  reference->referred = 0;
  while ( reference->reference_queue_list ) {
    smal_reference_queue_list *list = reference->reference_queue_list;
    queue_reference(list->reference_queue, reference);
    reference->reference_queue_list = list->next;
    free(list);
  }
  smal_thread_mutex_unlock(&reference->mutex);
}

/*
  For all smal_references,
  If reference is not reachable, do nothing.
  If reference's referred is not reachable, 
  forget referred and add reference to its reference queues.
*/
void smal_reference_before_sweep()
{
  voidP_voidP_TableIterator i;

  if ( ! initialized ) initialize();
  smal_thread_mutex_lock(&referred_table_mutex);
  voidP_voidP_TableIteratorInit(&referred_table, &i);
  while ( voidP_voidP_TableIteratorNext(&referred_table, &i) ) {
    smal_reference *reference = *voidP_voidP_TableIteratorData(&referred_table, &i);
    // fprintf(stderr, "  ref %p => %p\n", reference, reference->referred);
    if ( smal_object_reachableQ(reference) ) {
      // fprintf(stderr, "    ref %p reachable \n", reference);
      if ( ! smal_object_reachableQ(reference->referred) ) {
	referred_sweeped(reference);
      }
    }
  }
  smal_thread_mutex_unlock(&referred_table_mutex);
}



