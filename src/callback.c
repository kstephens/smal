#include "smal/callback.h"
#include "smal/dllist.h"
#include "smal/thread.h"
#include <stdlib.h> /* malloc(), free() */

void smal_callbacks_init(smal_callbacks *cbs)
{
  smal_dllist_init(cbs);
  smal_thread_mutex_init(&cbs->_mutex);
}

void* smal_callbacks_add(smal_callbacks *cbs, void (*func)(void *data), void *data)
{
  smal_callback *cb = malloc(sizeof(*cb));
  if ( cb ) {
    smal_dllist_init(cb);
    cb->func = func;
    cb->data = data;
    smal_thread_mutex_lock(&cbs->_mutex);
    smal_dllist_insert(cbs, cb);
    smal_thread_mutex_unlock(&cbs->_mutex);
  }
  return cb;
}

void smal_callbacks_remove(smal_callbacks *cbs, void *handle)
{
  smal_callback *cb = handle;
  if ( cb ) {
    smal_thread_mutex_lock(&cbs->_mutex);
    smal_dllist_delete(cb);
    smal_thread_mutex_unlock(&cbs->_mutex);
    free(cb);
  }
}

void  smal_callbacks_empty(smal_callbacks *cbs)
{
  smal_callback *entry;
  smal_thread_mutex_lock(&cbs->_mutex);
  smal_dllist_each(cbs, entry); {
    smal_callbacks_remove(cbs, entry);
  } smal_dllist_each_end(); 
  smal_thread_mutex_unlock(&cbs->_mutex);
}

void  smal_callbacks_invoke(smal_callbacks *cbs)
{
  smal_callback *entry;
  smal_thread_mutex_lock(&cbs->_mutex);
  smal_dllist_each(cbs, entry); {
    entry->func(entry->data);
  } smal_dllist_each_end(); 
  smal_thread_mutex_unlock(&cbs->_mutex);
}

