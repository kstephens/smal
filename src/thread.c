/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/thread.h"
#include "smal/dllist.h"
#include "smal/assert.h"

#include <stdlib.h> /* malloc(), free() */
#include <string.h> /* memset() */
#include <stdio.h>

#ifdef smal_thread_mutex_init
#undef smal_thread_mutex_init
#undef smal_thread_mutex_destroy
#undef smal_thread_mutex_lock
#undef smal_thread_mutex_unlock
#endif

#if SMAL_THREAD_MUTEX_DEBUG
static size_t mutex_n, mutex_destroy_n, mutex_lock_n, mutex_unlock_n;
#endif
#if SMAL_THREAD_MUTEX_DEBUG >= 1
static
void mutex_stats()
{
  fprintf(stderr,
	  "\nsmal mutex stats:\n  %16lu mutex_n \n  %16lu mutex_destroy_n \n  %16lu mutex_lock_n \n  %16lu mutex_unlock_n \n\n", 
	  (unsigned long) mutex_n,
	  (unsigned long) mutex_destroy_n,
	  (unsigned long) mutex_lock_n,
	  (unsigned long) mutex_unlock_n
	  );
}
#endif

static int thread_inited;
static smal_thread thread_main;

static
int _smal_thread_getstack_main(smal_thread *t, void **addrp, size_t *sizep)
{
  extern char **environ;
  void *bottom_of_stack = environ; /* HACK */
  void *top_of_stack = alloca(0);
  static size_t page_align = 4095;

  /* COMMON: x86, 68x, etc. */
  if ( top_of_stack < bottom_of_stack ) {
    bottom_of_stack += page_align - 1;
    smal_ALIGN(bottom_of_stack, page_align);
    *addrp = top_of_stack;
    *sizep = bottom_of_stack - top_of_stack;
  } else {
    bottom_of_stack -= page_align - 1;
    smal_ALIGN(bottom_of_stack, page_align);
    *addrp = bottom_of_stack;
    *sizep = top_of_stack - bottom_of_stack;
  }
  // fprintf(stderr, "  %p getstack: %p[0x%lx] %p\n", t, *addrp, (unsigned long) *sizep, *addrp + *sizep);
  return 0;
}

#if SMAL_PTHREAD

static smal_thread thread_list;
static pthread_rwlock_t thread_list_lock = PTHREAD_RWLOCK_INITIALIZER;

static pthread_key_t roots_key;

#if 0
static
void thread_exit(void *arg)
{
  smal_thread *t = arg;
  // fprintf(stderr, "  thread_exit() %p\n", (void*) pthread_self());
  pthread_rwlock_rwlock(&thread_list_lock);
  smal_DLLIST_DELETE(t);
  pthread_rwlock_unlock(&thread_list_lock);
  free(t);
}
#endif

static
void thread_init(smal_thread* t)
{
  memset(t, 0, sizeof(*t));
  t->thread = pthread_self();
  pthread_setspecific(roots_key, t);

  pthread_rwlock_wrlock(&thread_list_lock);
  smal_dllist_insert(&thread_list, t);
  pthread_rwlock_unlock(&thread_list_lock);

  t->status = smal_thread_ALIVE;
}

static
smal_thread *thread_register_self()
{
  smal_thread *t = malloc(sizeof(*t));
  thread_init(t);
  return t;
}


static
void thread_prepare()
{
  // fprintf(stderr, "  thread_prepare() %p\n", (void*) pthread_self());
}

static
void thread_parent()
{
  // fprintf(stderr, "  thread_parent() %p\n", (void*) pthread_self());
}

static
void thread_child()
{
  // fprintf(stderr, "  thread_child() %p\n", (void*) pthread_self());


  // fprintf(stderr, "  thread_child() %p = %p\n", (void*) pthread_self(), t);
}

static pthread_once_t _smal_thread_is_initialized = PTHREAD_ONCE_INIT;
static
void _smal_thread_init()
{
  if ( ! thread_inited ) {
    pthread_key_create(&roots_key, 0);

    pthread_rwlock_init(&thread_list_lock, 0);
    
    pthread_rwlock_wrlock(&thread_list_lock);
    smal_dllist_init(&thread_list);
    pthread_rwlock_unlock(&thread_list_lock);

    pthread_atfork(thread_prepare,
		   thread_parent,
		   thread_child);
    
    thread_init(&thread_main);

#if SMAL_THREAD_MUTEX_DEBUG >= 2
    atexit(mutex_stats);
#endif

    thread_inited = 1;
  }
}

void smal_thread_init()
{
  if ( ! thread_inited ) { /* fast,unsafe lock */
    (void) pthread_once(&_smal_thread_is_initialized, _smal_thread_init);
  }
}

smal_thread *smal_thread_self()
{
  if ( ! thread_inited ) smal_thread_init();

  {
    smal_thread *t = pthread_getspecific(roots_key);
    if ( ! t )
      t = thread_register_self();
    assert(t);
    // fprintf(stderr, "smal_thread_self() = %p: %p\n", t, (void*) t->thread);
    return t;
  }
}

