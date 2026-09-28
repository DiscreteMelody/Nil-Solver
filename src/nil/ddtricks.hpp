// A plain double-dummy trick engine for the part of a deal the nil question
// no longer touches.
//
// WHY IT EXISTS
// -------------
// Once every nil bid in the objective is down (or none was live to begin with)
// the packed value is an affine function of one side's trick count:
//
//     value = C + W * far_side_tricks,   W > 0
//
// which is ordinary double dummy with spades as trumps plus the breaking rule.
// The general search handles that correctly but without any of the machinery a
// trick solver lives on, and profiling put 30-60% of 13-card opposed-nil nodes
// in exactly those subtrees.  This engine answers them instead, borrowing the
// techniques DDS uses (Haglund, "Search algorithms for a bridge double dummy
// solver"):
//
//   * zero-window search: every call asks "can N/S take >= target?", and the
//     exact value comes from a short bisection over those booleans;
//   * a transposition table keyed on RELATIVE ranks at trick boundaries that
//     keeps a lower AND an upper bound per position, so probes at different
//     targets share work instead of overwriting each other;
//   * quick tricks for the side on lead, with a ruff check;
//   * a later-tricks bound from the top spades;
//   * DDS-style move ordering (cash winners, lead toward partner's winners,
//     cheapest sure winner in third and fourth seat, don't overtake a partner
//     who is already winning, ruff low, discard low), with the stored best lead
//     tried first;
//   * rank-equivalent moves collapsed, exactly as the main search does.
//
// VALIDITY OF THE TABLE ACROSS SOLVES
// -----------------------------------
// The key is a complete description of the position up to rank relabelling --
// who holds each remaining card of each suit in rank order, who is on lead, and
// whether spades are broken -- and the stored value is N/S's trick count from
// that point.  Neither depends on the deal it came from, the seat roles, the
// objective or the window, so the table is never invalidated between solves.
// It is thread_local, like the main table.
//
// RULES
// -----
// The engine uses legal_moves / spades_broken_after / beats from nil/rules.hpp,
// so it cannot disagree with the main search about what is legal.
#ifndef NIL_DDTRICKS_HPP
#define NIL_DDTRICKS_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "nil/cards.hpp"

namespace nil {
namespace dd {

struct DDStats {
    std::uint64_t calls = 0;        // positions handed to the engine by the main search
    std::uint64_t probes = 0;       // zero-window searches run
    std::uint64_t nodes = 0;        // engine nodes, every ply
    std::uint64_t tt_probes = 0;
    std::uint64_t tt_cuts = 0;      // boundary answered by stored bounds
    std::uint64_t quick_cuts = 0;   // boundary answered by quick tricks
    std::uint64_t later_cuts = 0;   // boundary answered by the top-spade bound
};

class Engine {
public:
    Engine();

    // Table size.  0 disables the table (correct, slow).  Resizing wipes it.
    void resize(std::size_t megabytes);
    std::size_t megabytes() const { return megabytes_; }
    void clear();

    // Can N/S take at least `target` of the remaining tricks?  The position
    // must be at a trick boundary (no cards on the table).  When `witness` is
    // non-null it receives a move for the side on lead that proves the answer
    // (a successful move for N/S, a refuting move for E/W); NO_CARD when the
    // answer is decided without looking at moves (target <= 0 or > tricks).
    bool ns_reach(const Hand hands[4], int leader, bool broken, int target, CardId* witness);

    // Exact N/S trick count, given that it is known to lie in [lo, hi].  When
    // `best` is non-null it receives an optimal lead for the side on lead.
    int ns_exact(const Hand hands[4], int leader, bool broken, int lo, int hi, CardId* best);

    const DDStats& stats() const { return stats_; }
    void reset_stats() { stats_ = DDStats(); }
    DDStats& mutable_stats() { return stats_; }

private:
    struct Pos {
        Hand h[4];
        CardId trick[4];
        int leader;
        int len;
        bool broken;
    };
    // A stored fact: bounds on N/S's tricks from any position with these suit
    // lengths per hand, this leader and breaking state, and these owners for
    // the top k cards of each suit.  Cards below the top k of a suit never won
    // a trick anywhere in the proof, so they are interchangeable -- DDS's
    // winning-ranks generalization.  The k's are part of the fact.
    struct Entry {
        std::uint64_t lengths;   // 4 bits per hand per suit
        std::uint32_t pat[4];    // k << 26 | owners of the top k, highest first
        std::uint8_t meta;       // leader | broken << 2 | 0x80 when used
        std::int8_t lo;
        std::int8_t hi;
        std::uint8_t depth;      // tricks remaining; replacement priority
        std::uint8_t move;       // stored best lead, relative
        std::uint8_t pad[3];
    };
    // Which k-profiles have been stored for one (lengths, leader, broken).  A
    // probe tries each: truncating the position's own patterns to a profile
    // gives an exact key, so the fact table stays a plain hash table instead
    // of a list scan.
    static constexpr int PROFILES = 12;
    struct Header {
        std::uint64_t lengths;
        std::uint8_t meta;       // | 0x80 when used
        std::uint8_t count;
        std::uint8_t next;       // round-robin replacement cursor
        std::uint8_t pad;
        std::uint16_t prof[PROFILES];  // k per suit, 4 bits each
    };
    struct Key {
        std::uint64_t lengths;
        std::uint8_t meta;
        std::uint32_t code[4];   // owners of every remaining card, highest first
        int n[4];                // suit lengths
        unsigned all[4];         // rank bits present, per suit
    };

    bool search(const Pos& p, int target, CardId* witness, unsigned rel[4]);
    // Answers when a stored fact decides `target`; `pat_out` receives the
    // matching fact's patterns so the caller can report what it relied on.
    bool probe(const Key& key, int target, bool& result, CardId& move, const Hand h[4],
               std::uint32_t pat_out[4]);
    void store(const Key& key, const unsigned rel[4], int lo, int hi, CardId move,
               const Hand h[4], int depth);
    Header* header(const Key& key, bool create);
    Entry* fact(std::uint64_t lengths, std::uint8_t meta, const std::uint32_t pat[4], bool create,
                int depth);

    std::vector<Entry> table_;
    std::size_t mask_ = 0;
    std::vector<Header> headers_;
    std::size_t hmask_ = 0;
    std::size_t megabytes_ = 0;
    DDStats stats_;
};

// One engine per thread; created on first use with the default table size.
Engine& engine();

inline constexpr std::size_t DD_DEFAULT_MEGABYTES = 64;

}  // namespace dd
}  // namespace nil

#endif  // NIL_DDTRICKS_HPP
