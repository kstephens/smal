#include <stdio.h> /* perror() */
#include <stdlib.h> /* malloc(), free() */
#include <string.h>
#include <assert.h>
#ifdef __APPLE__
int main(int argc, char **argv)
{
  fprintf(stderr, "%s: Skipping: Mach does not support Posix sigaction traps for SIGSEGV.\n", argv[0]);
  return 0;
}
#else
#include <sys/mman.h> /* mprotect() */
#include <signal.h>

static size_t pagesize = 4096;
static char area[8192];

static void *mp_addr = area;
static size_t mp_size;

static volatile int write_fault_signal;
static volatile siginfo_t write_fault_si;
static volatile void *write_fault_something;
static volatile void *write_fault_addr;

static
void write_fault(int sig, siginfo_t *si, void *something)
{
  int result;
  //  int errno_save = errno;

  fprintf(stderr, "write_fault %d\n", sig);

  write_fault_signal = sig;
  memcpy((void*) &write_fault_si, &si, sizeof(write_fault_si));

  fprintf(stderr, "si_addr = %p\n", si->si_addr);
  fprintf(stderr, "si_ptr  = %p\n", si->si_ptr);

  write_fault_something = something;
  write_fault_addr = si->si_addr;
  if ( ! write_fault_addr )
    write_fault_addr = si->si_ptr;

  fprintf(stderr, "  write_fault_addr = %p\n", write_fault_addr);
  result = mprotect(mp_addr, mp_size, PROT_READ | PROT_WRITE);
  assert(result == 0);

  // errno = errno_save;
}

int main(int argc, char **argv)
{
  int result;
  struct sigaction sa, sa_old;

  memset(&sa, 0, sizeof(sa));
  memset(&sa_old, 0, sizeof(sa_old));
  sa.sa_sigaction = write_fault;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  result = sigaction(SIGSEGV, &sa, &sa_old);
  assert(result == 0);

  memset(&sa, 0, sizeof(sa));
  memset(&sa_old, 0, sizeof(sa_old));
  sa.sa_sigaction = write_fault;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  result = sigaction(SIGBUS, &sa, &sa_old);
  assert(result == 0);

  /* OS X nor Linux will not allow mprotect(addr, size, ...) if addr && size are not page aligned. */
  {
    size_t offset;
    if ( (offset = ((size_t) mp_addr) % pagesize) )
      mp_addr += pagesize - offset;
    mp_size = pagesize;
  }
  result = mprotect(mp_addr, mp_size, PROT_READ);
  assert(result == 0);

  assert(write_fault_signal == 0);
  fprintf(stderr, "mp_addr = %p\n", mp_addr);
  *((void**) mp_addr) = 0;
  assert(write_fault_signal != 0);
  assert(write_fault_addr == mp_addr);

  fprintf(stderr, "\n%s OK\n", argv[0]);

  return 0;
}
#endif
