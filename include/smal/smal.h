/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#ifndef SMAL_SMAL_H
#define SMAL_SMAL_H

#include <stddef.h>
#include "smal/assert.h"
#include "smal/thread.h"

/* Configuration */

#ifndef SMAL_WRITE_BARRIER
#define SMAL_WRITE_BARRIER 1
#endif

#if SMAL_WRITE_BARRIER && (defined(__APPLE__) || defined(__linux__))
#define SMAL_SEGREGATE_BUFFER_FROM_PAGE 1
#endif

#ifndef SMAL_MARK_QUEUE
#define SMAL_MARK_QUEUE 1
#endif

#ifndef SMAL_REMEMBERED_SET
#define SMAL_REMEMBERED_SET 1
#endif


#define smal_alignedQ(ptr,align) (((size_t)(ptr) % (align)) == 0)
#define smal_ALIGN(ptr,align) if ( (size_t)(ptr) % (align) ) (ptr) += (align) - ((size_t)(ptr) % (align))

typedef void (*smal_mark_func)(void *ptr);
typedef void (*smal_free_func)(void *ptr);

struct smal_type;
typedef struct smal_type smal_type;

struct smal_type_descriptor;
typedef struct smal_type_descriptor smal_type_descriptor;

struct smal_bitmap;
typedef struct smal_bitmap smal_bitmap;

struct smal_buffer;
typedef struct smal_buffer smal_buffer;

struct smal_buffer_list;
typedef struct smal_buffer_list smal_buffer_list;

struct smal_buffer_list_head;
typedef struct smal_buffer_list_head smal_buffer_list_head;

struct smal_buffer_list_head {
  smal_buffer *next, *prev;
};

struct smal_buffer_list {
  struct smal_buffer_list *next, *prev;
  smal_buffer *buffer;
};

struct smal_stats;
typedef struct smal_stats smal_stats;

struct smal_stats {
  size_t alloc_id; /** may wrap. */
  size_t free_id; /** may wrap. */
  size_t buffer_id; /** may wrap. */
  size_t capacity_n; /** number of object that can be allocated with current buffers. */
  size_t alloc_n; /** number of objects allocated. */
  size_t avail_n; /** number of objects either unallocated or on free_list. */
  size_t live_n; /** number of objects known to be live. */
  size_t live_before_sweep_n; /** number of objects known to be live before sweep. */
  size_t free_n; /** number of objects on free_list. */
  size_t buffer_n; /** number of buffers active. */
  size_t collection_n; /** number of collections. */
  size_t mmap_size; /** bytes mmap()ed. */
  size_t mmap_total; /** total bytes mmap()ed, may wrap. */
  size_t buffer_mutations; /** mutations: valid only for buffers with dirty_write_barrier.  */
  smal_thread_mutex _mutex;
};
extern const char *smal_stats_names[];

struct smal_type_descriptor {
  size_t object_size;
  size_t object_alignment;
  smal_mark_func mark_func;
  smal_free_func free_func;
  int collections_per_sweep;
  int mostly_unchanging;
  void *opaque;
};

struct smal_type {
  smal_type *next, *prev; /** global list of all smal_types. */
  smal_buffer_list buffers; /** List of all smal_buffers for this smal_type. */
  smal_thread_rwlock buffers_lock;
  size_t type_id; /** Unique identifier for this smal_type. */
  smal_type_descriptor desc;
  smal_buffer *alloc_buffer; /** The buffer to allocate from. */
  smal_thread_mutex alloc_buffer_mutex;
  smal_stats stats; /** Stats for this smal_type. */
};

struct smal_bitmap {
  size_t size;
  unsigned int *bits;
  size_t bits_size;
  int set_n;
  int clr_n;
};

struct smal_buffer {
  smal_buffer *next, *prev; /** Global list of all smal_buffers. */
  smal_buffer_list type_buffer_list; /** List of all smal_buffers of this buffer's type. */

  size_t buffer_id; /** Unique for each allocated smal_buffer; may wrap. */
  size_t page_id; /** Related to the address of mmap_addr. */
  smal_type *type; /** The type of objects in this smal_buffer. */

  size_t object_size; /** == type->object_size. */
  size_t object_alignment; /** == type->object_alignment. */
  size_t object_capacity; /** Number of objects that can be allocated from this buffer. */

  void *mmap_addr; /** The address of mmap() memory for this buffer. */
  size_t mmap_size; /** The size of mmap() memory for this buffer. */

  void *begin_ptr; /** Start of object allocations. */
  void *end_ptr; /** End of object allocations; alloc_ptr guard. */
  void *alloc_ptr; /** Next location to allocate an object. */
  smal_thread_mutex alloc_ptr_mutex;
  smal_thread_lock alloc_disabled; /** Set to true during smal_collect(); allows other threads to continue allocating from other buffers. */

  smal_stats stats; /** Stats for this smal_buffer. */

  smal_bitmap mark_bits;
  smal_thread_rwlock mark_bits_lock;

  smal_bitmap free_bits;
  smal_thread_rwlock free_bits_lock;

  void *free_list; /** Free list of previously allocated but currently unused objects. */
  smal_thread_mutex free_list_mutex;

  smal_thread_rwlock write_protect_lock;
  int write_protect;   /** If true, region between write_protect_addr and write_protect_addr + write_protect_size is protected against writes. */
  void  *write_protect_addr; /** The protected region base address. */
  size_t write_protect_size; /** The protected region size. */

