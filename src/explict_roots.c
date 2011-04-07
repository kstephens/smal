
#include "smal/smal.h"
#include "smal/explicit_roots.h"
#include <stdio.h>

/* NOT THREAD-SAFE */
smal_roots *_smal_roots;

smal_roots *smal_roots_current()
{
  return _smal_roots;
}

void smal_roots_set_current(smal_roots *roots)
{
  _smal_roots = roots;
}

void smal_roots_mark_chain(smal_roots *roots)
{
  if ( ! roots )
    roots = smal_roots_current();

  while ( roots ) {
    int i;
    /* fprintf(stderr, "  roots %p [%d]\n", roots, roots->_n); */
    for ( i = 0; i < roots->_n; ++ i ) {
      void *ptr = * (void**) roots->_bindings[i];
      /* fprintf(stderr, "  mark %p => %p\n", roots->_bindings[i], ptr); */
      smal_mark_ptr(ptr);
    }
    roots = roots->_next;
  }
}


