// ROADMAP item 60, step 1 of 5: two of the three branches of the same-lean
// decision procedure.
//
// THE FULL PROCEDURE, proved rather than assumed -- see the ROADMAP entry --
// has three cases, by whether each bid is individually cooperatively
// reachable at all:
//
//   NEITHER reachable   -- both fail, on cards alone, regardless of anyone's
//                          play.  A "for all lines" fact, not a game.
//   EXACTLY ONE         -- the doomed one fails; the survivor's fate is
//                          settled by an ordinary single-nil check on it
//                          alone, because the doomed one's partner has
//                          nothing left of its own to protect.  Verified: 25
//                          of 25 hands, both leans, no conjunction needed.
//   BOTH reachable      -- needs the conjunction probe (C0/C1) and, in the
//                          cell neither forces, the delicate-cell tiebreak.
//                          NOT this file -- see item 60 step 2.
//
// This file is the first two rows of that table.  It reuses only what
// already exists and is already trusted: `solve_cooperative` (item 2b) and
// the ordinary single-nil `solve()` via `seat_roles_from_nil`.  It touches
// nothing about the conjunction machinery -- deliberately, since reusing
// that safely (its own internal shape gate currently checks for strict
// opposition specifically) is its own, separate step, not folded in here
// to finish the table in one patch.
#ifndef NIL_SAME_LEAN_HPP
#define NIL_SAME_LEAN_HPP

#include <cstdint>
#include <string>

#include "nil/position.hpp"
#include "nil/search.hpp"
#include "nil/seats.hpp"

namespace nil {

struct SameLeanOutcome {
    // Bit `s` set for each seat whose bid FAILS.  Meaningless when
    // `determined` is false.
    unsigned nils_set_mask = 0;
    // True when this call alone settled the outcome (the "neither" or
    // "exactly one" branches above).  False means both bids are
    // individually reachable -- the third branch, which is step 2's job,
    // not an error condition here.
    bool determined = false;
    std::uint64_t nodes = 0;
};

// Attempts the first two branches of item 60's decision procedure for a
// same-lean opposing-nils position.  Returns false only on a genuine error
// (an invalid position, or roles that are not two live bids on opposing
// sides); returning true with `out.determined == false` is the expected,
// non-error way of saying "the remaining branch is not this function's job."
bool solve_same_lean_partial(const Position& pos, const SeatRoles& roles,
                              const SearchOptions& opts, SameLeanOutcome& out,
                              std::string& err);

}  // namespace nil

#endif  // NIL_SAME_LEAN_HPP
