/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#ifndef _SMAL_FINALIZER_H
#define _SMAL_FINALIZER_H

typedef struct smal_finalizer smal_finalizer;

struct smal_finalizer {
  void *referred;
  void *data;
  void (*func)(smal_finalizer *finalizer);
  smal_finalizer *next;
};

smal_finalizer * smal_finalizer_create(void *referred, void (*func)(smal_finalizer *finalizer));
void smal_finalizer_remove(smal_finalizer *finalizer);

void smal_finalizer_before_sweep(); /* Call from smal_collect_before_sweep() */
void smal_finalizer_after_sweep(); /* Call from smal_collect_after_sweep() */

#endif
