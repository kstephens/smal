#ifndef _SMAL_EXPLICIT_ROOTS_H
#define _SMAL_EXPLICIT_ROOTS_H

typedef struct smal_roots {
  void **_bindings;
  int _n;
  struct smal_roots *_next;
} smal_roots;


smal_roots *smal_roots_current();
void smal_roots_set_current(smal_roots *);

#define smal_roots_begin(N) \
  smal_roots _roots = { _bindings, N, smal_roots_current() }; \
  smal_roots_set_current(&_roots)

#define smal_roots_end() \
 smal_roots_set_current(_roots._next); }

#define smal_roots_0() \
  { void *_bindings[] = { }; smal_roots_begin(0);
#define smal_roots_1(_1) \
  { void *_bindings[] = { &_1 }; smal_roots_begin(1)
#define smal_roots_2(_1,_2) \
  { void *_bindings[] = { &_1, &_2 }; smal_roots_begin(2)
#define smal_roots_3(_1,_2,_3) \
  { void *_bindings[] = { &_1, &_2, &_3 }; smal_roots_begin(3)
#define smal_roots_4(_1,_2,_3,_4) \
  { void *_bindings[] = { &_1, &_2, &_3, &_4 }; smal_roots_begin(4)
#define smal_roots_5(_1,_2,_3,_4,_5) \
  { void *_bindings[] = { &_1, &_2, &_3, &_4, &_5 }; smal_roots_begin(5)

void smal_roots_mark_chain(smal_roots *roots);

#endif
