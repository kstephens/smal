/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include <stdlib.h> /* malloc(), free() */
#include <string.h> /* memset() */
#include <sys/mman.h> /* mmap(), munmap() */
#include <stdio.h> /* perror() */
#include <sys/errno.h>
#include <stdarg.h>
#ifdef SMAL_PROF
#define NASSERT 1
#define malloc(x) ({ size_t size = (x); void *ptr = malloc(size); fprintf(stderr, "  SMAL_PROF: %s:%-4d: malloc(%lu) = %p\n", __FILE__, __LINE__, (unsigned long) size, ptr); ptr; })
#define free(x) fprintf(stderr, "  SMAL_PROF: %s:%-4d: free(%p) ignored\n", __FILE__, __LINE__, (x))
#endif
#include <assert.h>
#include "smal/smal.h"
#include "smal/dllist.h"


/*********************************************************************
 * Configuration
 */

#if 0
#define smal_buffer_object_size(buf) 24
#define smal_buffer_object_alignment(buf) smal_buffer_object_size(buf)
#endif

#ifndef smal_buffer_size_default
#define smal_buffer_size_default (4 * 4 * 1024)
#endif

#define smal_buffer_size smal_buffer_size_default
#define smal_buffer_mask (smal_buffer_size - 1)

#ifdef smal_buffer_size
size_t _smal_buffer_size = smal_buffer_size;
#else
size_t smal_buffer_size = smal_buffer_size_default;
#endif
#ifdef smal_buffer_mask
size_t _smal_buffer_mask = smal_buffer_mask;
#else
size_t smal_buffer_mask = smal_buffer_size_default - 1;
#endif


#ifndef smal_buffer_object_size
#define smal_buffer_object_size(buf) (buf)->object_size
#endif

#ifndef smal_buffer_object_alignment
#define smal_buffer_object_alignment(buf) (buf)->object_alignment
#endif

#ifndef SMAL_FREE_MARK_BITS
#define SMAL_FREE_MARK_BITS 0 /* free mark_bits after sweep? */
#endif

/*********************************************************************
 * bitmaps
 */

#define smal_BITS_PER_WORD (sizeof(unsigned int) * 8)

static
void smal_bitmap_clr_all(smal_bitmap *self)
{
  assert(self->bits);
  memset(self->bits, 0, self->bits_size);
  self->set_n = 0;
  self->clr_n = self->size;
}

#if 0
static
void smal_bitmap_set_all(smal_bitmap *self)
{
  memset(self->bits, ~0, self->bits_size);
  self->clr_n = 0;
  self->set_n = self->size;
}
#endif

static
int smal_bitmap_init(smal_bitmap *self)
{
  self->bits_size = sizeof(self->bits[0]) * ((self->size / smal_BITS_PER_WORD) + 1);
  self->set_n = self->clr_n = 0;
  assert(self->bits == 0);
  if ( ! (self->bits = malloc(self->bits_size)) )
    return -1;
  smal_bitmap_clr_all(self);
  return 0;
}

static
void smal_bitmap_free(smal_bitmap *self)
{
  if ( self->bits ) {
    free(self->bits);
    self->bits = 0;
  }
}


#define smal_bitmap_i(bm, i) ((i) / smal_BITS_PER_WORD)
#define smal_bitmap_w(bm, i) (bm)->bits[smal_bitmap_i(bm, i)]
#define smal_bitmap_b(bm, i) (1 << ((i) % smal_BITS_PER_WORD))
#define smal_bitmap_setQ(bm, i) (smal_bitmap_w(bm, i) & smal_bitmap_b(bm, i))
#define smal_bitmap_set(bm, i) (smal_bitmap_w(bm, i) |= smal_bitmap_b(bm, i))
#define smal_bitmap_set_c(bm, i) smal_bitmap_set(bm, i); (bm)->set_n ++; (bm)->clr_n --; 
#define smal_bitmap_clr(bm, i) (smal_bitmap_w(bm, i) &= ~ smal_bitmap_b(bm, i))
#define smal_bitmap_clr_c(bm, i) smal_bitmap_clr(bm, i); (bm)->set_n --; (bm)->clr_n ++;

/*********************************************************************
 * Debugging support.
 */

#ifndef SMAL_DEBUG
#define SMAL_DEBUG 0
#endif

int smal_debug_level = 0;

