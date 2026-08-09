// A fixed-size transposition table over StateKey.
//
// The old memo was an unordered_map that grew until the position was solved or
// the machine gave up, which is fine at five cards and hopeless at thirteen.
// This is the usual bounded alternative: a flat array of buckets, four entries
// each, indexed by the key's hash, with the full 128-bit key stored in the entry
// so that a hash collision costs one wasted comparison and never a wrong
// answer.  Nothing here is probabilistic.
//
// REPLACEMENT
// -----------
// Within a bucket, prefer to overwrite (a) the same position, (b) anything left
// over from an earlier search, then (c) the entry whose subtree was cheapest to
// compute.  `depth` is the number of cards still in the four hands, which is a
// direct proxy for how much work the entry saves: a 4-card entry is worth
// almost nothing and a 40-card entry is worth a fortune.  This is the scheme in
// Chang, "Building a Fast Double-Dummy Bridge Solver" (NYU TR1996-725) section
// 3, which found bucketed rehashing worth appreciably more than a plain
// always-replace table at the same size.
//
// GENERATIONS
// -----------
// A solve is only allowed to see its own entries: values are relative to one
// nil seat and one set of objective weights, so an entry from the previous
// position in a corpus run is not merely stale but wrong.  Bumping a 16-bit
// generation counter invalidates the whole table in constant time, which
// matters when the caller is nil_bench and there are 560 positions to get
// through.  The counter is only allowed to wrap onto a real clear().
//
// ROOM LEFT FOR ALPHA-BETA
// ------------------------
// Every entry written today is exact, because the search is still exhaustive.
// `bound` is stored anyway so that adding alpha-beta later is a change to the
// search and not to the table format: a fail-high stores BOUND_LOWER, a
// fail-low BOUND_UPPER, and the probe learns to compare against the window
// rather than returning unconditionally.
#ifndef NIL_TT_HPP
#define NIL_TT_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "nil/statekey.hpp"

namespace nil {

enum Bound : std::uint8_t {
    BOUND_EXACT = 0,
    BOUND_LOWER = 1,  // reserved for alpha-beta: value is at least this
    BOUND_UPPER = 2,  // reserved for alpha-beta: value is at most this
};

// 24 bytes, no padding on any sane ABI.
struct TTEntry {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
    std::int16_t value = 0;
    std::uint16_t generation = 0;  // 0 means "never written"
    std::uint8_t move = REL_NO_MOVE;
    std::uint8_t depth = 0;  // cards still in hands; the replacement priority
    std::uint8_t bound = BOUND_EXACT;
    std::uint8_t reserved = 0;
};

struct TTStats {
    std::uint64_t probes = 0;
    std::uint64_t hits = 0;
    std::uint64_t stores = 0;
    std::uint64_t evictions = 0;  // stores that displaced a different live position
};

class TranspositionTable {
public:
    static constexpr int WAYS = 4;

    // Rounds DOWN to a power-of-two bucket count that fits in `megabytes`.
    // Zero disables the table entirely.  Re-requesting the same size is free.
    void resize(std::size_t megabytes);

    void clear();       // wipe, O(size)
    void new_search();  // invalidate everything, O(1)

    bool enabled() const { return buckets_ != 0; }
    std::size_t buckets() const { return buckets_; }
    std::size_t bytes() const { return table_.size() * sizeof(TTEntry); }

    // Returns null on a miss.  The pointer is valid until the next store().
    const TTEntry* probe(const StateKey& key, std::uint64_t hash);

    void store(const StateKey& key, std::uint64_t hash, int value, RelMove move, int depth,
               std::uint8_t bound);

    const TTStats& stats() const { return stats_; }
    void reset_stats() { stats_ = TTStats(); }

private:
    std::vector<TTEntry> table_;
    std::size_t buckets_ = 0;
    std::size_t mask_ = 0;
    std::uint16_t generation_ = 0;
    TTStats stats_;
};

}  // namespace nil

#endif  // NIL_TT_HPP
