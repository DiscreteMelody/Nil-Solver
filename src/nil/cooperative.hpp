// ROADMAP item 2b: the cooperative reachability probe, ported from
// nil_oracle.py's solve_cooperative (patch 90) to C++.
//
// THIS IS A DIFFERENT KIND OF QUESTION from everything else in search.hpp.
// A, B, the two conjunctions and the opposing-nils objective are all
// ADVERSARIAL GUARANTEES: can one side force an outcome against any defence
// the other side plays?  This probe asks PURE EXISTENCE instead: does a line
// of play producing the named outcome exist AT ALL, with every one of the
// four seats free to help rather than two of them opposing?  No adversary,
// OR at every node, stop at the first success.
//
// WHY IT NEEDS ITS OWN SEARCH rather than riding on solve()'s Ctx the way
// item 78's conjunction probe does.  The conjunction is still a two-sided
// minimax question -- can the attacking side force its own indicator to 1 --
// so it reuses the alpha-beta skeleton with a different value function.
// This one has no minimising side at all: every seat plays whatever keeps a
// protected bidder off the trick, and the search is an AND across cards
// already fixed by the deal (there is only one hand to draw from) and an OR
// across which legal card to try.  Bolting that onto Ctx would mean a second
// reading of every field in it purely to confirm each one is inert; a
// separate function makes what is NOT there the whole of the argument.
//
// WHAT DOES NOT PORT.  The relative-rank transposition key in statekey.hpp is
// built for a search that compares a position's VALUE against a window --
// the gap it tracks is a threshold on that value -- and this probe has no
// value and no window.  The memo below is the OLD key statekey.hpp's own
// header comment
// describes replacing -- four raw hand masks plus leader/trick/broken, one
// entry per LITERAL position -- which is exactly nil_oracle.py's own memo key
// `(hands, leader, trick, spades_broken)`.  That is a faithful port, not a
// weak one: relative-rank compression is a real future win (it is where most
// of the main table's collapsing comes from) and re-deriving it for a boolean
// OR-search rather than a bounded value search is its own item, not this one.
//
// WHAT DOES PORT.  `distinct_moves`'s equivalence-class reduction (rules.hpp)
// is proved sound from the rules alone -- follow-suit, the break flag, and
// who beats whom -- and never mentions the objective being searched for.  A
// reachability question is decided by exactly those same rules, so the same
// swap-symmetry argument carries over unchanged: if the position after
// playing the higher of two rank-equivalent cards reaches a protected outcome,
// relabelling the two cards throughout the rest of the tree shows the lower
// one does too.  Collapsing equivalents changes the node count and never the
// answer, on this search exactly as on the minimax one.
//
// THE COST PROFILE IS ASYMMETRIC, and it is the reason this file exists
// rather than just calling the oracle.  "Pure existence, stops at the first
// success" is only true of a REACHABLE answer.  An OR-only search has no
// adversary and therefore no cutoff on an UNREACHABLE one: every line keeping
// every protected bid alive has to be tried and found wanting.  See the
// ROADMAP entry for item 2b for what that costs at 13 cards, where it matters,
// rather than at the small hands where the oracle can still afford to check
// it directly.
//
// NOT WIRED INTO solve() OR THE CASE TABLE.  Item 60 is what would consult
// this to decide whether T's `(make, make)` rule needs its guard; this patch
// ships the probe standing alone, callable and measurable, and touches
// nothing solve() already does.
#ifndef NIL_COOPERATIVE_HPP
#define NIL_COOPERATIVE_HPP

#include <cstdint>
#include <string>

#include "nil/position.hpp"
#include "nil/seats.hpp"

