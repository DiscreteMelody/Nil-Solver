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
// being asked about -- an entry ANSWERS it only when the entry is exact, or
// when the bound already falls outside it.
//
// An entry that matches the position but does not answer the window is a
// PARTIAL match, and it is still a fact: "the value is at least x", or "at most
// x", is true of the position however wide the window asking happens to be.
// probe() hands it back with `answers` false so the caller can narrow its own
// window onto it before searching -- roadmap item 41.  `hits` still counts only
// the probes the table ANSWERED, so that statistic keeps meaning what it always
// did, and `partial` still counts the rest.
//
// A partial can never close the window it narrows.  `answers` is false at a
// BOUND_LOWER exactly when its value is below beta, so raising alpha onto it
// leaves alpha below beta; symmetrically for BOUND_UPPER above alpha.  The
// caller therefore has one fewer degenerate case to handle than the general
// alpha-beta-with-memory shape suggests.
//
// (Roadmap item 5 wanted the stored MOVE off those partial entries for
// ordering, and this comment used to say probe() should be widened to hand them
// back.  It is now widened -- for the BOUND, which is a different thing.  Item
// 5 was closed twice on measurement: first because `partial` was identically
// zero (patch 12), then, once patch 22 gave it a population, because the stored
// move is a worse hint than what it displaces (patch 26).  Neither result says
// anything about the stored bound, which is not a hint at all.
//
// `partial` is still identically zero in MODE_FAST, and for the reason patch 12
// gave: every node there is asked about [0, 1] and every stored value is either
// BOUND_UPPER at 0 or BOUND_LOWER at 1, so an entry that matches the position
// always settles the window.  Narrowing is therefore a MODE_FULL item by
// arithmetic rather than by a gate.)
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

// `TTEntry::bound` carries two things, and this is the only place that knows it.
//
// Bits 0-1 are the Bound above.  Bits 2-5 are `need`: how many of each suit's
// live cards, counted from the top, an entry's backed-up winning ranks require
// pinned -- nil/ranks.hpp calls it the truncation level.  A node that reads an
// entry needs it to tell its own parent which of ITS cards were essential, and
// a parent that knows will store more of its own entries coarsely.
//
// Why it is squeezed in here rather than given a field.  TTEntry is 24 bytes
// with no padding on any sane ABI, and a fifteenth byte rounds it to 32 -- a
// third fewer entries in the same table, which would move every node count this
// project has banked whether or not the feature is switched on.  `bound` had
// six bits doing nothing.  Four of them now hold a number that is at most 13.
//
// It is per-ENTRY rather than per-suit on purpose: the exact vector is four
// nibbles and there is not room for sixteen bits.  One number applied to all
// four suits pins at least as much as the four would, so it is a safe
// over-approximation -- it costs some coarseness and cannot cost an answer.
inline constexpr std::uint8_t BOUND_KIND_MASK = 0x03;
inline constexpr int BOUND_NEED_SHIFT = 2;
inline constexpr int BOUND_NEED_MAX = 15;

inline std::uint8_t pack_bound(std::uint8_t kind, int need) {
    if (need < 0) need = 0;
    if (need > BOUND_NEED_MAX) need = BOUND_NEED_MAX;
    return static_cast<std::uint8_t>((kind & BOUND_KIND_MASK) |
                                     (static_cast<unsigned>(need) << BOUND_NEED_SHIFT));
}

inline std::uint8_t bound_kind(std::uint8_t packed) { return packed & BOUND_KIND_MASK; }

inline int bound_need(std::uint8_t packed) {
    return (packed >> BOUND_NEED_SHIFT) & BOUND_NEED_MAX;
}

// Which objective an entry's value is on the scale of.  Zero is what a
// never-written entry holds, so it deliberately names no objective.
enum ValueTag : std::uint8_t {
    TAG_NONE = 0,
    TAG_FULL = 1,  // the packed lexicographic value
    TAG_FAST = 2,  // the nil bidder's trick count
    // A pair that both bid: the primary level counts BIDS DOWN rather than
    // weighting one seat's tricks, so a value on this scale means something
    // different from a TAG_FULL value at the same cards.  It gets its own tag
    // for the same reason TAG_FAST does -- the key says which POSITION an entry
    // is about, and the tag says which QUESTION.
    TAG_MULTI_NIL = 3,
    // One bid on each side: the value is an outcome RANK plus one side's
    // tricks, which is a third scale again.
    TAG_OPPOSING_NILS = 4,
    // Item 78's probe: can one side force the other's bid down while keeping
    // its own?  A boolean on a two-valued scale, which is a fifth scale again --
    // and unlike TAG_FAST it is not symmetric in the two bidders, so it may not
    // share entries with a plain fast search at the same cards even though both
    // store 0 or 1.
    TAG_CONJUNCTION = 5,
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

    // Returns a live entry for `key` with this `tag` if the table holds one,
    // and sets `answers` to say whether it settles the question "where does the
    // value sit relative to [alpha, beta)?".
    //
    //   non-null, answers true    the caller may return the entry's value
    //   non-null, answers false   a PARTIAL match: a one-sided bound the caller
    //                             may narrow its window onto, counted in
    //                             `partial` and not in `hits`
    //   null                      no entry for this position
    //
    // The pointer is valid until the next store().
    const TTEntry* probe(const StateKey& key, std::uint64_t hash, std::uint8_t tag, int alpha,
                         int beta, bool& answers);

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
