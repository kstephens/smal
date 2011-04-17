/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#ifndef _SMAL_REFERENCE_H
#define _SMAL_REFERENCE_H

#include "smal/thread.h"

typedef struct smal_reference smal_reference;
typedef struct smal_reference_list smal_reference_list;
typedef struct smal_reference_queue smal_reference_queue;
typedef struct smal_reference_queue_list smal_reference_queue_list;

struct smal_reference {
  void *referred;
  smal_reference_queue_list *reference_queue_list;
  void *data;
  smal_thread_mutex mutex;
};

struct smal_reference_list {
  smal_reference *reference;
  smal_reference_list *next;
};

struct smal_reference_queue {
  smal_reference_list *reference_list;
  void *data;
  smal_thread_mutex mutex;
};

struct smal_reference_queue_list {
  smal_reference_queue *reference_queue;
  smal_reference_queue_list *next;
};

smal_reference * smal_reference_create_weak(void *referred, smal_reference_queue *ref_queue);
void* smal_reference_referred(smal_reference *weak);

smal_reference_queue *smal_reference_queue_create();
smal_reference *smal_reference_queue_take(smal_reference_queue *ref_queue);

void smal_reference_before_sweep(); /* Call from smal_collect_before_sweep() */

#endif
