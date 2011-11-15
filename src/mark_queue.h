/*
  SMAL
  Copyright (c) 2011 Kurt A. Stephens
*/

/* Optionally included directly into smal.c */

#define smal_mark_queue_SIZE 1020

typedef struct smal_mark_queue {
  void **front; // , **back;
  void *ptrs[smal_mark_queue_SIZE];
  void *back[0];
  struct smal_mark_queue *prev;
} smal_mark_queue;

static smal_mark_queue *mark_queue;
static int mark_queue_depth, mark_queue_depth_max;
static int mark_queue_add_depth;

static inline
void smal_mark_queue_free()
{
  smal_mark_queue *s = mark_queue;
#if 0
  fprintf(stderr, "  s_m_q_f() ");
#endif
  while ( s ) {
    smal_mark_queue *s_prev = s->prev;
    free(s);
    s = s_prev;
  }
  mark_queue = 0;
}

static inline
void smal_mark_queue_mark(int);

static inline
void smal_mark_queue_new()
{
#if 0
  fprintf(stderr, "  s_m_q_n()\n");
#endif
  smal_mark_queue *new_s = malloc(sizeof(*new_s));
  new_s->front = new_s->ptrs;
  // new_s->back = new_s->ptrs + smal_mark_queue_SIZE;
  new_s->prev = mark_queue;
  mark_queue = new_s;
}

static inline
void smal_mark_queue_start()
{
  mark_queue_depth_max = mark_queue_depth = mark_queue_add_depth = 0;
  smal_mark_queue_free();
  smal_mark_queue_new();
}

static inline
void smal_mark_queue_mark(int one)
{
  while ( mark_queue ) {
    while ( mark_queue->front > mark_queue->ptrs ) {
      void *ptr = *(-- mark_queue->front);
      void *referrer = *(-- mark_queue->front);
      // assert(mark_queue->front >= mark_queue->ptrs);
#if 0
      -- mark_queue_depth;
      // fprintf(stderr, "%*s m(%p)\n", -- mark_queue_depth, " ", ptr);
#endif
      _smal_mark_ptr_tail(referrer, ptr);
      if ( one )
	return;
    }
    if ( mark_queue->prev ) {
      smal_mark_queue *s_prev = mark_queue->prev;
      free(mark_queue);
      mark_queue = s_prev;
#if 0
      fprintf(stderr, "%*s F\n", mark_queue_depth, " ");
#endif
    } else {
      break;
    }
    // fprintf(stderr, "}");
  }
#if 0
  fprintf(stderr, " DONE (%lu max depth)\n", (unsigned long) mark_queue_depth_max);
#endif
}

static inline
void smal_mark_queue_mark_all()
{
#if 0
  fprintf(stderr, "  s_m_q_m_a() ");
#endif
  smal_mark_queue_mark(0);
#if 0
  fprintf(stderr, " DONE (%lu max depth)\n", (unsigned long) mark_queue_depth_max);
#endif
}

static inline
void smal_mark_queue_add(void *referrer, int ptr_n, void **ptrs, int pointers_to_pointersQ)
{
#if 0
  fprintf(stderr, "%*s s_m_q_a(%p, %d, %p, %d)\n", mark_queue_add_depth ++, " ", referrer, ptr_n, ptrs, pointers_to_pointersQ);
#endif
  // if ( ptr_n > 50 ) stop_here();
  if ( pointers_to_pointersQ ) {
    while ( -- ptr_n >= 0 ) {
      void **ptr_p = *(ptrs ++);
      void *ptr = ptr_p ? *ptr_p : 0;
      if ( ptr ) {
	if ( mark_queue->front >= mark_queue->back )
	  smal_mark_queue_new();
	*(mark_queue->front ++) = referrer;
	*(mark_queue->front ++) = ptr;
#if 0
	// fprintf(stderr, "%*s q(%p)\n", mark_queue_depth, " ", ptr);
	if ( mark_queue_depth_max < ++ mark_queue_depth ) mark_queue_depth_max = mark_queue_depth;
#endif
      }
    }
  } else {
    while ( -- ptr_n >= 0 ) {
      void *ptr = *(ptrs ++);
      if ( ptr ) {
	if ( mark_queue->front >= mark_queue->back )
	  smal_mark_queue_new();
	*(mark_queue->front ++) = referrer;
	*(mark_queue->front ++) = ptr;
#if 0
	// fprintf(stderr, "%*s q(%p)\n", mark_queue_depth , " ", ptr);
	if ( mark_queue_depth_max < ++ mark_queue_depth ) mark_queue_depth_max = mark_queue_depth;
#endif
      }
    }
  }
#if 0
  fprintf(stderr, "%*s s_m_q_a(%p, ...): DONE\n", -- mark_queue_add_depth, " ", referrer);
#endif
}

