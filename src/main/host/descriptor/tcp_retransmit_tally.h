#ifndef SHD_TCP_RETRANSMIT_TALLY_H_
#define SHD_TCP_RETRANSMIT_TALLY_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#endif // __cplusplus

struct _PacketSelectiveAcks;

/* Really hacky and brittle.  Only doing an explicit copy because #including
 * shd-tcp.h and shadow.h is not working. */
enum TCPProcessFlags_ {
    TCP_PF_NONE_ = 0,
    TCP_PF_PROCESSED_ = 1 << 0,
    TCP_PF_DATA_RECEIVED_ = 1 << 1,
    TCP_PF_DATA_ACKED_ = 1 << 2,
    TCP_PF_DATA_SACKED_ = 1 << 3,
    TCP_PF_DATA_LOST_ = 1 << 4,
    TCP_PF_RWND_UPDATED_ = 1 << 5,
};

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void retransmit_tally_init(void **p);
void retransmit_tally_destroy(void *p);

size_t retransmit_tally_size_bytes();

enum TCPProcessFlags_ retransmit_tally_update(void *p, uint32_t last_ack, uint32_t max_ack, bool is_dup);
void retransmit_tally_cleanup_sacked(void *p);
void retransmit_tally_mark_sacked(void* p, struct _PacketSelectiveAcks sacked);
/* Marks the block [begin, end) as lost. */
void retransmit_tally_mark_lost(void *p, uint32_t begin, uint32_t end);
void retransmit_tally_mark_retransmitted(void *p, uint32_t begin, uint32_t end);
void retransmit_tally_clear_retransmitted(void *p);
size_t retransmit_tally_num_lost_ranges(const void *p);
void retransmit_tally_populate_lost_ranges(const void *p, uint32_t *lost);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#ifdef __cplusplus
using SeqNum = std::int64_t;
// Using standard left-closed, right-open (i.e. half-open) semantics
using SeqRange = std::pair<SeqNum, SeqNum>;
using Ranges = std::vector<SeqRange>;

struct RetransmitTally {
   RetransmitTally();
   RetransmitTally &operator=(RetransmitTally &&rhs);

   RetransmitTally(const RetransmitTally &rhs) = delete;
   RetransmitTally &operator=(const RetransmitTally &rhs) = delete;

   void compute_lost();
   void tidy_ranges(Ranges *ranges);

   enum : std::uint64_t { kMagicNum = 0xBEEEEEEF,
                          kDuplAckLostThresh = 3 };

   SeqNum last_ack_;
   std::size_t num_dupl_ack_;
   std::uint64_t magic_num_;
   Ranges marked_lost_, sacked_, retransmitted_, lost_;
};
#endif // __cplusplus

#endif // SHD_TCP_RETRANSMIT_TALLY_H_
