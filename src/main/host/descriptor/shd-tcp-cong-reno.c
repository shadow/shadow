#include "shd-tcp-cong-reno.h"
#include "shd-tcp.h"
#include "shd-tcp-cong.h"

#include <stddef.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct CAReno_ {
    size_t duplicate_ack_n;
    guint32 ssthresh;
    const TCPCongHooks *state_hooks;
} CAReno;

static void ca_reno_slow_start_duplicate_ack_ev_(TCP *tcp) {
    CAReno *reno = tcp_cong(tcp)->ca;

    reno->duplicate_ack_n++;
}

static void ca_reno_slow_start_timeout_ev_(TCP *tcp) {
    CAReno *reno = tcp_cong(tcp)->ca;
}

static const struct TCPCongHooks_ slow_start_hooks_ = {
    .tcp_cong_duplicate_ack_ev = ca_reno_slow_start_duplicate_ack_ev_,
    .tcp_cong_timeout_ev = ca_reno_slow_start_timeout_ev_,
    .tcp_cong_ssthresh = NULL
};

static void ca_reno_init_(TCP *tcp, CAReno *reno) {
    tcp_setSendWindow(tcp, 1);
    reno->ssthresh = UINT_MAX;
    reno->duplicate_ack_n = 0;
    reno->state_hooks = &slow_start_hooks_;
}

static void tcp_cong_reno_duplicate_ack_ev_(TCP *tcp) {
    CAReno *reno = tcp_cong(tcp)->ca;
    reno->state_hooks->tcp_cong_duplicate_ack_ev(tcp);
}

static void tcp_cong_reno_timeout_ev_(TCP *tcp) {
    CAReno *reno = tcp_cong(tcp)->ca;
    reno->state_hooks->tcp_cong_duplicate_ack_ev(tcp);
}

static guint32 tcp_cong_reno_ssthresh_(TCP *tcp) {
    CAReno *reno = tcp_cong(tcp)->ca;
    return reno->ssthresh;
}

static const struct TCPCongHooks_ reno_hooks_ = {
    .tcp_cong_duplicate_ack_ev = tcp_cong_reno_duplicate_ack_ev_,
    .tcp_cong_timeout_ev= tcp_cong_reno_timeout_ev_,
    .tcp_cong_ssthresh = tcp_cong_reno_ssthresh_
};

void tcp_cong_reno_init(TCP *tcp) {
    CAReno *reno = malloc(sizeof(CAReno));
    ca_reno_init_(tcp, reno);

    tcp_cong(tcp)->hooks = (TCPCongHooks*)&reno_hooks_;
    tcp_cong(tcp)->ca = reno;
}
