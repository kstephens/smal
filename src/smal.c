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
#include "smal/thread.h"


static int initialized;
static void initialize();

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
#define smal_bitmap_set_c(bm, i) \
  do { \
    smal_bitmap_set(bm, i); \
    (bm)->set_n ++; \
    (bm)->clr_n --; \
  } while ( 0 )
#define smal_bitmap_clr(bm, i) (smal_bitmap_w(bm, i) &= ~ smal_bitmap_b(bm, i))
#define smal_bitmap_clr_c(bm, i)		\
  do {						\
    smal_bitmap_clr(bm, i);			\
    (bm)->set_n --;				\
    (bm)->clr_n ++;				\
  } while ( 0 )

/*********************************************************************
 * Debugging support.
 */

const char *smal_stats_names[] = {
  "alloc_id",
  "free_id",
  "capacity_n",
  "alloc_n",
  "avail_n",
  "live_n",
  "live_before_sweep_n",
  "free_n",
  "buffer_n",
  "mmap_size",
  "mmap_total",
  0
};

#ifndef SMAL_DEBUG
#define SMAL_DEBUG 0
#endif

int smal_debug_level = 0;
static smal_thread_mutex _smal_debug_mutex;

#if SMAL_DEBUG
static
void _smal_debug(const char *func, const char *format, ...)
{
  va_list vap;
  smal_thread_mutex_lock(&_smal_debug_mutex);
  fprintf(stderr, "\rsmal: t@%p %s: ", smal_thread_self(), func);
  va_start(vap, format);
  vfprintf(stderr, format, vap);
  va_end(vap);
  fprintf(stderr, "\n");
  fflush(stderr);
  smal_thread_mutex_unlock(&_smal_debug_mutex);
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
 * Global data.
 */

static smal_thread_rwlock alloc_lock;

/* global list of all smal_type. */
static smal_type type_head;
static smal_thread_mutex type_head_mutex;

/* global list of all smal_buffers. */
static smal_buffer buffer_head;
static smal_thread_rwlock buffer_head_lock;

static smal_buffer_list_head buffer_collecting;
static smal_thread_rwlock buffer_collecting_lock;

static size_t buffer_id_min, buffer_id_max;
static smal_thread_mutex buffer_id_min_max_mutex;
static smal_thread_lock buffer_id_min_max_keep;

static smal_buffer *buffer_table_init[] = { 0 };
static smal_buffer **buffer_table = buffer_table_init;
static size_t buffer_table_size = 1;
static smal_thread_rwlock buffer_table_lock;

static smal_thread_lock _smal_collect_inner_lock;

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

static int in_collect;
static int in_mark;
static int in_sweep;
static int no_collect;

static
void null_func(void *ptr)
{
  /* NOTHING */
}

/************************************************************************
 ** Maps buffer pages to actual smal_buffer addresses.
 */

#define smal_ptr_to_buffer(PTR)						\
  smal_WITH_RDLOCK(&buffer_table_lock, smal_buffer*, buffer_table[smal_buffer_buffer_id(PTR) % buffer_table_size])

#define smal_buffer_alloc_ptr(BUF)				       \
  smal_WITH_MUTEX(&(BUF)->alloc_ptr_mutex, void*, (BUF)->alloc_ptr)	       \
  
#define smal_buffer_ptr_is_in_rangeQ(BUF, PTR)				\
  ((BUF)->begin_ptr <= (PTR) && (PTR) < smal_buffer_alloc_ptr(BUF))

#define smal_buffer_ptr_is_alignedQ(BUF, PTR)			\
  smal_alignedQ((PTR), smal_buffer_object_alignment(BUF))

#define smal_buffer_ptr_is_validQ(BUF, PTR) \
  (					    \
   smal_buffer_ptr_is_alignedQ(BUF, PTR) && \
   smal_buffer_ptr_is_in_rangeQ(BUF, PTR)   \
					    )

void smal_buffer_print_all(smal_buffer *self, const char *action)
{
  smal_buffer *buf;
  fprintf(stderr, "  %s b@%p:\n    buffer_head { ", action, self);
  smal_dllist_each(&buffer_head, buf); {
    fprintf(stderr, " @%p, ", buf);
  } smal_dllist_each_end();
  fprintf(stderr, "}\n");
  fprintf(stderr, "    buffer_collecting { ");
  smal_dllist_each(&buffer_collecting, buf); {
    fprintf(stderr, " b@%p, ", buf);
  } smal_dllist_each_end();
  fprintf(stderr, "}\n");
}

static
void smal_buffer_table_add(smal_buffer *self)
{
  size_t i;
  size_t buffer_table_size_new;
  smal_buffer **buffer_table_new;
  
  // fprintf(stderr, "smal_buffer_table_add(%p)\n", self);

  /* Recompute buffer_id_min, buffer_id_max */
  smal_thread_mutex_lock(&buffer_id_min_max_mutex);

  /* buffer_id_min and buffer_id_max is stale. */
  if ( buffer_id_min == 0 && buffer_id_max == 0 ) {
    smal_buffer *buf;
    buffer_id_min = buffer_id_max = _smal_buffer_buffer_id(self);
    smal_thread_rwlock_rdlock(&buffer_head_lock);
    smal_dllist_each(&buffer_head, buf); {
      if ( buffer_id_min > buf->buffer_id )
	buffer_id_min = buf->buffer_id;
      if ( buffer_id_max < buf->buffer_id )
	buffer_id_max = buf->buffer_id;
    } smal_dllist_each_end();
    smal_thread_rwlock_unlock(&buffer_head_lock);
  } else {
    if ( buffer_id_min > self->buffer_id )
      buffer_id_min = self->buffer_id;
    if ( buffer_id_max < self->buffer_id )
      buffer_id_max = self->buffer_id;
  }

  buffer_table_size_new = (buffer_id_max - buffer_id_min) + 1;

  smal_thread_mutex_unlock(&buffer_id_min_max_mutex);

  buffer_table_new = malloc(sizeof(buffer_table_new[0]) * (buffer_table_size_new + 1));
  memset(buffer_table_new, 0, sizeof(buffer_table_new[0]) * (buffer_table_size_new + 1));

#if 0
  fprintf(stderr, "smal_buffer_table_add(%p): malloc([%lu]) = %p\n", self, (unsigned long) buffer_table_size_new, buffer_table_new);
  fprintf(stderr, "  %lu buffer_id %lu @ %p [%lu, %lu] %lu\n", (unsigned long) buffer_head.stats.buffer_n, (unsigned long) self->buffer_id, self, (unsigned long) buffer_id_min, (unsigned long) buffer_id_max, (unsigned long) buffer_table_size_new);
#endif

  smal_thread_rwlock_wrlock(&buffer_table_lock);

  if ( buffer_table ) {
    for ( i = 0; i < buffer_table_size; ++ i ) {
      smal_buffer *x = buffer_table[i];
      if ( x ) {
	size_t j = _smal_buffer_buffer_id(x) % buffer_table_size_new;
	assert(! buffer_table_new[j]);
	buffer_table_new[j] = x;
      }
    }
    // fprintf(stderr, "smal_buffer_table_add(%p): free(%p)\n", self, buffer_table);
    free(buffer_table);
  }

  buffer_table = buffer_table_new;
  buffer_table_size = buffer_table_size_new;

  i = _smal_buffer_buffer_id(self) % buffer_table_size;
  assert(! buffer_table[i]);
  buffer_table[i] = self;

  smal_thread_rwlock_unlock(&buffer_table_lock);

  smal_thread_rwlock_wrlock(&buffer_head_lock);
  smal_dllist_init(self);
  smal_dllist_insert(&buffer_head, self);
  // smal_buffer_print_all(self, "add");
  smal_thread_rwlock_unlock(&buffer_head_lock);
  
  smal_debug(3, "buffer_table_size = %d", (int) buffer_table_size);
}

static
void smal_buffer_table_remove(smal_buffer *self)
{
  size_t i;

  smal_thread_rwlock_wrlock(&buffer_table_lock);
  i = self->buffer_id % buffer_table_size;
  assert(buffer_table[i] == self);
  buffer_table[i] = 0;
  smal_thread_rwlock_unlock(&buffer_table_lock);

  // smal_thread_rwlock_wrlock(&buffer_head_lock);
  smal_dllist_delete(self); /* Remove from global buffer list. */
  // smal_buffer_print_all(self, "remove");

  // smal_thread_rwlock_unlock(&buffer_head_lock);

  /* Check for lock during collect. */
  if ( ! smal_thread_lock_test(&buffer_id_min_max_keep) ) {  
    /* Adjust buffer_table window. */
    smal_thread_mutex_lock(&buffer_id_min_max_mutex);
    /* Was buffer at beginning or end of buffer id space? */
    if ( buffer_id_min == self->buffer_id || buffer_id_max == self->buffer_id ) {
      buffer_id_min = buffer_id_max = 0;
    }
    smal_thread_mutex_unlock(&buffer_id_min_max_mutex);
  }
}


static
int smal_buffer_set_object_size(smal_buffer *self, size_t object_size);

#define smal_LOCK_STATS(N)						\
  do {									\
    smal_thread_mutex_##N(&buffer_head.stats._mutex);			\
    smal_thread_mutex_##N(&self->type->stats._mutex);			\
    smal_thread_mutex_##N(&self->stats._mutex);				\
  } while ( 0 ) 

#define smal_UPDATE_STATS(N, EXPR)					\
  do {									\
    buffer_head.stats.N EXPR;						\
    self->type->stats.N EXPR;						\
    self->stats.N EXPR;							\
  } while ( 0 )

static
smal_buffer *smal_buffer_alloc(smal_type *type)
{
  smal_buffer *self;
  size_t size;
  void *addr;
  size_t offset;
  int result;
  
  smal_debug(1, "()");

  /* Attempt first alignment via exact size. */
  size = smal_buffer_size;
  addr = mmap((void*) 0, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, (off_t) 0);
  smal_debug(2, " mmap(..., 0x%lx) = %p", (unsigned long) size, (void*) addr);

  if ( addr == MAP_FAILED ) {
    smal_debug(3, "mmap failed: %s", strerror(errno));
    return 0;
  }

  /* Not aligned? */
  if ( (offset = smal_buffer_offset(addr)) ) {
    void *keep_addr = 0, *free_addr = 0;
    size_t keep_size = 0, free_size = 0;

    result = munmap(addr, size);
    smal_debug(2, " munmap(%p,0x%lx) = %d", (void*) addr, (unsigned long) size, (int) result);

#if 0
    fprintf(stderr, "mmap retry, %p not aligned to 0x%lx\n", addr, (unsigned long) smal_buffer_size);
#endif

    /* mmap() enough to ensure a buffer of smal_buffer_size, aligned to smal_buffer_size */
    /* Allocate twice the size needed. */
    size = smal_buffer_size * 2;
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

    addr = keep_addr;
    size = keep_size;
  }

  self = addr;
  memset(self, 0, sizeof(*self));

  self->type = type;
  self->buffer_id = smal_buffer_buffer_id(self);
  self->mmap_addr = addr;
  self->mmap_size = size;
  self->begin_ptr = self + 1;
  self->type_buffer_list.buffer = self;

  smal_thread_mutex_init(&self->stats._mutex);
  smal_thread_mutex_init(&self->alloc_ptr_mutex);
  smal_thread_rwlock_init(&self->mark_bits_lock);
  smal_thread_rwlock_init(&self->free_bits_lock);
  smal_thread_mutex_init(&self->free_list_mutex);

  smal_thread_lock_init(&self->alloc_disabled);

  if ( smal_buffer_set_object_size(self, type->object_size) < 0 ) {
    smal_debug(2, " smal_buffer_set_object_size failed: %s", strerror(errno));
    result = munmap(self->mmap_addr, self->mmap_size);
    smal_debug(2, " munmap(%p,0x%lx) = %d", (void*) self->mmap_addr, (unsigned long) self->mmap_size, (int) result);
    self = 0;
  } else {
    smal_buffer_table_add(self);

    smal_thread_rwlock_wrlock(&type->buffers_lock);
    smal_dllist_init(&self->type_buffer_list);
    smal_dllist_insert(&type->buffers, &self->type_buffer_list);
    smal_thread_rwlock_unlock(&type->buffers_lock);

    smal_LOCK_STATS(lock);
    smal_UPDATE_STATS(buffer_n,  += 1);
    smal_UPDATE_STATS(mmap_size, += self->mmap_size);
    smal_UPDATE_STATS(mmap_total, += self->mmap_size);
    smal_LOCK_STATS(unlock);
  }

  smal_debug(1, "() = %p", (void*) self);

  // fprintf(stderr, "B");

  return self;
}

static
int smal_buffer_set_object_size(smal_buffer *self, size_t object_size)
{
  int result = 0;

  smal_debug(1, "%p: (%d)", self, (int) object_size);

  self->object_size = object_size;
  /* handle hardcoded object_size. */
  self->object_size = smal_buffer_object_size(self);

  /* Default alignment to sizeof(double) */
  if ( ! self->object_alignment )
    self->object_alignment = sizeof(double);

  /* handle hardcoded object_alignment. */
  self->object_alignment = smal_buffer_object_alignment(self);

  /* Align begin_ptr; start alloc_ptr. */
  smal_ALIGN(self->begin_ptr, self->object_alignment);
  self->alloc_ptr = self->begin_ptr;

  /* Restrict end_ptr to mmap boundary. */
  self->end_ptr = self->mmap_addr + self->mmap_size;

  /* Compute allowable object_capacity. */
  self->object_capacity = (self->end_ptr - self->begin_ptr) / self->object_size;
  self->end_ptr = self->begin_ptr + (self->object_capacity * self->object_size);
  assert(self->end_ptr <= self->mmap_addr + self->mmap_size);

  /* Free bits cover entire mmap area: see smal_buffer_i(). */
  self->free_bits.size = 
    self->mark_bits.size = (self->mmap_size / self->object_size) + 1;
  assert(self->mark_bits.size >= self->object_capacity);

  if ( smal_bitmap_init(&self->mark_bits) < 0 ) {
    result = -1;
    goto done;
  }
  if ( smal_bitmap_init(&self->free_bits) < 0 ) {
    result = -1;
    goto done;
  }

  smal_LOCK_STATS(lock);
  assert(self->stats.avail_n == 0);
  smal_UPDATE_STATS(capacity_n, += self->object_capacity);
  smal_UPDATE_STATS(avail_n,    += self->object_capacity);
  smal_LOCK_STATS(unlock);

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

static
void smal_buffer_pause_allocations(smal_buffer *self)
{
  smal_thread_lock_lock(&self->alloc_disabled);
}

static
void smal_buffer_resume_allocations(smal_buffer *self)
{
  smal_thread_lock_unlock(&self->alloc_disabled);
}

static
void smal_buffer_stop_allocations(smal_buffer *self)
{
  smal_thread_mutex_lock(&self->type->alloc_buffer_mutex);
  if ( self->type->alloc_buffer == self )
    self->type->alloc_buffer = 0;
  smal_thread_mutex_unlock(&self->type->alloc_buffer_mutex);
}


static 
void smal_buffer_free(smal_buffer *self)
{
  int result;
  void *addr = self->mmap_addr; 
  size_t size = self->mmap_size;

  smal_debug(1, "(%p)", self);

  // Remove self from buffer table.
  smal_buffer_table_remove(self);

  // Prevent smal_buffer_ptr_valid from being true.
  smal_thread_mutex_lock(&self->alloc_ptr_mutex);
  self->alloc_ptr = self->begin_ptr;
  smal_thread_mutex_unlock(&self->alloc_ptr_mutex);

  // Remove from type's alloc_buffer, if appropriate.
  smal_buffer_stop_allocations(self);
  
  // Remove from type's buffer list.
  smal_thread_rwlock_wrlock(&self->type->buffers_lock);
  smal_dllist_delete(&self->type_buffer_list);
  self->type_buffer_list.buffer = 0;
  smal_thread_rwlock_unlock(&self->type->buffers_lock);

  // Free bitmaps.
  smal_bitmap_free(&self->free_bits);
  smal_bitmap_free(&self->mark_bits);

  smal_LOCK_STATS(lock);

  smal_UPDATE_STATS(capacity_n, -= self->stats.capacity_n);
  smal_UPDATE_STATS(alloc_n,    -= self->stats.alloc_n);
  smal_UPDATE_STATS(avail_n,    -= self->stats.avail_n);
  smal_UPDATE_STATS(live_n,     -= self->stats.live_n);
  smal_UPDATE_STATS(free_n,     -= self->stats.free_n);
  smal_UPDATE_STATS(buffer_n,   -= 1);
  smal_UPDATE_STATS(mmap_size,  -= self->mmap_size);

  smal_LOCK_STATS(unlock);

  smal_thread_mutex_destroy(&self->stats._mutex);
  smal_thread_mutex_destroy(&self->alloc_ptr_mutex);
  smal_thread_rwlock_destroy(&self->mark_bits_lock);
  smal_thread_rwlock_destroy(&self->free_bits_lock);
  smal_thread_mutex_destroy(&self->free_list_mutex);

  smal_thread_lock_destroy(&self->alloc_disabled);

  // fprintf(stderr, "b");

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

#define smal_buffer_markQ(BUF, PTR)				\
  smal_bitmap_setQ(&(BUF)->mark_bits, smal_buffer_i(BUF, PTR))

#define smal_buffer_mark(BUF, PTR)				\
  smal_bitmap_set_c(&(BUF)->mark_bits, smal_buffer_i(BUF, PTR))

#define smal_buffer_freeQ(BUF, PTR)				\
  smal_bitmap_setQ(&(BUF)->free_bits, smal_buffer_i(BUF, PTR))

static inline
void _smal_buffer_mark_ptr(smal_buffer *buf, void *ptr)
{
  smal_thread_rwlock_rdlock(&buf->mark_bits_lock);
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
    smal_thread_rwlock_unlock(&buf->mark_bits_lock);
    smal_thread_rwlock_wrlock(&buf->mark_bits_lock);
    smal_buffer_mark(buf, ptr);
    smal_thread_rwlock_unlock(&buf->mark_bits_lock);
    // fprintf(stderr, "M");
    buf->type->mark_func(ptr);
    return;
  }
  smal_thread_rwlock_unlock(&buf->mark_bits_lock);
}

static inline
void _smal_mark_ptr(void *ptr)
{
  smal_buffer *buf;
  if ( (buf = smal_ptr_to_buffer(ptr)) ) {
    // smal_debug(5, "ptr %p => buf %p", ptr, buf);
    if ( smal_buffer_ptr_is_validQ(buf, ptr) ) {
      _smal_buffer_mark_ptr(buf, ptr);
    }
  }
}

void smal_mark_ptr(void *ptr)
{
  _smal_mark_ptr(ptr);
}


void smal_mark_ptr_range(void *ptr, void *ptr_end)
{
  // fprintf(stderr, "   smpr [%p, %p]\n", ptr, ptr_end);
  if ( ptr_end < ptr ) {
    void *tmp = ptr_end;
    ptr_end = ptr;
    ptr = tmp;
  }

  smal_ALIGN(ptr, __alignof__(void*));
  ptr_end -= sizeof(void*) - 1;

  // fprintf(stderr, "   smpr [%p, %p] ALIGNED\n", ptr, ptr_end);

  while ( ptr < ptr_end ) {
    void *p = *(void**) ptr;
    if ( p ) {
      // fprintf(stderr, "     smpr *%p = %p\n", ptr, p);
      _smal_mark_ptr(p);
    }
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
    _smal_buffer_mark_ptr(buf, ptr);
  }
}

int smal_object_reachableQ(void *ptr)
{
  smal_buffer *buf;
  if ( ! ptr )
    return 0;
  assert(in_collect);
  buf = smal_ptr_to_buffer(ptr);
  if ( smal_buffer_ptr_is_in_rangeQ(buf, ptr) )
    return ! ! smal_buffer_markQ(buf, ptr);
  else
    return 0;
}

void *smal_buffer_alloc_object(smal_buffer *self)
{
  void *ptr;
  int free_n = 0;
  int alloc_n = 0;

  if ( smal_thread_lock_test(&self->alloc_disabled) )
    return 0;
  
  smal_thread_mutex_lock(&self->free_list_mutex);
  if ( (ptr = self->free_list) ) {
    // fprintf(stderr, "  t@%p b@%p free_n %lu => @%p\n", smal_thread_self(), self, (unsigned long) self->stats.free_n, ptr);
    free_n = 1;

    self->free_list = * (void**) ptr;
    smal_thread_rwlock_wrlock(&self->free_bits_lock);
    smal_bitmap_clr_c(&self->free_bits, smal_buffer_i(self, ptr));
    smal_thread_rwlock_unlock(&self->free_bits_lock);

    smal_thread_mutex_unlock(&self->free_list_mutex);

  } else {
    smal_thread_mutex_unlock(&self->free_list_mutex);

    smal_thread_mutex_lock(&self->alloc_ptr_mutex);
    if ( self->alloc_ptr < self->end_ptr ) {
      alloc_n = 1;
      ptr = self->alloc_ptr;
      self->alloc_ptr += smal_buffer_object_size(self);
      assert(self->alloc_ptr <= self->end_ptr);
    } else {
      ptr = 0; /* buffer is full. */
    }
    smal_thread_mutex_unlock(&self->alloc_ptr_mutex);
  }

  smal_LOCK_STATS(lock);
  if ( ptr ) {
    smal_UPDATE_STATS(alloc_id, += 1);
    smal_UPDATE_STATS(live_n, += 1);
    if ( free_n ) {
      assert(buffer_head.stats.free_n > 0);
      smal_UPDATE_STATS(free_n, -= 1);
    }
    else if ( alloc_n ) {
      smal_UPDATE_STATS(alloc_n, += 1);
    }
    assert(self->stats.avail_n > 0);
    assert(self->type->stats.avail_n > 0);
    assert(buffer_head.stats.avail_n > 0);
    smal_UPDATE_STATS(avail_n, -= 1);

    smal_thread_rwlock_wrlock(&self->mark_bits_lock);
    smal_buffer_mark(self, ptr);
    smal_thread_rwlock_unlock(&self->mark_bits_lock);
  } else {
    assert(self->stats.avail_n == 0);
  }
  smal_LOCK_STATS(unlock);

  smal_debug(4, "(%p) = %p", self, ptr);
  smal_debug(4, "  alloc_ptr = %p, stats.alloc_n = %d", self->alloc_ptr, self->stats.alloc_n);
  smal_debug(4, "  stats.free_n = %d, stats.avail_n = %d, stats.live_n = %d",
	     self->stats.free_n, self->stats.avail_n, self->stats.live_n);

  return ptr;
}


static
void smal_buffer_free_object(smal_buffer *self, void *ptr)
{
  // assume free_bits and free_list mutexs are locked.
  // smal_thread_mutex_lock(&self->free_bits_mutex);
  smal_bitmap_set_c(&self->free_bits, smal_buffer_i(self, ptr));
  // smal_thread_mutex_unlock(&self->free_bits_mutex);

  // Should we unlock free_bits and free_list?
  self->type->free_func(ptr);

  // smal_thread_mutex_lock(&self->free_list_mutex);
  * ((void**) ptr) = self->free_list;
  self->free_list = ptr;
  // smal_thread_mutex_unlock(&self->free_list_mutex);

  smal_LOCK_STATS(lock);
  smal_UPDATE_STATS(free_n, += 1);
  assert(buffer_head.stats.free_n);
  smal_UPDATE_STATS(free_id, += 1);
  smal_UPDATE_STATS(avail_n, += 1);
  smal_LOCK_STATS(unlock);

  smal_debug(4, "%p: (%p)", self, ptr);
  smal_debug(4, "  alloc_ptr = %p, stats.alloc_n = %d", self->alloc_ptr, self->stats.alloc_n);
  smal_debug(4, "  stats.free_n = %d, stats.avail_n = %d, stats.live_n = %d",
	     self->stats.free_n, self->stats.avail_n, self->stats.live_n);
}

static
void smal_buffer_before_mark(smal_buffer *self)
{
  /* Pause allocations from this buffer. */
  smal_buffer_pause_allocations(self);

  // Remove from type's alloc_buffer, if appropriate.
  smal_buffer_stop_allocations(self);

  /* Clear mark bits. */
  smal_thread_rwlock_wrlock(&self->mark_bits_lock);
  smal_bitmap_clr_all(&self->mark_bits);
  smal_thread_rwlock_unlock(&self->mark_bits_lock);

  /* Prepare to re-compute live_n. */
  smal_thread_mutex_lock(&self->stats._mutex);
  self->stats.live_before_sweep_n = self->stats.live_n;
  smal_thread_mutex_unlock(&self->stats._mutex);
}

static
void smal_buffer_sweep(smal_buffer *self)
{
  /* Assume every alloc after now is an explicit allocation. */
  void *alloc_ptr = smal_buffer_alloc_ptr(self);
  size_t live_n = 0;
#if 0
  void **free_ptrs = malloc(sizeof(free_ptrs[0]) * self->object_capacity);
  void **free_ptrs_p = free_ptrs;
#endif

  // fprintf(stderr, "  s_b_s(b@%p)\n", self);

  smal_thread_rwlock_wrlock(&self->mark_bits_lock);
  smal_thread_rwlock_wrlock(&self->free_bits_lock);
  smal_thread_mutex_lock(&self->free_list_mutex);

  smal_debug(3, "(%p)", self);
  smal_debug(4, "  mark_bits.set_n = %d", self->mark_bits.set_n);

  {
    void *ptr;
    for ( ptr = self->begin_ptr; ptr < alloc_ptr; ptr += smal_buffer_object_size(self) ) {
      if ( smal_buffer_markQ(self, ptr) ) {
	++ live_n;
	// fprintf(stderr, "+");
      } else {
	if ( ! smal_buffer_freeQ(self, ptr) ) {
	  // fprintf(stderr, "-");
	  // *(free_ptrs_p ++) = ptr;
	  smal_buffer_free_object(self, ptr);
	}
      }
    }
  }

  // free(free_ptrs);

  smal_thread_mutex_unlock(&self->free_list_mutex);
  smal_thread_rwlock_unlock(&self->free_bits_lock);
  smal_thread_rwlock_unlock(&self->mark_bits_lock);

  smal_debug(4, "  live_n = %d, stats.free_n = %d",
	     live_n, self->stats.free_n);
  // assert(self->mark_bits.set_n == self->stats.live_n);

  smal_LOCK_STATS(lock);
  smal_UPDATE_STATS(live_n, -= self->stats.live_before_sweep_n);
  smal_UPDATE_STATS(live_n, += live_n);
  smal_LOCK_STATS(unlock);
   
  /* Does buffer have live objects? */
  if ( live_n ) {
    /* Buffer can possibly be allocated from. */
    smal_buffer_resume_allocations(self);
  } else {
    /* No additional objects should have been allocated. */
    assert(self->alloc_ptr == alloc_ptr);
    
    smal_buffer_free(self);
  }
}

void _smal_collect_inner()
{
  smal_buffer *buf;

  smal_debug(1, "()");

  if ( no_collect || in_collect ) return;

  smal_thread_lock_begin(&_smal_collect_inner_lock); {
 
  smal_collect_before_mark();

  ++ in_collect;

  /* Wait until allocators are done, then:
     Pause allocators while current buffers are being paused and moved
     elsewhere.
  */
  smal_thread_rwlock_wrlock(&alloc_lock);

  /* Prevent invalidating buffer_id min, max. */
  smal_thread_lock_lock(&buffer_id_min_max_keep);

  smal_thread_rwlock_wrlock(&buffer_head_lock);
  smal_thread_rwlock_wrlock(&buffer_collecting_lock);

  /* Move all active buffers to buffer_collecting. */
  smal_dllist_append(&buffer_collecting, &buffer_head);
  // smal_buffer_print_all(0, "buffer_head -> buffer_collecting");
  // assert(buffer_head.next == (void *) &buffer_head);
  // assert(buffer_head.prev == (void *) &buffer_head);

  /* Pause collecting for each buffer, prepare for marking. */
  smal_dllist_each(&buffer_collecting, buf); {
    smal_buffer_before_mark(buf);
  } smal_dllist_each_end();

  smal_thread_rwlock_unlock(&buffer_collecting_lock);
  smal_thread_rwlock_unlock(&buffer_head_lock);

  /* Allocation can resume in other threads, using new blocks. */
  smal_thread_rwlock_unlock(&alloc_lock);

  // Mark roots.
  ++ in_mark;
  smal_collect_mark_roots();
  -- in_mark;

  smal_collect_after_mark();

  /* Begin sweep. */
  smal_collect_before_sweep();

  smal_thread_rwlock_wrlock(&buffer_collecting_lock);
  ++ in_sweep;

  smal_dllist_each(&buffer_collecting, buf); {
    smal_buffer_sweep(buf);
  } smal_dllist_each_end();

  /* Move all remaining buffers back to active buffers. */
  smal_thread_rwlock_wrlock(&buffer_head_lock);
  smal_dllist_append(&buffer_head, &buffer_collecting);
  // smal_buffer_print_all(0, "buffer_head <- buffer_collecting");
  smal_thread_rwlock_unlock(&buffer_head_lock);

  -- in_sweep;
  smal_thread_rwlock_unlock(&buffer_collecting_lock);

  /* Allow invalidation of buffer_id min, max. */
  smal_thread_lock_unlock(&buffer_id_min_max_keep);

  -- in_collect;

  smal_collect_after_sweep();

  smal_debug(1, "  stats.alloc_n = %d, stats.live_n = %d, stats.avail_n = %d, stats.free_n = %d",
	     buffer_head.stats.alloc_n,
	     buffer_head.stats.live_n,
	     buffer_head.stats.avail_n,
	     buffer_head.stats.free_n
	    );

  } smal_thread_lock_end(&_smal_collect_inner_lock);
}

/**********************************************/


smal_type *smal_type_for(size_t object_size, smal_mark_func mark_func, smal_free_func free_func)
{
  smal_type *self;
  
  if ( ! initialized ) initialize();

  /* must be big enough for free list next pointer. */
  if ( object_size < sizeof(void*) )
    object_size = sizeof(void*);
  /* Align size to at least sizeof(double) */
  smal_ALIGN(object_size, sizeof(double));

  if ( ! mark_func )
    mark_func = null_func;
  if ( ! free_func )
    free_func = null_func;

  smal_thread_mutex_lock(&type_head_mutex);
  smal_dllist_each(&type_head, self); {
    if ( self->object_size == object_size && 
	 self->mark_func == mark_func &&
	 self->free_func == free_func ) {
      smal_thread_mutex_unlock(&type_head_mutex);
      return self;
    }
  } smal_dllist_each_end();
  smal_thread_mutex_unlock(&type_head_mutex);

  self = malloc(sizeof(*self));
  memset(self, 0, sizeof(*self));
  self->type_id = ++ type_head.type_id;
  self->object_size = object_size;
  self->mark_func = mark_func;
  self->free_func = free_func;
  smal_dllist_init(&self->buffers);

  smal_thread_mutex_init(&self->stats._mutex);
  smal_thread_rwlock_init(&self->buffers_lock);
  smal_thread_mutex_init(&self->alloc_buffer_mutex);
  
  smal_thread_mutex_lock(&type_head_mutex);
  smal_dllist_init(self);
  smal_dllist_insert(&type_head, self);
  smal_thread_mutex_unlock(&type_head_mutex);

  return self;
}

void smal_type_free(smal_type *self)
{
  smal_thread_mutex_lock(&type_head_mutex);
  smal_dllist_delete(self);
  smal_thread_mutex_unlock(&type_head_mutex);

  {
    smal_buffer_list *buf_list;
    
    smal_thread_rwlock_wrlock(&self->buffers_lock);
    smal_dllist_each(&self->buffers, buf_list); {
      smal_buffer *buf = buf_list->buffer;
      assert(buf && buf->type == self);
      smal_buffer_free(buf);
    } smal_dllist_each_end();
    smal_thread_rwlock_unlock(&self->buffers_lock);
  }

  smal_thread_mutex_destroy(&self->alloc_buffer_mutex);
  smal_thread_rwlock_destroy(&self->buffers_lock);
  smal_thread_mutex_destroy(&self->stats._mutex);
  
  free(self);
}

static
smal_buffer *smal_type_find_alloc_buffer(smal_type *self)
{
  smal_buffer *least_avail_buf = 0;
  smal_buffer_list *buf_list;

  smal_thread_rwlock_rdlock(&self->buffers_lock);
  smal_dllist_each(&self->buffers, buf_list); {
    smal_buffer *buf = buf_list->buffer;
    // fprintf(stderr, "  type %p buf %p TRY\n", self, buf);
    assert(buf && buf->type == self);
    smal_thread_mutex_lock(&buf->stats._mutex);
    if ( buf->stats.avail_n && ! smal_thread_lock_test(&buf->alloc_disabled) ) {
      // fprintf(stderr, "  type %p buf %p avail_n %lu\n", self, buf, buf->stats.avail_n);
      if ( ! least_avail_buf )
	least_avail_buf = buf;
      else { 
	if ( smal_WITH_MUTEX(&least_avail_buf->stats._mutex, int, least_avail_buf->stats.avail_n > buf->stats.avail_n) ) {
	  least_avail_buf = buf;
	}
      }
    }
    smal_thread_mutex_unlock(&buf->stats._mutex);
  } smal_dllist_each_end();
  smal_thread_rwlock_unlock(&self->buffers_lock);

  // fprintf(stderr, "  type %p buf %p avail_n %lu <==== \n", self, least_avail_buf, least_avail_buf ? least_avail_buf->stats.avail_n : 0);

  return least_avail_buf;
}

static
smal_buffer *smal_type_alloc_buffer(smal_type *self)
{
  smal_buffer *buf;

  // Assume alloc_buffer_mutex is locked.
  if ( (buf = self->alloc_buffer) ) {
    assert(buf->type == self);
  } else {
    if ( ! (buf = self->alloc_buffer = smal_type_find_alloc_buffer(self)) ) {
      buf = self->alloc_buffer = smal_buffer_alloc(self);
      /* If 0, out-of-memory */
      // fprintf(stderr, "  type %p buf %p NEW\n", self, buf);
    }
  }

  return buf;
}

void *smal_alloc(smal_type *self)
{
  void *ptr = 0;
  smal_buffer *alloc_buffer;

  /* Allow multiple allocators (readers). */
  smal_thread_rwlock_rdlock(&alloc_lock);

  smal_thread_mutex_lock(&self->alloc_buffer_mutex);

  /* Validate or allocate a smal_buffer. */
  if ( ! (alloc_buffer = smal_type_alloc_buffer(self)) )
    goto done;

  /* If current smal_buffer cannot provide. */
  if ( ! (ptr = smal_buffer_alloc_object(alloc_buffer)) ) {
    // fprintf(stderr, "  type %p buf %p EMPTY\n", self, self->alloc_buffer);

    /* Allocate a new smal_buffer. */
    self->alloc_buffer = 0;
    
    /* If cannot allocate new smal_buffer, out-of-memory. */
    if ( ! (alloc_buffer = smal_type_alloc_buffer(self)) )
      goto done;

    /* Allocate object from smal_buffer. */
    ptr = smal_buffer_alloc_object(alloc_buffer);
  }

 done:

  smal_thread_mutex_unlock(&self->alloc_buffer_mutex);
  smal_thread_rwlock_unlock(&alloc_lock);

  return ptr;
}

void smal_free(void *ptr)
{
  int error = 1;
  smal_buffer *buf;

  smal_debug(4, "() %p", ptr);

  if ( (buf = smal_ptr_to_buffer(ptr)) ) {
    smal_debug(5, "ptr %p => buf %p", ptr, buf);
    if ( smal_buffer_ptr_is_validQ(buf, ptr) ) {
      assert(buf->buffer_id == smal_buffer_buffer_id(buf));
      smal_debug(6, "ptr %p is valid in buf %p", ptr, buf);
      smal_thread_mutex_lock(&buf->free_list_mutex);
      smal_thread_rwlock_wrlock(&buf->free_bits_lock);
      smal_buffer_free_object(buf, ptr);
      smal_thread_rwlock_unlock(&buf->free_bits_lock);
      smal_thread_mutex_unlock(&buf->free_list_mutex);
      error = 0;
    }
  }

  if ( error ) abort();
}

/********************************************************************/

void smal_each_object(void (*func)(smal_type *type, void *ptr, void *arg), void *arg)
{
  smal_buffer *buf;
  // int stop = 0;

  if ( in_collect ) abort();
  if ( ! initialized ) initialize();

  // ++ no_collect;

  // fprintf(stderr, "  s_e_o: \n");
  smal_thread_rwlock_rdlock(&alloc_lock);

  smal_thread_rwlock_rdlock(&buffer_head_lock);
  smal_dllist_each(&buffer_head, buf); {
    void *ptr, *alloc_ptr = smal_buffer_alloc_ptr(buf);
    // fprintf(stderr, "    s_e_o bh b@%p\n", buf);
    smal_thread_rwlock_rdlock(&buf->free_bits_lock);
    for ( ptr = buf->begin_ptr; ptr < alloc_ptr; ptr += smal_buffer_object_size(buf) ) {
      if ( ! smal_buffer_freeQ(buf, ptr) ) {
	smal_thread_rwlock_unlock(&buf->free_bits_lock);
	func(buf->type, ptr, arg);
	smal_thread_rwlock_rdlock(&buf->free_bits_lock);
	// if ( result < 0 ) { stop = result; break }
      }
    }
    smal_thread_rwlock_unlock(&buf->free_bits_lock);
    // if ( stop ) break;
  } smal_dllist_each_end();
  smal_thread_rwlock_unlock(&buffer_head_lock);

  smal_thread_rwlock_rdlock(&buffer_collecting_lock);
  smal_dllist_each(&buffer_collecting, buf); {
    void *ptr, *alloc_ptr = smal_buffer_alloc_ptr(buf);
    // fprintf(stderr, "    s_e_o bc b@%p\n", buf);
    smal_thread_rwlock_rdlock(&buf->free_bits_lock);
    for ( ptr = buf->begin_ptr; ptr < alloc_ptr; ptr += smal_buffer_object_size(buf) ) {
      if ( ! smal_buffer_freeQ(buf, ptr) ) {
	smal_thread_rwlock_unlock(&buf->free_bits_lock);
	func(buf->type, ptr, arg);
	smal_thread_rwlock_rdlock(&buf->free_bits_lock);
	// if ( result < 0 ) { stop = result; break }
      }
    }
    smal_thread_rwlock_unlock(&buf->free_bits_lock);
    // if ( stop ) break;
  } smal_dllist_each_end();
  smal_thread_rwlock_unlock(&buffer_collecting_lock);

  smal_thread_rwlock_unlock(&alloc_lock);

  // -- no_collect;
  // return stop;
}

void smal_global_stats(smal_stats *stats)
{
  smal_thread_mutex_lock(&buffer_head.stats._mutex);
  *stats = buffer_head.stats;
  smal_thread_mutex_unlock(&buffer_head.stats._mutex);
}

void smal_type_stats(smal_type *type, smal_stats *stats)
{
  smal_thread_mutex_lock(&type->stats._mutex);
  *stats = type->stats;
  smal_thread_mutex_lock(&type->stats._mutex);
}

/********************************************************************/

static smal_thread_once _initalized = smal_thread_once_INIT;
static void _initialize()
{
  {
    const char *s;
    if ( (s = getenv("SMAL_DEBUG_LEVEL")) ) {
      smal_debug_level = atoi(s);
      if ( smal_debug_level > 0 && ! SMAL_DEBUG ) {
	fprintf(stderr, "SMAL: SMAL_DEBUG not compiled in\n");
      }
    }
  }

  if ( smal_debug_level >= 1 ) {
    fprintf(stderr, "\n %s:%d %s()\n", __FILE__, __LINE__, __FUNCTION__);
  }

  memset(&buffer_head, 0, sizeof(buffer_head));
  memset(&buffer_collecting, 0, sizeof(buffer_collecting));
  memset(&type_head, 0, sizeof(type_head));

  smal_thread_mutex_init(&_smal_debug_mutex);
  smal_thread_rwlock_init(&alloc_lock);
  smal_thread_mutex_init(&type_head_mutex);
  smal_thread_rwlock_init(&buffer_head_lock);
  smal_thread_mutex_init(&buffer_head.stats._mutex);
  smal_thread_rwlock_init(&buffer_collecting_lock);
  smal_thread_mutex_init(&buffer_id_min_max_mutex);
  smal_thread_rwlock_init(&buffer_table_lock);

  smal_thread_lock_init(&buffer_id_min_max_keep);
  smal_thread_lock_init(&_smal_collect_inner_lock);

  smal_dllist_init(&buffer_head);
  smal_dllist_init(&buffer_collecting);
  smal_dllist_init(&type_head);

  buffer_id_min = 0; buffer_id_max = 0;
  buffer_table_size = 1;
  buffer_table =   malloc(sizeof(buffer_table[0]) * buffer_table_size);
  memset(buffer_table, 0, sizeof(buffer_table[0]) * buffer_table_size);

  initialized = 1;

  if ( smal_debug_level >= 1 ) {
    fprintf(stderr, "\n %s:%d %s(): DONE\n", __FILE__, __LINE__, __FUNCTION__);
  }
}
static
void initialize()
{
  smal_thread_do_once(&_initalized, _initialize);
}

void smal_init()
{
  initialize();
}

void smal_shutdown()
{
  smal_buffer *buf;
  smal_type *type;

  if ( ! initialized ) return;
  if ( in_collect ) abort();

  ++ no_collect;

  smal_dllist_each(&buffer_head, buf); {
    smal_buffer_free(buf);
  } smal_dllist_each_end();

  smal_dllist_each(&type_head, type); {
    smal_type_free(type);
  } smal_dllist_each_end();

  free(buffer_table);
  buffer_table = 0;

  {
    static smal_thread_once zero = smal_thread_once_INIT;
    _initalized = zero;
  }
  initialized = 0;

  -- no_collect;
}


