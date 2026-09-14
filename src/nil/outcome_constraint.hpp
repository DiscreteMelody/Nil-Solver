// ROADMAP item 60, step 3 of 5: the hard outcome constraint, on its own.
//
// WHAT THIS ANSWERS.  Given an outcome the presolve has already pinned --
// "nil 1 is set and nil 2 makes" -- does a line of play producing EXACTLY
// that outcome exist, and what does the constraint cost to enforce?  No
// objective, no trick counts, no optimisation: this is the FILTER, shipped
// and tested by itself, so that whatever ends up being optimised inside it
// (step 4) is a separate question with its own separate answer.
//
// WHY IT IS NOT AN OBJECTIVE.  The outcome is pinned by the presolve, not
// discovered here.  T's framing: "solve where nil 1 is set and nil 2 makes
// while maximizing tricks for the cover hands" -- the first half is this
// file, the second half is step 4, and keeping them apart is deliberate,
// because step 4's objective turned out NOT to be the ordinary zero-sum
// question it looked like (the doomed bidder's own tricks count for neither
// side, so the two sides' counts do not sum to a constant).  Nothing in this
// file depends on how that is resolved.
//
// THE CONSTRAINT IS AN `OR` OF DISCARDS, exactly as T specified: with nil 1
// pinned SET and nil 2 pinned MAKES, discard every line where nil 1 makes OR
// nil 2 is set.  Equivalently, keep only lines where every seat in
// `require_set_mask` takes at least one trick AND no seat in
// `require_live_mask` takes any.
//
// THE TWO HALVES ARE NOT SYMMETRIC, and that is the main thing this file
// encodes:
//
//   * `require_live_mask` (must MAKE) is checkable the instant it is
//     violated.  A bid never un-breaks, so the moment such a seat wins a
//     trick the whole subtree below is dead -- the line is abandoned WITHOUT
//     RECURSING.  That is genuine pruning, and it is free.
//
//   * `require_set_mask` (must be SET) has no cheap early failure.  "This
//     seat still has cards and has not won yet" does not mean doomed; it may
//     win on the last trick.  Absent a structural proof that its remaining
//     cards can never win again -- which this file does not attempt -- the
//     only certain failure point is the TERMINAL state.  So this half is
//     enforced at the end of the line, and the memo (not pruning) is what
//     keeps it from being pure waste.
//
// INVALID LINES ARE SKIPPED, NOT SCORED, and a note on why that matters even
// though this file has no scores yet.  An earlier design sketch proposed
// marking a dead line with a sentinel value -- "negative infinity for
// whoever is maximising".  THAT IS WRONG and step 4 must not inherit it: at
// a MINIMISING node a hugely negative value is attractive, so a minimiser
// would steer INTO dead lines rather than away from them.  Flipping the
// sentinel's sign per node type fails for the mirror reason, once the value
// propagates up past a node of the other type.  The only sound shape is the
// one used here and in `cooperative.cpp`: a dead child is SKIPPED by its
// parent, and "every child here was dead" propagates as its own signal,
// distinct from any value.  `bool` is that signal in this file; step 4 needs
// the same separation between "no valid line" and "a valid line worth N".
#ifndef NIL_OUTCOME_CONSTRAINT_HPP
#define NIL_OUTCOME_CONSTRAINT_HPP

#include <cstdint>
#include <string>

#include "nil/position.hpp"
#include "nil/seats.hpp"

