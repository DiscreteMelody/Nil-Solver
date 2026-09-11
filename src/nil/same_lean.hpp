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
//   BOTH reachable      -- the delicate cell.  PARTLY resolved (step 2): if
//                          either side can force rank 3 -- its own bid alive,
//                          the other's dead -- it takes it, settled by the
//                          item 78 conjunction probe.  When NEITHER side can
//                          force it, the cell is still open: the tiebreak
//                          that was going to fill it (cooperative
//                          reachability of the shared lean's preferred
//                          middle) is DISPROVED, measured wrong on 48 of 116
//                          OPPONENT-lean and 5 of 116 COVER-lean cells.  See
//                          same_lean.cpp's rung 2 comment and the ROADMAP
//                          entry.
//
// This file is the whole table as of step 2.  It reuses only what already
// exists and is already trusted: `solve_cooperative` (item 2b), the ordinary
// single-nil `solve()` via `seat_roles_from_nil`, and the item 78 conjunction
// probe via `SearchOptions::conjunction_seat` (whose shape gate step 2
// widened to admit this lean, on the ground that its indicator is rank 3 and
// `side_rank` decides rank 3 before it reads the lean).
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
    // True when this call settled the outcome.  False means the position
    // landed in the one cell still open -- both bids individually reachable
    // AND neither side able to force rank 3 -- which solve() reports as
    // unhandled rather than guessing at.
    bool determined = false;
    std::uint64_t nodes = 0;
};

// Runs item 60's decision procedure for a same-lean opposing-nils position.
// Returns false only on a genuine error (an invalid position, or roles that
// are not two live bids on opposing sides).  Returning true with
// `out.determined == false` is the expected, non-error way of saying the
// position landed in the cell the procedure does not yet resolve.
bool solve_same_lean_partial(const Position& pos, const SeatRoles& roles,
                              const SearchOptions& opts, SameLeanOutcome& out,
                              std::string& err);

}  // namespace nil

#endif  // NIL_SAME_LEAN_HPP
