#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <linux/unistd.h>


void *system_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset);
int system_munmap (uint8_t *start, size_t size);
int system_mprotect (const void *addr, size_t len, int prot);
void system_write (int fd, const void *buf, size_t size);
int system_open (const char *name, int oflag, mode_t mode);
int system_open_ro (const char *file);
int system_unlink (const char *name);
int system_sendfile (int out_fd, int in_fd, off_t *offset, size_t count);
int system_read (int fd, void *buffer, size_t to_read);
int system_lseek (int fd, off_t offset, int whence);
int system_fstat (const char *file, struct stat *buf);
void system_close (int fd);
void system_exit (int status);
int system_getpagesize (void);
void system_futex_wake (uint32_t *uaddr, uint32_t val);
void system_futex_wait (uint32_t *uaddr, uint32_t val);
int system_getrlimit (int resource, struct rlimit *rlim);
int system_setrlimit (int resource, struct rlimit *rlim);
unsigned long system_getpid (void);

#endif /* SYSTEM_H */