namespace nil {

struct ConstrainedLineSolution {
    unsigned require_live_mask = 0;
    unsigned require_set_mask = 0;
    // Does a line satisfying BOTH halves exist?
    bool satisfiable = false;
    std::uint64_t nodes = 0;
    // Lines abandoned by the must-make half without recursing.  Reported
    // because the two halves' costs are worth telling apart when step 4 comes
    // to pay them: this is the half that prunes, and this is how much.
    std::uint64_t live_prunes = 0;
};

// Is the pinned outcome reachable at all?
//
// `require_live_mask` and `require_set_mask` must be disjoint, and every bit
// in either must name a seat holding a LIVE bid (`roles[seat] == ROLE_NIL`).
// A seat already carrying ROLE_NIL_SET is refused from BOTH masks rather
// than silently accommodated: its bid has already failed by the caller's own
// assertion, so requiring it to make is a contradiction and requiring it to
// be set is a tautology -- either way the caller has misunderstood the state
// and should be told, not answered.
//
// `use_memo` and `collapse_equivalents` mirror the same fields on
// SearchOptions and change node counts only, never the answer.  The
// equivalence-class argument carries over from rules.hpp unchanged: which
// seat wins a trick is decided by the rules alone, and both halves of the
// constraint are functions of who wins.
bool solve_constrained_line(const Position& pos, const SeatRoles& roles,
                            unsigned require_live_mask, unsigned require_set_mask,
                            bool use_memo, bool collapse_equivalents,
                            ConstrainedLineSolution& out, std::string& err);

// ---------------------------------------------------------------------------
// STEP 4: the same constraint, now with something optimised inside it.
//
// THE PRIORITY, T's, and it is one idea rather than three rules.  A trick
// taken by a nil bidder counts toward NOBODY's bid -- not even its own
// partner's -- so a trick landing on a set nil is a wasted trick for that
// team.  Each side therefore wants, in strict order:
//
//   1. MAXIMISE its own cover hand's tricks
//   2. MINIMISE the other side's cover hand's tricks
//   3. MAXIMISE the other side's NIL's tricks
//
// Level 3 is level 2 seen from the other end: the four counts sum to the
// tricks remaining, so with levels 1 and 2 fixed, maximising the opponent's
// nil is identical to minimising one's OWN nil's tricks.  Nothing in the
// order pulls against anything else, and the order is COMPLETE -- fixing two
// cover hands and one nil determines the fourth count, so no ties survive it.
// Level 3 is vacuous when a nil is pinned to MAKE (it takes zero tricks by
// definition) and binds only when both nils are set.
//
// THIS IS NOT A ZERO-SUM SCALAR, which is why it is its own search rather
// than a new weight on the existing engine.  Both cover hands can prefer the
// SAME outcome -- each would rather take a trick itself than let a dead nil
// absorb it -- and two objectives that sometimes agree cannot be one number
// with a sign flip.  Measured before building: against the closest zero-sum
// stand-in ("maximise my tricks minus theirs", which WOULD drop into the
// existing alpha-beta), this order disagrees on 3 of 227 hands, and the
// disagreements are real rather than tie-breaking noise -- the zero-sum
// stand-in gives up a trick of its OWN to cost the opponent two, which this
// order never does.
//
// SUFFIX COUNTS, and why the memo is still sound.  Each node returns the
// tricks taken FROM THERE ON, not the running total.  A lexicographic
// comparison of (prefix + suffix) against (prefix + suffix') with the same
// prefix is decided identically by comparing the suffixes alone, component
// by component, so a memo entry computed under one prefix is valid under any
// other.  `satisfied` still belongs in the key, because the must-be-set half
// is a fact about the prefix, not about the suffix's value.
struct ConstrainedTricksSolution {
    unsigned require_live_mask = 0;
    unsigned require_set_mask = 0;
    bool satisfiable = false;
    // Tricks per seat on the chosen line.  Meaningless when !satisfiable, and
    // equally meaningless when `exhausted` -- see below.
    int seat_tricks[4] = {0, 0, 0, 0};
    std::uint64_t nodes = 0;
    // Did the search hit its node budget before finishing?  This search does
    // not reach 13 cards (see the patch 102 ROADMAP entry), so a caller that
    // wires it into a general-purpose entry point MUST be able to give up
    // rather than hang.  When this is true EVERY other field except `nodes`
    // is meaningless: an aborted search is discarded whole, never partially
    // believed.
    bool exhausted = false;
};

// Same constraint and same validation as solve_constrained_line, plus: each
// side must hold exactly one live bid, since the priority above is written in
// terms of "this side's nil" and "this side's cover hand".
// `node_budget` of 0 means no limit.  Any other value caps the search: on
// hitting it the run stops and reports `exhausted`, with no partial answer.
bool solve_constrained_tricks(const Position& pos, const SeatRoles& roles,
                              unsigned require_live_mask, unsigned require_set_mask,
                              bool use_memo, bool collapse_equivalents,
                              std::uint64_t node_budget,
                              ConstrainedTricksSolution& out, std::string& err);

}  // namespace nil

#endif  // NIL_OUTCOME_CONSTRAINT_HPP
