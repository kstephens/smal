/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include <stdlib.h> /* malloc(), free() */
#include <unistd.h> /* getpid() */
#include <string.h> /* memset() */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h> /* open() */
#include <sys/mman.h> /* mmap(), munmap() */
#include <stdio.h> /* perror() */
#include <sys/errno.h>
#include <stdarg.h>
#ifdef SMAL_PROF
#define NASSERT 1
#define malloc(x) ({ size_t size = (x); void *ptr = malloc(size); fprintf(stderr, "  SMAL_PROF: %s:%-4d: malloc(%lu) = %p\n", __FILE__, __LINE__, (unsigned long) size, ptr); ptr; })
#define free(x) fprintf(stderr, "  SMAL_PROF: %s:%-4d: free(%p) ignored\n", __FILE__, __LINE__, (x))
#endif
#include "smal/smal.h"
#include "smal/internal.h"
#include "smal/dllist.h"
#include "smal/thread.h"
#include "smal/assert.h"


static int initialized;
static void initialize();

#ifdef smal_page_size
size_t _smal_page_size = smal_page_size;
#else
size_t smal_page_size = smal_page_size_default;
#endif
#ifdef smal_page_mask
size_t _smal_page_mask = smal_page_mask;
#else
size_t smal_page_mask = smal_page_size_default - 1;
#endif

/*********************************************************************
 * Debugging support.
 */

const char *smal_stats_names[] = {
  "alloc_id",
  "free_id",
  "buffer_id",
  "capacity_n",
  "alloc_n",
  "avail_n",
  "live_n",
  "live_before_sweep_n",
  "free_n",
  "buffer_n",
  "collection_n",
  "mmap_size",
  "mmap_total",
  "buffer_mutations",
  0
};

int smal_debug_level = 0;
static smal_thread_mutex _smal_debug_mutex;
static int debug_level[smal_debug_END];

#ifndef SMAL_DEBUG
#define SMAL_DEBUG 0
#endif

void smal_debug_set_level(smal_debug_t dt, int value)
{
  assert(smal_debug_BEGIN < dt && dt < smal_debug_END);
#if ! SMAL_DEBUG
  {
    static int once = 0;
    if ( ! once && value > 0 ) {
      once = 1;
      fprintf(stderr, "\nsmal: warning SMAL_DEBUG not enabled at compile time\n");
    }
  }
#endif
  if ( dt == smal_debug_all ) {
    for ( dt = smal_debug_BEGIN + 1; dt < smal_debug_END; ++ dt )
      debug_level[dt] = value;
  } else {
    debug_level[dt] = value;
  }
}

void smal_debug_print_smaps()
{
#ifdef __linux__
  int result;
  char cmd[1024];
  sprintf(cmd, "grep -B 1 Size: /proc/%d/smaps", (int) getpid());
  result = system(cmd);
  (void) result;
#endif
}

