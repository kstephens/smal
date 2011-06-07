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
  voidP_TableInit(&self->ptr_table, 101);
  self->buf = buf;
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
  // fprintf(stderr, "  %p rs C\n", self->buf);
  voidP_TableClear(&self->ptr_table);
  self->ptrs_valid = 0;
}

static inline
void smal_remembered_set_add(smal_remembered_set *self, void *referrer, void *ptr)
{
  if ( voidP_TableAdd(&self->ptr_table, ptr) )  {
    // fprintf(stderr, "*");
    // fprintf(stderr, "  %p %p -> %p\n", self->buf, referrer, ptr);
  } else {
    // fprintf(stderr, ".");
    // fprintf(stderr, "  %p %p X> %p\n", self->buf, referrer, ptr);
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
  // fprintf(stderr, "  %p rs nptrs %d\n", self->buf, (int) self->n_ptrs);
}

static inline
void smal_remembered_set_mark(smal_remembered_set *self)
{
  smal_remembered_set_finish(self);
  // fprintf(stderr, "  %p rs mark %d\n", self->buf, (int) self->n_ptrs);
  smal_mark_ptr_n(0, self->n_ptrs, self->ptrs);
}
