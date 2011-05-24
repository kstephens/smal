#ifdef __APPLE__
#define exn_init smal_write_barrier_init_os
static void exn_init();
#define my_handle_exn smal_write_barrier_mutation
#endif

void smal_write_barrier_init()
{
  smal_write_barrier_init_os();
}

void smal_buffer_write_protect(smal_buffer *self)
{
  int result;
#ifdef __APPLE__
  /* Mach cannot mprotect() non-page alignments.
     This also implies SMAL_SEGREGATE_BUFFER_FROM_PAGE or else all locking and accounting 
     will induce erronous dirty write barrier activity.
  */
#if ! SMAL_SEGREGATE_BUFFER_FROM_PAGE
#error Must enable SMAL_SEGREGATE_BUFFER_FROM_PAGE when using write barrier on __APPLE__ Mach.
#endif
  void *addr = smal_buffer_to_page(self);
  size_t size = smal_page_size;
#else
  /* Linux can mprotect() non-page alignments. */
  void *addr = self->begin_ptr;
  size_t size = self->end_ptr - addr;
#endif

  smal_thread_rwlock_wrlock(&self->write_protect_lock);
  if ( ! (
	  self->write_protect && 
	  self->write_protect_addr == addr &&
	  self->write_protect_size == size
	  ) ) {
    result = mprotect(addr, size, PROT_READ);
    smal_debug(2, " mprotect(@%p,0x%lx,0x%x) = %d", (void*) addr, (unsigned long) size, (unsigned int) PROT_READ);
    if ( result ) abort();
    if ( ! result ) {
      self->write_protect = 1;
      self->write_protect_addr = addr;
      self->write_protect_size = size;
    }
  }
  smal_thread_rwlock_unlock(&self->write_protect_lock);
}

void smal_buffer_write_unprotect(smal_buffer *self)
{
  int result;

  smal_thread_rwlock_wrlock(&self->write_protect_lock);
  if ( self->write_protect ) {
    result = mprotect(self->write_protect_addr, self->write_protect_size, PROT_READ | PROT_WRITE);
    smal_debug(2, " mprotect(@%p,0x%lx,0x%x) = %d", (void*) self->write_protect_addr, (unsigned long) self->write_protect_size, (unsigned int) PROT_READ | PROT_WRITE);
    if ( result ) abort();
    if ( ! result ) {
      self->write_protect = 0;
      self->write_protect_addr = 0;
      self->write_protect_size = 0;
    }
  }
  smal_thread_rwlock_unlock(&self->write_protect_lock);
}

int smal_write_barrier_mutation(void *addr, int code)
{
  smal_buffer *self = smal_addr_to_buffer(addr);
  if ( self && self->dirty_write_barrier ) {
    smal_thread_rwlock_rdlock(&self->dirty_lock);
    if ( ! self->dirty ) {
      // fprintf(stderr, "\n  dirty @%p in buf @%p [%p, %p)\n", addr, self, self->begin_ptr, self->alloc_ptr); fflush(stderr);
      smal_thread_rwlock_unlock(&self->dirty_lock);
      smal_LOCK_STATS(lock);
      smal_UPDATE_STATS(dirty_mutations, += 1);
      smal_LOCK_STATS(unlock);
      smal_thread_rwlock_wrlock(&self->dirty_lock);
      self->dirty = 1;
    }
    smal_thread_rwlock_unlock(&self->dirty_lock);
    /* Allow mutator to continue unabated. */
    smal_buffer_write_unprotect(self);
    return 1; /* OK */
  }
  return 0; /* NOT OK */
}

void smal_buffer_clear_dirty(smal_buffer *self)
{
  smal_thread_rwlock_wrlock(&self->dirty_lock);
  self->dirty = 0;
  smal_thread_rwlock_unlock(&self->dirty_lock);
  if ( self->dirty_write_barrier ) 
    smal_buffer_write_protect(self);
}

void smal_buffer_reprotect_dirty(smal_buffer *self)
{
  smal_thread_rwlock_wrlock(&self->dirty_lock);
  self->dirty = 1;
  smal_thread_rwlock_unlock(&self->dirty_lock);
  if ( self->dirty_write_barrier ) 
    smal_buffer_write_protect(self);
}

#ifdef __APPLE__
#include "write_barrier_mach.h"
#endif
