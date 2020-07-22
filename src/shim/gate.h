#ifndef GATE_H_
#define GATE_H_

#include <stddef.h>
#include <stdint.h>

/*
 * Dummy class to use in place that has sufficient memory.
 * Can't expose the real internals here because we don't want to introduce C++
 * constructs to the world. PIMPL idiom isn't ideal because we want the caller
 * to be able to specify the memory where the gate lives.
 */
typedef struct _Gate {
  uint8_t _p[128];
} Gate;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void gate_init(Gate *gate);
void gate_open(Gate *gate);
void gate_pass_and_close(Gate *gate);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // GATE_H_