#if SMAL_DEBUG
static
void _smal_debug(const char *func, const char *format, ...)
{
  va_list vap;
  fprintf(stderr, "\rsmal: %s: ", func);
  va_start(vap, format);
  vfprintf(stderr, format, vap);
  va_end(vap);
  fprintf(stderr, "\n");
  fflush(stderr);
}

#define smal_debug(level, msg, args...)		\
  do {						\
  if ( smal_debug_level >= level ) 		\
    _smal_debug(__FUNCTION__, msg, ##args);	\
  } while(0)
#else
#define smal_debug(level, msg, args...) (void)0
#endif

/*********************************************************************
 * Debugging support.
 */

/* global list of all smal_type. */
static smal_type type_head;

/* global list of all smal_buffers. */
static smal_buffer buffer_head;

static
size_t buffer_id_min, buffer_id_max;

static
smal_buffer **buffer_table;
static
size_t buffer_table_size;

#define _smal_buffer_buffer_id(PTR) (((size_t) (PTR)) / smal_buffer_size)
#define _smal_buffer_offset(PTR) (((size_t) (PTR)) & smal_buffer_mask)
#define _smal_buffer_addr(PTR) ((void*)(((size_t) (PTR)) & ~smal_buffer_mask))

size_t smal_buffer_buffer_id(void *ptr) {
  return _smal_buffer_buffer_id(ptr);
}
#define smal_buffer_buffer_id(PTR) _smal_buffer_buffer_id(PTR)

size_t smal_buffer_offset(void *ptr) {
  return _smal_buffer_offset(ptr);
}
#define smal_buffer_offset(PTR) _smal_buffer_offset(PTR)

smal_buffer *smal_buffer_addr(void *ptr) {
  return _smal_buffer_addr(ptr);
}
#define smal_buffer_addr(PTR) _smal_buffer_addr(PTR)

static int in_gc;
static int no_gc;
static int inited;

static
void null_func(void *ptr)
{
  /* NOTHING */
}

/************************************************************************
 ** Maps buffer pages to actual smal_buffer addresses.
 */

#define smal_ptr_to_buffer(PTR)					\
  buffer_table[smal_buffer_buffer_id(PTR) % buffer_table_size]

#define smal_buffer_ptr_is_in_rangeQ(BUF, PTR)			\
  ((BUF)->begin_ptr <= (PTR) && (PTR) < (BUF)->alloc_ptr)

#define smal_buffer_ptr_is_alignedQ(BUF, PTR)			\
  smal_alignedQ((PTR), smal_buffer_object_alignment(BUF))

#define smal_buffer_ptr_is_validQ(BUF, PTR) \
  (					    \
   smal_buffer_ptr_is_alignedQ(BUF, PTR) && \
   smal_buffer_ptr_is_in_rangeQ(BUF, PTR)   \
					    )

static
void smal_buffer_table_add(smal_buffer *self)
{
  size_t i;
  size_t buffer_table_size_new;
  smal_buffer **buffer_table_new;
  
  // fprintf(stderr, "smal_buffer_table_add(%p)\n", self);

  if ( buffer_id_min == 0 && buffer_id_max == 0 ) {
    buffer_id_min = buffer_id_max = self->buffer_id;
  } else {
    if ( buffer_id_min > self->buffer_id )
      buffer_id_min = self->buffer_id;
    if ( buffer_id_max < self->buffer_id )
      buffer_id_max = self->buffer_id;
  }

  buffer_table_size_new = (buffer_id_max - buffer_id_min) + 1;
  buffer_table_new = malloc(sizeof(buffer_table_new[0]) * (buffer_table_size_new + 1));
  // fprintf(stderr, "smal_buffer_table_add(%p): malloc(%lu) = %p\n", self, (unsigned long) (sizeof(buffer_table_new[0]) * (buffer_table_size_new + 1)), buffer_table_new);
  memset(buffer_table_new, 0, sizeof(buffer_table_new[0]) * (buffer_table_size_new + 1));

  if ( buffer_table ) {
    for ( i = 0; i < buffer_table_size; ++ i ) {
      smal_buffer *x = buffer_table[i];
      if ( x ) {
	size_t j = x->buffer_id % buffer_table_size_new;
	assert(! buffer_table_new[j]);
	buffer_table_new[j] = x;
      }
    }
    // fprintf(stderr, "smal_buffer_table_add(%p): free(%p)\n", self, buffer_table);
    free(buffer_table);
  }

  buffer_table = buffer_table_new;
  buffer_table_size = buffer_table_size_new;

  i = self->buffer_id % buffer_table_size;
  assert(! buffer_table[i]);
  buffer_table[i] = self;

  smal_DLLIST_INSERT(&buffer_head, self);

  smal_debug(3, "buffer_table_size = %d", (int) buffer_table_size);
}

static
void smal_buffer_table_remove(smal_buffer *self)
{
  size_t i = self->buffer_id % buffer_table_size;
  assert(buffer_table[i] == self);
  buffer_table[i] = 0;

  smal_DLLIST_DELETE(self);
}


static
int smal_buffer_set_object_size(smal_buffer *self, size_t object_size);

static 
smal_buffer *smal_buffer_alloc(smal_type *type)
{
  smal_buffer *self;

  /* mmap() enough to ensure a buffer of smal_buffer_size, aligned to smal_buffer_size */
  size_t size = smal_buffer_size * 2; 
  void *addr;
  size_t offset;
  void *keep_addr, *free_addr;
  size_t keep_size, free_size;
  int result;
  
  smal_debug(1, "()");

  addr = mmap((void*) 0, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, (off_t) 0);
  smal_debug(2, " mmap(..., 0x%lx) = %p", (unsigned long) size, (void*) addr);

  if ( addr == MAP_FAILED ) {
    smal_debug(3, "mmap failed: %s", strerror(errno));
    return 0;
  }

  if ( (offset = smal_buffer_offset(addr)) ) {
    smal_debug(3, "offset %p = 0x%0lx", (void*) addr, (unsigned long) offset);
    free_addr = addr;
    keep_addr = addr + (smal_buffer_size - offset);
    free_size = keep_addr - addr;
    keep_size = smal_buffer_size;
    assert(keep_addr == free_addr + free_size);
  } else {
    keep_addr = addr;
    keep_size = smal_buffer_size;
    free_addr = addr + smal_buffer_size;
    free_size = size - keep_size;
  }
  
  smal_debug(3, "keeping %p[0x%0lx]", (void*) keep_addr, (unsigned long) keep_size);
  smal_debug(3, "freeing %p[0x%0lx]", (void*) free_addr, (unsigned long) free_size);

  assert(keep_addr >= addr);
  assert(keep_addr + keep_size <= addr + size);

  /* Return the unused, unaligned half. */
  result = munmap(free_addr, free_size);
  smal_debug(2, " munmap(%p,0x%lx) = %d", (void*) free_addr, (unsigned long) free_size, (int) result);

  self = keep_addr;
  memset(self, 0, sizeof(*self));

  self->next = self->prev = 0;
  self->type = type;
  self->buffer_id = smal_buffer_buffer_id(self);
  self->mmap_addr = keep_addr;
  self->mmap_size = keep_size;
  self->begin_ptr = self + 1;

  if ( smal_buffer_set_object_size(self, type->object_size) < 0 ) {
    munmap(self->mmap_addr, self->mmap_size);
    self = 0;
  } else {
    smal_buffer_table_add(self);
  }

  ++ self->type->stats.buffer_n;
  ++ buffer_head.stats.buffer_n;

  smal_debug(1, "() = %p", (void*) self);

  return self;
}


static 
void smal_buffer_free(smal_buffer *self)
{
  int result;
  void *addr = self->mmap_addr; 
  size_t size = self->mmap_size;

  smal_debug(1, "(%p)", self);

  if ( self->type ) {
    if ( self->type->alloc_buffer == self )
      self->type->alloc_buffer = 0;
    self->type->stats.buffer_n -= 1;
    self->type->stats.alloc_n -= self->stats.alloc_n;
    self->type->stats.avail_n -= self->stats.avail_n;
    self->type->stats.free_n  -= self->stats.free_n;
  }

  smal_bitmap_free(&self->free_bits);
  smal_bitmap_free(&self->mark_bits);

  smal_buffer_table_remove(self);

  assert(buffer_head.stats.alloc_n >= self->stats.alloc_n);
  buffer_head.stats.alloc_n -= self->stats.alloc_n;
  assert(buffer_head.stats.avail_n >= self->stats.avail_n);
  buffer_head.stats.avail_n -= self->stats.avail_n;
  assert(buffer_head.stats.free_n  >= self->stats.free_n);
  buffer_head.stats.free_n  -= self->stats.free_n;

  buffer_head.stats.buffer_n -= 1;

  result = munmap(addr, size);
  smal_debug(2, " munmap(%p,0x%lx) = %d", (void*) addr, (unsigned long) size, (int) result);
}

/************************************************************************************
 * bitmaps
 */

#define smal_buffer_i(BUF, PTR) \
  (((void*)(PTR) - (void*)(BUF)) / smal_buffer_object_size(BUF))

/************************************************************************************
 * Mark bits
 */

#define smal_buffer_markQ(BUF, PTR) \
  smal_bitmap_setQ(&(BUF)->mark_bits, smal_buffer_i(BUF, PTR))

#define smal_buffer_mark(BUF, PTR)				\
  smal_bitmap_set_c(&(BUF)->mark_bits, smal_buffer_i(BUF, PTR))

static
int smal_buffer_set_object_size(smal_buffer *self, size_t object_size)
{
  int result = 0;

  smal_debug(1, "%p: (%d)", self, (int) object_size);

  self->object_size = object_size;
  /* handle hardcoded object_size. */
  self->object_size = smal_buffer_object_size(self);

  if ( ! self->object_alignment )
    self->object_alignment = self->object_size;
  /* handle hardcoded object_alignment. */
  self->object_alignment = smal_buffer_object_alignment(self);

  /* Align begin_ptr. */
  smal_ALIGN(self->begin_ptr, self->object_alignment);
  self->alloc_ptr = self->begin_ptr;

  /* Align and restrict end_ptr to mmap boundary. */
  self->end_ptr = self->mmap_addr + self->mmap_size;
  smal_ALIGN(self->end_ptr, self->object_alignment);
  if ( self->end_ptr > self->mmap_addr + self->mmap_size )
    self->end_ptr -= self->object_size;

  self->object_capacity = (self->end_ptr - self->begin_ptr) / self->object_size;

  self->mark_bits.size = self->object_capacity;
  if ( smal_bitmap_init(&self->mark_bits) < 0 ) {
    result = -1;
    goto done;
  }

  self->free_bits.size = self->object_capacity;
  if ( smal_bitmap_init(&self->free_bits) < 0 ) {
    result = -1;
    goto done;
  }

  self->stats.avail_n = self->object_capacity;
  self->type->stats.avail_n += self->stats.avail_n;
  buffer_head.stats.avail_n += self->stats.avail_n;

 done:
  smal_debug(2, "  object_size = %d, object_alignment = %d",
	    (int) self->object_size, (int) self->object_alignment);

  smal_debug(2, "  begin_ptr = %p, end_ptr = %p, object_capacity = %lu, mark_bits.size = %lu", 
	    (void*) self->begin_ptr, (void*) self->end_ptr, 
	    (unsigned long) self->object_capacity,
	    (unsigned long) self->mark_bits.size);

  if ( result < 0 ) {
    smal_bitmap_free(&self->mark_bits);
    smal_bitmap_free(&self->free_bits);
  }

  return result;
}

static inline
void _smal_mark_ptr(void *ptr)
{
  smal_buffer *buf;
  if ( (buf = smal_ptr_to_buffer(ptr)) ) {
    // smal_debug(5, "ptr %p => buf %p", ptr, buf);
    if ( smal_buffer_ptr_is_validQ(buf, ptr) ) {
      // assert(buf->buffer_id == smal_buffer_buffer_id(buf));
#if 0
      smal_debug(6, "ptr %p is valid in buf %p", ptr, buf);
      smal_debug(7, "smal_buffer_mark_word(%p, %p) = 0x%08x", buf, ptr,
		(unsigned int) smal_buffer_mark_word(buf, ptr));
#endif
      if ( ! smal_buffer_markQ(buf, ptr) ) {
#if 0
	smal_debug(5, "ptr %p is unmarked", ptr);
#endif
	smal_buffer_mark(buf, ptr);
	buf->type->mark_func(ptr);
      }
    }
  }
}

void smal_mark_ptr(void *ptr)
{
  _smal_mark_ptr(ptr);
}


void smal_mark_ptr_range(void *ptr, void *ptr_end)
{
  smal_ALIGN(ptr, sizeof(int));
  ptr_end -= sizeof(void*) - 1;

  while ( ptr < ptr_end ) {
    _smal_mark_ptr(*(void**) ptr);
    ptr += sizeof(int);
  }
}


void smal_mark_ptr_exact(void *ptr)
{
  smal_buffer *buf;
  if ( ! ptr )
    return;
  buf = smal_ptr_to_buffer(ptr);
  if ( smal_buffer_ptr_is_in_rangeQ(buf, ptr) ) {
    if ( ! smal_buffer_markQ(buf, ptr) ) {
      smal_buffer_mark(buf, ptr);
      buf->type->mark_func(ptr);
    }
  }
}

#if 1
#define smal_buffer_check_free_list(self) (void) 0
#else
static 
void smal_buffer_check_free_list(smal_buffer *self)
{
  void *p;
  int i = 0;
  // fprintf(stderr, "%p free_list [%d] = { ", (void*) self, (int) self->free_n);
  for ( p = self->free_list; p; p = *((void**) p) ) {
    // fprintf(stderr, "%p, ", p);
    assert(++ i <= self->free_n);
  }
  // fprintf(stderr, "}\n");
}
#endif

void *smal_buffer_alloc_object(smal_buffer *self)
{
  void *ptr;

  if ( (ptr = self->free_list) ) {
    assert(self->stats.free_n > 0);

    self->free_list = * (void**) ptr;
    smal_bitmap_clr_c(&self->free_bits, smal_buffer_i(self, ptr));

    -- self->stats.free_n;
    -- self->type->stats.free_n;
    assert(buffer_head.stats.free_n > 0);
    -- buffer_head.stats.free_n;
  } else if ( self->alloc_ptr < self->end_ptr ) {

    ptr = self->alloc_ptr;
    self->alloc_ptr += smal_buffer_object_size(self);

    ++ self->stats.alloc_n;
    ++ self->type->stats.alloc_n;
    ++ buffer_head.stats.alloc_n;
  } else {
    ptr = 0;
  }

  if ( ptr ) {
    ++ self->stats.alloc_id;
    ++ self->type->stats.alloc_id;
    ++ buffer_head.stats.alloc_id;

    ++ self->stats.live_n;
    assert(self->stats.avail_n);
    ++ self->type->stats.live_n;
    ++ buffer_head.stats.live_n;

    -- self->stats.avail_n;
    assert(self->type->stats.avail_n);
    -- self->type->stats.avail_n;
    assert(buffer_head.stats.avail_n);
    -- buffer_head.stats.avail_n;

    if ( in_gc ) {
      smal_buffer_mark(self, ptr);
    }
  }

  smal_debug(4, "(%p) = %p", self, ptr);
  smal_debug(4, "  alloc_ptr = %p, stats.alloc_n = %d", self->alloc_ptr, self->stats.alloc_n);
  smal_debug(4, "  stats.free_n = %d, stats.avail_n = %d, stats.live_n = %d",
	     self->stats.free_n, self->stats.avail_n, self->stats.live_n);

  smal_buffer_check_free_list(self);

  return ptr;
}


static
void smal_buffer_free_object(smal_buffer *self, void *ptr)
{
  smal_bitmap_set_c(&self->free_bits, smal_buffer_i(self, ptr));

  self->type->free_func(ptr);

  * ((void**) ptr) = self->free_list;
  self->free_list = ptr;

  ++ self->stats.free_n;
  assert(self->stats.free_n);
  ++ self->type->stats.free_n;
  ++ buffer_head.stats.free_n;
  assert(buffer_head.stats.free_n);

  ++ self->stats.avail_n;
  ++ self->type->stats.avail_n;
  ++ buffer_head.stats.avail_n;

  ++ self->stats.free_id;
  ++ self->type->stats.free_id;
  ++ buffer_head.stats.free_id;
  
  smal_debug(4, "%p: (%p)", self, ptr);
  smal_debug(4, "  alloc_ptr = %p, stats.alloc_n = %d", self->alloc_ptr, self->stats.alloc_n);
  smal_debug(4, "  stats.free_n = %d, stats.avail_n = %d, stats.live_n = %d",
	     self->stats.free_n, self->stats.avail_n, self->stats.live_n);

  smal_buffer_check_free_list(self);
}


void smal_buffer_sweep(smal_buffer *self)
{
  smal_debug(3, "(%p)", self);
  smal_debug(4, "  mark_bits.set_n = %d", self->mark_bits.set_n);
  if ( self->mark_bits.set_n ) {
    void *ptr;
    for ( ptr = self->begin_ptr; ptr < self->alloc_ptr; ptr += smal_buffer_object_size(self) ) {
      if ( smal_buffer_markQ(self, ptr) ) {
	++ self->stats.live_n;
      } else {
	if ( ! smal_bitmap_setQ(&self->free_bits, smal_buffer_i(self, ptr)) ) {
	  smal_buffer_free_object(self, ptr);
	}
      }
    }
    smal_debug(4, "  stats.live_n = %d, stats.free_n = %d",
	      self->stats.live_n, self->stats.free_n);
    assert(self->mark_bits.set_n == self->stats.live_n);

#ifndef SMAL_PROF
#if SMAL_FREE_MARK_BITS
    smal_bitmap_free(&self->mark_bits);
#endif
#endif

    self->type->stats.live_n += self->stats.live_n;
    buffer_head.stats.live_n += self->stats.live_n;
  } else {
    /* All objects in this buffer are free. */
    assert(! self->stats.live_n);
    /* Call free_func, if necessary. */
    if ( self->type->free_func != null_func ) {
      void *ptr;
      for ( ptr = self->begin_ptr; ptr < self->alloc_ptr; ptr += smal_buffer_object_size(self) ) {
	self->type->free_func(ptr);
      }
    }

    /* Update free_id by the number actually sweeped. */
    {
      size_t sweep_n = self->stats.alloc_n - self->stats.free_n;
      self->stats.free_id       += sweep_n;
      self->type->stats.free_id += sweep_n;
      buffer_head.stats.free_id += sweep_n;
    }
    smal_buffer_free(self);
  }
}

static
void smal_buffer_pre_mark(smal_buffer *self)
{
#if SMAL_FREE_MARK_BITS
  smal_bitmap_init(&self->mark_bits);
#endif
  /* Clear mark bits. */
  smal_bitmap_clr_all(&self->mark_bits);

  /* Prepare to re-compute live_n. */
  self->type->stats.live_n -= self->stats.live_n;
  buffer_head.stats.live_n -= self->stats.live_n;
  self->stats.live_n = 0;
}


void smal_collect()
{
  smal_buffer *buf;

  smal_debug(1, "()");

  if ( in_gc || no_gc ) return;

  in_gc = 1;

  smal_DLLIST_each(&buffer_head, buf) {
    smal_buffer_pre_mark(buf);
  } smal_DLLIST_each_END();

  smal_mark_roots();

  smal_DLLIST_each(&buffer_head, buf) {
    smal_buffer_sweep(buf);
  } smal_DLLIST_each_END();

  in_gc = 0;
  smal_debug(1, "  stats.alloc_n = %d, stats.live_n = %d, stats.avail_n = %d, stats.free_n = %d",
	     buffer_head.stats.alloc_n,
	     buffer_head.stats.live_n,
	     buffer_head.stats.avail_n,
	     buffer_head.stats.free_n
	    );
}

void smal_init()
{
  const char *s;

  if ( inited )
    return;

  if ( (s = getenv("SMAL_DEBUG_LEVEL")) ) {
    smal_debug_level = atoi(s);
    if ( smal_debug_level > 0 && ! SMAL_DEBUG ) {
      fprintf(stderr, "SMAL: SMAL_DEBUG not compiled in\n");
    }
  }

  if ( ! buffer_head.next ) {
    smal_DLLIST_INIT(&buffer_head);
  }

  if ( ! type_head.next ) { 
    smal_DLLIST_INIT(&type_head);
  }

  inited = 1;
}


/**********************************************/


smal_type *smal_type_for(size_t object_size, smal_mark_func mark_func, smal_free_func free_func)
{
  smal_type *type;
  
  if ( ! inited ) {
    smal_init();
  }

  /* must be big enough for free list next pointer. */
  if ( object_size < sizeof(void*) )
    object_size = sizeof(void*);
  /* Align size to at least sizeof(double) */
  smal_ALIGN(object_size, sizeof(double));

  if ( ! mark_func )
    mark_func = null_func;
  if ( ! free_func )
    free_func = null_func;

  smal_DLLIST_each(&type_head, type) {
    if ( type->object_size == object_size && 
	 type->mark_func == mark_func &&
	 type->free_func == free_func ) {
      return type;
    }
  } smal_DLLIST_each_END();

  type = malloc(sizeof(*type));
  memset(type, 0, sizeof(*type));
  type->type_id = ++ type_head.type_id;
  type->object_size = object_size;
  type->mark_func = mark_func;
  type->free_func = free_func;
  smal_DLLIST_INSERT(&type_head, type);

  return type;
}

void smal_type_free(smal_type *self)
{
  smal_buffer *buf;

  smal_DLLIST_each(&buffer_head, buf) {
    if ( buf->type == self )
      smal_buffer_free(buf);
  } smal_DLLIST_each_END();

  smal_DLLIST_DELETE(self);
  free(self);
}

static
smal_buffer *smal_type_find_alloc_buffer(smal_type *self)
{
  smal_buffer *most_used = 0;
  smal_buffer *buf;

  /* TODO: Find the smal_buffer that has the least free objects available. */
  smal_DLLIST_each(&buffer_head, buf) {
    most_used = buf;
    if ( buf->type == self && 
	 (buf->free_list || buf->stats.live_n != buf->object_capacity) )
      return buf;
  } smal_DLLIST_each_END();

  return 0;
}

static
smal_buffer *smal_type_alloc_buffer(smal_type *self)
{
  if ( self->alloc_buffer ) {
    assert(self->alloc_buffer->type == self);
  } else {
    if ( ! (self->alloc_buffer = smal_type_find_alloc_buffer(self)) ) {
      self->alloc_buffer = smal_buffer_alloc(self);
      if ( (self->alloc_buffer = smal_buffer_alloc(self)) ) {
      } else {
	return 0; /* OOM */
      }
    }
  }
  return self->alloc_buffer;
}

void *smal_alloc(smal_type *self)
{
  void *ptr;

  /* Validate or allocated a smal_buffer. */
  if ( ! smal_type_alloc_buffer(self) )
    return 0;

  /* If current smal_buffer cannot provide. */
  if ( ! (ptr = smal_buffer_alloc_object(self->alloc_buffer)) ) {
    /* Allocate a new smal_buffer. */
    self->alloc_buffer = 0;
    
    /* If cannot allocate new smal_buffer, out-of-memory. */
    if ( ! smal_type_alloc_buffer(self) )
      return 0;

    /* Allocate object from smal_buffer. */
    ptr = smal_buffer_alloc_object(self->alloc_buffer);
  }

  return ptr;
}

void smal_free(void *ptr)
{
  smal_buffer *buf;
  if ( (buf = smal_ptr_to_buffer(ptr)) ) {
    smal_debug(5, "ptr %p => buf %p", ptr, buf);
    if ( smal_buffer_ptr_is_validQ(buf, ptr) ) {
      assert(buf->buffer_id == smal_buffer_buffer_id(buf));
      smal_debug(6, "ptr %p is valid in buf %p", ptr, buf);
      smal_buffer_free_object(buf, ptr);
    }
#if 0
    /* HAVE NOT FULLY MARKED! */
    if ( ! buf->stats.live_n )
      smal_buffer_free(buf);
#endif
  }
}

/********************************************************************/

void smal_each_object(void (*func)(smal_type *type, void *ptr, void *arg), void *arg)
{
  smal_buffer *buf;

  if ( in_gc )
    return;

  ++ no_gc;

  smal_DLLIST_each(&buffer_head, buf) {
    void *ptr;
    for ( ptr = buf->begin_ptr; ptr < buf->alloc_ptr; ptr += smal_buffer_object_size(buf) ) {
      if ( ! smal_bitmap_setQ(&buf->free_bits, smal_buffer_i(buf, ptr)) ) {
	func(buf->type, ptr, arg);
      }
    }
  } smal_DLLIST_each_END();

  -- no_gc;
}

void smal_shutdown()
{
  smal_buffer *buf;
  smal_type *type;

  if ( ! inited )
    return;

  if ( in_gc )
    abort();

  ++ no_gc;

  smal_DLLIST_each(&buffer_head, buf) {
    smal_buffer_free(buf);
  } smal_DLLIST_each_END();

  smal_DLLIST_each(&type_head, type) {
    smal_type_free(type);
  } smal_DLLIST_each_END();

  free(buffer_table);
  buffer_table = 0;

  -- no_gc;

  inited = 0;
}

void smal_global_stats(smal_stats *stats)
{
  *stats = buffer_head.stats;
}

void smal_type_stats(smal_type *type, smal_stats *stats)
{
  *stats = type->stats;
}

