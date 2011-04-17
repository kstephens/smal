/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#ifndef SMAL_SMAL_H
#define SMAL_SMAL_H

#include <stddef.h>

#define smal_alignedQ(ptr,align) (((size_t)(ptr) % (align)) == 0)
#define smal_ALIGN(ptr,align) if ( (size_t)(ptr) % (align) ) (ptr) += (align) - ((size_t)(ptr) % (align))

typedef void (*smal_mark_func)(void *ptr);
typedef void (*smal_free_func)(void *ptr);

struct smal_type;
typedef struct smal_type smal_type;

struct smal_bitmap;
typedef struct smal_bitmap smal_bitmap;

struct smal_buffer;
typedef struct smal_buffer smal_buffer;

typedef struct small_buffer_list {
  smal_buffer *next, *prev;
} smal_buffer_list;

struct smal_stats;
typedef struct smal_stats smal_stats;

struct smal_stats {
  size_t alloc_id; /* may wrap. */
  size_t free_id; /* may wrap. */
  size_t alloc_n; /* number of objects allocated. */
  size_t avail_n; /* number of objects either unallocated or on free_list. */
  size_t live_n; /* number of objects known to be live. */
  size_t free_n; /* number of objects on free_list. */
  size_t buffer_n; /* number of buffers active. */
};
extern const char *smal_stats_names[];

struct smal_type {
  smal_type *next, *prev; /* global list of all smal_types. */
  size_t type_id;
  size_t object_size;
  smal_mark_func mark_func;
  smal_free_func free_func;
  smal_buffer *alloc_buffer;
  smal_stats stats;
};

struct smal_bitmap {
  size_t size;
  unsigned int *bits;
  size_t bits_size;
  int set_n;
  int clr_n;
};

struct smal_buffer {
  smal_buffer *next, *prev; /* global list of all smal_buffers. */
  smal_buffer_list type_list; /* list of all smal_buffers of this buffer's type. */

  size_t buffer_id;
  smal_type *type;

  size_t object_size; /* == type->object_size. */
  size_t object_alignment; /* defaults to sizeof(double). */
  size_t object_capacity; /* number of objects that can be allocated from this buffer. */

  void *mmap_addr;
  size_t mmap_size;

  void *begin_ptr; /* start of object allocations. */
  void *end_ptr; /* alloc_ptr guard. */
  void *alloc_ptr; /* next location to allocate an object. */

  smal_stats stats;

  smal_bitmap mark_bits;

  void *free_list;
  smal_bitmap free_bits;
};

extern int smal_debug_level;

smal_type *smal_type_for(size_t object_size, smal_mark_func mark_func, smal_free_func free_func);
void smal_type_free(smal_type *type);
void *smal_alloc(smal_type *type);
void smal_free(void *ptr);
void smal_mark_ptr(void *ptr); /* user can call this method. */
void smal_mark_ptr_exact(void *ptr); /* assumes ptr is 0 or known to be properly allocated and aligned. */
void smal_mark_ptr_range(void *ptr, void *ptr_end);

void smal_collect(); /* user can call this method. */
/* Disables GC while executing. */
void smal_each_object(void (*func)(smal_type *type, void *ptr, void *arg), void *arg);

/* Completely shuts down smal.  Frees all allocated memory. */
void smal_shutdown();

void smal_global_stats(smal_stats *stats);
void smal_type_stats(smal_type *type, smal_stats *stats);

/* Functions that must be defined by users: */

void smal_collect_inner_before(void *top_of_stack);
void smal_collect_before_mark();
void smal_collect_after_mark();
void smal_collect_before_sweep();
void smal_collect_after_sweep();
void smal_mark_roots(); /* TODO: rename this to smal_collect_mark_roots(). */

/* Low-level/extension functions */
int smal_object_reachableQ(void *ptr);

/* Can be called only from within smal_collect_*() callbacks. */
void smal_each_sweepable_object(int (*func)(smal_type *type, void *ptr, void *arg), void *arg);

#endif

