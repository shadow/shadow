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

struct Futex *futex_new (void);
void futex_delete (struct Futex *futex);
void futex_construct (struct Futex *futex);
void futex_destruct (struct Futex *futex);
void futex_lock (struct Futex *futex);
void futex_unlock (struct Futex *futex);

#ifdef __cplusplus
}
#endif

#endif /* FUTEX_H */
