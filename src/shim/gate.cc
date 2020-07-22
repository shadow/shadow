#include "gate.h"

#include <cstring>

#include <atomic>
#include <utility>

struct GateImpl {
  std::atomic<bool> x;
};

static_assert(sizeof(Gate) >= sizeof(GateImpl),
              "gate dummy class not larger than gate implementation class");

static inline GateImpl *gate_to_gate_impl(Gate *gate) {
  return reinterpret_cast<GateImpl *>(gate);
}

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

void gate_init(Gate *gate) {
  auto *gate_impl = gate_to_gate_impl(gate);
  std::memset(gate_impl, 0, sizeof(GateImpl));
  std::atomic_init(&gate_impl->x, false);
}

void gate_open(Gate *gate) {
  auto *gate_impl = gate_to_gate_impl(gate);
  gate_impl->x.store(true, std::memory_order_release);
}

void gate_pass_and_close(Gate *gate) {
  auto *gate_impl = gate_to_gate_impl(gate);

  bool expected = true;

  while (!gate_impl->x.compare_exchange_weak(
      expected, false, std::memory_order_acquire)) {
    expected = true;
    __builtin_ia32_pause();
  }
}

#ifdef __cplusplus
}
#endif  // __cplusplus
