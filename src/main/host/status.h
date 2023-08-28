/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_STATUS_H_
#define SRC_MAIN_HOST_STATUS_H_

/* Bitfield representing possible status types and their states.
 * These represent statuses that the StatusListener can wait on. */
typedef enum _Status Status;
enum _Status {
    STATUS_NONE = 0,
    /* the descriptor has been initialized and it is now OK to
     * unblock any plugin waiting on a particular status  */
    STATUS_FILE_ACTIVE = 1 << 0,
    /* can be read, i.e. there is data waiting for user */
    STATUS_FILE_READABLE = 1 << 1,
    /* can be written, i.e. there is available buffer space */
    STATUS_FILE_WRITABLE = 1 << 2,
    /* user already called close */
    STATUS_FILE_CLOSED = 1 << 3,
    /* a wakeup operation occurred on a futex */
    STATUS_FUTEX_WAKEUP = 1 << 4,
    /* a listening socket is allowing connections; only applicable to connection-oriented unix
     * sockets */
    STATUS_SOCKET_ALLOWING_CONNECT = 1 << 5,
    /* a child process had an event reportable via e.g. waitpid */
    STATUS_CHILD_EVENT = 1 << 6,
};

#endif // SRC_MAIN_HOST_STATUS_H
