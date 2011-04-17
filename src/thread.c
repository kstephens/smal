/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/thread.h"
#include "smal/dllist.h"

#include <assert.h>
#include <stdlib.h> /* malloc(), free() */
#include <string.h> /* memset() */
#include <stdio.h>

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
  fprintf(stderr, "  %p getstack: %p[0x%lx] %p\n", t, *addrp, (unsigned long) *sizep, *addrp + *sizep);
  return 0;
}

#if SMAL_PTHREAD

static smal_thread thread_list;

static pthread_key_t roots_key;

static pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;

#if 0
static
void thread_exit(void *arg)
{
  smal_thread *t = arg;
  // fprintf(stderr, "  thread_exit() %p\n", (void*) pthread_self());
  pthread_mutex_lock(&thread_list_mutex);
  smal_DLLIST_DELETE(t);
  free(t);
  pthread_mutex_unlock(&thread_list_mutex);
}
#endif

static
void thread_init(smal_thread *t)
{
  pthread_mutex_lock(&thread_list_mutex);

  t->thread = pthread_self();
  t->roots = 0;
  
  smal_dllist_insert(&thread_list, t);
  
  pthread_setspecific(roots_key, t);

  pthread_mutex_unlock(&thread_list_mutex);
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
  smal_thread *t;

  // fprintf(stderr, "  thread_child() %p\n", (void*) pthread_self());

  t = malloc(sizeof(*t));
  memset(t, 0, sizeof(*t));
  thread_init(t);

  // fprintf(stderr, "  thread_child() %p = %p\n", (void*) pthread_self(), t);
}

static pthread_once_t _smal_thread_is_initialized = PTHREAD_ONCE_INIT;
static
void _smal_thread_init()
{
  if ( ! thread_inited ) {
    pthread_mutex_init(&thread_list_mutex, 0);
    pthread_mutex_lock(&thread_list_mutex);
    
    smal_dllist_init(&thread_list);

    pthread_key_create(&roots_key, 0);

    pthread_atfork(thread_prepare,
		   thread_parent,
		   thread_child);
    
    pthread_mutex_unlock(&thread_list_mutex);

    thread_init(&thread_main);

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
  if ( ! thread_inited )
    smal_thread_init();

  {
    smal_thread *t = pthread_getspecific(roots_key);
    if ( ! t ) {
      thread_child();
      t = pthread_getspecific(roots_key);
    }
    assert(t);
    // fprintf(stderr, "smal_thread_self() = %p: %p\n", t, (void*) t->thread);
    return t;
  }
}

int smal_thread_getstack(smal_thread *t, void **addrp, size_t *sizep)
{
  // FIXME: // return pthread_getstack(t->thread, addrp, sizep);
  return _smal_thread_getstack_main(t, addrp, sizep);
}

void smal_thread_each(void (*func)(smal_thread *t, void *arg), void *arg)
{
  pthread_mutex_lock(&thread_list_mutex);

  {
    smal_thread *t;
    
    smal_dllist_each(&thread_list, t); {
      func(t, arg);
    } smal_dllist_each_end();
  }

  pthread_mutex_unlock(&thread_list_mutex);
}

int smal_thread_do_once(smal_thread_once *once, void (*init_routine)())
{
  return pthread_once(once, init_routine);
}

int smal_thread_mutex_init(smal_thread_mutex *mutex)
{
  return pthread_mutex_init(mutex, 0);
}

int smal_thread_mutex_lock(smal_thread_mutex *mutex)
{
  return pthread_mutex_lock(mutex);
}

int smal_thread_mutex_unlock(smal_thread_mutex *mutex)
{
  return pthread_mutex_unlock(mutex);
}

#else

/* NOT THREAD-SAFE */

void smal_thread_init()
{
  thread_inited = 1;
  /* NOTHING */
}

smal_roots *smal_roots_self()
{
  return &thread_main;
}

int smal_thread_getstack(smal_thread *t, void **addrp, size_t *sizep)
{
  assert(t == &thread_main);
  return _smal_thread_getstack_main(t, addrp, sizep);
}

void smal_thread_each(void (*func)(smal_thread *t, void *arg), void *arg)
{
  func(&thread_main, arg);
}

int smal_thread_do_once(smal_thread_once *once, void (*init_routine)())
{
  if ( ! *once ) {
    *once = 1;
    init_routine();
  }
}

int smal_thread_mutex_init(smal_thread_mutex *mutex)
{
  return 0;
}

int smal_thread_mutex_lock(smal_thread_mutex *mutex)
{
  return 0;
}

int smal_thread_mutex_unlock(smal_thread_mutex *mutex)
{
  return 0;
}

#endif