  smal_thread_rwlock mutation_lock;
  int mutation_write_barrier; /** If true, use write barrier flag mutation if write protect region is modified. */
  int mutation; /** If true, elements within smal_buffer allocation space were mutated. */

  int markable;  /** If true, this buffer should mark objects into it. */
  int sweepable; /** If true, this buffer is up for sweeping. */

#if SMAL_REMEMBERED_SET
  int use_remembered_set;
  int record_remembered_set;
  struct smal_remembered_set *remembered_set;
  int remembered_set_valid;
#endif
};

extern int smal_debug_level;

smal_type *smal_type_for_desc(smal_type_descriptor *desc);
smal_type *smal_type_for(size_t object_size, smal_mark_func mark_func, smal_free_func free_func); /** Deprecated. */
void smal_type_free(smal_type *type);

void smal_alloc_p(smal_type *type, void **ptrp); /** Thread-safe. */
void *smal_alloc(smal_type *type); /** Not thread-safe: reference is returned in a register. */
void smal_free(void *ptr); /** Thread-safe. */
void smal_free_p(void **ptrp); /** Thread-safe. */

/** Start a collection. */
void smal_collect(); /* Thread-safe. */
void smal_collect_wait_for_sweep(); /* Thread-safe. */

/* Mark pointers. */
/** Users can call these method only during smal_collect(): */
void smal_mark_ptr(void *referrer, void *ptr); 
void smal_mark_ptr_p(void *referrer, void **ptrp);
void smal_mark_ptr_n(void *referrer, int n_ptrs, void **ptrs);

/** Mark groups of possible pointers. */
void smal_mark_ptr_range(void *referrer, void *ptr, void *ptr_end); /** Assumes arbitrary alignments of possible pointers within region. */
void smal_mark_bindings(int n_bindings, void ***bindings);

/** If func() returns < 0; stop iterating, returns < 0 if func() < 0. */
int smal_each_object(int (*func)(smal_type *type, void *ptr, void *arg), void *arg);

/** Completely shuts down smal.  Frees all allocated memory. */
void smal_shutdown();

/** Get stats. */
void smal_global_stats(smal_stats *stats); /* thread-safe */
void smal_type_stats(smal_type *type, smal_stats *stats); /* thread-safe */

/* smal_collect() callbacks: must be defined by users: */

void smal_collect_inner_before(void *top_of_stack);
void smal_collect_before_mark();
void smal_collect_after_mark();
void smal_collect_before_sweep();
void smal_collect_after_sweep();
void smal_collect_mark_roots();

/* Low-level/extension functions */
int smal_object_reachableQ(void *ptr);
smal_buffer *smal_buffer_from_ptr(void *ptr);
void smal_buffer_print_all(smal_buffer *self, const char *action);

/*********************************************************************
 * Configuration
 */

typedef enum smal_debug_t {
  smal_debug_BEGIN,
  smal_debug_all,
  smal_debug_buffer,
  smal_debug_object_alloc,
  smal_debug_object_free,
  smal_debug_mmap,
  smal_debug_collect,
  smal_debug_mark,
  smal_debug_sweep,
  smal_debug_write_barrier,
  smal_debug_mprotect,
  smal_debug_remembered_set,
  smal_debug_END,
} smal_debug_t;

void smal_debug_set_level(smal_debug_t dt, int value);

/*********************************************************************
 * Configuration
 */

#if 0
#define smal_buffer_object_size(buf) 24
#define smal_buffer_object_alignment(buf) smal_buffer_object_size(buf)
#endif

#ifndef smal_page_size_default
#define smal_page_size_default ((size_t) (4 * 4 * 1024))
#endif

#define smal_page_size smal_page_size_default
#define smal_page_mask (smal_page_size - 1)

#ifndef smal_buffer_object_size
#define smal_buffer_object_size(buf) (buf)->object_size
#endif

#ifndef smal_buffer_object_alignment
#define smal_buffer_object_alignment(buf) (buf)->object_alignment
#endif

/*********************************************************************
 * addr -> page mapping.
 */

#define smal_addr_page_id(PTR) (((size_t) (PTR)) / smal_page_size)
#define smal_addr_page_offset(PTR) (((size_t) (PTR)) & smal_page_mask)
#define smal_addr_page(PTR) ((void*)(((size_t) (PTR)) & ~smal_page_mask))

#ifndef SMAL_SEGREGATE_BUFFER_FROM_PAGE
#define SMAL_SEGREGATE_BUFFER_FROM_PAGE 0
#endif

#if SMAL_SEGREGATE_BUFFER_FROM_PAGE

/* Pointer to small_buffer is stored in first word of mmap region. */
struct smal_page {
  smal_buffer *buffer;
  double objects[0];
};

#define smal_addr_to_buffer(PTR)		\
  (*((smal_buffer**)(smal_addr_page(PTR))))

#define smal_buffer_to_page(BUF)		\
  ((void*) (BUF)->mmap_addr)

#define smal_buffer_page_id(BUF) ((BUF)->page_id) 

#else

/* smal_buffer is stored at head of mmap region. */
struct smal_page {
  smal_buffer buffer;
  double objects[0];
};

#define smal_addr_to_buffer(PTR)		\
  (((smal_buffer*)(smal_addr_page(PTR))))

#define smal_buffer_to_page(BUF)		\
  ((void*) (BUF))

#define smal_buffer_page_id(BUF) smal_addr_page_id(BUF)

#endif


#endif

