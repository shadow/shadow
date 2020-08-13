#ifndef GATE_H_
#define GATE_H_

#include <atomic>
#include <cstddef>

#include <pthread.h>
#include <semaphore.h>

/*
 * Intended to be private to IPC module.
 */

struct Gate {
  std::atomic<bool> x;
  sem_t semaphore;
  std::size_t spin_ctr;
  pthread_spinlock_t lock;
};

void gate_init(Gate *gate);
void gate_open(Gate *gate);
void gate_pass_and_close(Gate *gate);

#endif // GATE_H_
