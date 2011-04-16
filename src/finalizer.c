/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/finalizer.h"

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
};


static int initialized;
static voidP_voidP_Table referred_table;

static smal_finalized *find_finalized_by_referred(void *ptr)
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

static void add_finalized(smal_finalized *finalized)
{
  voidP_voidP_TableAdd(&referred_table, finalized->referred, finalized);
}

static void remove_finalized(smal_finalized *finalized)
{
  voidP_voidP_TableRemove(&referred_table, finalized->referred);
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
    add_finalized(finalized);
  }

  if ( ! (finalizer = malloc(sizeof(*finalizer))) )
    return 0;

  finalizer->referred = ptr;
  finalizer->func = func;
  finalizer->data = 0;

  finalizer->next = finalized->finalizer_list;
  finalized->finalizer_list = finalizer;

  return finalizer;
}

static
smal_finalized *finalized_queue;

static
void referred_sweeped(smal_finalized *finalized)
{
  // fprintf(stderr, "    ref %p => %p referred unreachable\n", finalizer, finalizer->referred);
  /* Prevent sweep this time. */
  smal_mark_ptr_exact(finalized->referred);
  /* Forget all finalizers. */
  remove_finalized(finalized);
  /* Add to finalized queue. */
  finalized->next = finalized_queue;
  finalized_queue = finalized;
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
  voidP_voidP_TableIteratorInit(&referred_table, &i);
  while ( voidP_voidP_TableIteratorNext(&referred_table, &i) ) {
    smal_finalized *finalized = *voidP_voidP_TableIteratorData(&referred_table, &i);
    if ( ! smal_object_reachableQ(finalized->referred) ) {
      referred_sweeped(finalized);
    }
  }
}

void smal_finalizer_after_sweep()
{
  smal_finalized *finalized;
  while ( (finalized = finalized_queue) ) {
    smal_finalizer *finalizer;
    while ( (finalizer = finalized->finalizer_list) ) {
      finalizer->func(finalizer);
      finalizer = finalized->finalizer_list->next;
      free(finalized->finalizer_list);
      finalized->finalizer_list = finalizer;
    }
    finalized = finalized->next;
    free(finalized_queue);
    finalized_queue = finalized;
  }
}

