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
futex_destruct (struct Futex *futex)
{
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