void smal_thread_died(smal_thread *t)
{
  if ( ! t ) t = smal_thread_self();
  assert(t->status == smal_thread_ALIVE);
  t->status = smal_thread_DEAD;
  t->bottom_of_stack = t->top_of_stack = 0;
  memset(&t->registers, 0, sizeof(t->registers));
}


int smal_thread_getstack(smal_thread *t, void **addrp, size_t *sizep)
{
  // FIXME: // return pthread_getstack(t->thread, addrp, sizep);
  return _smal_thread_getstack_main(t, addrp, sizep);
}

int smal_thread_each(int (*func)(smal_thread *t, void *arg), void *arg)
{
  int result = 0;
  smal_thread *t;
  
  pthread_rwlock_rdlock(&thread_list_lock);
  smal_dllist_each(&thread_list, t); {
    result = func(t, arg);
    if ( result < 0 ) break;
  } smal_dllist_each_end();
  pthread_rwlock_unlock(&thread_list_lock);

  return result;
}

int smal_thread_do_once(smal_thread_once *once, void (*init_routine)())
{
  return smal_ASSERT(pthread_once(once, init_routine), == 0);
}

#else /* ! SMAL_THREAD */

/* NOT THREAD-SAFE */

void smal_thread_init()
{
  thread_inited = 1;
  /* NOTHING */
}

smal_thread *smal_thread_self()
{
  return &thread_main;
}

int smal_thread_getstack(smal_thread *t, void **addrp, size_t *sizep)
{
  assert(t == &thread_main);
  return _smal_thread_getstack_main(t, addrp, sizep);
}

int smal_thread_each(int (*func)(smal_thread *t, void *arg), void *arg)
{
  return func(&thread_main, arg);
}

int smal_thread_do_once(smal_thread_once *once, void (*init_routine)())
{
  if ( ! *once ) {
    *once = 1;
    init_routine();
  }
  return 0;
}

#endif

void smal_thread_save_registers(smal_thread *thread)
{
  setjmp(thread->registers._jb);
}

int smal_thread_mutex_init(smal_thread_mutex *mutex)
{
#if SMAL_THREAD_MUTEX_DEBUG >= 2
#if SMAL_PTHREAD
  if ( ! thread_inited ) smal_thread_init();
#else
  static int once;
  if ( ! once ) {
    once = 1;
    atexit(mutex_stats);
  }
#endif
#endif

#if SMAL_THREAD_MUTEX_DEBUG
  ++ mutex_n;
#if SMAL_THREAD_MUTEX_DEBUG >= 3
  fprintf(stderr, "\n  t@%p s_t_m_i(%p) = %lu\n", smal_thread_self(), mutex, (unsigned long) mutex_n);
#endif
#if ! SMAL_PTHREAD
  *mutex = 0;
#endif
#endif

#if SMAL_PTHREAD
  return pthread_mutex_init(mutex, 0);
#else
  return 0;
#endif
}

int smal_thread_mutex_destroy(smal_thread_mutex *mutex)
{
#if SMAL_THREAD_MUTEX_DEBUG
  ++ mutex_destroy_n;
#if SMAL_THREAD_MUTEX_DEBUG >= 3
  fprintf(stderr, "\n  t@%p s_t_m_d(%p) = %lu\n", smal_thread_self(), mutex, (unsigned long) mutex_destroy_n);
#endif
#if ! SMAL_PTHREAD
  *mutex = 0;
#endif
#endif

#if SMAL_PTHREAD
  return pthread_mutex_destroy(mutex);
#else
  return 0;
#endif
}

int smal_thread_mutex_lock(smal_thread_mutex *mutex)
{
  int result = 0;

#if SMAL_THREAD_MUTEX_DEBUG >= 3
  fprintf(stderr, "\n  t@%p s_t_m_l %p ...\n", smal_thread_self(), mutex);
#endif

#if SMAL_PTHREAD
  result = pthread_mutex_lock(mutex);
#endif

#if SMAL_THREAD_MUTEX_DEBUG
  ++ mutex_lock_n;
#if SMAL_THREAD_MUTEX_DEBUG >= 3
  fprintf(stderr, "\n  t@%p s_t_m_l %p {\n", smal_thread_self(), mutex);
#endif
#if ! SMAL_PTHREAD
  if ( *mutex )
    abort();
  ++ *mutex;
  result = 0;
#endif
#endif

  return result;
}

int smal_thread_mutex_unlock(smal_thread_mutex *mutex)
{
#if SMAL_THREAD_MUTEX_DEBUG
  ++ mutex_unlock_n;
#if SMAL_THREAD_MUTEX_DEBUG >= 3
  fprintf(stderr, "\n  t@%p s_t_m_u %p  }\n", smal_thread_self(), mutex);
#endif
#if ! SMAL_PTHREAD
  if ( ! *mutex )
    abort();
  -- *mutex;
#endif
#endif

#if SMAL_PTHREAD
  return pthread_mutex_unlock(mutex);
#else
  return 0;
#endif
}


