/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

/* Optionally included directly into smal.c */

// FIXME: Use a voidP_Table, with no data.
#include "hash/voidP_voidP_Table.h"

typedef struct smal_remembered_set {
  voidP_voidP_Table ptr_table;
  int ptrs_valid;
  void **ptrs;
  size_t n_ptrs;
} smal_remembered_set;

static inline
void 
smal_remembered_set_init(smal_remembered_set *self)
{
  voidP_voidP_TableInit(&self->ptr_table, 101);
  self->ptrs_valid = 0;
  self->ptrs = 0;
  self->n_ptrs = 0;
}

static inline
void 
smal_remembered_set_destroy(smal_remembered_set *self)
{
  voidP_voidP_TableDestroy(&self->ptr_table);
  if ( self->ptrs )
    free(self->ptrs);
}

static inline
void 
smal_remembered_set_clear(smal_remembered_set *self)
{
  voidP_voidP_TableClear(&self->ptr_table);
  self->ptrs_valid = 0;
}

static inline
void smal_remembered_set_add(smal_remembered_set *self, void *ptr)
{
  if ( voidP_voidP_TableAdd(&self->ptr_table, ptr, ptr) ) 
    self->ptrs_valid = 0;
}

static inline
void smal_remembered_set_collapse(smal_remembered_set *self)
{
  voidP_voidP_TableIterator i;
  void **ptrp;

  self->n_ptrs = self->ptr_table._nentries;

  ptrp = self->ptrs = 
    self->ptrs ? 
    realloc(self->ptrs, sizeof(self->ptrs[0]) * self->n_ptrs) :
    malloc(sizeof(self->ptrs[0]) * self->n_ptrs);
  
  voidP_voidP_TableIteratorInit(&self->ptr_table, &i);
  while ( voidP_voidP_TableIteratorNext(&self->ptr_table, &i) ) {
    void *ptr = *voidP_voidP_TableIteratorKey(&self->ptr_table, &i);
    *(ptrp ++) = ptr;
  }

  self->ptrs_valid = 1;
  voidP_voidP_TableClear(&self->ptr_table);
}

static inline
void smal_remembered_set_mark(smal_remembered_set *self)
{
  if ( ! self->ptrs_valid )
    smal_remembered_set_collapse(self);

  smal_mark_ptr_n(0, self->n_ptrs, self->ptrs);
}
