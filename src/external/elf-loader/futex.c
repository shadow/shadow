#include "futex.h"
#include "machine.h"
#include "system.h"
#include "vdl-alloc.h"

struct Futex *
futex_new (void)
{
  struct Futex *futex = vdl_alloc_new (struct Futex);
  futex_construct (futex);
  return futex;
}

void
futex_delete (struct Futex *futex)
{
  futex_destruct (futex);
  vdl_alloc_delete (futex);
}

void
futex_construct (struct Futex *futex)
{
  futex->state = 0;
}

void
futex_lock (struct Futex *futex)
{
  uint32_t c;
  if ((c = machine_atomic_compare_and_exchange (&futex->state, 0, 1)) != 0)
    {
      do
        {
          if (c == 2
              || machine_atomic_compare_and_exchange (&futex->state, 1,
                                                      2) != 0)
            {
              system_futex_wait (&futex->state, 2);
            }
        }
      while ((c =
              machine_atomic_compare_and_exchange (&futex->state, 0,
                                                   2)) != 0);
    }
}

void
futex_unlock (struct Futex *futex)
{
  if (machine_atomic_dec (&futex->state) != 1)
    {
      futex->state = 0;
      system_futex_wake (&futex->state, 1);
    }
}

// basic readers-writer lock
// makes no attempt to address writer starvation or r->w upgrades
struct RWLock *
rwlock_new (void)
{
  struct RWLock *lock = vdl_alloc_new (struct RWLock);
  rwlock_construct (lock);
  return lock;
}

void
rwlock_delete (struct RWLock *lock)
{
  rwlock_destruct (lock);
  vdl_alloc_delete (lock);
}

void
rwlock_construct (struct RWLock *lock)
{
  lock->reader = futex_new ();
  lock->global = futex_new ();
  lock->count = 0;
}

void
rwlock_destruct (struct RWLock *lock)
{
  futex_delete (lock->global);
  futex_delete (lock->reader);
}

void
read_lock (struct RWLock *lock)
{
  futex_lock (lock->reader);
  lock->count++;
  if (lock->count == 1)
    {
      futex_lock (lock->global);
    }
  futex_unlock (lock->reader);
}

void
read_unlock (struct RWLock *lock)
{
  futex_lock (lock->reader);
  lock->count--;
  if (lock->count == 0)
    {
      futex_unlock (lock->global);
    }
  futex_unlock (lock->reader);
}

void
write_lock (struct RWLock *lock)
{
  futex_lock (lock->global);
}

void
write_unlock (struct RWLock *lock)
{
  futex_unlock (lock->global);
}
