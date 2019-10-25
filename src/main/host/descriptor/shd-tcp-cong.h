#ifndef SHD_TCP_CONG_H_
#define SHD_TCP_CONG_H_

#include "shadow.h"

// congestion event hooks

typedef void (*TCPCongDuplicateAckEv)(TCP *tcp);
typedef void (*TCPCongTimeoutEv)(TCP *tcp);
typedef guint (*TCPCongSSThresh)(TCP *tcp);

typedef struct TCPCongHooks_ {
    TCPCongDuplicateAckEv tcp_cong_duplicate_ack_ev;
    TCPCongTimeoutEv tcp_cong_timeout_ev;
    TCPCongSSThresh tcp_cong_ssthresh;
} TCPCongHooks;

typedef struct TCPCong_ {
    const TCPCongHooks *hooks;
    void *ca;
} TCPCong;

#endif // SHD_TCP_CONG_H_
