#ifndef BINARY_SPINNING_SEM_H_
#define BINARY_SPINNING_SEM_H_

#include <atomic>
#include <cstddef>

#include <pthread.h>
#include <semaphore.h>

// Intended to be private to the ipc module.

/*
 * Implements a partially-functioning binary semaphore with optimistic
 * spinning: the wait() caller will spin for a number of cycles --- if post()
 * is called during the spinning, then the waiting thread will immediately
 * resume. After thresh_ spins, falls back to a POSIX sem_t semaphore.
 */
class BinarySpinningSem {
  public:
    /*
     * Initialize the semaphore to the zero state.
     *
     * THREAD SAFETY: not thread-safe.
     */
    BinarySpinningSem(ssize_t spin_max);

    /* 
     * Initialize the semaphore to the zero state.
     *
     * THREAD SAFETY: not thread-safe.
     */
    void init();

    /*
     * Set the semaphore value to one.
     *
     * (rwails) !!IMPORTANT!!
     * Calling two posts() without a wait() in-between is not implemented
     * and leads to undefined behavior. The call chain on a particular
     * semaphore should look like:
     *
     * post() -> wait() -> post() -> wait() -> post() ...
     *
     * (where post and wait can be occurring in different processes)
     *
     * THREAD SAFETY: thread-safe; This operation is thread-safe, but it is
     * unlikely that this function will be called by two threads in a correct
     * program.
     */
    void post();

    /*
     * Wait for the semaphore to achieve value one; then, atomically sets the
     * semaphore value back to zero.
     *
     * The caller can set `spin` to false to immediately block on the semaphore
     * instead of spinning.  This is useful when the caller knows other cores
     * will need to do work before the semaphore will become available.
     *
     * (rwails) !!IMPORTANT!!
     * See note in post(). Same call chain restriction applies for wait().
     *
     * THREAD SAFETY: thread-safe; This operation is thread-safe, but it is
     * unlikely that this function will be called by two threads in a correct
     * program.
     */
    void wait(bool spin = true);

    /*
     * Atomically check if the semaphore is available (has value one). If
     * so takes the semaphore (sets it back to zero) and returns 0. Otherwise
     * returns -1 and sets errno to EAGAIN.
     */
    int trywait();

    BinarySpinningSem(const BinarySpinningSem &rhs) = delete;
    BinarySpinningSem &operator=(const BinarySpinningSem &rhs) = delete;

  private:
    sem_t _semaphore;
    ssize_t _thresh;
};

#endif // BINARY_SPINNING_SEM_H_
