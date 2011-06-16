/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

/* Optionally included directly into smal.c */

#include "hash/voidP_Table.h"

typedef struct smal_remembered_set {
  voidP_Table ptr_table;
  int ptrs_valid;
  void **ptrs;
  size_t n_ptrs;
  smal_buffer *buf;
} smal_remembered_set;

static inline
void 
smal_remembered_set_init(smal_remembered_set *self, smal_buffer *buf)
{
  self->buf = buf;
  smal_debug(remembered_set, 2, " b@%p init", self->buf);
  voidP_TableInit(&self->ptr_table, 101);
  self->ptrs_valid = 0;
  self->ptrs = 0;
  self->n_ptrs = 0;
}

static inline
smal_remembered_set *
smal_remembered_set_new(smal_buffer *buf)
{
  smal_remembered_set *self = malloc(sizeof(*self));
  memset(self, 0, sizeof(*self));
  smal_remembered_set_init(self, buf);
  return self;
}

static inline
void 
smal_remembered_set_destroy(smal_remembered_set *self)
{
  smal_debug(remembered_set, 2, " b@%p destroy", self->buf);
  voidP_TableDestroy(&self->ptr_table);
  if ( self->ptrs )
    free(self->ptrs);
  self->buf = 0;
}

static inline
void 
smal_remembered_set_free(smal_remembered_set *self)
{
  if ( ! self ) return;
  smal_remembered_set_destroy(self);
  free(self);
}

static inline
void 
smal_remembered_set_clear(smal_remembered_set *self)
{
  smal_debug(remembered_set, 2, " b@%p clear", self->buf);
  voidP_TableClear(&self->ptr_table);
  self->ptrs_valid = 0;
}

static inline
void smal_remembered_set_add(smal_remembered_set *self, void *referrer, void *ptr)
{
  if ( voidP_TableAdd(&self->ptr_table, ptr) )  {
    smal_debug(remembered_set, 4, " b@%p @%p -> @%p", self->buf, referrer, ptr); 
  } else {
    smal_debug(remembered_set, 9, " b@%p @%p X> @%p", self->buf, referrer, ptr); 
  }
}

static inline
void smal_remembered_set_finish(smal_remembered_set *self)
{
  voidP_TableIterator i;
  void **ptrp;

  if ( self->ptrs_valid )
    return;

  self->n_ptrs = self->ptr_table._nentries;

  ptrp = self->ptrs = 
    self->ptrs ? 
    realloc(self->ptrs, sizeof(self->ptrs[0]) * self->n_ptrs) :
    malloc(sizeof(self->ptrs[0]) * self->n_ptrs);
  
  voidP_TableIteratorInit(&self->ptr_table, &i);
  while ( voidP_TableIteratorNext(&self->ptr_table, &i) ) {
    void *ptr = *voidP_TableIteratorKey(&self->ptr_table, &i);
    *(ptrp ++) = ptr;
  }
  assert(ptrp - self->ptrs == self->n_ptrs);
    
  self->ptrs_valid = 1;
  voidP_TableClear(&self->ptr_table);
  smal_debug(remembered_set, 2, " b@%p finish nptrs %d", self->buf, (int) self->n_ptrs);
}

static inline
void smal_remembered_set_mark(smal_remembered_set *self)
{
  smal_remembered_set_finish(self);
  smal_debug(remembered_set, 3, " b@%p mark %d", self->buf, (int) self->n_ptrs);
  smal_mark_ptr_n(0, self->n_ptrs, self->ptrs);
}
