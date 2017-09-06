#ifndef FUTEX_H
#define FUTEX_H

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>

struct Futex
{
  uint32_t state __attribute__ ((aligned(4)));
};

struct RWLock
{
  struct Futex *reader, *global;
  int count;
};


struct Futex *futex_new (void);
void futex_delete (struct Futex *futex);
void futex_construct (struct Futex *futex);
#define futex_destruct(futex)
void futex_lock (struct Futex *futex);
void futex_unlock (struct Futex *futex);

struct RWLock *rwlock_new (void);
void rwlock_delete (struct RWLock *lock);
void rwlock_construct (struct RWLock *lock);
void rwlock_destruct(struct RWLock *lock);
void read_lock (struct RWLock *lock);
void read_unlock (struct RWLock *lock);
void write_lock (struct RWLock *lock);
void write_unlock (struct RWLock *lock);

#ifdef __cplusplus
}
#endif

#endif /* FUTEX_H */
