#include "shd-tcp-cong-reno.h"
#include "shd-tcp.h"
#include "shd-tcp-cong.h"

#include <stddef.h>
#include <stdlib.h>

typedef struct CAReno_ {
    guint32 ssthresh;
    size_t duplicate_ack_n;
} CAReno;

static void tcp_cong_reno_duplicate_ack_ev_(TCP *tcp) {
}

static guint32 tcp_cong_reno_ssthresh_(TCP *tcp) {
    CAReno *reno = tcp_get_cong(tcp)->ca;
    return reno->ssthresh;
}

static const struct TCPCongHooks_ reno_hooks_ = {
    .tcp_cong_duplicate_ack_ev = tcp_cong_reno_duplicate_ack_ev_,
    .tcp_cong_ssthresh = tcp_cong_reno_ssthresh_
};

void tcp_cong_reno_init(TCPCong *tcp_cong) {
    Reno *reno = malloc(sizeof(CAReno));
    tcp_cong->hooks = (TCPCongHooks*)&reno_hooks_;
    tcp_cong->ca = reno;
}
