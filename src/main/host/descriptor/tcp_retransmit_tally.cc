#include "main/host/descriptor/tcp_retransmit_tally.h"
#include "main/network/legacypacket.h"
#include <algorithm>
#include <cassert>
#include <gmodule.h>
#include <iostream>
#include <random>
#include <string>
#include <utility>

static bool still_sorted_(const Ranges &r) {
   bool sorted = true;

   if (r.size() > 0) {
      for (std::size_t idx = 0; sorted && idx < r.size() - 1; ++idx) {
         sorted = r[idx].first < r[idx].second;
         sorted = sorted && (r[idx].second < r[idx + 1].first);
      }
   }

   return sorted;
}

static RetransmitTally *cast_and_assert(void *p) {
   auto *rt = static_cast<RetransmitTally *>(p);
   assert(rt->magic_num_ == RetransmitTally::kMagicNum);
   return rt;
}

static const RetransmitTally *cast_and_assert(const void *p) {
   auto *rt = static_cast<const RetransmitTally *>(p);
   assert(rt->magic_num_ == RetransmitTally::kMagicNum);
   return rt;
}

static bool range_contains(const SeqRange &range, SeqNum value) {
   return (value >= range.first && value < range.second);
}

static bool range_overlap(const SeqRange &lhs, const SeqRange &rhs) {
   return lhs.first < rhs.second && rhs.first < lhs.second;
}

static bool range_adj(const SeqRange &lhs, const SeqRange &rhs) {
   return (lhs.second == rhs.first || rhs.second == lhs.first);
}

static bool ranges_contains(const Ranges &ranges, SeqNum value) {
  for (const auto &range : ranges) {
    if (range_contains(range, value)) {
      return true;
    }
  }

  return false;
}

static std::pair<Ranges::iterator, Ranges::iterator>
ranges_mergable(Ranges &ranges, const SeqRange &value) {
   assert(still_sorted_(ranges));
   std::pair<Ranges::iterator, Ranges::iterator> mergable;

   mergable.first = ranges.end();

   auto itr = ranges.begin();
   for (; itr != ranges.end() && value.second >= itr->first; ++itr) {
      if (mergable.first == ranges.cend()
          && (range_overlap(*itr, value) || range_adj(*itr, value)))
      {
         mergable.first = itr;
      }
   }

   mergable.second = itr;

   assert(still_sorted_(ranges));

   return mergable;
}

static void range_merge(SeqRange *x, const SeqRange &y) {
   x->first = std::min(x->first, y.first);
   x->second = std::max(x->second, y.second);
}

static void ranges_insert(Ranges *ranges, const SeqRange &value) {
   assert(still_sorted_(*ranges));
   auto original = *ranges;
   auto mergable = ranges_mergable(*ranges, value);

   if (mergable.first == ranges->end()) {
      ranges->insert(mergable.second, value);
   } else {
      auto itr = mergable.first;
      range_merge(&(*itr), value);
      for (auto jtr = itr + 1; jtr != mergable.second; ++jtr) {
         range_merge(&(*itr), *jtr);
      }
      ranges->erase(mergable.first + 1, mergable.second);
   }

   assert(still_sorted_(*ranges));
}

static Ranges range_subtract(const SeqRange &lhs, const SeqRange &rhs) {
   // Can produce two ranges in the case that rhs \in lhs.
   // Returning a std::vector makes checking for this case easy.
   Ranges result;
   result.reserve(2);

   if (range_overlap(lhs, rhs)) {
      if (lhs.first < rhs.first) {
         result.emplace_back(lhs.first, rhs.first);
      }

      if (rhs.second < lhs.second) {
         result.emplace_back(rhs.second, lhs.second);
      }
   } else {
      result.push_back(lhs);
   }

   assert(still_sorted_(result));

   return result;
}

static Ranges ranges_subtract(const Ranges &lhs, const Ranges &rhs) {
   Ranges result;

   if (rhs.size() == 0) {
      result = lhs; // Assignment here to encourage RVO
   } else if (lhs.size() > 0) {

      std::size_t idx = 0;
      auto jtr = rhs.cbegin();
      SeqRange lhs_to_consider = lhs[0];

      while (idx < lhs.size() && jtr != rhs.cend()) {
         if (jtr->second <= lhs_to_consider.first) {
            ++jtr;
         }
         else if (lhs_to_consider.second <= jtr->first) {
            result.push_back(lhs_to_consider);
            ++idx;
            if (idx < lhs.size()) { lhs_to_consider = lhs[idx]; }
         } else {
            Ranges sub = range_subtract(lhs_to_consider, *jtr);
            if (sub.size() == 2) { result.push_back(sub.front()); }
            if (sub.size() >= 1) { lhs_to_consider = sub.back(); }
            else {
               ++idx;
               if (idx < lhs.size()) { lhs_to_consider = lhs[idx]; }
            }
         }
      }

      if (jtr == rhs.cend()) {
         result.push_back(lhs_to_consider);
         ++idx;

         while (idx < lhs.size()) {
            result.push_back(lhs[idx++]);
         }
      }

   }

   for (const auto &range : result) {
      assert(range.first < range.second);
   }

   assert(still_sorted_(result));

   return result;
}

