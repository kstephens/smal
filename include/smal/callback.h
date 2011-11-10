/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/
#ifndef smal_CALLBACK_H
#define smal_CALLBACK_H

#include "smal/smal.h"
#include "smal/thread.h"

#define smal_callback_DECL(X) void X (void *data)
typedef struct smal_callback {
  struct smal_callback *next, *prev;
  smal_callback_DECL((*func));
  void *data;
} smal_callback;

typedef struct smal_callbacks {
  struct smal_callback *next, *prev;
  smal_thread_mutex _mutex;
} smal_callbacks;

void  smal_callbacks_init(smal_callbacks *cbs);
void* smal_callbacks_add(smal_callbacks *cbs, smal_callback_DECL((*func)), void *data);
void  smal_callbacks_remove(smal_callbacks *cbs, void *cb);
void  smal_callbacks_empty(smal_callbacks *cbs);
void  smal_callbacks_invoke(smal_callbacks *cbs);

#endif
