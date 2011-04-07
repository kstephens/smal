#ifndef SMAL_H
#define SMAL_H

/********************************************************************* 
SMAL: A Simple, barebones Mark-sweep ALlocator.

sgc_type represents a user-data type of a fixed size, mark and free callbacks.

sgc_buffer represents a mmap() region of sgc_buffer_size aligned to sgc_buffer_size.
sgc_buffer is at the head of the mmap() region.

Objects are parcelled on-demand by incrementing alloc_ptr, starting at begin_ptr.
Objects that are returned to the sgc_buffer are kept on its free_list.

Each sgc_buffer maintains a mark bitmap for each object parcelled from it.
The mark bitmap is transient; it's allocated and freed during sgc_collect.

The alignment restrictions of sgc_buffers allows for very fast pointer->buffer
mapping.
*/

#include <stddef.h>

typedef void (*sgc_mark_func)(void *ptr);
typedef void (*sgc_free_func)(void *ptr);

struct sgc_type;
typedef struct sgc_type sgc_type;
struct sgc_buffer;
typedef struct sgc_buffer sgc_buffer;

struct sgc_type {
  sgc_type *next, *prev; /* global list of all sgc_types. */
  size_t type_id;
  size_t element_size;
  sgc_mark_func mark_func;
  sgc_free_func free_func;
  sgc_buffer *alloc_buffer;
};

struct sgc_buffer {
  sgc_buffer *next, *prev; /* global list of all sgc_buffers. */

  size_t buffer_id;
  sgc_type *type;

  size_t element_size;
  size_t element_alignment;
  size_t element_capacity; /* number of elements that can be allocated from this buffer. */

  void *mmap_addr;
  size_t mmap_size;

  void *begin_ptr; /* start of element allocations. */
  void *end_ptr; /* alloc_ptr guard. */

  void *alloc_ptr; /* next location to allocate an element. */
  size_t alloc_n; /* number of elements allocated. */

  size_t live_n; /* number of elements known to be live. */

  unsigned int *mark_bits;
  int mark_bits_n; /* number of marked bits. */
  size_t mark_bits_size; /* number of elements in mark_bits. */

  void *free_list;
  int free_list_n; /* number of elements on free_list. */
};

sgc_type *sgc_type_for(size_t element_size, sgc_mark_func mark_func, sgc_free_func free_func);
void *sgc_type_alloc(sgc_type *type);
void sgc_mark_ptr(void *ptr); /* user can call this method. */
void sgc_mark_roots(); /* user must define this method. */
void sgc_collect(); /* user can call this method. */

#endif
