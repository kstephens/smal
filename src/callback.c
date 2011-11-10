#include "smal/callback.h"
#include "smal/dllist.h"
#include <stdlib.h> /* malloc(), free() */

void* smal_callback_add(smal_callback *base, void (*func)(void *data), void *data)
{
  smal_callback *cb = malloc(sizeof(*cb));
  if ( cb ) {
    smal_dllist_init(cb);
    cb->func = func;
    cb->data = data;
    smal_dllist_insert(base, cb);
  }
  return cb;
}

void smal_callback_remove(smal_callback *base, void *handle)
{
  smal_callback *cb = handle;
  if ( cb ) {
    smal_dllist_delete(cb);
    free(cb);
  }
}

void  smal_callback_empty(smal_callback *base)
{
  smal_callback *entry;
  smal_dllist_each(base, entry); {
    smal_callback_remove(base, entry);
  } smal_dllist_each_end(); 
}

void  smal_callback_invoke(smal_callback *base)
{
  smal_callback *entry;
  smal_dllist_each(base, entry); {
    entry->func(entry->data);
  } smal_dllist_each_end(); 
}

