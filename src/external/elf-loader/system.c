#include "system.h"
#include "machine.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/param.h> // for EXEC_PAGESIZE
#include <linux/futex.h>

/* The magic checks below for -256 are probably misterious to non-kernel programmers:
 * they come from the fact that we call the raw system calls, not the libc wrappers
 * here so, we get the kernel return value which does not give us errno so, the
 * error number is multiplexed with the return value of the system call itself.
 * In practice, since there are less than 256 errnos defined (on my system, 131), 
 * the kernel returns -errno to indicate an error, the expected value otherwise.
 */

void *system_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
  void *retval = machine_system_mmap (start, length, prot, flags, fd, offset);
  return retval;
}

int system_munmap (uint8_t *start, size_t size)
{
  int status = MACHINE_SYSCALL2 (munmap, start, size);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}
int system_mprotect (const void *addr, size_t len, int prot)
{
  int status = MACHINE_SYSCALL3 (mprotect, addr, len, prot);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}
void system_write (int fd, const void *buf, size_t size)
{
  MACHINE_SYSCALL3(write,fd,buf,size);
}
int system_open_ro (const char *file)
{
  int status = MACHINE_SYSCALL2 (open,file,O_RDONLY);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}
int system_read (int fd, void *buffer, size_t to_read)
{
  int status = MACHINE_SYSCALL3 (read, fd, buffer, to_read);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}
int system_lseek (int fd, off_t offset, int whence)
{
  int status = MACHINE_SYSCALL3 (lseek, fd, offset, whence);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}
int system_fstat (const char *file, struct stat *buf)
{
  int status = MACHINE_SYSCALL2 (stat,file,buf);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}
void system_close (int fd)
{
  MACHINE_SYSCALL1 (close,fd);
}
void system_exit (int status)
{
  MACHINE_SYSCALL1 (exit, status);
}

int system_getpagesize (void)
{
  // Theoretically, this should be a dynamically-calculated value but, really, I don't
  // know how to query the kernel for this so, instead, we use the kernel headers.
  return EXEC_PAGESIZE;
}
void system_futex_wake (uint32_t *uaddr, uint32_t val)
{
  MACHINE_SYSCALL6 (futex, uaddr, FUTEX_WAKE, val, 0, 0, 0);
}
void system_futex_wait (uint32_t *uaddr, uint32_t val)
{
  MACHINE_SYSCALL6 (futex, uaddr, FUTEX_WAIT, val, 0, 0, 0);
}
