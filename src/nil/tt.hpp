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
// BOUNDS
// ------
// An entry no longer necessarily holds a value.  A search that cut off knows
// only which side of its window the value lies on, so `bound` says which:
// BOUND_LOWER for a fail-high, BOUND_UPPER for a fail-low, BOUND_EXACT for a
// node that looked at everything.  probe() therefore needs the window it is
// being asked about -- an entry answers it only when the entry is exact, or
// when the bound already falls outside it.  An entry that matches the position
// but merely narrows the window is counted as `partial` and reported as a miss,
// so that `hits` keeps meaning "nodes answered from the table".
//
// (Roadmap item 5 wanted the stored MOVE off those partial entries for
// ordering, and this comment used to say probe() should be widened to hand them
// back.  It should not be: there are no partial entries to widen onto.  In
// MODE_FAST every node is asked about [0, 1] and every stored value is either
// BOUND_UPPER at 0 or BOUND_LOWER at 1, so an entry that matches the position
// always settles the window -- a node that finds an entry returns without
// searching, and a node that searches never found one.  `partial` is therefore
// identically zero there, which is the population item 5 would have ordered.
// Patch 12 closes the item on that argument; see ROADMAP.md.  If a later item
// varies the window, `partial` goes non-zero and the item comes back.)
//
// TAGS
// ----
// A value is only meaningful against the objective weights that produced it: a
// fast-mode 1 and a full-mode 1 are different numbers, and a fast-mode
// BOUND_LOWER read by a full-mode search is a wrong answer rather than a slow
// one.  What actually keeps the two apart is that every solve bumps the
// generation, so no solve ever sees another's entries at all.  `tag` is a
// second lock on the same door: probe() requires it to match, so a future
// change that lets two objectives share a generation gets misses instead of
// nonsense.  Zero is reserved for "never written" and matches nothing.
#ifndef NIL_TT_HPP
#define NIL_TT_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "nil/statekey.hpp"

namespace nil {

enum Bound : std::uint8_t {
    BOUND_EXACT = 0,  // the node looked at every move: this is the value
    BOUND_LOWER = 1,  // the node cut off high: the value is at least this
    BOUND_UPPER = 2,  // the node cut off low: the value is at most this
};

// Which objective an entry's value is on the scale of.  Zero is what a
// never-written entry holds, so it deliberately names no objective.
enum ValueTag : std::uint8_t {
    TAG_NONE = 0,
    TAG_FULL = 1,  // the packed lexicographic value
    TAG_FAST = 2,  // the nil bidder's trick count
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
    std::uint8_t tag = TAG_NONE;
};

struct TTStats {
    std::uint64_t probes = 0;
    std::uint64_t hits = 0;    // probes the table answered
    std::uint64_t partial = 0; // probes that matched but only narrowed the window
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

    // Returns null unless a live entry for `key` with this `tag` settles the
    // question "where does the value sit relative to [alpha, beta)?".  A match
    // that only narrows the window counts as `partial` and returns null too.
    // The pointer is valid until the next store().
    const TTEntry* probe(const StateKey& key, std::uint64_t hash, std::uint8_t tag, int alpha,
                         int beta);

    void store(const StateKey& key, std::uint64_t hash, int value, RelMove move, int depth,
               std::uint8_t bound, std::uint8_t tag);

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
