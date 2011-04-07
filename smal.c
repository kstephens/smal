
#include <stdlib.h> /* malloc(), free() */
#include <string.h> /* memset() */
#include <sys/mman.h> /* mmap(), munmap() */
#include <stdio.h> /* perror() */
#include <sys/errno.h>
#include <stdarg.h>
#include <assert.h>
#include "sgc.h"

#if 0
#define sgc_buffer_element_size(buf) 24
#define sgc_buffer_element_alignment(buf) sgc_buffer_element_size(buf)
#endif

#ifndef sgc_buffer_element_size
#define sgc_buffer_element_size(buf) (buf)->element_size
#endif

#ifndef sgc_buffer_element_alignment
#define sgc_buffer_element_alignment(buf) (buf)->element_alignment
#endif

#define sgc_DLLIST_INIT(X) (X)->next = (X)->prev = (void*) X
#define sgc_DLLIST_INSERT(H,X)			\
  do {						\
    (X)->prev = (void*) (H);			\
    (X)->next = (void*) ((H)->next);		\
    (H)->next->prev = (void*) (X);		\
    (H)->next = (void*) (X);			\
  } while (0)

#define sgc_DLLIST_DELETE(X)			\
  do {						\
    (X)->prev->next = (X)->next;		\
    (X)->next->prev = (X)->prev;		\
  } while(0)

