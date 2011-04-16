/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/reference.h"

#include <assert.h>
#include <stdlib.h> /* malloc(), free() */
#include <string.h> /* memcpy() */
#include <stdio.h>

#include "hash/voidP_voidP_Table.h"

static int initialized;
static voidP_voidP_Table referred_table;

static smal_reference *find_reference_by_referred(void *ptr)
{
  void **ptrp;
  if ( ! initialized ) {
    voidP_voidP_TableInit(&referred_table, 8);
    initialized = 1;
  }
  if ( (ptrp = voidP_voidP_TableGet(&referred_table, ptr)) )
    return *ptrp;
  else
    return 0;
}

static void add_reference(smal_reference *reference)
{
  voidP_voidP_TableAdd(&referred_table, reference->referred, reference);
}

static void remove_reference(smal_reference *reference)
{
  voidP_voidP_TableRemove(&referred_table, reference->referred);
}


static smal_type *reference_type;
static void reference_type_mark(void *object)
{
  smal_reference *reference = object;
  smal_reference_queue_list *ref_queue_list = reference->reference_queue_list;
  while ( ref_queue_list ) {
    smal_mark_ptr_exact(ref_queue_list->reference_queue);
    ref_queue_list = ref_queue_list->next;
  }
}
static void reference_type_free(void *object)
{
  smal_reference *reference = object;
  smal_reference_queue_list *ref_queue_list;
  while ( (ref_queue_list = reference->reference_queue_list) ) {
    ref_queue_list = ref_queue_list->next;
    free(reference->reference_queue_list);
    reference->reference_queue_list = ref_queue_list;
  }  
  remove_reference(object);
}


smal_reference * smal_reference_create_weak(void *ptr, smal_reference_queue *ref_queue)
{
  smal_reference *reference;

  if ( ! (reference = find_reference_by_referred(ptr)) ) {
    if ( ! reference_type )
      reference_type = smal_type_for(sizeof(*reference), reference_type_mark, reference_type_free);
    if ( ! (reference = smal_alloc(reference_type)) )
      return 0;
    reference->data = 0;
    reference->referred = ptr;
    reference->reference_queue_list = 0;
    add_reference(reference);
  }
  if ( ref_queue ) {
    smal_reference_queue_list *ref_queue_list = malloc(sizeof(*ref_queue_list));
    if ( ! ref_queue_list )
      return 0;
    ref_queue_list->reference_queue = ref_queue;
    ref_queue_list->next = reference->reference_queue_list;
    reference->reference_queue_list = ref_queue_list;
  }
  return reference;
}

void* smal_reference_referred(smal_reference *reference)
{
  return reference->referred;
}

static smal_type *reference_queue_type;
static void ref_queue_mark(void *ptr)
{
  smal_reference_queue *ref_queue = ptr;
  smal_reference_list *list = ref_queue->reference_list;
  while ( list ) {
    // fprintf(stderr, "ref_queue %p marking %p\n", ref_queue, list->reference);
    smal_mark_ptr_exact(list->reference);
    list = list->next;
  }
}

smal_reference_queue *smal_reference_queue_create()
{
  smal_reference_queue *ref_queue;
  if ( ! reference_queue_type )
    reference_queue_type = smal_type_for(sizeof(*ref_queue), ref_queue_mark, 0);
  ref_queue = smal_alloc(reference_queue_type);
  ref_queue->reference_list = 0;
  return ref_queue;
}

static
void queue_reference(smal_reference_queue *ref_queue, smal_reference *reference)
{
  smal_reference_list *list;
  if ( ! (list = malloc(sizeof(*list))) )
    return;
  list->reference = reference;
  list->next = ref_queue->reference_list;
  ref_queue->reference_list = list;
  // fprintf(stderr, "  ref %p queued into %p\n", reference, ref_queue);
}

smal_reference * smal_reference_queue_take(smal_reference_queue *ref_queue)
{
  smal_reference_list *list;
  if ( (list = ref_queue->reference_list) ) {
    smal_reference *reference = list->reference;
    ref_queue->reference_list = list->next;
    free(list);
    return reference;
  } else
    return 0;
}


static
void referred_sweeped(smal_reference *reference)
{
  // fprintf(stderr, "    ref %p => %p referred unreachable\n", reference, reference->referred);
  remove_reference(reference);
  reference->referred = 0;
  while ( reference->reference_queue_list ) {
    smal_reference_queue_list *list = reference->reference_queue_list;
    queue_reference(list->reference_queue, reference);
    reference->reference_queue_list = list->next;
    free(list);
  }
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
}



