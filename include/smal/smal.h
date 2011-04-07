#ifndef SMAL_SMAL_H
#define SMAL_SMAL_H


/* SMAL: a [S]imple [M]ark-sweep [AL]locator
   Copyright Kurt Stephens 2011.
 */

#include <stddef.h>

#ifndef SMAL_PTHREAD
#define SMAL_PTHREAD 0
#endif

#if SMAL_PTHREAD
#include <pthread.h>
#endif

typedef void (*smal_mark_func)(void *ptr);
typedef void (*smal_free_func)(void *ptr);

struct smal_type;
typedef struct smal_type smal_type;
struct smal_buffer;
typedef struct smal_buffer smal_buffer;

struct smal_type {
  smal_type *next, *prev; /* global list of all smal_types. */
  size_t type_id;
  size_t object_size;
  smal_mark_func mark_func;
  smal_free_func free_func;
  smal_buffer *alloc_buffer;
};

struct smal_buffer {
  smal_buffer *next, *prev; /* global list of all smal_buffers. */

  size_t buffer_id;
  smal_type *type;

  size_t object_size;
  size_t object_alignment;
  size_t object_capacity; /* number of objects that can be allocated from this buffer. */

  void *mmap_addr;
  size_t mmap_size;

  void *begin_ptr; /* start of object allocations. */
  void *end_ptr; /* alloc_ptr guard. */

  void *alloc_ptr; /* next location to allocate an object. */
  size_t alloc_n; /* number of objects allocated. */

  size_t avail_n; /* number of objects either unallocated or on free_list. */

  size_t live_n; /* number of objects known to be live. */

  unsigned int *mark_bits;
  int mark_bits_n; /* number of marked bits. */
  size_t mark_bits_size; /* number of objects in mark_bits. */

  void *free_list;
  int free_list_n; /* number of objects on free_list. */
};

extern int smal_debug_level;

smal_type *smal_type_for(size_t object_size, smal_mark_func mark_func, smal_free_func free_func);
void *smal_type_alloc(smal_type *type);
void smal_type_free(void *ptr);
void smal_mark_ptr(void *ptr); /* user can call this method. */
void smal_mark_ptr_exact(void *ptr); /* assumes ptr is 0 or known to be properly allocated and aligned. */
void smal_mark_roots(); /* user must define this method. */
void smal_collect(); /* user can call this method. */
/* Cannot be called during gc. */
void smal_each_object(void (*func)(smal_type *type, void *ptr, void *arg), void *arg);

#endif