#define smal_debug_ENABLED(area, level) \
  (SMAL_DEBUG && debug_level[smal_debug_##area] >= level)

#if SMAL_DEBUG
static
void _smal_debug(smal_debug_t dtype, const char *func, const char *format, ...)
{
  va_list vap;
  smal_thread_mutex_lock(&_smal_debug_mutex);
  fprintf(stderr, "\rsmal: T@%p %s ", smal_thread_self(), func);
  va_start(vap, format);
  vfprintf(stderr, format, vap);
  va_end(vap);
  fprintf(stderr, "\n");
  fflush(stderr);
  smal_thread_mutex_unlock(&_smal_debug_mutex);
}

#define smal_debug(area, level, msg, args...)				\
  do {									\
    if ( smal_debug_ENABLED(area, level) )				\
      _smal_debug(smal_debug_##area, __FUNCTION__, msg, ##args);	\
  } while(0)
#else
#define smal_debug(area, level, msg, args...) ((void) 0)
#endif

/*********************************************************************
 * bitmaps
 */

#define smal_BITS_PER_WORD (sizeof(unsigned int) * 8)

#ifndef smal_bitmap_COUNTS
#define smal_bitmap_COUNTS 0
#endif

static
void smal_bitmap_clr_all(smal_bitmap *self)
{
  assert(self->bits);
  memset(self->bits, 0, self->bits_size);
#if smal_bitmap_COUNTS
  self->set_n = 0;
  self->clr_n = self->size;
#endif
}

#if 0
static
void smal_bitmap_set_all(smal_bitmap *self)
{
  memset(self->bits, ~0, self->bits_size);
#if smal_bitmap_COUNTS
  self->clr_n = 0;
  self->set_n = self->size;
#endif
}
#endif

static
int smal_bitmap_init(smal_bitmap *self)
{
  self->bits_size = sizeof(self->bits[0]) * ((self->size / smal_BITS_PER_WORD) + 1);
#if smal_bitmap_COUNTS
  self->set_n = self->clr_n = 0;
#endif
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
  do {				 \
    smal_bitmap_set(bm, i);	 \
    if ( smal_bitmap_COUNTS ) {	 \
      (bm)->set_n ++;		 \
      (bm)->clr_n --;		 \
    }				 \
  } while ( 0 )
#define smal_bitmap_clr(bm, i) (smal_bitmap_w(bm, i) &= ~ smal_bitmap_b(bm, i))
#define smal_bitmap_clr_c(bm, i)		\
  do {						\
    smal_bitmap_clr(bm, i);			\
    if ( smal_bitmap_COUNTS ) {			\
      (bm)->set_n --;				\
      (bm)->clr_n ++;				\
    }						\
  } while ( 0 )

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

static size_t page_id_min, page_id_max;
static int page_id_min_max_valid;

static smal_buffer *buffer_table_init[] = { 0 };
static smal_buffer **buffer_table = buffer_table_init;
static size_t buffer_table_size = 1;
static smal_thread_rwlock buffer_table_lock;

static smal_thread_lock _smal_collect_inner_lock;

static size_t collect_id; /** may wrap. */
static int in_collect; /** protected by alloc_lock */
static int in_mark;
static int in_sweep;
static int no_collect;

static
void null_free_func(void *ptr)
{
  /* NOTHING */
}

static
void* null_mark_func(void *ptr)
{
  /* NOTHING */
  return 0;
}

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

/*********************************************************************
 * mmap(), munmap()
 */

static inline
void *smal_mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset)
{
  void *result;
#ifndef SMAL_USE_DEV_ZERO
#define SMAL_USE_DEV_ZERO 0
#endif
#if SMAL_USE_DEV_ZERO
  if ( addr == 0 && (flags & MAP_ANON) && fd == -1 ) {
    static int dev_zero = -1;
    if ( dev_zero == -1 ) {
      dev_zero = open("/dev/zero", O_RDWR);
      assert(dev_zero != -1);
    }
    fd = dev_zero;
    flags &= ~MAP_ANON;
  }
#endif
  result = mmap(addr, length, prot, flags, fd, offset);
  smal_debug(mmap, 3, " mmap(@%p, 0x%lx, 0%o, %d, 0x%lx) = @%p (%s)", 
	     addr, (unsigned long) length, 
	     flags, fd, offset, result, strerror(errno));
  // smal_debug_print_smaps();
  return result;
}

static inline
int smal_munmap(void *addr, size_t length)
{
  int result;
  result = munmap(addr, length);
  smal_debug(mmap, 2, " munmap(@%p, 0x%lx) = %d (%s)", (void*) addr, (unsigned long) length, (int) result, strerror(errno));
  // smal_debug_print_smaps();
  assert(result == 0);
  return result;
}

/*********************************************************************
 * subsystems
 */

static inline
void* _smal_mark_ptr(void *, void*);

static inline
void _smal_mark_ptr_tail(void *, void*);

#if SMAL_WRITE_BARRIER
#include "write_barrier.h"
#endif

#if SMAL_MARK_QUEUE
#include "mark_queue.h"
#define smal_after_mark_func() smal_mark_queue_mark(0)
#else
#define smal_after_mark_func() ((void) 0)
#endif

#if SMAL_REMEMBERED_SET
#include "remembered_set.h"
#endif

/************************************************************************
 ** Map any address to smal_buffer*.
 */

#define smal_ptr_to_buffer(PTR)						\
  smal_WITH_RDLOCK(&buffer_table_lock, smal_buffer*, buffer_table[smal_addr_page_id(PTR) % buffer_table_size])

#define smal_buffer_i(BUF, PTR) \
  (((void*)(PTR) - smal_buffer_to_page(BUF)) / smal_buffer_object_size(BUF))

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

static inline
void smal_buffer_table_add(smal_buffer *self)
{
  size_t i;
  size_t buffer_table_size_new;
  smal_buffer **buffer_table_new;
  
  // fprintf(stderr, "smal_buffer_table_add(%p)\n", self);

  smal_thread_rwlock_wrlock(&buffer_table_lock);

  /* Recompute page_id_min, page_id_max */
  if ( page_id_min_max_valid ) {
    /* page_id_min and page_id_max needs updating. */
    if ( page_id_min > smal_buffer_page_id(self) )
      page_id_min = smal_buffer_page_id(self);
    else if ( page_id_max < smal_buffer_page_id(self) )
      page_id_max = smal_buffer_page_id(self);
  } else {
    /* page_id_min and page_id_max is completely stale. */
    smal_buffer *buf;
    page_id_min = page_id_max = smal_buffer_page_id(self);
    assert(page_id_min != 0);
    assert(page_id_max != 0);
    smal_thread_rwlock_rdlock(&buffer_head_lock);
    smal_dllist_each(&buffer_head, buf); {
      if ( page_id_min > smal_buffer_page_id(buf) )
	page_id_min = smal_buffer_page_id(buf);
      else if ( page_id_max < smal_buffer_page_id(buf) )
	page_id_max = smal_buffer_page_id(buf);
    } smal_dllist_each_end();
    smal_thread_rwlock_unlock(&buffer_head_lock);
    page_id_min_max_valid = 1;
  }

  buffer_table_size_new = (page_id_max - page_id_min) + 1;

  buffer_table_new = malloc(sizeof(buffer_table_new[0]) * (buffer_table_size_new + 1));
  memset(buffer_table_new, 0, sizeof(buffer_table_new[0]) * (buffer_table_size_new + 1));

#if 0
  fprintf(stderr, "smal_buffer_table_add(%p): malloc([%lu]) = %p\n", self, (unsigned long) buffer_table_size_new, buffer_table_new);
  fprintf(stderr, "  %lu page_id %lu @ %p [%lu, %lu] %lu\n", (unsigned long) buffer_head.stats.buffer_n, (unsigned long) self->page_id, self, (unsigned long) page_id_min, (unsigned long) page_id_max, (unsigned long) buffer_table_size_new);
#endif

  if ( smal_likely(buffer_table) ) {
    for ( i = 0; i < buffer_table_size; ++ i ) {
      smal_buffer *x = buffer_table[i];
      if ( smal_likely(x) ) {
	size_t j = smal_buffer_page_id(x) % buffer_table_size_new;
	assert(! buffer_table_new[j]);
	buffer_table_new[j] = x;
      }
    }
    // fprintf(stderr, "smal_buffer_table_add(%p): free(%p)\n", self, buffer_table);
    free(buffer_table);
  }

  buffer_table = buffer_table_new;
  buffer_table_size = buffer_table_size_new;

  i = smal_buffer_page_id(self) % buffer_table_size;
  assert(! buffer_table[i]);
  buffer_table[i] = self;

  smal_thread_rwlock_unlock(&buffer_table_lock);

  smal_thread_rwlock_wrlock(&buffer_head_lock);
  smal_dllist_init(self);
  smal_dllist_insert(&buffer_head, self);
  // smal_buffer_print_all(self, "add");
  smal_thread_rwlock_unlock(&buffer_head_lock);
  
  smal_debug(all, 3, "buffer_table_size = %d", (int) buffer_table_size);
}

static inline
void smal_buffer_table_remove(smal_buffer *self)
{
  size_t i;

  smal_thread_rwlock_wrlock(&buffer_table_lock);

  i = smal_buffer_page_id(self) % buffer_table_size;
  assert(buffer_table[i] == self);
  buffer_table[i] = 0;

  /* Was buffer at beginning or end of buffer id space? */
  if ( smal_unlikely(page_id_min == self->page_id) || 
       smal_unlikely(page_id_max == self->page_id) ) {
    page_id_min_max_valid = 0;
  }

  smal_thread_rwlock_unlock(&buffer_table_lock);

  // smal_thread_rwlock_wrlock(&buffer_head_lock);
  smal_dllist_delete(self); /* Remove from global buffer list. */
  // smal_buffer_print_all(self, "remove");
  // smal_thread_rwlock_unlock(&buffer_head_lock);
}

static inline
int smal_buffer_set_object_size(smal_buffer *self, size_t object_size);

#ifdef SMAL_WRITE_BARRIER
void smal_buffer_write_unprotect(smal_buffer *);
#endif

static // inline
smal_buffer *smal_buffer_alloc(smal_type *type)
{
  smal_buffer *self;
  size_t mmap_size;
  void *mmap_addr;
  size_t offset;
  int result;
  int ok = 0;
  
  smal_debug(buffer, 1, "()");

  /* Attempt first alignment via exact size. */
  mmap_size = smal_page_size;
  mmap_addr = smal_mmap((void*) 0, mmap_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, (off_t) 0);
  if ( mmap_addr == MAP_FAILED ) {
    smal_debug(mmap, 3, "mmap failed: %s", strerror(errno));
    return 0;
  }

  /* Not aligned? */
  if ( (offset = smal_addr_page_offset(mmap_addr)) ) {
    void  *keep_addr = 0, *free_addr_1 = 0, *free_addr_2 = 0;
    size_t keep_size = 0,  free_size_1 = 0,  free_size_2 = 0;

    result = smal_munmap(mmap_addr, mmap_size);

    /* mmap() enough to ensure a buffer of smal_page_size, aligned to smal_page_size */
    /* Allocate twice the size needed. */
    mmap_size = smal_page_size * 2;
    mmap_addr = smal_mmap((void*) 0, mmap_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, (off_t) 0);
    if ( mmap_addr == MAP_FAILED ) {
      smal_debug(mmap, 3, "mmap failed: %s", strerror(errno));
      return 0;
    }
    
    if ( (offset = smal_addr_page_offset(mmap_addr)) ) {
      smal_debug(buffer, 4, "offset @%p = 0x%0lx", (void*) mmap_addr, (unsigned long) offset);
      free_addr_1 = mmap_addr;
      keep_addr = mmap_addr + (smal_page_size - offset);
      free_size_1 = keep_addr - free_addr_1;
      keep_size = smal_page_size;
      free_addr_2 = keep_addr + smal_page_size;
      free_size_2 = (mmap_addr + mmap_size) - free_addr_2; 
    } else {
      keep_addr = mmap_addr;
      keep_size = smal_page_size;
      free_addr_1 = mmap_addr + smal_page_size;
      free_size_1 = mmap_size - keep_size;
    }
    
    smal_debug(mmap, 3, "keeping @%p[0x%0lx]", (void*) keep_addr, (unsigned long) keep_size);
    
    assert(smal_addr_page_offset(keep_addr) == 0);
    assert(keep_addr >= mmap_addr);
    assert(keep_addr + keep_size <= mmap_addr + mmap_size);
    
    /* Return the unused, unaligned part before keep_addr. */
    if ( free_size_1 ) {
      smal_debug(mmap, 3, "freeing 1 @%p[0x%0lx]", (void*) free_addr_1, (unsigned long) free_size_1);
      assert(free_addr_1 >= mmap_addr);
      assert(free_addr_1 + free_size_1 <= mmap_addr + mmap_size);
      result = smal_munmap(free_addr_1, free_size_1);
      assert(result == 0);
    }

    /* Return the unused, unaligned part after keep_addr + keep_size. */
    if ( free_size_2 ) {
      smal_debug(mmap, 3, "freeing 2 @%p[0x%0lx]", (void*) free_addr_2, (unsigned long) free_size_2);
      assert(free_addr_2 >= mmap_addr);
      assert(free_addr_2 + free_size_2 <= mmap_addr + mmap_size);
      result = smal_munmap(free_addr_2, free_size_2);
      assert(result == 0);
    }

    mmap_addr = keep_addr;
    mmap_size = keep_size;
  }

#if SMAL_SEGREGATE_BUFFER_FROM_PAGE
  if ( ! (self = malloc(sizeof(*self))) )
    goto done;
  memset(self, 0, sizeof(*self));
#else
  self = mmap_addr; 
  /* Assume mmap() memory is zeroed. */
#endif

  self->type = type;
  self->page_id = smal_addr_page_id(mmap_addr);
  self->mmap_addr = mmap_addr;
  self->mmap_size = mmap_size;
#if SMAL_SEGREGATE_BUFFER_FROM_PAGE
  *(void**) mmap_addr = self;
  self->begin_ptr = mmap_addr + sizeof(void*);
#else
  self->begin_ptr = self + 1;
#endif
  self->type_buffer_list.buffer = self;

  smal_thread_mutex_init(&self->stats._mutex);
  smal_thread_mutex_init(&self->alloc_ptr_mutex);
  // smal_thread_rwlock_init(&self->mark_bits_lock);
  smal_thread_rwlock_init(&self->free_bits_lock);
  smal_thread_mutex_init(&self->free_list_mutex);

  smal_thread_lock_init(&self->alloc_disabled);

#if SMAL_WRITE_BARRIER
  smal_thread_rwlock_init(&self->write_protect_lock);
  smal_thread_rwlock_init(&self->mutation_lock);
#endif

  if ( smal_likely(smal_buffer_set_object_size(self, type->desc.object_size) >= 0) ) {
    smal_buffer_table_add(self);

    smal_thread_rwlock_wrlock(&type->buffers_lock);
    smal_dllist_init(&self->type_buffer_list);
    smal_dllist_insert(&type->buffers, &self->type_buffer_list);
    smal_thread_rwlock_unlock(&type->buffers_lock);

    smal_LOCK_STATS(lock);
    smal_UPDATE_STATS(buffer_id,  += 1); /* for global and type */
    self->buffer_id = self->stats.buffer_id = buffer_head.stats.buffer_id;
    smal_UPDATE_STATS(buffer_n,   += 1);
    smal_UPDATE_STATS(mmap_size,  += self->mmap_size);
    smal_UPDATE_STATS(mmap_total, += self->mmap_size);
    smal_LOCK_STATS(unlock);
  }

  ok = 1;
  goto done;

 done:
  if ( smal_unlikely(! ok) ) {
    smal_debug(buffer, 2, " smal_buffer_set_object_size failed: %s", strerror(errno));
    result = smal_munmap(mmap_addr, mmap_size);
    assert(result == 0);
#if SMAL_SEGREGATE_BUFFER_FROM_PAGE
    free(self);
#endif
    self = 0;
    mmap_addr = 0;
    mmap_size = 0;
  }

  smal_debug(buffer, 1, "() = b@%p", (void*) self);
  smal_debug(mmap, 2, " b@%p usable region [ @%p, @%p )", self, mmap_addr, mmap_addr + mmap_size);
  // smal_debug_print_smaps();

  return self;
}

static inline
int smal_buffer_set_object_size(smal_buffer *self, size_t object_size)
{
  int result = 0;

  smal_debug(buffer, 1, "@%p: (%d)", self, (int) object_size);

  self->object_size = object_size;
  /* handle hardcoded object_size. */
  self->object_size = smal_buffer_object_size(self);

  /* Default alignment to sizeof(double) */
  self->object_alignment = self->type->desc.object_alignment;
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

  if ( smal_unlikely(smal_bitmap_init(&self->mark_bits) < 0) ) {
    result = -1;
    goto done;
  }
  if ( smal_unlikely(smal_bitmap_init(&self->free_bits) < 0) ) {
    result = -1;
    goto done;
  }

#if SMAL_WRITE_BARRIER
  if ( self->type->desc.mostly_unchanging )
    self->mutation_write_barrier = 
      self->use_remembered_set = 1;
#endif

  smal_LOCK_STATS(lock);
  assert(self->stats.avail_n == 0);
  smal_UPDATE_STATS(capacity_n, += self->object_capacity);
  smal_UPDATE_STATS(avail_n,    += self->object_capacity);
  smal_LOCK_STATS(unlock);

 done:
  smal_debug(buffer, 2, "  object_size = %d, object_alignment = %d",
	    (int) self->object_size, (int) self->object_alignment);

  smal_debug(buffer, 2, "  begin_ptr = @%p, end_ptr = @%p, object_capacity = %lu, mark_bits.size = %lu", 
	    (void*) self->begin_ptr, (void*) self->end_ptr, 
	    (unsigned long) self->object_capacity,
	    (unsigned long) self->mark_bits.size);

  if ( smal_unlikely(result < 0) ) {
    smal_bitmap_free(&self->mark_bits);
    smal_bitmap_free(&self->free_bits);
  }

  return result;
}

static inline
void smal_buffer_pause_allocations(smal_buffer *self)
{
  smal_thread_lock_lock(&self->alloc_disabled);
}

static inline
void smal_buffer_resume_allocations(smal_buffer *self)
{
  smal_thread_lock_unlock(&self->alloc_disabled);
}

static inline
void smal_buffer_stop_allocations(smal_buffer *self)
{
  smal_thread_mutex_lock(&self->type->alloc_buffer_mutex);
  if ( self->type->alloc_buffer == self )
    self->type->alloc_buffer = 0;
  smal_thread_mutex_unlock(&self->type->alloc_buffer_mutex);
}

static inline
void smal_buffer_free(smal_buffer *self)
{
  int result;
  void *mmap_addr = self->mmap_addr; 
  size_t mmap_size = self->mmap_size;

  smal_debug(buffer, 1, "(@%p)", self);

  // Disable write barrier.
#if SMAL_WRITE_BARRIER
  smal_buffer_write_unprotect(self);
#endif

#if SMAL_REMEMBERED_SET
  smal_remembered_set_free(self->remembered_set);
#endif

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
  // smal_thread_rwlock_destroy(&self->mark_bits_lock);
  smal_thread_rwlock_destroy(&self->free_bits_lock);
  smal_thread_mutex_destroy(&self->free_list_mutex);

  smal_thread_lock_destroy(&self->alloc_disabled);

#if SMAL_WRITE_BARRIER
  smal_thread_rwlock_destroy(&self->write_protect_lock);
  smal_thread_rwlock_destroy(&self->mutation_lock);
#endif

  // fprintf(stderr, "b");

#if SMAL_SEGREGATE_BUFFER_FROM_PAGE
  free(self);
#endif

  result = smal_munmap(mmap_addr, mmap_size);
  assert(result == 0);
}

smal_buffer *smal_buffer_from_ptr(void *ptr)
{
  smal_buffer *buf = smal_ptr_to_buffer(ptr);
  return buf && smal_buffer_ptr_is_validQ(buf, ptr) ? buf : 0;
}

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
void * _smal_buffer_mark_ptr(smal_buffer *self, void *referrer, void *ptr)
{
  assert(in_collect); /* mark_bits should only be touched by smal_collect() and its thread. */
  // smal_thread_rwlock_rdlock(&self->mark_bits_lock);
  // assert(buf->page_id == smal_buffer_page_id(self));
#if 0
  smal_debug(mark, 6, "ptr @%p is valid in buf @%p", ptr, self);
  smal_debug(mark, 7, "smal_buffer_mark_ptr(@%p, @%p, @%p) = 0x%08x", self, referrer, ptr,
	     (unsigned int) smal_buffer_mark_word(self, ptr));
#endif

#if SMAL_REMEMBERED_SET
  {
    smal_buffer *referrer_buf;
    /* Record pointers from the referrer buffer to the ptr buffer? */
    if ( referrer && 
	 (referrer_buf = smal_ptr_to_buffer(referrer))->record_remembered_set &&
	 referrer_buf != self ) {
      smal_remembered_set_add(referrer_buf->remembered_set, referrer, ptr);
    }
  }
#endif

  if ( ! smal_buffer_markQ(self, ptr) ) {
#if 0
    smal_debug(mark, 5, "ptr @%p is unmarked", ptr);
#endif

    smal_buffer_mark(self, ptr);

    if ( smal_unlikely(! self->markable) )
      return 0;
#ifndef smal_MARK_FUNC
#define smal_MARK_FUNC(ptr) self->type->desc.mark_func(ptr)
#endif
    ptr = smal_MARK_FUNC(ptr);
    smal_after_mark_func();
    return ptr;
  }
  return 0;
}

static inline
void * _smal_mark_ptr(void *referrer, void *ptr)
{
  smal_buffer *buf;
  if ( (buf = smal_ptr_to_buffer(ptr)) ) {
    // smal_debug(mark, 5, "ptr @%p => buf @%p", ptr, buf);
    if ( smal_buffer_ptr_is_validQ(buf, ptr) ) {
      return _smal_buffer_mark_ptr(buf, referrer, ptr);
    }
  }
  return 0;
}

static inline
void _smal_mark_ptr_tail(void *referrer, void *ptr)
{
  do {
    void *new_referrer = ptr;
    ptr = _smal_mark_ptr(referrer, ptr);
    referrer = new_referrer;
  } while ( ptr );
}

/********************************************************************
 * Marking
 */

#if SMAL_MARK_QUEUE

void smal_mark_ptr(void *referrer, void *ptr)
{
  smal_mark_queue_add(referrer, 1, &ptr, 0);
}
void smal_mark_ptr_n(void *referrer, int n_ptrs, void **ptrs)
{
  smal_mark_queue_add(referrer, n_ptrs, ptrs, 0);
}
void smal_mark_bindings(int n, void ***bindings)
{
  smal_mark_queue_add(0, n, (void**) bindings, 1);
}

#else

#define smal_mark_queue_start() ((void) 0)
#define smal_mark_queue_mark_all() ((void) 0)
void smal_mark_ptr(void *referrer, void *ptr)
{
  _smal_mark_ptr_tail(referrer, ptr);
}
void smal_mark_ptr_n(void *referrer, int n_ptrs, void **ptrs)
{
  while ( -- n_ptrs >= 0 ) {
    _smal_mark_ptr_tail(referrer, *(ptrs ++));
}
void smal_mark_bindings(int n, void ***bindings)
{
  while ( -- n >= 0 ) {
    void *ptr = * *(bindings ++);
    _smal_mark_ptr_tail(0, ptr);
  }
}
#endif

static inline
void _smal_mark_ptr_range(void *referrer, void *ptr, void *ptr_end)
{
  // fprintf(stderr, "   smpr [@%p, @%p] ALIGNED\n", ptr, ptr_end);

  while ( ptr < ptr_end ) {
    void *p = *(void**) ptr;
    // fprintf(stderr, "     smpr *@%p = @%p\n", ptr, p);
    smal_mark_ptr(referrer, p);
    ptr += sizeof(int);
  }
}

void smal_mark_ptr_range(void *referrer, void *ptr, void *ptr_end)
{
  // fprintf(stderr, "   smpr [@%p, @%p]\n", ptr, ptr_end);
  if ( ptr_end < ptr ) {
    void *tmp = ptr_end;
    ptr_end = ptr;
    ptr = tmp;
  }

  smal_ALIGN(ptr, __alignof__(void*));
  ptr_end -= sizeof(void*) - 1;

  _smal_mark_ptr_range(referrer, ptr, ptr_end);
}

/* Can only be called during collection. */
int smal_object_reachableQ(void *ptr)
{
  smal_buffer *buf;
  // assert(in_collect);
  buf = smal_ptr_to_buffer(ptr);
  if ( smal_likely(smal_buffer_ptr_is_in_rangeQ(buf, ptr)) )
    return ! ! smal_buffer_markQ(buf, ptr);
  else
    return 0;
}

static // inline
void *smal_buffer_alloc_object(smal_buffer *self)
{
  void *ptr;
  int free_n = 0;
  int alloc_n = 0;

  if ( smal_unlikely(smal_thread_lock_test(&self->alloc_disabled)) )
    return 0;
  
  smal_thread_mutex_lock(&self->free_list_mutex);
  if ( smal_likely((ptr = self->free_list)) ) {
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
    if ( smal_likely(self->alloc_ptr < self->end_ptr) ) {
      alloc_n = 1;
      ptr = self->alloc_ptr;
      self->alloc_ptr += smal_buffer_object_size(self);
      assert(self->alloc_ptr <= self->end_ptr);
    } else {
      ptr = 0; /* buffer is full. */
    }
    smal_thread_mutex_unlock(&self->alloc_ptr_mutex);
  }

#if SMAL_WRITE_BARRIER
  /* Assume buffer is mutated because we are allocating from it. */
  if ( smal_likely(ptr) )
    smal_buffer_assume_mutation(self); 
#endif

  smal_LOCK_STATS(lock);
  if ( smal_likely(ptr) ) {
    smal_UPDATE_STATS(alloc_id, += 1);
    smal_UPDATE_STATS(live_n, += 1);
    smal_UPDATE_STATS(free_n, -= free_n);
    smal_UPDATE_STATS(alloc_n, += alloc_n);
#if 0
    assert(self->stats.avail_n > 0);
    assert(self->type->stats.avail_n > 0);
    assert(buffer_head.stats.avail_n > 0);
#endif
    smal_UPDATE_STATS(avail_n, -= 1);

    /* If during collection, assume smal_alloc() is a reentrant call or from another thread,
       protect it from sweep.
    */
    if ( in_collect ) {
      // smal_thread_rwlock_wrlock(&self->mark_bits_lock);
      smal_buffer_mark(self, ptr);
      // smal_thread_rwlock_unlock(&self->mark_bits_lock);
    }
  } else {
    assert(self->stats.avail_n == 0);
  }
  smal_LOCK_STATS(unlock);

  smal_debug(object_alloc, 2, "(b@%p) = @%p #%lu", self, ptr, (unsigned long) buffer_head.stats.alloc_id);
  smal_debug(object_alloc, 3, "  alloc_ptr = @%p, stats.alloc_n = %d", self->alloc_ptr, self->stats.alloc_n);
  smal_debug(object_alloc, 3, "  stats.free_n = %d, stats.avail_n = %d, stats.live_n = %d",
	     self->stats.free_n, self->stats.avail_n, self->stats.live_n);

  return ptr;
}


static
void smal_buffer_free_object(smal_buffer *self, void *ptr)
{
  // assume free_bits and free_list mutexs are locked.
  // smal_thread_mutex_lock(&self->free_bits_mutex);
  assert(! smal_bitmap_setQ(&self->free_bits, smal_buffer_i(self, ptr)));
  smal_bitmap_set_c(&self->free_bits, smal_buffer_i(self, ptr));
  // smal_thread_mutex_unlock(&self->free_bits_mutex);

  // Should we unlock free_bits and free_list?
  self->type->desc.free_func(ptr);

  // smal_thread_mutex_lock(&self->free_list_mutex);
  * ((void**) ptr) = self->free_list;
  self->free_list = ptr;
  // smal_thread_mutex_unlock(&self->free_list_mutex);

  smal_LOCK_STATS(lock);
  smal_UPDATE_STATS(free_n, += 1);
  assert(buffer_head.stats.free_n);
  smal_UPDATE_STATS(free_id, += 1);
#if 0
  if ( buffer_head.stats.free_id == 101 )
    abort();
#endif
  smal_UPDATE_STATS(avail_n, += 1);
  smal_LOCK_STATS(unlock);

  smal_debug(object_free, 2, "b@%p: (@%p) #%lu", self, ptr, (unsigned long) buffer_head.stats.free_id);
  smal_debug(object_free, 3, "  alloc_ptr = @%p, stats.alloc_n = %d", self->alloc_ptr, self->stats.alloc_n);
  smal_debug(object_free, 3, "  stats.free_n = %d, stats.avail_n = %d, stats.live_n = %d",
	     self->stats.free_n, self->stats.avail_n, self->stats.live_n);
}

static
void smal_buffer_before_mark(smal_buffer *self)
{
  /* Pause allocations from this buffer. */
  smal_buffer_pause_allocations(self);

  // Remove from type's alloc_buffer, if appropriate.
  smal_buffer_stop_allocations(self);

  /* Should this buffer be sweepable and markable this time? */
  self->markable = 
    self->sweepable = 
    ((++ self->stats.collection_n) % 
     self->type->desc.collections_per_sweep == 0
     );

  /* Prepare to re-compute live_n. */
  // smal_thread_mutex_lock(&self->stats._mutex); // Is this lock necessary? allocations have been paused. 
  self->stats.live_before_sweep_n = self->stats.live_n;
  // smal_thread_mutex_unlock(&self->stats._mutex);

  /* Prepare remembered_set. */
#if SMAL_REMEMBERED_SET
  if ( self->use_remembered_set ) {
    if ( ! self->remembered_set )
      self->remembered_set = smal_remembered_set_new(self);

    /* If buffer was mutated, its remembered set is no longer valid. */
    if ( self->mutation ) {
      self->remembered_set_valid = 0;
      // fprintf(stderr, "  @%p remembered_set_valid = 0\n", self);
    }

    /* If remembered_set is not valid,
       clear it, mark through this buffer, to capture its references to other buffers. */
    if ( ! self->remembered_set_valid )
      self->record_remembered_set = 
	self->markable = 1; 

    if ( self->record_remembered_set )
      smal_remembered_set_clear(self->remembered_set);
  }
#endif

  if ( smal_likely(self->markable) ) {
    /* Clear mark bits. */
    smal_bitmap_clr_all(&self->mark_bits);
  }

#if 0
  if ( self->sweepable )
    fprintf(stderr, "  @%p sweepable\n", self);
  if ( self->markable )
    fprintf(stderr, "  @%p markable\n", self);
#endif
}

static
void smal_buffer_sweep(smal_buffer *self)
{
  /* Assume every alloc after now is an errant allocation. */
  void *alloc_ptr = smal_buffer_alloc_ptr(self);
  size_t live_n = 0;
#if 0
  void **free_ptrs = malloc(sizeof(free_ptrs[0]) * self->object_capacity);
  void **free_ptrs_p = free_ptrs;
#endif

#if SMAL_REMEMBERED_SET
  /*
    If remembered_set was recorded, finish it, mark it valid and use it during future 
    mark phases.
  */
  if ( smal_unlikely(self->record_remembered_set) ) {
    self->record_remembered_set = 0;
    smal_remembered_set_finish(self->remembered_set);
    self->remembered_set_valid = 1;
    // fprintf(stderr, "  @%p remembered_set_valid = 1 (%d)\n", self, (int) self->remembered_set->n_ptrs);
    // self->sweepable = 0;
  }
#endif

  /* Sweep this time? */
  if ( smal_likely(self->sweepable) ) {

#if SMAL_WRITE_BARRIER
    /* Avoid spurious write barrier faults during sweep. */
    smal_buffer_write_unprotect(self); 
#endif

  // fprintf(stderr, "  s_b_s(b@%p)\n", self);

  // smal_thread_rwlock_wrlock(&self->mark_bits_lock);
  // smal_thread_rwlock_wrlock(&self->free_bits_lock);
  // smal_thread_mutex_lock(&self->free_list_mutex);

    smal_debug(sweep, 3, "(@%p)", self);
    smal_debug(sweep, 4, "  mark_bits.set_n = %d", self->mark_bits.set_n);

  {
    void *ptr;
    for ( ptr = self->begin_ptr; ptr < alloc_ptr; ptr += smal_buffer_object_size(self) ) {
      if ( smal_buffer_markQ(self, ptr) ) {
	++ live_n;
	// fprintf(stderr, "+");
      } else {
	if ( smal_likely(! smal_buffer_freeQ(self, ptr)) ) {
	  // fprintf(stderr, "-");
	  // *(free_ptrs_p ++) = ptr;
	  smal_buffer_free_object(self, ptr);
	}
      }
    }
  }

  // free(free_ptrs);

  //smal_thread_mutex_unlock(&self->free_list_mutex);
  //smal_thread_rwlock_unlock(&self->free_bits_lock);
  // smal_thread_rwlock_unlock(&self->mark_bits_lock);

  smal_debug(sweep, 4, "  live_n = %d, stats.free_n = %d",
	     live_n, self->stats.free_n);
  // assert(self->mark_bits.set_n == self->stats.live_n);

  smal_LOCK_STATS(lock);
  smal_UPDATE_STATS(live_n, -= self->stats.live_before_sweep_n);
  smal_UPDATE_STATS(live_n, += live_n);
  smal_LOCK_STATS(unlock);
   
  } else {
    live_n = self->stats.live_before_sweep_n;
  }

  /* No additional objects should have been allocated during sweep. */
  assert(self->alloc_ptr == alloc_ptr);
  
  /* Does buffer have live objects? */
  if ( smal_likely(live_n) ) {
    /* Buffer can possibly be allocated from. */
    smal_buffer_resume_allocations(self);

#if SMAL_WRITE_BARRIER
    /* Clear mutation bit and prepare write barrier. */
    smal_buffer_clear_mutation(self);
#endif
  } else {
    smal_buffer_free(self);
  }
}

static
void *_smal_collect_sweep_buffers(void *arg);

void _smal_collect_inner()
{
  smal_buffer *buf;

  smal_debug(collect, 1, "()");

  if ( no_collect || in_collect ) return;

  if ( smal_thread_lock_lock(&_smal_collect_inner_lock) ) {
 
  smal_collect_before_mark();

  /* Wait until allocators are done, then:
     Pause allocators while current buffers are being paused and moved
     elsewhere.
  */
  smal_thread_rwlock_wrlock(&alloc_lock);

  ++ in_collect;
  ++ collect_id;

  ++ buffer_head.stats.collection_n;

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
  smal_mark_queue_start();
  { 
    smal_thread *thr = smal_thread_self();
    smal_mark_ptr_range(0, &thr->registers, &thr->registers + 1);
  }
  smal_collect_mark_roots();

  smal_mark_queue_mark_all();

#if SMAL_REMEMBERED_SET
  smal_dllist_each(&buffer_collecting, buf); {
    if ( buf->remembered_set_valid )
      smal_remembered_set_mark(buf->remembered_set);
  } smal_dllist_each_end();
#endif

  smal_mark_queue_mark_all();

  smal_collect_after_mark();
  /* smal_collect_after_mark() (finalizers) may have queued marks. */
  smal_mark_queue_mark_all();
  -- in_mark;

  /* Begin sweep. */
  smal_collect_before_sweep();

#if 0
  smal_thread_spawn_or_inline(
			      _smal_collect_sweep_buffers, 
			      0);
#else
  _smal_collect_sweep_buffers(0);
#endif
  }
}

static
void *sweep_thread;

static
void *_smal_collect_sweep_buffers(void *arg)
{
  smal_buffer *buf;

  // fprintf(stderr, "_smal_collect_sweep_buffers()\n");
  sweep_thread = smal_thread_self(); // FAILS

  // fprintf(stderr, "_smal_collect_sweep_buffers(): buffer_collecting_lock ++\n");
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
  // fprintf(stderr, "_smal_collect_sweep_buffers(): buffer_collecting_lock --\n");

  -- in_collect;

  smal_collect_after_sweep();

  smal_debug(collect, 1, "  stats.alloc_n = %d, stats.live_n = %d, stats.avail_n = %d, stats.free_n = %d",
	     buffer_head.stats.alloc_n,
	     buffer_head.stats.live_n,
	     buffer_head.stats.avail_n,
	     buffer_head.stats.free_n
	    );

  smal_thread_lock_unlock(&_smal_collect_inner_lock);

  // fprintf(stderr, "_smal_collect_sweep_buffers(): DONE\n");
  {
    void *tmp = sweep_thread;
    sweep_thread = 0;
    smal_thread_died(tmp);
  }

  return 0;
}

void smal_collect_wait_for_sweep()
{
  smal_thread_join(sweep_thread);
}

/**********************************************/

smal_type *smal_type_for_desc(smal_type_descriptor *desc)
{
  smal_type *self;

  if ( smal_unlikely(! initialized) ) initialize();

  /* must be big enough for free list next pointer. */
  if ( desc->object_size < sizeof(void*) )
    desc->object_size = sizeof(void*);
  /* Align size to at least sizeof(double) */
  smal_ALIGN(desc->object_size, sizeof(double));

  if ( ! desc->object_alignment )
    desc->object_alignment = sizeof(double);

  if ( ! desc->mark_func )
    desc->mark_func = null_mark_func;
  if ( ! desc->free_func )
    desc->free_func = null_free_func;
  if ( ! desc->collections_per_sweep )
    desc->collections_per_sweep = 1; /* sweep on every collection. */

  smal_thread_mutex_lock(&type_head_mutex);
  smal_dllist_each(&type_head, self); {
    if ( ! memcmp(&self->desc, desc, sizeof(*desc)) ) {
      smal_thread_mutex_unlock(&type_head_mutex);
      return self;
    }
  } smal_dllist_each_end();

  self = malloc(sizeof(*self));
  memset(self, 0, sizeof(*self));
  self->type_id = ++ type_head.type_id;
  self->desc = *desc;
  smal_dllist_init(&self->buffers);

  smal_thread_mutex_init(&self->stats._mutex);
  smal_thread_rwlock_init(&self->buffers_lock);
  smal_thread_mutex_init(&self->alloc_buffer_mutex);
  
  smal_dllist_init(self);
  smal_dllist_insert(&type_head, self);
  smal_thread_mutex_unlock(&type_head_mutex);

  return self;
}

smal_type *smal_type_for(size_t object_size, smal_mark_func mark_func, smal_free_func free_func)
{
  smal_type_descriptor desc;
  memset(&desc, 0, sizeof(desc));
  desc.object_size = object_size;
  desc.mark_func = mark_func;
  desc.free_func = free_func;
  return smal_type_for_desc(&desc);
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
    // fprintf(stderr, "  type @%p buf @%p TRY\n", self, buf);
    assert(buf && buf->type == self);
    smal_thread_mutex_lock(&buf->stats._mutex);
    if ( buf->stats.avail_n && ! smal_thread_lock_test(&buf->alloc_disabled) ) {
      // fprintf(stderr, "  type @%p buf @%p avail_n %lu\n", self, buf, buf->stats.avail_n);
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

  // fprintf(stderr, "  type @%p buf @%p avail_n %lu <==== \n", self, least_avail_buf, least_avail_buf ? least_avail_buf->stats.avail_n : 0);

  return least_avail_buf;
}

static
smal_buffer *smal_type_alloc_buffer(smal_type *self)
{
  smal_buffer *buf;

  // Assume alloc_buffer_mutex is locked.
  if ( smal_likely((buf = self->alloc_buffer)) ) {
    assert(buf->type == self);
  } else {
    /* Scan for a buffer. */
    if ( ! (buf = self->alloc_buffer = smal_type_find_alloc_buffer(self)) ) {
      buf = self->alloc_buffer = smal_buffer_alloc(self);
      /* If 0, out-of-memory */
      // fprintf(stderr, "  type @%p buf @%p NEW\n", self, buf);
    }
  }

  return buf;
}

void smal_alloc_p(smal_type *self, void **ptrp)
{
  void *ptr = 0;
  smal_buffer *alloc_buffer;

  /* Allow multiple allocators (readers). */
  smal_thread_rwlock_rdlock(&alloc_lock);

  /* Validate or allocate a smal_buffer. */
  smal_thread_mutex_lock(&self->alloc_buffer_mutex);
  if ( smal_unlikely(! (alloc_buffer = smal_type_alloc_buffer(self))) ) {
    goto done;
  }

  /* If current smal_buffer cannot provide, */
  if ( smal_unlikely(! (ptr = smal_buffer_alloc_object(alloc_buffer))) ) {
    // fprintf(stderr, "  type @%p buf @%p EMPTY\n", self, self->alloc_buffer);

    /* Allocate a new smal_buffer. */
    self->alloc_buffer = 0;
    
    /* If cannot allocate new smal_buffer, out-of-memory. */
    if ( smal_unlikely(! (alloc_buffer = smal_type_alloc_buffer(self))) ) {
      goto done;
    }
    // smal_thread_mutex_unlock(&self->alloc_buffer_mutex);

    /* Allocate object from smal_buffer. */
    ptr = smal_buffer_alloc_object(alloc_buffer);
  }
  
 done:
  smal_thread_mutex_unlock(&self->alloc_buffer_mutex);
  smal_thread_rwlock_unlock(&alloc_lock);

  *ptrp = ptr;
}

/* not-thread safe. */
void *smal_alloc(smal_type *type)
{
  void *ptr = 0;
  smal_alloc_p(type, &ptr);
  return ptr;
}

void smal_free(void *ptr)
{
  int error = 1;
  smal_buffer *buf;

  smal_debug(object_free, 2, "() @%p", ptr);

  if ( smal_unlikely(buf = smal_ptr_to_buffer(ptr)) ) {
    smal_debug(object_free, 3, "ptr @%p => buf b@%p", ptr, buf);
    if ( smal_likely(smal_buffer_ptr_is_validQ(buf, ptr)) ) {
      assert(buf->page_id == smal_buffer_page_id(buf));
      smal_debug(object_free, 3, "ptr @%p is valid in buf b@%p", ptr, buf);
      smal_thread_mutex_lock(&buf->free_list_mutex);
      smal_thread_rwlock_wrlock(&buf->free_bits_lock);
      smal_buffer_free_object(buf, ptr);
      smal_thread_rwlock_unlock(&buf->free_bits_lock);
      smal_thread_mutex_unlock(&buf->free_list_mutex);
      error = 0;
    }
  }

  if ( smal_unlikely(error) ) abort();
}

/********************************************************************/

static
int smal_each_object_list(smal_buffer_list_head *list, int (*func)(smal_type *type, void *ptr, void *arg), void *arg)
{
  smal_buffer *buf;
  int result = 0;

  smal_dllist_each(list, buf); {
    void *ptr, *alloc_ptr = smal_buffer_alloc_ptr(buf);
    // fprintf(stderr, "    s_e_o bh b@%p\n", buf);
    smal_thread_rwlock_rdlock(&buf->free_bits_lock);
    for ( ptr = buf->begin_ptr; ptr < alloc_ptr; ptr += smal_buffer_object_size(buf) ) {
      if ( ! smal_buffer_freeQ(buf, ptr) ) {
	smal_thread_rwlock_unlock(&buf->free_bits_lock);
	result = func(buf->type, ptr, arg);
	if ( smal_unlikely(result < 0) ) break;
	smal_thread_rwlock_rdlock(&buf->free_bits_lock);
      }
    }
    if ( smal_unlikely(result < 0) ) break;
    smal_thread_rwlock_unlock(&buf->free_bits_lock);
  } smal_dllist_each_end();

  return result;
}

int smal_each_object(int (*func)(smal_type *type, void *ptr, void *arg), void *arg)
{
  int result = 0;

  if ( smal_unlikely(in_collect) ) abort();
  if ( smal_unlikely(! initialized) ) initialize();

  // ++ no_collect;

  // fprintf(stderr, "  s_e_o: \n");
  /* Pause smal_collect (writer) */
  smal_thread_rwlock_rdlock(&alloc_lock);

  smal_thread_rwlock_rdlock(&buffer_head_lock);
  result = smal_each_object_list((void*) &buffer_head, func, arg);
  smal_thread_rwlock_unlock(&buffer_head_lock);

  if ( smal_unlikely(result < 0) ) goto done;

  smal_thread_rwlock_rdlock(&buffer_collecting_lock);
  result = smal_each_object_list(&buffer_collecting, func, arg);
  smal_thread_rwlock_unlock(&buffer_collecting_lock);

  done:
  smal_thread_rwlock_unlock(&alloc_lock);
  // -- no_collect;
  return result;
}

void smal_global_stats(smal_stats *stats)
{
  if ( smal_unlikely(! initialized) ) initialize();
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

#if 0
  smal_debug_set_level(smal_debug_mprotect, 9);
  smal_debug_set_level(smal_debug_mmap, 9);
  smal_debug_set_level(smal_debug_write_barrier, 9);
  fprintf(stderr, "smal: pid = %d\n", (int) getpid());
#endif

  if ( smal_debug_level >= 1 ) {
    smal_debug_set_level(smal_debug_all, smal_debug_level);
    fprintf(stderr, "\n %s:%d %s()\n", __FILE__, __LINE__, __FUNCTION__);
  }

  memset(&buffer_head, 0, sizeof(buffer_head));
  smal_dllist_init(&buffer_head);

  memset(&buffer_collecting, 0, sizeof(buffer_collecting));
  smal_dllist_init(&buffer_collecting);

  memset(&type_head, 0, sizeof(type_head));
  smal_dllist_init(&type_head);

  smal_thread_mutex_init(&_smal_debug_mutex);
  smal_thread_rwlock_init(&alloc_lock);
  smal_thread_mutex_init(&type_head_mutex);
  smal_thread_rwlock_init(&buffer_head_lock);
  smal_thread_mutex_init(&buffer_head.stats._mutex);
  smal_thread_rwlock_init(&buffer_collecting_lock);
  smal_thread_rwlock_init(&buffer_table_lock);

  smal_thread_lock_init(&_smal_collect_inner_lock);

  page_id_min = 0; page_id_max = 0;
  page_id_min_max_valid = 0;
  buffer_table_size = 1;
  buffer_table =   malloc(sizeof(buffer_table[0]) * buffer_table_size);
  memset(buffer_table, 0, sizeof(buffer_table[0]) * buffer_table_size);

#if SMAL_WRITE_BARRIER
  smal_write_barrier_init();
#endif

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


