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
static voidP_voidP_Table referred_table, reference_table;

static smal_type *reference_type;
static void reference_type_mark(void *object)
{
  smal_reference *reference = object;
  smal_reference_queue_list *ref_queue_list = reference->reference_queue_list;
  while ( ref_queue_list ) {
    smal_mark_ptr(ref_queue_list->reference_queue);
    ref_queue_list = ref_queue_list->next;
  }
}

static smal_reference *find_reference_by_referred(void *ptr)
{
  void **ptrp;
  if ( ! initialized ) {
    voidP_voidP_TableInit(&referred_table, 8);
    voidP_voidP_TableInit(&reference_table, 8);
    initialized = 1;
  }
  if ( (ptrp = voidP_voidP_TableGet(&referred_table, ptr)) ) {
    return *ptrp;
  } else {
    return 0;
  }
}

static smal_reference *find_reference(smal_reference *reference)
{
  void **ptrp;
  if ( (ptrp = voidP_voidP_TableGet(&reference_table, reference)) ) {
    return *ptrp;
  } else {
    return 0;
  }
}

static void add_reference(smal_reference *reference)
{
  voidP_voidP_TableAdd(&referred_table, reference->referred, reference);
  voidP_voidP_TableAdd(&reference_table, reference, reference);
}

static void remove_reference(smal_reference *reference)
{
  voidP_voidP_TableRemove(&referred_table, reference->referred);
  voidP_voidP_TableRemove(&reference_table, reference);
}


smal_reference * smal_reference_create_weak(void *ptr, smal_reference_queue *ref_queue)
{
  smal_reference *reference;

  if ( ! (reference = find_reference_by_referred(ptr)) ) {
    if ( ! reference_type ) {
      reference_type = smal_type_for(sizeof(*reference), reference_type_mark, 0);
    }
    reference = smal_alloc(reference_type);
    reference->data = 0;
    reference->referred = ptr;
    reference->reference_queue_list = 0;
    add_reference(reference);
  }
  if ( ref_queue ) {
    smal_reference_queue_list *ref_queue_list = malloc(sizeof(*ref_queue_list));
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
    smal_mark_ptr(list->reference);
    list = list->next;
  }
}

smal_reference_queue *smal_reference_queue_create()
{
  smal_reference_queue *ref_queue;
  if ( ! reference_queue_type ) {
    reference_queue_type = smal_type_for(sizeof(*ref_queue), ref_queue_mark, 0);
  }
  ref_queue = smal_alloc(reference_queue_type);
  ref_queue->reference_list = 0;
  return ref_queue;
}

static
void queue_reference(smal_reference_queue *ref_queue, smal_reference *reference)
{
  smal_reference_list *list;
  list = malloc(sizeof(*list));
  list->reference = reference;
  list->next = ref_queue->reference_list;
  ref_queue->reference_list = list;
}

smal_reference * smal_reference_queue_take(smal_reference_queue *ref_queue)
{
  smal_reference_list *list;
  if ( (list = ref_queue->reference_list) ) {
    smal_reference *reference = list->reference;
    ref_queue->reference_list = list->next;
    free(list);
    return reference;
  } else {
    return 0;
  }
}


/* For each freed objects:
   add it to its reference_queue,
   Remove it from the ptr->smal_reference table.
*/
static
int freed_object(smal_type *type, void *ptr, void *arg)
{
  smal_reference *reference;

  /* Is ptr a reference to smal_reference*? */
  if ( type == reference_type && (reference = find_reference(ptr)) ) {
    remove_reference(reference);
  } else
  /* Does ptr have a smal_reference pointing to it? */
  if ( (reference = find_reference_by_referred(ptr)) ) {
    remove_reference(reference);
    reference->referred = 0;
    while ( reference->reference_queue_list ) {
      smal_reference_queue_list *list = reference->reference_queue_list;
      queue_reference(list->reference_queue, reference);
      reference->reference_queue_list = list->next;
      free(list);
    }
  }
  return 0;
}

void smal_reference_before_sweep()
{
  smal_collect_each_freed_object(freed_object, 0);
}

void smal_reference_after_sweep()
{
}



