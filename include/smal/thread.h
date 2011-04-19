/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#ifndef _SMAL_THREAD_H
#define _SMAL_THREAD_H

#include "smal/assert.h"

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
#define smal_thread_mutex_init(M)    smal_ASSERT(pthread_mutex_init(M, 0), == 0)
#define smal_thread_mutex_destroy(M) smal_ASSERT(pthread_mutex_destroy(M), == 0)
#define smal_thread_mutex_lock(M)    smal_ASSERT(pthread_mutex_lock(M),    == 0)
#define smal_thread_mutex_unlock(M)  smal_ASSERT(pthread_mutex_unlock(M),  == 0)
#endif
#else
#if ! SMAL_THREAD_MUTEX_DEBUG
#define smal_thread_mutex_init(M)    ((void) (M))
#define smal_thread_mutex_destroy(M) ((void) (M))
#define smal_thread_mutex_lock(M)    ((void) (M))
#define smal_thread_mutex_unlock(M)  ((void) (M))
#endif
#endif

#define smal_WITH_MUTEX(M, TYPE, EXPR)		\
  ({						\
    TYPE _smal_W_M_result;			\
    smal_thread_mutex_lock(M);			\
    _smal_W_M_result = (EXPR);			\
    smal_thread_mutex_unlock(M);		\
    _smal_W_M_result;				\
  })

/******************************************************/

#if SMAL_PTHREAD
typedef pthread_rwlock_t smal_thread_rwlock;
#define smal_thread_rwlock_init(L)    smal_ASSERT(pthread_rwlock_init(L, 0), == 0)
#define smal_thread_rwlock_destroy(L) smal_ASSERT(pthread_rwlock_destroy(L), == 0)
#define smal_thread_rwlock_rdlock(L)  smal_ASSERT(pthread_rwlock_rdlock(L),  == 0)
#define smal_thread_rwlock_wrlock(L)  smal_ASSERT(pthread_rwlock_wrlock(L),  == 0)
#define smal_thread_rwlock_unlock(L)  smal_ASSERT(pthread_rwlock_unlock(L),  == 0)
#else
typedef struct { int _rwlock; } smal_thread_rwlock;
#define smal_thread_rwlock_init(L)    ((void) (L))
#define smal_thread_rwlock_destroy(L) ((void) (L))
#define smal_thread_rwlock_rdlock(L)  ((void) (L))
#define smal_thread_rwlock_wrlock(L)  ((void) (L))
#define smal_thread_rwlock_unlock(L)  ((void) (L))
#endif

#define smal_WITH_RDLOCK(L, TYPE, EXPR)		\
  ({						\
    TYPE _smal_W_M_result;			\
    smal_thread_rwlock_rdlock(L);		\
    _smal_W_M_result = (EXPR);			\
    smal_thread_rwlock_unlock(L);		\
    _smal_W_M_result;				\
  })

#define smal_WITH_WRLOCK(L, TYPE, EXPR)		\
  ({						\
    TYPE _smal_W_M_result;			\
    smal_thread_rwlock_wrlock(L);		\
    _smal_W_M_result = (EXPR);			\
    smal_thread_rwlock_unlock(L);		\
    _smal_W_M_result;				\
  })

/******************************************************/

typedef struct smal_thread_lock {
  int state;
  smal_thread_rwlock lock;
} smal_thread_lock;

#define smal_thread_lock_init(LOCK)		\
  do {						\
    (LOCK)->state = 0;				\
    smal_thread_rwlock_init(&(LOCK)->lock);	\
  } while ( 0 ) 

#define smal_thread_lock_destroy(LOCK)		\
  smal_thread_rwlock_destroy(&(LOCK)->lock)

#define smal_thread_lock_test(LOCK)   smal_WITH_RDLOCK(&(LOCK)->lock, int, (LOCK)->state)
#define smal_thread_lock_lock(LOCK)   smal_WITH_WRLOCK(&(LOCK)->lock, int, (LOCK)->state ++)
#define smal_thread_lock_unlock(LOCK) smal_WITH_WRLOCK(&(LOCK)->lock, int, -- (LOCK)->state)
#define smal_thread_lock_begin(LOCK)  do { if ( ! smal_thread_lock_lock(LOCK) ) {
#define smal_thread_lock_end(LOCK)    smal_thread_lock_unlock(LOCK); } } while ( 0 )

#endif
