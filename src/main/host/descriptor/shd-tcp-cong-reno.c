#include "shd-tcp-cong-reno.h"
#include "shd-tcp.h"
#include "shd-tcp-cong.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct CAReno_ {

    const TCPCongHooks *state_hooks;

    size_t duplicate_ack_n;

    guint32 cong_avoid_nacked;
    guint32 ssthresh;

} CAReno;

/*
 * Prototype these to avoid circular refs.
 */
static inline const struct TCPCongHooks_ *slow_start_hooks_();
static inline const struct TCPCongHooks_ *fast_recovery_hooks_();
static inline const struct TCPCongHooks_ *cong_avoid_hooks_();

/* HELPERS *******************************************************/

static inline void ssthresh_halve(TCP *tcp, CAReno *reno) {
    reno->ssthresh = (tcp_cong(tcp)->cwnd / 2) + 1;
}

/*
 * Pass in a non-zero value for n to ack n packets during the transition.
 */
static inline void transition_to_cong_avoid(TCP *tcp, CAReno *reno, guint32 n) {
    reno->cong_avoid_nacked = 0;
    reno->state_hooks = cong_avoid_hooks_();
    reno->state_hooks->tcp_cong_new_ack_ev(tcp, n);
    info("[CONG] fd %i transition_to_cong_avoid", ((Descriptor*)tcp)->handle);
}

/* SLOW START *******************************************************/

static void ca_reno_slow_start_duplicate_ack_ev_(TCP *tcp) {
    CAReno *reno = tcp_cong(tcp)->ca;
    reno->duplicate_ack_n++;

    if (reno->duplicate_ack_n == 3) { // transition to fast recovery

        debug("[CONG-AVOID] three duplicate acks");
        info("[CONG] fd %i three duplicate acks transition_to_fast_recovery", ((Descriptor*)tcp)->handle);

        ssthresh_halve(tcp, reno);
        tcp_cong(tcp)->cwnd = reno->ssthresh + 3;

        reno->state_hooks = fast_recovery_hooks_();
    }
}

static void ca_reno_slow_start_new_ack_ev_(TCP *tcp, guint32 n) {
    CAReno *reno = tcp_cong(tcp)->ca;

    reno->duplicate_ack_n = 0;

    guint32 new_cwnd = tcp_cong(tcp)->cwnd;
    new_cwnd += n;

    bool transition = (new_cwnd >= reno->ssthresh);

    if (transition) { // transition to cong avoid

        // If we have gotten too many acked packets, up the cwnd to ssthresh
        // and then transition into congestion avoidance with the leftover
        // acks.

        guint32 nleft = new_cwnd - reno->ssthresh;
        new_cwnd = reno->ssthresh;
        tcp_cong(tcp)->cwnd = new_cwnd;
        transition_to_cong_avoid(tcp, reno, nleft);

    } else {
        tcp_cong(tcp)->cwnd = new_cwnd;
    }
}

/* FAST RECOVERY *******************************************************/

static void ca_reno_fast_recovery_duplicate_ack_ev_(TCP *tcp) {
    tcp_cong(tcp)->cwnd += 1;
}

static void ca_reno_fast_recovery_new_ack_ev_(TCP *tcp, guint32 n) {
    CAReno *reno = tcp_cong(tcp)->ca;

    reno->duplicate_ack_n = 0;
    tcp_cong(tcp)->cwnd = reno->ssthresh;

    transition_to_cong_avoid(tcp, reno, n);
}

/* CONG AVOID *******************************************************/

static void ca_reno_cong_avoid_new_ack_ev_(TCP *tcp, guint32 n) {
    CAReno *reno = tcp_cong(tcp)->ca;

    reno->cong_avoid_nacked += n;

    // We only increase one for each send window
    while(reno->cong_avoid_nacked >= tcp_cong(tcp)->cwnd) {
        reno->cong_avoid_nacked -= tcp_cong(tcp)->cwnd ;
        tcp_cong(tcp)->cwnd += 1;
    }
}

/*******************************************************************/

static void ca_reno_init_(TCP *tcp, CAReno *reno) {
    tcp_cong(tcp)->cwnd = 10;
    reno->ssthresh = INT32_MAX;
    reno->cong_avoid_nacked = 0;
    reno->duplicate_ack_n = 0;
    reno->state_hooks = slow_start_hooks_();
}

