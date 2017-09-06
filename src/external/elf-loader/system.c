#include "system.h"
#include "machine.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/param.h>          // for EXEC_PAGESIZE
#include <linux/futex.h>
#include <sys/resource.h>

/* The magic checks below for -256 are probably misterious to non-kernel programmers:
 * they come from the fact that we call the raw system calls, not the libc wrappers
 * here so, we get the kernel return value which does not give us errno so, the
 * error number is multiplexed with the return value of the system call itself.
 * In practice, since there are less than 256 errnos defined (on my system, 131),
 * the kernel returns -errno to indicate an error, the expected value otherwise.
 */

void *
system_mmap (void *start, size_t length, int prot, int flags, int fd,
             off_t offset)
{
  void *retval = machine_system_mmap (start, length, prot, flags, fd, offset);
  return retval;
}

int
system_munmap (uint8_t * start, size_t size)
{
  int status = MACHINE_SYSCALL2 (munmap, start, size);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}

int
system_mprotect (const void *addr, size_t len, int prot)
{
  int status = MACHINE_SYSCALL3 (mprotect, addr, len, prot);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}

void
system_write (int fd, const void *buf, size_t size)
{
  MACHINE_SYSCALL3 (write, fd, buf, size);
}

int
system_open (const char *name, int oflag, mode_t mode)
{
  int status = MACHINE_SYSCALL3 (open, name, oflag, mode);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}

int
system_open_ro (const char *file)
{
  int status = MACHINE_SYSCALL2 (open, file, O_RDONLY);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}

int
system_unlink (const char *name)
{
  int status = MACHINE_SYSCALL1 (unlink, name);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}

int
system_sendfile (int out_fd, int in_fd, off_t *offset, size_t count)
{
  int status = MACHINE_SYSCALL4 (sendfile, out_fd, in_fd, offset, count);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}


int
system_read (int fd, void *buffer, size_t to_read)
{
  int status = MACHINE_SYSCALL3 (read, fd, buffer, to_read);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}

int
system_lseek (int fd, off_t offset, int whence)
{
  int status = MACHINE_SYSCALL3 (lseek, fd, offset, whence);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}

int
system_fstat (const char *file, struct stat *buf)
{
  int status = MACHINE_SYSCALL2 (stat, file, buf);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}

void
system_close (int fd)
{
  MACHINE_SYSCALL1 (close, fd);
}

void
system_exit (int status)
{
  MACHINE_SYSCALL1 (exit, status);
}

int
system_getpagesize (void)
{
  // Theoretically, this should be a dynamically-calculated value but, really, I don't
  // know how to query the kernel for this so, instead, we use the kernel headers.
  return EXEC_PAGESIZE;
}

void
system_futex_wake (uint32_t * uaddr, uint32_t val)
{
  MACHINE_SYSCALL6 (futex, uaddr, FUTEX_WAKE, val, 0, 0, 0);
}

void
system_futex_wait (uint32_t * uaddr, uint32_t val)
{
  MACHINE_SYSCALL6 (futex, uaddr, FUTEX_WAIT, val, 0, 0, 0);
}

int
system_getrlimit (int resource, struct rlimit *rlim)
{
  int status = MACHINE_SYSCALL2 (getrlimit, resource, rlim);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}

int
system_setrlimit (int resource, struct rlimit *rlim)
{
  int status = MACHINE_SYSCALL2 (setrlimit, resource, rlim);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}

unsigned long
system_getpid (void)
{
  return MACHINE_SYSCALL0 (getpid);
}

int system_sigaction(int signum, struct sigaction *act,
              struct sigaction *oldact)
{
  // Rather than implement signal handling ourselves, we let the vDSO do it.
  // See the manpage for sigreturn for more info.
  act->sa_flags &= ~0x04000000; // ~SA_RESTORER; not in signal.h for some reason
  act->sa_restorer = 0;
  // See "C library/kernel differences" in the sigaction manpage for why we
  // need to use rt_sigaction instead of sigaction, and what that is.
  // XXX: Do something smarter than the magic 8 here. It's documented as
  // requiring the value sizeof(sigset_t), which is currently 128, but in the
  // actual code it's (largest signal number+1)/8, which according to strace is
  // currently 8 on my system (not easily accessible anywhere else AFAICT).
  // Crashes if set to anything else.
  int status = MACHINE_SYSCALL4 (rt_sigaction, signum, act, oldact, 8);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}
