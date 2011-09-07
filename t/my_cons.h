/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

#include "smal/smal.h"
#include "smal/thread.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h> /* memset() */
#include <unistd.h> /* getpid() */
#include <assert.h>

typedef void *my_oop;
typedef struct my_cons {
  my_oop car, cdr;
} my_cons;

static smal_type *my_cons_type;

static void * my_cons_mark (void *ptr)
{
  smal_mark_ptr(ptr, ((my_cons *) ptr)->car);
  return ((my_cons *) ptr)->cdr;
}

void my_print_stats()
{
  smal_stats stats = { 0 };
  int i;

  smal_global_stats(&stats);
  for ( i = 0; smal_stats_names[i]; ++ i ) {
    fprintf(stdout, "  %16lu %s\n", (unsigned long) (((size_t*) &stats)[i]), smal_stats_names[i]);
  }
  fprintf(stderr, "\n");
}

