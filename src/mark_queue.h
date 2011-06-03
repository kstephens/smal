/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

/* Optionally included directly into smal.c */

#define smal_mark_queue_SIZE 256

typedef struct smal_mark_queue {
  struct smal_mark_queue *next, *prev;
  void **front, **back;
  void *ptrs[smal_mark_queue_SIZE + 2];
} smal_mark_queue;

static smal_mark_queue mark_queue = { &mark_queue, &mark_queue };
static size_t mark_queue_depth, mark_queue_depth_max;

static inline
void smal_mark_queue_start()
{
#if 0
  if ( ! mark_queue.next )
    smal_dllist_init(&mark_queue);
#else
  assert(mark_queue.next);
#endif
  mark_queue_depth_max = mark_queue_depth;
}

static inline
void smal_mark_queue_add(smal_mark_queue *self, void *referrer, int ptr_n, void **ptrs, int pointers_to_pointersQ)
{
  smal_mark_queue *s = self->prev;
  void **ptr_n_p = 0;

#if 0
  if ( ptr_n > 1 ) {
    fprintf(stderr, "q(%d,%d)", ptr_n, pointers_to_pointersQ);
  }
#endif

  while ( ptr_n > 0 ) {
    if ( ! (s != self && s->back < s->ptrs + smal_mark_queue_SIZE) ) {
      smal_mark_queue *new_s = malloc(sizeof(*new_s));
      new_s->front = new_s->back = new_s->ptrs;
      smal_dllist_init(new_s);
      smal_dllist_insert_all(self->prev, new_s);
      // fprintf(stderr, "{");
      s = new_s;
    }
    *(s->back ++) = referrer;
    *(ptr_n_p = s->back ++) = 0;
    if ( pointers_to_pointersQ ) {
      while ( ptr_n > 0 && s->back < s->ptrs + smal_mark_queue_SIZE ) {
	void **ptr_p = *(ptrs ++);
	void *ptr = ptr_p ? *ptr_p : 0;
	if ( ptr ) {
	  // fprintf(stderr, "m");
	  *(s->back ++) = ptr;
	  ++ *ptr_n_p;
#if 0
	  if ( mark_queue_depth_max < ++ mark_queue_depth )
	    mark_queue_depth_max = mark_queue_depth;
#endif
	}
	-- ptr_n;
      }
    } else {
      while ( ptr_n > 0 && s->back < s->ptrs + smal_mark_queue_SIZE ) {
	void *ptr = *(ptrs ++);
	if ( ptr ) {
	  // fprintf(stderr, "m");
	  *(s->back ++) = ptr;
	  ++ *ptr_n_p;
#if 0
	  if ( mark_queue_depth_max < ++ mark_queue_depth )
	    mark_queue_depth_max = mark_queue_depth;
#endif
	}
	-- ptr_n;
      }
    }
  }
}

static inline
void smal_mark_queue_add_one(void *referrer, void *ptr)
{
  smal_mark_queue_add(&mark_queue, referrer, 1, &ptr, 0);
}

static inline
void smal_mark_queue_mark_all(smal_mark_queue *self)
{
  smal_mark_queue *s;
  // fprintf(stderr, "  s_m_s_a() ");
  while ( (s = self->next) != self ) {
    while ( s->front < s->back ) {
      void *referrer = *(s->front ++);
      int n_ptrs = (size_t) *(s->front ++);
      while ( n_ptrs -- > 0 ) {
      void *ptr;
      // fprintf(stderr, "M");
#if 0
      -- mark_queue_depth;
#endif
      ptr = *(s->front ++);
      if ( s->front == s->back ) 
	s->front = s->back = s->ptrs;
      _smal_mark_ptr(referrer, ptr);
    }
    }
    smal_dllist_delete(s);
    free(s);
    // fprintf(stderr, "}");
  }
  // fprintf(stderr, " DONE (%lu max depth)\n", (unsigned long) mark_queue_depth_max);
  assert(mark_queue_depth == 0);
}

