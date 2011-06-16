#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/mman.h> /* mprotect() */
#include <signal.h>

static struct sigaction sa_for_sig_old[64];
static struct sigaction sa_for_sig_new[64];

static char *sig_name;
static char *si_code_name;
static
void smal_get_sig_info(int sig, siginfo_t *si, void *something)
{
  sig_name = "unknown";
  si_code_name = "unknown";
  switch ( sig ) {
#ifdef SIGSEGV
  case SIGSEGV:
    sig_name = "SIGSEGV";
    switch ( si->si_code ) {
#ifdef SEGV_MAPERR
    case SEGV_MAPERR:
      si_code_name = "SEGV_MAPERR";
      break;
#endif
#ifdef SEGV_ACCERR
    case SEGV_ACCERR:
      si_code_name = "SEGV_ACCERR";
      break;
    }
#endif
    break;
#endif
#ifdef SIGBUS
  case SIGBUS:
    sig_name = "SIGBUS";
    switch ( si->si_code ) {
#ifdef BUG_ADRALN
    case BUS_ADRALN:
      si_code_name = "BUS_ADRALN";
      break;
#endif
#ifdef BUG_ADDRERR
    case BUS_ADRERR:
      si_code_name = "BUS_ADRERR";
      break;
#endif
#ifdef BUS_OBJERR
    case BUS_OBJERR:
      si_code_name = "BUS_OBJERR";
      break;
    }
#endif
    break;
#endif
  }
}

static
void write_fault(int sig, siginfo_t *si, void *something)
{
  int result;
  int errno_save = errno;
  void *addr;

  /* Sometimes si_addr is 0? */
  addr = si->si_addr;
#if 0
  if ( ! addr )
    addr = si->si_ptr; /* si_ptr? */
#endif

  result = smal_write_barrier_mutation(addr, sig);

  smal_get_sig_info(sig, si, something);
  fprintf(stderr, "  write_fault %d (%s) si_code %d (%s) at si_ptr @%p si_addr @%p addr @%p => %d\n", 
	  sig, sig_name, 
	  si->si_code, si_code_name,  
	  si->si_ptr, si->si_addr,
	  addr,
	  result);

  errno = errno_save;
  if ( ! result ) {
    void (*sa)(int, siginfo_t *, void*) = sa_for_sig_old[sig].sa_sigaction;
    if ( (void*) sa != (void*) SIG_DFL && (void*) sa != (void*) SIG_IGN ) {
      sa(sig, si, something);
    } else {
      smal_get_sig_info(sig, si, something);
      fprintf(stderr, "\nsmal: pid %d: SIGNAL %d (%s) si_code %d (%s) at @%p\n", 
	      (int) getpid(),
	      sig, sig_name, 
	      si->si_code, si_code_name,
	      addr);
      abort();
    }
  }
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
  sa_new->sa_flags = SA_SIGINFO | SA_RESTART
#ifdef SA_NODEFER
    | SA_NODEFER
#endif
    ;
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

