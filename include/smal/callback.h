/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/
#ifndef smal_CALLBACK_H
#define smal_CALLBACK_H

#define smal_callback_DECL(X) void X (void *data)
typedef struct smal_callback {
  struct smal_callback *next, *prev;
  smal_callback_DECL((*func));
  void *data;
} smal_callback;

void* smal_callback_add(smal_callback *base, smal_callback_DECL((*func)), void *data);
void  smal_callback_remove(smal_callback *base, void *cb);
void  smal_callback_empty(smal_callback *base);
void  smal_callback_invoke(smal_callback *base);

#endif
