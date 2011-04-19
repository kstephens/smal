/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#ifndef _SMAL_THREAD_H
#define _SMAL_THREAD_H

#ifndef SMAL_THREAD_MUTEX_DEBUG
#define SMAL_THREAD_MUTEX_DEBUG 0
#endif

#ifndef SMAL_PTHREAD
#define SMAL_PTHREAD 0
#endif

#include <setjmp.h>

#if SMAL_PTHREAD
#include <pthread.h>
#endif

#ifndef smal_jmp_buf
#define smal_jmp_buf jmp_buf
#endif

typedef struct smal_thread {
  struct smal_thread *next, *prev;
#if SMAL_PTHREAD
  pthread_t thread;
#else
  void *thread;
#endif
  void *roots;
  void *bottom_of_stack;
  void *top_of_stack;
  struct { jmp_buf _jb; } registers;
  void *user_data[4];
} smal_thread;

#if SMAL_PTHREAD
typedef pthread_once_t smal_thread_once;
#define smal_thread_once_INIT PTHREAD_ONCE_INIT
typedef pthread_mutex_t smal_thread_mutex;
#else
typedef int smal_thread_once;
#define smal_thread_once_INIT 0
typedef int smal_thread_mutex;
#endif

int smal_thread_do_once(smal_thread_once *once, void (*init_routine)());

void smal_thread_init(); // ???
smal_thread *smal_thread_self();
int smal_thread_getstack(smal_thread *t, void **addrp, size_t *sizep);
void smal_thread_each(void (*func)(smal_thread *t, void *arg), void *arg);

int smal_thread_mutex_init(smal_thread_mutex *m);
int smal_thread_mutex_destroy(smal_thread_mutex *m);
int smal_thread_mutex_lock(smal_thread_mutex *m);
int smal_thread_mutex_unlock(smal_thread_mutex *m);

#if SMAL_PTHREAD
#if ! SMAL_THREAD_MUTEX_DEBUG
#define smal_thread_mutex_init(M)   pthread_mutex_init(M, 0)
#define smal_thread_mutex_destroy(M)pthread_mutex_destroy(M)
#define smal_thread_mutex_lock(M)   pthread_mutex_lock(M)
#define smal_thread_mutex_unlock(M) pthread_mutex_unlock(M)
#endif
#else
#if ! SMAL_THREAD_MUTEX_DEBUG
#define smal_thread_mutex_init(M)   (void) (M)
#define smal_thread_mutex_destroy(M)(void) (M)
#define smal_thread_mutex_lock(M)   (void) (M)
#define smal_thread_mutex_unlock(M) (void) (M)
#endif
#endif

/******************************************************/

#define smal_WITH_MUTEX(M, TYPE, EXPR)		\
  ({						\
    TYPE _smal_W_M_result;			\
    smal_thread_mutex_lock(M);			\
    _smal_W_M_result = (EXPR);			\
    smal_thread_mutex_unlock(M);		\
    _smal_W_M_result;				\
  })

typedef struct smal_thread_lock {
  int lock;
  smal_thread_mutex mutex;
} smal_thread_lock;

#define smal_thread_lock_init(LOCK)		\
  do {						\
    (LOCK)->lock = 0;				\
    smal_thread_mutex_init(&(LOCK)->mutex);	\
  } while ( 0 ) 

#define smal_thread_lock_destroy(LOCK)		\
  smal_thread_mutex_destroy(&(LOCK)->mutex)

#define smal_thread_lock_test(LOCK)   smal_WITH_MUTEX(&(LOCK)->mutex, int, (LOCK)->lock)
#define smal_thread_lock_lock(LOCK)   smal_WITH_MUTEX(&(LOCK)->mutex, int, (LOCK)->lock ++)
#define smal_thread_lock_unlock(LOCK) smal_WITH_MUTEX(&(LOCK)->mutex, int, -- (LOCK)->lock)
#define smal_thread_lock_begin(LOCK)  do { if ( ! smal_thread_lock_lock(LOCK) ) {
#define smal_thread_lock_end(LOCK)    smal_thread_lock_unlock(LOCK); } } while ( 0 )

#endif
