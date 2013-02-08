#include <stdlib.h> /* malloc(), free() */
#include <pthread.h> /* pthread_rwlock_* */
#include <sys/errno.h> /* EDEADLK */
#include <stdio.h> /* perror() */
#include <assert.h>

#define my_ASSERT(X,E)							\
  (void) ({                                                              \
    int _result = (X);							\
    fprintf(stderr, "  %s:%d %s = %d\n", __FILE__, __LINE__, #X, _result); \
    if ( ! (_result E) ) {						\
      fprintf(stderr, "    FAILED: expected %s %s\n", #X, #E);		\
      abort();								\
    }									\
    _result;								\
  })

int main(int argc, char **argv)
{
  pthread_rwlock_t lock_1, lock_2;

  my_ASSERT(pthread_rwlock_init(&lock_1, 0), == 0);
  my_ASSERT(pthread_rwlock_init(&lock_2, 0), == 0);

  {
    my_ASSERT(pthread_rwlock_rdlock(&lock_1), == 0);
    
    /* rdlock() is reentrant. */
    {
      my_ASSERT(pthread_rwlock_rdlock(&lock_1), == 0);
      
      my_ASSERT(pthread_rwlock_unlock(&lock_1), == 0);
    }

    my_ASSERT(pthread_rwlock_unlock(&lock_1), == 0);
  }

  {
    my_ASSERT(pthread_rwlock_wrlock(&lock_1), == 0);
    
    /* wrlock() is not reentrant. */
    {
      my_ASSERT(pthread_rwlock_wrlock(&lock_1), == EDEADLK);
      
      // my_ASSERT(pthread_rwlock_unlock(&lock_1), == 0);
    }

    my_ASSERT(pthread_rwlock_unlock(&lock_1), == 0);
  }

  my_ASSERT(pthread_rwlock_destroy(&lock_1), == 0);
  my_ASSERT(pthread_rwlock_destroy(&lock_2), == 0);

  fprintf(stderr, "\n%s OK\n", argv[0]);
  return 0;
}
