#ifndef _SMAL_DLLIST_H
#define _SMAL_DLLIST_H

#define smal_DLLIST_INIT(X) (X)->next = (X)->prev = (void*) X

#define smal_DLLIST_INSERT(H,X)			\
  do {						\
    (X)->prev = (void*) (H);			\
    (X)->next = (void*) ((H)->next);		\
    (H)->next->prev = (void*) (X);		\
    (H)->next = (void*) (X);			\
  } while (0)

#define smal_DLLIST_DELETE(X)			\
  do {						\
    (X)->prev->next = (X)->next;		\
    (X)->next->prev = (X)->prev;		\
  } while(0)

#define smal_DLLIST_each(HEAD, X)			\
  do {							\
    void *X##_next = (HEAD)->next;			\
    while ( (void*) (X##_next) != (void*) (HEAD) ) {	\
      (X) = X##_next;					\
      X##_next = (X)->next;				\
  
#define smal_DLLIST_each_END()			\
    }						\
  } while(0)					\

#endif
