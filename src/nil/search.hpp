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

    // Memoise on the FULL state (all four hands, leader, cards on the current
    // trick, broken flag).  The search is a pure function of exactly that
    // state, so the cache changes neither the value nor the principal
    // variation -- it is memoisation of a pure function, not alpha-beta or any
    // other search enhancement.  There is still no pruning of any kind.
    bool use_memo = true;
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
};

// Weights that pack the lexicographic pair into one integer:
//
//     value = primary_weight * nil_tricks + secondary_sign * nil_side_tricks
//
// which the nil side minimises and the opponents maximise.  primary_weight is
// (tricks remaining + 1), strictly larger than any possible secondary term, so
// the primary always dominates; zero when the nil is already set.
struct ObjectiveWeights {
    int primary = 0;
    int secondary_sign = -1;
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

std::string format_pv_compact(const Solution& sol);       // "N:D2 E:DA S:D5 W:D7"
std::string format_pv(const Position& pos, const Solution& sol);  // one line per trick
std::string format_solution(const Position& pos, const Solution& sol,
                            const SearchOptions& opts);

}  // namespace nil

#endif  // NIL_SEARCH_HPP
