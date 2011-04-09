/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#ifndef _SMAL_THREAD_H
#define _SMAL_THREAD_H

#ifndef SMAL_PTHREAD
#define SMAL_PTHREAD 0
#endif

#if SMAL_PTHREAD
#include <pthread.h>
#endif

typedef struct smal_thread {
  struct smal_thread *next, *prev;
#if SMAL_PTHREAD
  pthread_t thread;
#else
  void *thread;
#endif
  void *roots;
} smal_thread;

void smal_thread_init();
smal_thread *smal_thread_self();
int smal_thread_getstack(smal_thread *t, void **addrp, size_t *sizep);
void smal_thread_each(void (*func)(smal_thread *t, void *arg), void *arg);

#if SMAL_PTHREAD
typedef pthread_mutex_t smal_mutex;
#else
typedef int smal_mutex;
#endif

void smal_mutex_init(smal_mutex *m);
int smal_mutex_lock(smal_mutex *m);
int smal_mutex_unlock(smal_mutex *m);

#endif
