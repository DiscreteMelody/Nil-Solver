// Exhaustive double-dummy search for the nil question.
//
// THE QUESTION
// ------------
// Given a layout, a leader, a spades-broken flag and a nil bidder, play the
// hand out under a LEXICOGRAPHIC objective:
//
//   PRIMARY    the nil bidder's trick count.  The nil bidder and its covering
//              partner MINIMISE it; both opponents MAXIMISE it.
//
//   SECONDARY  each pair's own trick count, used only to choose among lines
//              that are already equally good for the primary:
//                  minimise_own_tricks = false   each pair takes what it can
//                  minimise_own_tricks = true    each pair sheds what it can
//
//   TERTIARY   which of the nil side's two partners holds those tricks.  Only
//              the covering partner's tricks count towards the partner's bid,
//              so among lines where the pair takes the same total, the pair
//              prefers the nil bidder to take fewer.  Inert while the primary
//              is on (it has already pinned the nil bidder's count) and off in
//              the "shed" direction (bags accrue to the pair whoever won).
//
// The primary is not ordinary trick maximisation: a side will happily throw
// away a trick of its own if that forces one onto the nil bidder.  The
// secondary only breaks ties.
//
// Both components are strictly opposed, because the two pairs' trick counts
// sum to a constant -- the nil side taking more is identical to the opponents
// taking fewer.  That is why one flag sets a coherent direction for both sides
// at once, and why plain minimax over the packed pair is still well defined.
//
// `nil_already_set` drops the primary objective.  Use it once the nil has
// actually been broken in the real game: there is nothing left to protect or
// to attack, and only the secondary objective matters.
//
// `nil_fails` is (nil_tricks > 0), or forced true when the caller has told us
// the nil is already set.
//
// RELATION TO nil_oracle.py
// -------------------------
// The oracle fixes the coalitions by seat parity (N/S always minimise), so it
// answers this question exactly when its designated player sits N or S.  Here
// the coalitions follow the nil bidder's own parity, which is the same thing up
// to a relabelling of seats; tools/crosscheck.py rotates each deal so the nil
// bidder sits North before asking the oracle.
#ifndef NIL_SEARCH_HPP
#define NIL_SEARCH_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nil/position.hpp"

namespace nil {

struct Play {
    int seat = 0;
    CardId card = NO_CARD;
};

struct SearchOptions {
    // See rules.hpp: false is the literal reading of the break rule and matches
    // nil_oracle.py's default.
    bool break_on_forced_spade_lead = false;

    // Direction of the tie-break.  false: each pair takes as many tricks as it
    // can.  true: each pair takes as few as it can (bag avoidance).
    bool minimise_own_tricks = false;

    // Drop the primary objective: the nil has already been broken, so neither
    // side has anything left to protect or attack.
    bool nil_already_set = false;

    // Look positions up in a transposition table.  The search is a pure
    // function of the position, so the table changes neither the value nor the
    // principal variation -- it is memoisation, not alpha-beta or any other
    // search enhancement.  There is still no pruning of any kind.
    //
    // What it keys on is nil/statekey.hpp: not the literal state, but the
    // smallest description of it that still determines the value.  Two
    // positions that differ only in absolute ranks, or only in which card is
    // currently winning a trick as opposed to how many live cards it beats, are
    // one entry.
    bool use_memo = true;

    // Table size in mebibytes, rounded DOWN to a power-of-two bucket count.
    // Zero has the same effect as use_memo = false.
    //
    // The table is bounded, so once a search overflows it the node count
    // depends on this number: a bigger table finds more of its own earlier work
    // and visits fewer nodes.  Benchmarks are only comparable at equal size,
    // which is why nil_bench records it in the history file.
    std::size_t tt_megabytes = 32;
};

// Who took what along a line.
struct Tally {
    int nil_tricks = 0;       // the nil bidder alone
    int nil_side_tricks = 0;  // the nil bidder and its covering partner
    int opponent_tricks = 0;  // the other pair
};

struct Solution {
    int nil_tricks = 0;
    int nil_side_tricks = 0;
    int opponent_tricks = 0;
    bool nil_fails = false;
    int value = 0;           // the packed lexicographic scalar the search minimised
    int nil_seat = 0;
    std::vector<Play> pv;    // principal variation, one entry per remaining card
    std::uint64_t nodes = 0;

    // Transposition table behaviour for this solve.  `tt_hits` is the number of
    // nodes answered from the table; `tt_evictions` counts stores that threw
    // away a different live position, which is the signal that the table is too
    // small for the depth being attempted.
    std::uint64_t tt_probes = 0;
    std::uint64_t tt_hits = 0;
    std::uint64_t tt_stores = 0;
    std::uint64_t tt_evictions = 0;
};

// Weights that pack the three levels into one integer:
//
//     value = primary   * nil_tricks
//           + secondary * nil_side_tricks
//           + tertiary  * nil_tricks
//
// which the nil side minimises and the opponents maximise.  With
// K = tricks remaining + 1, primary is K*K and secondary is +/-K, so each level
// strictly outranks everything below it and the three compare
// lexicographically.  primary is zero when the nil is already set.
//
// A caveat about the one case the tertiary bites -- a nil already set while the
// pair is still taking tricks.  There, "the pair maximises its partner's
// tricks" and "the opponents maximise their own" are not strictly opposed:
// both sides would rather the nil bidder took nothing, so the split between the
// two partners is slack that only one side cares about, not a tug of war.  The
// tertiary sits BELOW the pair's total on purpose, so the opponents' objective
// stays exactly "take as many as we can" and the split resolves against the
// pair.  The partner count reported is therefore the one the pair can
// guarantee, not the one it might get if the opponents were helping.
struct ObjectiveWeights {
    int primary = 0;
    int secondary = -1;
    int tertiary = 0;
};

ObjectiveWeights objective_weights(int tricks_remaining, const SearchOptions& opts);

// Validates, searches, then independently replays the PV as a self-check.
// Returns false and sets `err` on an invalid position or an internal
// inconsistency.
bool solve(const Position& pos, int nil_seat, const SearchOptions& opts,
           Solution& out, std::string& err);

// Replays a PV, checking every play for legality and turn order, and reports
// who took what.  Also useful for checking a PV produced elsewhere.
bool replay_pv(const Position& pos, const std::vector<Play>& pv, int nil_seat,
               bool break_on_forced_spade_lead, Tally& tally_out, std::string& err);

// The transposition table is kept between calls, because a corpus run solves
// hundreds of positions and reallocating tens of megabytes for each one costs
// more than the search does.  Call this to hand the memory back; the next
// solve() will simply allocate again.
void release_transposition_table();

std::string format_pv_compact(const Solution& sol);       // "N:D2 E:DA S:D5 W:D7"
std::string format_pv(const Position& pos, const Solution& sol);  // one line per trick
std::string format_solution(const Position& pos, const Solution& sol,
                            const SearchOptions& opts);

}  // namespace nil

#endif  // NIL_SEARCH_HPP
