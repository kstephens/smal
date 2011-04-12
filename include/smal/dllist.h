/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#ifndef _SMAL_DLLIST_H
#define _SMAL_DLLIST_H

#define smal_dllist_init(X) (X)->next = (X)->prev = (void*) X

#define smal_dllist_insert(H,X)			\
  do {						\
    (X)->prev = (void*) (H);			\
    (X)->next = (void*) ((H)->next);		\
    (H)->next->prev = (void*) (X);		\
    (H)->next = (void*) (X);			\
  } while (0)

#define smal_dllist_delete(X)			\
  do {						\
    (X)->prev->next = (X)->next;		\
    (X)->next->prev = (X)->prev;		\
  } while(0)

#define smal_dllist_each(HEAD, X)			\
  do {							\
    void *X##_next = (HEAD)->next;			\
    while ( (void*) (X##_next) != (void*) (HEAD) ) {	\
      (X) = X##_next;					\
      X##_next = (X)->next;				\
  
#define smal_dllist_each_end()			\
    }						\
  } while(0)					\

#endif
