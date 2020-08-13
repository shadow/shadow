#include "gate.h"

#include <cstring>
#include <unistd.h>

#include <sched.h>

// (rwails) The number times we should increment the counter and 
// check the atomic bool before we fall back to the semaphore.
// TODO: Move to an environment variable?
#define SHD_GATE_SPIN_MAX 8096

void gate_init(Gate *gate) {
  std::memset(gate, 0, sizeof(Gate));
  sem_init(&gate->semaphore, 1, 0);
  pthread_spin_init(&gate->lock, PTHREAD_PROCESS_SHARED);

  // std::atomic_init(&gate->x, false);
}

void gate_open(Gate *gate) {
  pthread_spin_lock(&gate->lock);
  sem_post(&gate->semaphore);
  gate->x.store(true, std::memory_order_release);
  pthread_spin_unlock(&gate->lock);
}

void gate_pass_and_close(Gate *gate) {
  bool expected = true;

  while (gate->spin_ctr++ < SHD_GATE_SPIN_MAX &&
          !gate->x.compare_exchange_weak(
      expected, false, std::memory_order_acquire)) {
    expected = true;
    __asm__ ( "pause" ); // (rwails) Not sure if this op is helpful.
  }

  sem_wait(&gate->semaphore);
  gate->spin_ctr = 0;

  pthread_spin_lock(&gate->lock);
  gate->x.store(false, std::memory_order_release);
  pthread_spin_unlock(&gate->lock);
}
