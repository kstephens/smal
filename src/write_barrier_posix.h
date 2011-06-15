#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/mman.h> /* mprotect() */
#include <signal.h>

static struct sigaction sa_for_sig_old[64];
static struct sigaction sa_for_sig_new[64];

static
void write_fault(int sig, struct siginfo *si, void *something)
{
  int result;
  int errno_save = errno;
  void *addr = si->si_ptr;

  // fprintf(stderr, "write_fault %d at @%p\n", sig, addr);

  result = smal_write_barrier_mutation(addr, sig);

  fprintf(stderr, "  write_fault %d at @%p => %d\n", sig, addr, result);

  errno = errno_save;

  if ( ! result )
    sa_for_sig_old[sig].sa_sigaction(sig, si, something);
}

static
void setup_signal_handler(int sig)
{
  struct sigaction *sa_new, *sa_old;
  int result;

  assert(0 <= sig && sig < 64);
 
  sa_new = &sa_for_sig_new[sig];
  sa_old = &sa_for_sig_old[sig];

  memset(sa_new, 0, sizeof(*sa_new));
  memset(sa_old, 0, sizeof(*sa_old));
  sa_new->sa_sigaction = write_fault;
  sa_new->sa_flags = SA_SIGINFO | SA_RESTART;
  result = sigaction(sig, sa_new, sa_old);
  assert(result == 0);
}

void smal_write_barrier_init_os()
{
#ifdef SIGSEGV
  setup_signal_handler(SIGSEGV);
#endif
#ifdef SIGBUS
  setup_signal_handler(SIGBUS);
#endif
}

