#ifdef __APPLE__
#define exc_init smal_write_barrier_init_os
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
  /* Mach cannot mprotect non-page alignments. */
  void *addr = smal_buffer_to_page(addr);
  size_t size = smal_page_size;
#else
  void *addr = self->begin_ptr;
  size_t size = self->alloc_ptr - addr;
#endif
  if ( self->write_protected && 
       self->write_protected_addr == addr &&
       self->write_protect_size == size )
    return;
  result = mprotect(addr, size, PROT_READ);
  smal_debug(2, " mprotect(%p,0x%lx,0x%x) = %d", (void*) addr, (unsigned long) size, (unsigned int) PROT_READ);
  if ( ! result ) abort();
  if ( ! result ) {
    self->write_protected = 1;
    self->write_protect_addr = addr;
    self->write_protect_size = size;
  }
}

void smal_buffer_write_unprotect(smal_buffer *self)
{
  int result;
  if ( ! self->write_protected )
    return;
  result = mprotect(self->write_protect_addr, self->write_protect_size, PROT_READ | PROT_WRITE);
  smal_debug(2, " mprotect(%p,0x%lx,0x%x) = %d", (void*) self->begin_ptr, (unsigned long) self->write_protect_size, (unsigned int) PROT_READ | PROT_WRITE);
  if ( ! result ) abort();
  if ( ! result ) {
    self->write_protected = 0;
    self->write_protect_addr = 0;
    self->write_protect_size = 0;
  }
}

int smal_write_barrier_mutation(void *addr, int code)
{
  smal_buffer *buf = smal_addr_to_buffer(addr);
  if ( buf ) {
    buf->dirty = 1;
    smal_buffer_write_unprotect(buf);
  }
}

#ifdef __APPLE__
#include "write_barrier_mach.h"
#endif
