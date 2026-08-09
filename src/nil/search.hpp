// Exhaustive double-dummy search for the nil question.
//
// THE QUESTION
// ------------
// Given a layout, a leader, a spades-broken flag and a nil bidder, how many
// tricks does the nil bidder take when
//
//     the nil bidder and its partner both play to MINIMISE the nil bidder's
//     trick count, and both opponents play to MAXIMISE it?
//
// The two coalitions have exactly opposed objectives over a single scalar, so
// plain minimax is well defined.  Note that a side will happily throw away a
// trick of its own if that forces one onto the nil bidder; no notion of "own
// tricks" appears anywhere in the search.
//
// `nil_fails` is then simply (tricks > 0): the defenders can force the nil
// bidder to win at least one trick no matter how well it and its partner play.
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

    // Memoise on the FULL state (all four hands, leader, cards on the current
    // trick, broken flag).  The search is a pure function of exactly that
    // state, so the cache changes neither the value nor the principal
    // variation -- it is memoisation of a pure function, not alpha-beta or any
    // other search enhancement.  There is still no pruning of any kind.
    bool use_memo = true;
};

struct Solution {
    int tricks = 0;          // tricks the nil bidder takes under optimal play
    bool nil_fails = false;  // tricks > 0
    int nil_seat = 0;
    std::vector<Play> pv;    // principal variation, one entry per remaining card
    std::uint64_t nodes = 0;
};

// Validates, searches, then independently replays the PV as a self-check.
// Returns false and sets `err` on an invalid position or an internal
// inconsistency.
bool solve(const Position& pos, int nil_seat, const SearchOptions& opts,
           Solution& out, std::string& err);

// Replays a PV, checking every play for legality and turn order, and reports
// the nil bidder's trick count.  Also useful for checking a PV produced
// elsewhere.
bool replay_pv(const Position& pos, const std::vector<Play>& pv, int nil_seat,
               bool break_on_forced_spade_lead, int& tricks_out, std::string& err);

std::string format_pv_compact(const Solution& sol);       // "N:D2 E:DA S:D5 W:D7"
std::string format_pv(const Position& pos, const Solution& sol);  // one line per trick
std::string format_solution(const Position& pos, const Solution& sol);

}  // namespace nil

#endif  // NIL_SEARCH_HPP
