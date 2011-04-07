/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#ifndef _SMAL_THREAD_H
#define _SMAL_THREAD_H

#include "smal/smal.h"

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

void smal_thread_each(void (*func)(smal_thread *t, void *arg), void *arg);

#endif
