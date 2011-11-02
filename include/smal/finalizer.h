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
smal_finalizer * smal_finalizer_remove_all(void *referred);
smal_finalizer * smal_finalizer_copy_finalizers(void *ptr, void *to_ptr);

smal_type *smal_finalizer_type();
smal_type *smal_finalized_type();

void smal_finalizer_before_mark(); /* Call from smal_collect_before_mark() */
void smal_finalizer_after_mark(); /* Call from smal_collect_after_mark() */
void smal_finalizer_before_sweep(); /* Call from smal_collect_before_sweep() */
void smal_finalizer_after_sweep(); /* Call from smal_collect_after_sweep() */

/** Number of finalizers to execute in smal_finalizers_after_sweep().
    Defaults to 0 (ALL).
 */
extern int smal_finalizer_sweep_amount;
/** Execute n finalizers.  Returns 0, if there are no remaining queued finalizers. */
int smal_finalizer_sweep_some(int n);

#endif
