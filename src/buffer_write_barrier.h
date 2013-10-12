#ifdef __APPLE__
#define exn_init smal_buffer_write_barrier_init_os
static void exn_init();
#define my_handle_exn smal_write_barrier_mutation
#else
void smal_buffer_write_barrier_init_os();
#endif

static inline
void smal_buffer_write_barrier_init()
{
  smal_buffer_write_barrier_init_os();
}

static inline
int smal_mprotect(smal_buffer *self, void *addr, size_t size, int prot)
{
  int result = mprotect(addr, size, prot);
  smal_debug(mprotect, 2, " b@%p mprotect(@%p, 0x%lx, %d) = %d (%s)", self, (void*) addr, (unsigned long) size, (unsigned int) prot, result, strerror(errno));
  if ( result ) abort();
  // smal_debug_print_smaps();
  return result;
}

static inline
void smal_buffer_write_protect(smal_buffer *self)
{
#if defined(__APPLE__) || defined(__linux__)
  /* Mach cannot mprotect() non-page alignments.
     This also implies SMAL_SEGREGATE_BUFFER_FROM_PAGE or else all locking and accounting 
     will induce erronous mutation write barrier activity.
  */
#if ! SMAL_SEGREGATE_BUFFER_FROM_PAGE
#error Must enable SMAL_SEGREGATE_BUFFER_FROM_PAGE when using write barrier on __linux__ or __APPLE__ Mach.
#endif
  void *addr = smal_buffer_to_page(self);
  size_t size = smal_page_size;
#else
  /* May be able to mprotect() non-page alignments? */
  void *addr = self->begin_ptr;
  size_t size = self->end_ptr - addr;
#endif

  smal_thread_rwlock_wrlock(&self->write_protect_lock);
  if ( ! (
	  self->write_protect && 
	  self->write_protect_addr == addr &&
	  self->write_protect_size == size
	  ) ) {
    smal_mprotect(self, addr, size, PROT_READ);
    self->write_protect = 1;
    self->write_protect_addr = addr;
    self->write_protect_size = size;
  }
  smal_thread_rwlock_unlock(&self->write_protect_lock);
}

static inline
void smal_buffer_write_unprotect_force(smal_buffer *self)
{
  if ( self->write_protect_size ) {
    smal_mprotect(self, self->write_protect_addr, self->write_protect_size, PROT_READ | PROT_WRITE);
    self->write_protect = 0;
    self->write_protect_addr = 0;
    self->write_protect_size = 0;
  }
}

static inline
void smal_buffer_write_unprotect(smal_buffer *self)
{
  smal_thread_rwlock_wrlock(&self->write_protect_lock);
  if ( self->write_protect )
    smal_buffer_write_unprotect_force(self);
  smal_thread_rwlock_unlock(&self->write_protect_lock);
}

static inline
int smal_write_barrier_mutation(void *addr, int code)
{
  smal_buffer *self = smal_addr_to_buffer(addr);

  smal_debug(write_barrier, 4, " (@%p, sig %d) => b@%p", addr, code, self, self ? self->mutation_write_barrier : 0);
  if ( self ) { 
    smal_debug(write_barrier, 3, " (@%p, sig %d) => b@%p [@%p, @%p) mutation_write_barrier=%d", addr, code, self, self->begin_ptr, self->alloc_ptr, self->mutation_write_barrier);
    if ( self->mutation_write_barrier ) {
      smal_thread_rwlock_rdlock(&self->mutation_lock);
      if ( ! self->mutation ) {
	smal_debug(write_barrier, 2, " mutation @%p in buf b@%p", addr, self, self->begin_ptr, self->alloc_ptr);
	smal_thread_rwlock_unlock(&self->mutation_lock);
	smal_LOCK_STATS(lock);
	smal_UPDATE_STATS(buffer_mutations, += 1);
	smal_LOCK_STATS(unlock);
	smal_thread_rwlock_wrlock(&self->mutation_lock);
	self->mutation = 1;
      }
      smal_thread_rwlock_unlock(&self->mutation_lock);
      /* Allow mutator to continue unabated. */
      smal_buffer_write_unprotect(self);
      return 1; /* OK */
    }
#if 0
    /* Allow mutator to continue unabated. */
    self->write_protect_addr = self->mmap_addr;
    self->write_protect_size = self->mmap_size;
    smal_buffer_write_unprotect_force(self);
    return 2; /* OK?: in actual buffer, but mutation_write_barrier not enabled! */
#endif
  }
  return 0; /* NOT OK */
}

static inline
void smal_buffer_clear_mutation(smal_buffer *self)
{
  smal_thread_rwlock_wrlock(&self->mutation_lock);
  self->mutation = 0;
  smal_thread_rwlock_unlock(&self->mutation_lock);
  if ( self->mutation_write_barrier ) 
    smal_buffer_write_protect(self);
}

static inline
void smal_buffer_assume_mutation(smal_buffer *self)
{
  smal_thread_rwlock_wrlock(&self->mutation_lock);
  self->mutation = 1;
  smal_thread_rwlock_unlock(&self->mutation_lock);
  if ( self->mutation_write_barrier ) 
    smal_buffer_write_unprotect(self);
}

#ifdef __APPLE__
#include "buffer_write_barrier_mach.h"
#endif

#ifdef __linux__
#include "buffer_write_barrier_posix.h"
#endif
