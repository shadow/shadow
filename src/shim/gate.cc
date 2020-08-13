#include "gate.h"

#include <cassert>
#include <cstdio>
#include <cstddef>
#include <cstring>

#include <atomic>
#include <utility>

#include <pthread.h>
#include <semaphore.h>
#include <sched.h>

#include <unistd.h>

#define MAGIC_CONST 0xBEEFBEEF

struct GateImpl {
  std::size_t magic;
  std::atomic<bool> x;
  sem_t semaphore;
  std::size_t spin_ctr;
  pthread_spinlock_t lock;
};

static_assert(sizeof(Gate) >= sizeof(GateImpl),
              "gate dummy class not larger than gate implementation class");

static inline GateImpl *gate_to_gate_impl(Gate *gate) {
  auto *gate_impl = reinterpret_cast<GateImpl *>(gate);
  assert(gate_impl->magic == MAGIC_CONST);
  return gate_impl;
}

extern "C" {

void gate_init(Gate *gate) {
  std::memset(gate, 0, sizeof(Gate));
  auto *gate_impl = reinterpret_cast<GateImpl *>(gate);
  gate_impl->magic = MAGIC_CONST;
  sem_init(&gate_impl->semaphore, 1, 0);
  pthread_spin_init(&gate_impl->lock, PTHREAD_PROCESS_SHARED);
  // std::atomic_init(&gate_impl->x, false);
}

void gate_open(Gate *gate) {
  auto *gate_impl = gate_to_gate_impl(gate);

  pthread_spin_lock(&gate_impl->lock);
  sem_post(&gate_impl->semaphore);
  gate_impl->x.store(true, std::memory_order_release);
  pthread_spin_unlock(&gate_impl->lock);
}

void gate_pass_and_close(Gate *gate) {
  auto *gate_impl = gate_to_gate_impl(gate);

  bool expected = true;

  while (gate_impl->spin_ctr++ < 8096 &&
          !gate_impl->x.compare_exchange_weak(
      expected, false, std::memory_order_acquire)) {
    expected = true;
      sched_yield();
    __builtin_ia32_pause();
  }

  sem_wait(&gate_impl->semaphore);
  gate_impl->spin_ctr = 0;

  pthread_spin_lock(&gate_impl->lock);
  gate_impl->x.store(false, std::memory_order_release);
  pthread_spin_unlock(&gate_impl->lock);

}

}
