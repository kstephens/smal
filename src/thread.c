
#include "smal/smal.h"
#include "smal/thread.h"
#include "smal/dllist.h"

#include <assert.h>
#include <stdlib.h> /* malloc(), free() */
#include <stdio.h>

static smal_thread thread_main;

#if SMAL_PTHREAD

static smal_thread thread_list;

static pthread_key_t roots_key;

static pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;

// static
void thread_exit(void *arg)
{
  smal_thread *t = arg;
  fprintf(stderr, "  thread_exit() %p\n", (void*) pthread_self());
  pthread_mutex_lock(&thread_list_mutex);
  smal_DLLIST_DELETE(t);
  free(t);
  pthread_mutex_unlock(&thread_list_mutex);
}

static
void thread_init(smal_thread *t)
{
  pthread_mutex_lock(&thread_list_mutex);

  t->thread = pthread_self();
  t->roots = 0;
  
  smal_DLLIST_INSERT(&thread_list, t);
  
  pthread_setspecific(roots_key, t);

  pthread_mutex_unlock(&thread_list_mutex);
}

static
void thread_prepare()
{
  fprintf(stderr, "  thread_prepare() %p\n", (void*) pthread_self());
}

static
void thread_parent()
{
  fprintf(stderr, "  thread_parent() %p\n", (void*) pthread_self());
}

static
void thread_child()
{
  smal_thread *t;

  fprintf(stderr, "  thread_child() %p\n", (void*) pthread_self());

  t = malloc(sizeof(*t));
  thread_init(t);

  fprintf(stderr, "  thread_child() %p = %p\n", (void*) pthread_self(), t);
}

void smal_thread_init()
{
  if ( ! roots_key ) {
    pthread_mutex_init(&thread_list_mutex, 0);
    pthread_mutex_lock(&thread_list_mutex);
    
    smal_DLLIST_INIT(&thread_list);

    pthread_key_create(&roots_key, 0);

    pthread_atfork(thread_prepare,
		   thread_parent,
		   thread_child);
    
    pthread_mutex_unlock(&thread_list_mutex);

    thread_init(&thread_main);
  }

}

smal_thread *smal_thread_self()
{
  if ( ! roots_key )
    smal_thread_init();

  {
    smal_thread *t = pthread_getspecific(roots_key);
    if ( ! t ) {
      thread_child();
      t = pthread_getspecific(roots_key);
    }
    assert(t);
    fprintf(stderr, "smal_thread_self() = %p: %p\n", t, (void*) t->thread);
    return t;
  }
}

void smal_thread_each(void (*func)(smal_thread *t, void *arg), void *arg)
{
  pthread_mutex_lock(&thread_list_mutex);

  {
    smal_thread *t;
    
    smal_DLLIST_each(&thread_list, t) {
      func(t, arg);
    } smal_DLLIST_each_END();
  }

  pthread_mutex_unlock(&thread_list_mutex);
}

#else

/* NOT THREAD-SAFE */

void smal_thread_init()
{
  /* NOTHING */
}

smal_roots *smal_roots_self()
{
  return &thread_main;
}

void smal_thread_each(void (*func)(smal_thread *t, void *arg), void *arg)
{
  func(&thread_main, arg);
}

#endif

