/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#ifndef _SMAL_DLLIST_H
#define _SMAL_DLLIST_H

typedef struct smal_dllist_head 
{
  struct smal_dllist_head *next, *prev;
} smal_dllist_head;

#define smal_dllist_init(X) (X)->next = (X)->prev = (void*) (X)

// Assumes X is empty.
// Assumes H is not empty.
#define smal_dllist_insert(H,X)			\
  do {						\
    (X)->prev = (void*) (H);			\
    (X)->next = (void*) ((H)->next);		\
    (H)->next->prev = (void*) (X);		\
    (H)->next = (void*) (X);			\
  } while (0)

// Inserts all of B including B after A.
#define smal_dllist_insert_all(A,B)		   \
  do {						   \
    smal_dllist_head				   \
      *_a_next = (void*) ((A)->next),		   \
      *_b_prev = (void*) ((B)->prev);		   \
    (B)->prev = (void*) (A);			   \
    (A)->next = (void*) (B);			   \
    _b_prev->next = (void*) _a_next;		   \
    _a_next->prev = (void*) _b_prev;		   \
  } while ( 0 )

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

// Append B to A, empties B.
// Assume neither A or B is empty.
#define smal_dllist_append(A, B)			\
  do {							\
    smal_dllist_head *_b_next = (void*) ((B)->next);	\
    if ( _b_next != (void*) (B) ) {			\
      smal_dllist_head *_b_prev = (void*) ((B)->prev);	\
      smal_dllist_head *_a_prev = (void*) ((A)->prev);	\
      (A)->prev = (void*) _b_prev;			\
      _a_prev->next = _b_next;				\
      _b_next->prev = _a_prev;				\
      _b_prev->next = (void*) (A);			\
      smal_dllist_init(B);				\
    }							\
  } while(0)

#endif