namespace nil {

struct CooperativeSolution {
    // Seats whose bids this run required to survive.  Echoes the input so a
    // caller holding only the result can still describe what it asked.
    unsigned protect_mask = 0;
    // Is there a line of play, with all four seats cooperating, on which
    // every seat in `protect_mask` finishes never having taken a trick?
    bool reachable = false;
    std::uint64_t nodes = 0;
};

// Can every bid named in `protect_mask` survive together, on SOME line, with
// nobody opposing?  Mirrors nil_oracle.py's solve_cooperative exactly:
//
//   * every bit of `protect_mask` must name a seat holding a LIVE bid --
//     `roles[seat] == ROLE_NIL`.  A seat already declared down with
//     ROLE_NIL_SET cannot be protected, because it is down on every line by
//     the caller's own assertion; asking for it is refused via `err` rather
//     than silently answered false.  A seat that never bid at all is refused
//     the same way.  (An empty `protect_mask` is not an error -- it asks
//     whether ANY legal line exists, which is true of every valid position --
//     but it is also not a question this probe exists to answer.)
//   * the search itself reads no roles at all, only the cards: a protected
//     seat's bid dies the instant it wins a trick, on any line, cooperative
//     or not, so the recursion abandons that branch without descending
//     further rather than tracking a broken-bid mask through the state.
//
// `use_memo` mirrors SearchOptions::use_memo; `collapse_equivalents` mirrors
// SearchOptions::collapse_equivalents.  Both default true elsewhere in this
// codebase, and the caller is expected to pass those same fields through
// rather than duplicate their meaning under new names -- see nil_cli.cpp's
// `--cooperative` handling for the intended call shape.  Neither changes the
// answer, only the node count; that is what makes them safe to ablate.
//
// Returns false and sets `err` on an invalid position (nil::validate) or a
// protect seat that is not a live bid.
bool solve_cooperative(const Position& pos, const SeatRoles& roles, unsigned protect_mask,
                       bool use_memo, bool collapse_equivalents, CooperativeSolution& out,
                       std::string& err);

// ---------------------------------------------------------------------------
// THE DUAL QUESTION, for item 60's OPPONENT-lean (`3/3`) fallback.
//
// Proved (not just measured) in the ROADMAP entry for this item: in the cell
// where neither conjunction is forceable, the equilibrium is confined to
// {both survive, both fail}, and whichever of the two the shared lean prefers
// wins WHENEVER IT IS REACHABLE.  COVER prefers both-survive, and
// `solve_cooperative` already answers that.  OPPONENT prefers both-fail, and
// nothing above answers THAT -- this does.
//
// NOT A SIGN-FLIP OF `solve_cooperative`, and the reason is state.  A
// protected bid there dies the instant it is broken, so the line is simply
// abandoned; no memo key needs to say more.  A bid required to FAIL here is
// satisfied the FIRST time its seat wins a trick and stays satisfied for the
// rest of the line -- membership in `fail_mask` is not enough to know whether
// a given seat has already done its part, so the search carries a second
// mask (`satisfied`, part of the recursive state and the memo key) tracking
// exactly that, the same shape of bookkeeping `_search_conjunction`'s
// `broken_nils` uses for a different reason.
struct CooperativeFailSolution {
    // Seats whose bids this run required to fail (take at least one trick).
    unsigned fail_mask = 0;
    // Is there a line, everyone cooperating, on which EVERY seat in
    // `fail_mask` takes at least one trick by the end -- not necessarily the
    // same trick, and not necessarily in any particular order?
    bool reachable = false;
    std::uint64_t nodes = 0;
};

// Same validation as `solve_cooperative`: every bit of `fail_mask` must name
// a seat holding a live bid (`roles[seat] == ROLE_NIL`).  Asking to force the
// failure of a seat that never bid, or one already declared down, is refused
// via `err` rather than silently answered -- a `ROLE_NIL_SET` seat's bid has
// already failed by the caller's own assertion, which is a reason to leave it
// out of `fail_mask` rather than a case for this function to re-derive.
//
// `use_memo` and `collapse_equivalents` mean what they do for
// `solve_cooperative`, and the same equivalence-class argument from
// cooperative.hpp's own header carries over unchanged: which seat wins a
// trick, and therefore whether `fail_mask` gets satisfied, is decided by the
// rules alone, so swapping two rank-equivalent cards throughout the rest of
// the tree cannot change the answer, only the node count.
bool solve_cooperative_fail(const Position& pos, const SeatRoles& roles, unsigned fail_mask,
                            bool use_memo, bool collapse_equivalents,
                            CooperativeFailSolution& out, std::string& err);

}  // namespace nil

#endif  // NIL_COOPERATIVE_HPP