static void tcp_cong_reno_delete_(TCP *tcp) {
    free(tcp_cong(tcp)->ca);
}

static void tcp_cong_reno_duplicate_ack_ev_(TCP *tcp) {
    CAReno *reno = tcp_cong(tcp)->ca;
    reno->state_hooks->tcp_cong_duplicate_ack_ev(tcp);
}

static bool tcp_cong_reno_fast_recovery_(TCP *tcp) {
    CAReno *reno = tcp_cong(tcp)->ca;
    return reno->state_hooks == fast_recovery_hooks_();
}

static void tcp_cong_reno_new_ack_ev_(TCP *tcp, guint32 n) {
    CAReno *reno = tcp_cong(tcp)->ca;
    reno->state_hooks->tcp_cong_new_ack_ev(tcp, n);
}

/* All timeouts have the same behavior! */
static void tcp_cong_reno_timeout_ev_(TCP *tcp) {

    CAReno *reno = tcp_cong(tcp)->ca;

    reno->duplicate_ack_n = 0;
    ssthresh_halve(tcp, reno);
    tcp_cong(tcp)->cwnd = 10;

    // transition to slow start
    reno->state_hooks = slow_start_hooks_();
    info("[CONG] fd %i transition_to_slow_start", ((Descriptor*)tcp)->handle);
}

static guint32 tcp_cong_reno_ssthresh_(TCP *tcp) {
    CAReno *reno = tcp_cong(tcp)->ca;
    return reno->ssthresh;
}

static const struct TCPCongHooks_ reno_hooks_ = {
    .tcp_cong_delete = tcp_cong_reno_delete_,
    .tcp_cong_duplicate_ack_ev = tcp_cong_reno_duplicate_ack_ev_,
    .tcp_cong_fast_recovery = tcp_cong_reno_fast_recovery_,
    .tcp_cong_new_ack_ev = tcp_cong_reno_new_ack_ev_,
    .tcp_cong_timeout_ev = tcp_cong_reno_timeout_ev_,
    .tcp_cong_ssthresh = tcp_cong_reno_ssthresh_
};

void tcp_cong_reno_init(TCP *tcp) {
    CAReno *reno = malloc(sizeof(CAReno));
    ca_reno_init_(tcp, reno);

    tcp_cong(tcp)->cwnd = 1;
    tcp_cong(tcp)->hooks = (TCPCongHooks*)&reno_hooks_;
    tcp_cong(tcp)->ca = reno;
}

static const struct TCPCongHooks_ slow_start_hooks__ = {
    .tcp_cong_delete = NULL,
    .tcp_cong_duplicate_ack_ev = ca_reno_slow_start_duplicate_ack_ev_,
    .tcp_cong_fast_recovery = NULL,
    .tcp_cong_new_ack_ev = ca_reno_slow_start_new_ack_ev_,
    .tcp_cong_timeout_ev = NULL,
    .tcp_cong_ssthresh = NULL
};

static const struct TCPCongHooks_ fast_recovery_hooks__ = {
    .tcp_cong_delete = NULL,
    .tcp_cong_duplicate_ack_ev = ca_reno_fast_recovery_duplicate_ack_ev_,
    .tcp_cong_fast_recovery = NULL,
    .tcp_cong_new_ack_ev = ca_reno_fast_recovery_new_ack_ev_,
    .tcp_cong_timeout_ev = NULL,
    .tcp_cong_ssthresh = NULL
};

/* slow start and cong avoidance have the same dupl act behavior */
static const struct TCPCongHooks_ cong_avoid_hooks__ = {
    .tcp_cong_delete = NULL,
    .tcp_cong_duplicate_ack_ev = ca_reno_slow_start_duplicate_ack_ev_,
    .tcp_cong_fast_recovery = NULL,
    .tcp_cong_new_ack_ev = ca_reno_cong_avoid_new_ack_ev_,
    .tcp_cong_timeout_ev = NULL,
    .tcp_cong_ssthresh = NULL
};

static inline const struct TCPCongHooks_ *slow_start_hooks_() {
    return &slow_start_hooks__;
}

static inline const struct TCPCongHooks_ *fast_recovery_hooks_() {
    return &fast_recovery_hooks__;
}

static inline const struct TCPCongHooks_ *cong_avoid_hooks_() {
    return &cong_avoid_hooks__;
}
