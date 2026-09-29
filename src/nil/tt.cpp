#include "nil/tt.hpp"

namespace nil {

void TranspositionTable::resize(std::size_t megabytes) {
    if (megabytes == 0) {
        table_.clear();
        table_.shrink_to_fit();
        buckets_ = 0;
        mask_ = 0;
        return;
    }

    const std::size_t bucket_bytes = sizeof(TTEntry) * WAYS;
    const std::size_t budget = megabytes * 1024u * 1024u;
    std::size_t want = 1;
    while (want * 2 * bucket_bytes <= budget) want *= 2;

    if (want == buckets_) {
        new_search();
        return;
    }
    buckets_ = want;
    mask_ = want - 1;
    table_.assign(buckets_ * WAYS, TTEntry());
    generation_ = 1;
    stats_ = TTStats();
}

void TranspositionTable::clear() {
    table_.assign(table_.size(), TTEntry());
    generation_ = 1;
    stats_ = TTStats();
}

void TranspositionTable::new_search() {
    stats_ = TTStats();
    if (generation_ == 0xFFFFu) {
        clear();  // wrapping would resurrect entries from 65535 searches ago
        return;
    }
    ++generation_;
}

const TTEntry* TranspositionTable::probe(const StateKey& key, std::uint64_t hash, std::uint8_t tag,
                                         int alpha, int beta, bool& answers) {
    answers = false;
    if (!buckets_) return nullptr;
    ++stats_.probes;
    const TTEntry* bucket = &table_[(hash & mask_) * WAYS];
    for (int i = 0; i < WAYS; ++i) {
        const TTEntry& e = bucket[i];
        if (e.generation != generation_ || e.tag() != tag || e.lo != key.lo || e.hi != key.hi) {
            continue;
        }
        // An exact value answers any window.  A bound answers only the windows
        // it already falls outside: knowing the value is at least X settles a
        // search whose beta is at or below X, and nothing narrower.
        answers = e.lower == e.upper || e.lower >= beta || e.upper <= alpha;
        // Either way the entry comes back.  A match that does not answer is a
        // one-sided bound on this position, which is worth strictly more to the
        // caller than a miss even though it cannot end the node; see the header.
        if (answers) {
            ++stats_.hits;
        } else {
            ++stats_.partial;
        }
        return &e;
    }
    return nullptr;
}

void TranspositionTable::store(const StateKey& key, std::uint64_t hash, int value, RelMove move,
                               int depth, std::uint8_t bound, std::uint8_t tag, bool maximizing) {
    (void)depth;  // read back out of the key; see key_card_count
    if (!buckets_) return;
    TTEntry* bucket = &table_[(hash & mask_) * WAYS];

    // Lower score wins the eviction.  A dead entry scores 0; a live one scores
    // 1 + depth, so anything from an earlier search always goes first and among
    // live entries the cheapest subtree goes next.
    TTEntry* victim = &bucket[0];
    int victim_score = -1;
    for (int i = 0; i < WAYS; ++i) {
        TTEntry& e = bucket[i];
        const bool live = e.generation == generation_;
        if (live && e.lo == key.lo && e.hi == key.hi) {
            victim = &e;  // refresh in place; never evict ourselves
            victim_score = -1;
            break;
        }
        const int score = live ? 1 + key_card_count(StateKey{e.lo, e.hi}) : 0;
        if (victim_score < 0 || score < victim_score) {
            victim = &e;
            victim_score = score;
        }
    }

    ++stats_.stores;
    const bool same = victim->generation == generation_ && victim->lo == key.lo &&
                      victim->hi == key.hi;
    if (victim->generation == generation_ && !same) ++stats_.evictions;

    const std::uint8_t kind = bound_kind(bound);
    const std::int16_t v = static_cast<std::int16_t>(value);
    std::int16_t lower = kind == BOUND_UPPER ? TT_NO_LOWER : v;
    std::int16_t upper = kind == BOUND_LOWER ? TT_NO_UPPER : v;
    const bool witness = kind == BOUND_EXACT || (maximizing ? kind == BOUND_LOWER
                                                            : kind == BOUND_UPPER);
    RelMove keep_move = move;
    if (merge_ && same && victim->tag() == tag) {
        const std::int16_t merged_lower = victim->lower > lower ? victim->lower : lower;
        const std::int16_t merged_upper = victim->upper < upper ? victim->upper : upper;
        if (merged_lower <= merged_upper) {
            // The move follows the bound it witnesses: replaced only by a store
            // that witnesses the side it is on AND is what that side now says.
            const bool sets_side = maximizing ? lower == merged_lower : upper == merged_upper;
            if (!(witness && sets_side) && victim->move != REL_NO_MOVE) keep_move = victim->move;
            lower = merged_lower;
            upper = merged_upper;
        }
    }

    victim->lo = key.lo;
    victim->hi = key.hi;
    victim->lower = lower;
    victim->upper = upper;
    victim->generation = generation_;
    victim->move = keep_move;
    victim->meta = static_cast<std::uint8_t>((tag & 7u) |
                                             (static_cast<unsigned>(bound_need(bound)) << 3));
}

}  // namespace nil
