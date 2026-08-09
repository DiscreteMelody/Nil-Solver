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

const TTEntry* TranspositionTable::probe(const StateKey& key, std::uint64_t hash) {
    if (!buckets_) return nullptr;
    ++stats_.probes;
    const TTEntry* bucket = &table_[(hash & mask_) * WAYS];
    for (int i = 0; i < WAYS; ++i) {
        const TTEntry& e = bucket[i];
        if (e.generation == generation_ && e.lo == key.lo && e.hi == key.hi) {
            ++stats_.hits;
            return &e;
        }
    }
    return nullptr;
}

void TranspositionTable::store(const StateKey& key, std::uint64_t hash, int value, RelMove move,
                               int depth, std::uint8_t bound) {
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
        const int score = live ? 1 + static_cast<int>(e.depth) : 0;
        if (victim_score < 0 || score < victim_score) {
            victim = &e;
            victim_score = score;
        }
    }

    ++stats_.stores;
    if (victim->generation == generation_ && (victim->lo != key.lo || victim->hi != key.hi)) {
        ++stats_.evictions;
    }

    victim->lo = key.lo;
    victim->hi = key.hi;
    victim->value = static_cast<std::int16_t>(value);
    victim->generation = generation_;
    victim->move = move;
    victim->depth = static_cast<std::uint8_t>(depth < 0 ? 0 : (depth > 255 ? 255 : depth));
    victim->bound = bound;
    victim->reserved = 0;
}

}  // namespace nil
