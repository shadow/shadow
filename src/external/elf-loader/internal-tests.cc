#include <unistd.h>
#include <iostream>

bool test_alloc (void);
bool test_futex (void);
bool test_list (void);

#define RUN_TEST(name)					\
  do {							\
    bool result = test_##name ();			\
    if (!result) {ok = false;}				\
    const char *result_string = result?"PASS":"FAIL";	\
    std::cout << #name << "=" << result_string << std::endl;	\
  } while (false)

int main (int argc, char *argv[])
{
  bool ok = true;
  RUN_TEST (alloc);
  RUN_TEST (futex);
  RUN_TEST (list);
  return ok?0:1;
}

// a bunch of functions needed to link the tests correctly.
#include <linux/futex.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern "C" void system_futex_wake (uint32_t *uaddr, uint32_t val)
{
  syscall (SYS_futex, uaddr, FUTEX_WAKE, val, 0, 0, 0);
}
extern "C" void system_futex_wait (uint32_t *uaddr, uint32_t val)
{
  syscall (SYS_futex, uaddr, FUTEX_WAIT, val, 0, 0, 0);
}
extern "C" void *system_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
  return malloc (length);
}
extern "C" int system_munmap (uint8_t *start, size_t size)
{
  free (start);
  return 0;
}
extern "C" uint32_t 
machine_atomic_compare_and_exchange (uint32_t *val, uint32_t old, 
				     uint32_t new_value)
{
  return __sync_val_compare_and_swap (val, old, new_value);
}
extern "C" uint32_t machine_atomic_dec (uint32_t *val)
{
  return __sync_fetch_and_sub (val, 1);
}
extern "C" void *vdl_alloc_malloc (size_t size)
{
  return malloc (size);
}
extern "C" void *vdl_alloc_global(size_t size)
{
  return malloc (size);
}
extern "C" void vdl_alloc_free (void *buffer)
{
  return free (buffer);
}
extern "C" void vdl_memmove (void *dst, const void *src, size_t len)
{
  memmove (dst, src, len);
}
extern "C" void vdl_memcpy (void *dst, const void *src, size_t len)
{
  memcpy (dst, src, len);
}
extern "C" void vdl_memset(void *s, int c, size_t n)
{
  memset (s, c, n);
}
extern "C" int vdl_memcmp (void *a, void *b, size_t n)
{
  return memcmp (a, b, n);
}
