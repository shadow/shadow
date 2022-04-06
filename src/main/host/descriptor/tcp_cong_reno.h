#ifndef SHD_TCP_CONG_RENO_H_
#define SHD_TCP_CONG_RENO_H_

#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/tcp_cong.h"

// the name linux gives for this congestion control algorithm
extern const char* TCP_CONG_RENO_NAME;

void tcp_cong_reno_init(TCP *tcp);

#endif // SHD_TCP_CONG_RENO_H_