#define sgc_DLLIST_each(HEAD, X)			\
  do {							\
    void *X##_next = (HEAD)->next;			\
    while ( (void*) (X##_next) != (void*) (HEAD) ) {	\
      (X) = X##_next;					\
      X##_next = (X)->next;				\
  
#define sgc_DLLIST_each_END()			\
    }						\
  } while(0)					\

/* global list of all sgc_type. */
static sgc_type type_head;

/* global list of all sgc_buffers. */
static sgc_buffer buffer_head;

#define buffer_size (16 * 8 * 1024)
#define buffer_mask (buffer_size - 1)

#ifdef buffer_size
size_t _buffer_size = buffer_size;
#else
size_t buffer_size = 16 * 8 * 1024;
#endif
#ifdef buffer_mask
size_t _buffer_mask = buffer_mask;
#else
size_t buffer_mask = (16 * 8 * 1024) - 1;
#endif

static
sgc_buffer **buffer_table;
static
size_t buffer_table_size;

#define sgc_alignedQ(ptr,align) (((size_t)(ptr) % (align)) == 0)
#define sgc_ALIGN(ptr,align) if ( (size_t)(ptr) % (align) ) ptr += (align) - ((size_t)(ptr) % (align))

#define _sgc_buffer_hash(PTR) (((size_t) (PTR)) / buffer_size)
#define _sgc_buffer_offset(PTR) (((size_t) (PTR)) & buffer_mask)
#define _sgc_buffer_addr(PTR) ((void*)(((size_t) (PTR)) & ~buffer_mask))

size_t sgc_buffer_hash(void *ptr) {
  return _sgc_buffer_hash(ptr);
}
#define sgc_buffer_hash(PTR) _sgc_buffer_hash(PTR)

size_t sgc_buffer_offset(void *ptr) {
  return _sgc_buffer_offset(ptr);
}
#define sgc_buffer_offset(PTR) _sgc_buffer_offset(PTR)

sgc_buffer *sgc_buffer_addr(void *ptr) {
  return _sgc_buffer_addr(ptr);
}
#define sgc_buffer_addr(PTR) _sgc_buffer_addr(PTR)

static int in_gc;
static int inited;

static
void null_func(void *ptr)
{
  /* NOTHING */
}

/************************************************************************
 ** Maps buffer pages to actual sgc_buffer addresses.
 */

static
void sgc_buffer_table_add(sgc_buffer *self)
{
  size_t i;

  if ( ! buffer_table ) {
    buffer_table_size = 1;
    buffer_table = malloc(sizeof(buffer_table[0]) * buffer_table_size + 1);
    buffer_table[buffer_table_size] = 0;
  }

  i = sgc_buffer_hash(self) % buffer_table_size;
  if ( buffer_table[i] ) {
    do {
      size_t table_size_new = buffer_table_size + 1;
      sgc_buffer **table_new = malloc(sizeof(table_new[0]) * (table_size_new + 1));
      memset(table_new, 0, sizeof(table_new[0]) * (table_size_new + 1));
      table_new[table_size_new] = 0;
      for ( i = 0; i < buffer_table_size; ++ i ) {
	sgc_buffer *x = buffer_table[i];
	size_t j = sgc_buffer_hash(x) % table_size_new;
	assert(! table_new[j]);
	table_new[j] = x;
      }
      free(buffer_table);
      buffer_table = table_new;
      buffer_table_size = table_size_new;
      i = sgc_buffer_hash(self) % buffer_table_size;
    } while ( buffer_table[i] );
  }
  i = sgc_buffer_hash(self) % buffer_table_size;
  assert(! buffer_table[i]);
  buffer_table[i] = self;

  sgc_DLLIST_INSERT(&buffer_head, self);
}

static
void sgc_buffer_table_remove(sgc_buffer *self)
{
  size_t i = sgc_buffer_hash(self) % buffer_table_size;
  assert(buffer_table[i] == self);
  buffer_table[i] = 0;

  sgc_DLLIST_DELETE(self);
}


int sgc_debug_level = 4;

static
void _sgc_debug(const char *func, const char *format, ...)
{
  va_list vap;
  fprintf(stderr, "\rsgc: %s: ", func);
  va_start(vap, format);
  vfprintf(stderr, format, vap);
  va_end(vap);
  fprintf(stderr, "\n");
  fflush(stderr);
}
#define sgc_debug(level, msg, args...)		\
  do {						\
  if ( sgc_debug_level >= level ) 		\
    _sgc_debug(__FUNCTION__, msg, ##args);	\
  } while(0)

static 
sgc_buffer *sgc_buffer_alloc()
{
  sgc_buffer *self;

  /* mmap() enough to ensure a buffer of buffer_size aligned to buffer_size */
  size_t size = buffer_size * 2; 
  void *addr;
  size_t offset;
  void *keep_addr, *free_addr;
  size_t keep_size, free_size;
  int result;
  
  addr = mmap((void*) 0, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, (off_t) 0);
  sgc_debug(1, " mmap(..., %lu) = %p", (unsigned long) size, (void*) addr);

  if ( addr == MAP_FAILED ) {
    sgc_debug(2, "mmap failed: %s", strerror(errno));
    return 0;
  }

  if ( (offset = sgc_buffer_offset(addr)) ) {
    sgc_debug(2, "offset %p = 0x%0lx", (void*) addr, (unsigned long) offset);
    free_addr = addr;
    keep_addr = addr + (buffer_size - offset);
    free_size = keep_addr - addr;
    keep_size = buffer_size;
    assert(keep_addr == free_addr + free_size);
  } else {
    keep_addr = addr;
    keep_size = buffer_size;
    free_addr = addr + buffer_size;
    free_size = size - keep_size;
  }
  
  sgc_debug(3, "keeping %p[0x%0lx]", (void*) keep_addr, (unsigned long) keep_size);
  sgc_debug(3, "freeing %p[0x%0lx]", (void*) free_addr, (unsigned long) free_size);

  assert(keep_addr >= addr);
  assert(keep_addr + keep_size <= addr + size);

  /* Return the unused, unaligned half. */
  result = munmap(free_addr, free_size);
  sgc_debug(2, " munmap(%p,0x%lx) = %d", (void*) free_addr, (unsigned long) free_size, (int) result);

  self = keep_addr;
  memset(self, 0, sizeof(*self));

  self->next = self->prev = 0;
  self->buffer_id = ++ buffer_head.buffer_id;
  self->mmap_addr = keep_addr;
  self->mmap_size = keep_size;
  self->begin_ptr = self + 1;

  sgc_buffer_table_add(self);

  return self;
}


static 
void sgc_buffer_dealloc(sgc_buffer *self)
{
  int result;
  void *addr = self->mmap_addr; 
  size_t size = self->mmap_size;

  if ( self->type && self->type->alloc_buffer )
    self->type->alloc_buffer = 0;

  if ( self->mark_bits ) free(self->mark_bits);
  sgc_buffer_table_remove(self);

  buffer_head.alloc_n -= self->alloc_n;
  buffer_head.free_list_n -= self->free_list_n;

  result = munmap(addr, size);
  sgc_debug(2, " munmap(%p,%lu) = %d", (void*) addr, (unsigned long) size, (int) result);
}


/************************************************************************************
 * Mark bits
 */

#define sgc_BITS_PER_WORD (sizeof(unsigned int) * 8)

#define sgc_ptr_to_buffer(PTR) \
  buffer_table[sgc_buffer_hash(PTR) % buffer_table_size]

#define sgc_buffer_ptr_is_validQ(BUF, PTR) \
  ((BUF)->begin_ptr <= (PTR) && (PTR) < (BUF)->alloc_ptr && sgc_alignedQ((PTR), sgc_buffer_element_alignment(BUF)))

#define sgc_buffer_mark_offset(BUF, PTR) \
  (((void*)(PTR) - (void*)(BUF)) / sgc_buffer_element_size(BUF))

#define sgc_buffer_mark_i(BUF, PTR) \
  (sgc_buffer_mark_offset(BUF, PTR) / sgc_BITS_PER_WORD)

#define sgc_buffer_mark_b(BUF, PTR) \
  (1 << (sgc_buffer_mark_offset(BUF, PTR) % sgc_BITS_PER_WORD))

#define sgc_buffer_mark_word(BUF, PTR) \
  ((BUF)->mark_bits[sgc_buffer_mark_i(BUF, PTR)])

#define sgc_buffer_markQ(BUF, PTR) \
  (sgc_buffer_mark_word(BUF, PTR) & sgc_buffer_mark_b(BUF, PTR))

#define sgc_buffer_mark(BUF, PTR)					\
  do {									\
    sgc_buffer_mark_word(BUF, PTR) |= sgc_buffer_mark_b(BUF, PTR); \
    ++ (BUF)->mark_bits_n;						\
  } while (0);

#define sgc_buffer_mark_clear(BUF, PTR) \
  sgc_buffer_mark_word(BUF, PTR) &= ~sgc_buffer_mark_b(BUF, PTR)


static
void sgc_buffer_clear_mark_bits(sgc_buffer *self)
{
  if ( ! self->mark_bits ) {
    self->mark_bits = malloc(sizeof(self->mark_bits[0]) * self->mark_bits_size);
  }
  memset(self->mark_bits, 0, sizeof(self->mark_bits[0]) * self->mark_bits_size);
  self->mark_bits_n = 0;
}

static
void sgc_buffer_free_mark_bits(sgc_buffer *self)
{
  if ( self->mark_bits ) {
    free(self->mark_bits);
    self->mark_bits = 0;
  }
  self->mark_bits_n = 0;
}

static
void sgc_buffer_set_element_size(sgc_buffer *self, size_t element_size)
{
  sgc_debug(1, "%p, %d", self, (int) element_size);

  self->element_size = element_size;
  /* handle hardcoded element_size. */
  self->element_size = sgc_buffer_element_size(self);

  if ( ! self->element_alignment )
    self->element_alignment = self->element_size;
  /* handle hardcoded element_alignment. */
  self->element_alignment = sgc_buffer_element_alignment(self);

  sgc_ALIGN(self->begin_ptr, self->element_alignment);
  self->alloc_ptr = self->begin_ptr;

  self->end_ptr = self->mmap_addr + self->mmap_size;
  sgc_ALIGN(self->end_ptr, self->element_alignment);
  if ( self->end_ptr > self->mmap_addr + self->mmap_size )
    self->end_ptr -= self->element_size;

  self->element_capacity = (self->end_ptr - self->begin_ptr) / self->element_size;

  self->mark_bits_size = (self->mmap_size / self->element_size / sgc_BITS_PER_WORD) + 1;
  self->mark_bits_n = 0;

  sgc_debug(2, "  element_size = %d, element_alignment = %d",
	    (int) self->element_size, (int) self->element_alignment);

  sgc_debug(2, "  begin_ptr = %p, end_ptr = %p, element_capacity = %lu, mark_bits_size = %lu", 
	    (void*) self->begin_ptr, (void*) self->end_ptr, 
	    (unsigned long) self->element_capacity,
	    (unsigned long) self->mark_bits_size);
}


void sgc_mark_ptr(void *ptr)
{
  sgc_buffer *buf;
  if ( (buf = sgc_ptr_to_buffer(ptr)) ) {
    sgc_debug(5, "ptr %p => buf %p", ptr, buf);
    if ( sgc_buffer_ptr_is_validQ(buf, ptr) ) {
      sgc_debug(6, "ptr %p is valid in buf %p", ptr, buf);
      sgc_debug(7, "sgc_buffer_mark_word(%p, %p) = 0x%08x", buf, ptr,
		(unsigned int) sgc_buffer_mark_word(buf, ptr));
      if ( ! sgc_buffer_markQ(buf, ptr) ) {
	sgc_debug(5, "ptr %p is unmarked", ptr);
	sgc_buffer_mark(buf, ptr);
	buf->type->mark_func(ptr);
      }
    }
  }
}

void *sgc_buffer_alloc_element(sgc_buffer *self)
{
  void *ptr;

  if ( (ptr = self->free_list) ) {
    assert(self->free_list_n > 0);
    self->free_list = *(void**)self->free_list;
    -- self->free_list_n;
    assert(buffer_head.free_list_n > 0);
    -- buffer_head.free_list_n;
  } else if ( self->alloc_ptr < self->end_ptr ){
    ptr = self->alloc_ptr;
    self->alloc_ptr += sgc_buffer_element_size(self);
    ++ self->alloc_n;
    ++ buffer_head.alloc_n;
  } else {
    ptr = 0;
  }

  if ( ptr ) {
    ++ self->live_n;
    
    if ( in_gc ) {
      sgc_buffer_mark(self, ptr);
    }
  }

  sgc_debug(4, "buf = %p, ptr = %p", self, ptr);
  sgc_debug(4, "  alloc_ptr = %p, alloc_n = %d", self->alloc_ptr, self->alloc_n);
  sgc_debug(4, "  free_list_n = %d, live_n = %d",
	    self->free_list_n, self->live_n);

  return ptr;
}


static
void sgc_buffer_free_element(sgc_buffer *self, void *ptr)
{
  assert(sgc_buffer_ptr_is_validQ(self, ptr));

  self->type->free_func(ptr);
  * ((void**) ptr) = self->free_list;
  self->free_list = ptr;
  ++ self->free_list_n;
  assert(self->free_list_n);
  ++ buffer_head.free_list_n;
  assert(buffer_head.free_list_n);

  sgc_debug(4, "buf = %p, ptr = %p", self, ptr);
  sgc_debug(4, "  alloc_ptr = %p, alloc_n = %d", self->alloc_ptr, self->alloc_n);
  sgc_debug(4, "  free_list_n = %d, live_n = %d",
	    self->free_list_n, self->live_n);

}


void sgc_buffer_sweep(sgc_buffer *self)
{
  sgc_debug(3, "%p", self);
  sgc_debug(4, "  mark_bits_n = %d", self->mark_bits_n);
  if ( self->mark_bits_n ) {
    void *ptr;
    for ( ptr = self->begin_ptr; ptr < self->alloc_ptr; ptr += sgc_buffer_element_size(self) ) {
      if ( sgc_buffer_markQ(self, ptr) ) {
	++ self->live_n;
      } else {
	sgc_buffer_free_element(self, ptr);
      }
    }
    sgc_debug(4, "  live_n = %d, free_list_n = %d",
	      self->live_n, self->free_list_n);
    assert(self->mark_bits_n == self->live_n);
    sgc_buffer_free_mark_bits(self);
    buffer_head.live_n += self->live_n;
  } else {
    /* All elements in this buffer are free. */
    assert(! self->live_n);
    /* Call free_func, if necessary. */
    if ( self->type->free_func != null_func ) {
      void *ptr;
      for ( ptr = self->begin_ptr; ptr < self->alloc_ptr; ptr += sgc_buffer_element_size(self) ) {
	self->type->free_func(ptr);
      }
    }
    sgc_buffer_dealloc(self);
  }
}


void sgc_buffer_pre_mark(sgc_buffer *self)
{
  sgc_buffer_clear_mark_bits(self);
  self->live_n = 0;
}


void sgc_collect()
{
  sgc_buffer *buf;

  sgc_debug(1, "");

  if ( in_gc ) return;

  in_gc = 1;

  buffer_head.live_n = 0;
  sgc_DLLIST_each(&buffer_head, buf) {
    sgc_buffer_pre_mark(buf);
  } sgc_DLLIST_each_END();

  sgc_mark_roots();

  sgc_DLLIST_each(&buffer_head, buf) {
    sgc_buffer_sweep(buf);
  } sgc_DLLIST_each_END();

  in_gc = 0;
  sgc_debug(1, "  alloc_n = %d, live_n = %d, free_list_n = %d",
	    buffer_head.alloc_n,
	    buffer_head.live_n,
	    buffer_head.free_list_n
	    );
}

void sgc_init()
{
  if ( inited )
    return;

  if ( ! buffer_head.next ) {
    sgc_DLLIST_INIT(&buffer_head);
  }
  if ( ! type_head.next ) { 
    sgc_DLLIST_INIT(&type_head);
  }

  inited = 1;
}


/**********************************************/


sgc_type *sgc_type_for(size_t element_size, sgc_mark_func mark_func, sgc_free_func free_func)
{
  sgc_type *type;
  
  if ( ! inited ) {
    sgc_init();
  }

  /* must be big enough for free list next pointer. */
  if ( element_size < sizeof(void*) )
    element_size = sizeof(void*);
  /* Align size to at least sizeof(double) */
  sgc_ALIGN(element_size, sizeof(double));

  if ( ! mark_func )
    mark_func = null_func;
  if ( ! free_func )
    free_func = null_func;

  sgc_DLLIST_each(&type_head, type) {
    if ( type->element_size == element_size && 
	 type->mark_func == mark_func &&
	 type->free_func == free_func ) {
      return type;
    }
  } sgc_DLLIST_each_END();
  type = malloc(sizeof(*type));
  memset(type, 0, sizeof(*type));
  type->type_id = ++ type_head.type_id;
  type->element_size = element_size;
  type->mark_func = mark_func;
  type->free_func = free_func;
  sgc_DLLIST_INSERT(&type_head, type);

  return type;
}


static
sgc_buffer *sgc_type_find_alloc_buffer(sgc_type *self)
{
  sgc_buffer *buf;
  sgc_DLLIST_each(&buffer_head, buf) {
    if ( buf->type == self && buf->live_n != buf->element_capacity )
      return buf;
  } sgc_DLLIST_each_END();
  return 0;
}

static
sgc_buffer *sgc_type_alloc_buffer(sgc_type *self)
{
  if ( self->alloc_buffer ) {
    assert(self->alloc_buffer->type == self);
  } else {
    if ( ! (self->alloc_buffer = sgc_type_find_alloc_buffer(self)) ) {
      if ( (self->alloc_buffer = sgc_buffer_alloc()) ) {
	self->alloc_buffer->type = self;
	sgc_buffer_set_element_size(self->alloc_buffer, self->element_size);
      } else {
	return 0; /* OOM */
      }
    }
  }
  return self->alloc_buffer;
}

void *sgc_type_alloc(sgc_type *self)
{
  void *ptr;

  if ( ! sgc_type_alloc_buffer(self) )
    return 0;

  if ( ! (ptr = sgc_buffer_alloc_element(self->alloc_buffer)) ) {
    if ( ! sgc_type_alloc_buffer(self) )
      return 0;
    ptr = sgc_buffer_alloc_element(self->alloc_buffer);
  }

  return ptr;
}

/********************************************************************/