extern "C" {

void retransmit_tally_init(void **p) {
   *p = new RetransmitTally{};
}

void retransmit_tally_destroy(void *p) {
   auto *rt = cast_and_assert(p);
   delete rt;
}

size_t retransmit_tally_size_bytes() {
   return sizeof(RetransmitTally);
}

enum TCPProcessFlags_ retransmit_tally_update(void *p, uint32_t last_ack, uint32_t max_ack, bool is_dup)
{
   auto rt = cast_and_assert(p);

   int ret = TCP_PF_NONE_;

   if (is_dup && last_ack == rt->last_ack_) {
      ++rt->num_dupl_ack_;
   } else if (last_ack > rt->last_ack_) { // new ack branch
      rt->last_ack_ = last_ack;
      rt->num_dupl_ack_ = 0;
      rt->tidy_ranges(&rt->marked_lost_);
      rt->tidy_ranges(&rt->sacked_);
      rt->tidy_ranges(&rt->retransmitted_);
   }

   if (rt->num_dupl_ack_ >= RetransmitTally::kDuplAckLostThresh
       && !ranges_contains(rt->retransmitted_, rt->last_ack_)) {
      // std::cerr << "3 dupl acks!" << std::endl;
      // std::cerr << last_ack << std::endl;
      //uint32_t right_edge_exclusive = MAX(max_ack, rt->last_ack_ + 1);
      uint32_t right_edge_exclusive = rt->last_ack_ + 1;
      ranges_insert(&rt->marked_lost_, {rt->last_ack_, right_edge_exclusive});
      rt->compute_lost(); // sacked packets are removed from lost list here
      if (rt->lost_.size() > 0) { ret |= TCP_PF_DATA_LOST_; }
   }

   return static_cast<TCPProcessFlags_>(ret);
}

void retransmit_tally_mark_sacked(void* p, PacketSelectiveAcks sacked) {
    auto rt = cast_and_assert(p);
    SeqRange sacked_block{-1, -1};

    for (unsigned int i = 0; i < sacked.len; i++) {
        sacked_block.first = sacked.ranges[i].start;
        sacked_block.second = sacked.ranges[i].end;
        assert(sacked_block.second > 0);
        ranges_insert(&rt->sacked_, sacked_block);
    }
}

void retransmit_tally_mark_lost(void *p, uint32_t begin, uint32_t end) {
   auto rt = cast_and_assert(p);
   if (begin == end + 1) { return; } // fin?
   if (begin == end) { end += 1; }
   assert(begin < end);
   SeqRange lost_block{begin, end};
   ranges_insert(&rt->marked_lost_, lost_block);
   rt->compute_lost();
}

void retransmit_tally_mark_retransmitted(void *p, uint32_t begin, uint32_t end)
{
   auto rt = cast_and_assert(p);
   SeqRange retransmitted_block{begin, end};
   ranges_insert(&rt->retransmitted_, retransmitted_block);
   rt->compute_lost();
}

void retransmit_tally_clear_retransmitted(void *p) {
   auto rt = cast_and_assert(p);
   rt->retransmitted_.clear();
}

size_t retransmit_tally_num_lost_ranges(const void *p) {
   auto rt = cast_and_assert(p);
   return rt->lost_.size();
}

void retransmit_tally_populate_lost_ranges(const void *p, uint32_t *lost) {
   auto rt = cast_and_assert(p);

   for (std::size_t idx = 0; idx < rt->lost_.size(); ++idx) {
      const auto &range = rt->lost_[idx];
      lost[2*idx] = range.first;
      lost[2*idx + 1] = range.second;
   }
}

} // extern "C"

RetransmitTally::RetransmitTally()
   : last_ack_(-1),
     num_dupl_ack_(0),
     magic_num_(kMagicNum),
     marked_lost_{}, sacked_{}, retransmitted_{}, lost_{}
{
}

RetransmitTally &RetransmitTally::operator=(RetransmitTally &&rhs) {
   last_ack_ = rhs.last_ack_;
   num_dupl_ack_ = rhs.num_dupl_ack_;
   magic_num_ = kMagicNum;
   marked_lost_ = std::move(rhs.marked_lost_);
   sacked_ = std::move(rhs.sacked_);
   retransmitted_ = std::move(rhs.retransmitted_);
   lost_ = std::move(rhs.lost_);
   return *this;
}

void RetransmitTally::compute_lost() {
   lost_ = ranges_subtract(marked_lost_, sacked_);
   lost_ = ranges_subtract(lost_, retransmitted_);
}

void RetransmitTally::tidy_ranges(Ranges *ranges) {
   assert(still_sorted_(*ranges));
   auto original = *ranges;

   auto pred = [=] (const SeqRange range) -> bool {
      return last_ack_ >= range.second;
   };

   if (ranges->size() > 0 && last_ack_ >= ranges->front().first
       && last_ack_ < ranges->front().second - 1)
   {
      ranges->front().first = last_ack_;
   }
   else if (ranges->size() > 0 && last_ack_ >= ranges->front().second - 1) {
      auto new_end = std::remove_if(ranges->begin(), ranges->end(), pred);
      ranges->erase(new_end, ranges->end());
   }

   assert(still_sorted_(*ranges));
}
