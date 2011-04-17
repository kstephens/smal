#include <stdlib.h> /* malloc(), free() */
#include <sys/mman.h> /* mmap(), munmap() */
#include <stdio.h> /* perror() */
#include <assert.h>

int main(int argc, char **argv)
{
  void *addr, *addr2;
  size_t size = 16 * 1024;

  addr = mmap((void*) 0, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, (off_t) 0);
  assert(addr != MAP_FAILED);

  /* Assert: memory mapped twice does not error. */
  addr2 = mmap(addr + (size / 2), size / 2, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, (off_t) 0);
  assert(addr2 != MAP_FAILED);

  return 0;
}
