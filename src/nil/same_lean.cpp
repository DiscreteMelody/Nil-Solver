#include "nil/same_lean.hpp"

#include "nil/cooperative.hpp"

namespace nil {

bool solve_same_lean_partial(const Position& pos, const SeatRoles& roles,
                              const SearchOptions& opts, SameLeanOutcome& out,
                              std::string& err) {
    const unsigned live = live_nil_mask(roles);
    int near = -1;
    int far = -1;
    for (int s = 0; s < 4; ++s) {
        if (!(live & (1u << s))) continue;
        if (near < 0) {
            near = s;
        } else {
            far = s;
        }
    }
    if (near < 0 || far < 0) {
        err = "solve_same_lean_partial needs exactly two live bids on opposing sides (" +
              describe_seat_roles(roles) + ")";
        return false;
    }

    std::uint64_t total_nodes = 0;
    std::string sub_err;

    CooperativeSolution coop_near;
    CooperativeSolution coop_far;
    if (!solve_cooperative(pos, roles, 1u << near, opts.use_memo, opts.collapse_equivalents,
                           opts.same_lean_probe_budget, coop_near, sub_err)) {
        err = sub_err;
        return false;
    }
    if (!solve_cooperative(pos, roles, 1u << far, opts.use_memo, opts.collapse_equivalents,
                           opts.same_lean_probe_budget, coop_far, sub_err)) {
        err = sub_err;
        return false;
    }
    total_nodes += coop_near.nodes + coop_far.nodes;

    // A probe that gave up has told us nothing.  Every branch below reads
    // these two booleans, so an exhausted probe must stop the procedure here
    // rather than be read as `false` -- "no line exists" and "I stopped
    // looking" are different facts and only the first is an answer.
    if (coop_near.exhausted || coop_far.exhausted) {
        out.determined = false;
        out.exhausted = true;
        out.nodes = total_nodes;
        return true;
    }

    if (!coop_near.reachable && !coop_far.reachable) {
        // Neither bid is reachable even with every seat helping: both fail,
        // regardless of anyone's actual play.  Nothing left to search.
        out.nils_set_mask = (1u << near) | (1u << far);
        out.determined = true;
        out.nodes = total_nodes;
        return true;
    }

    if (coop_near.reachable != coop_far.reachable) {
        // Exactly one is doomed.  Its partner has nothing of its own left to
        // protect, so an ordinary single-nil check on the survivor -- built
        // via seat_roles_from_nil, not the parity-hardcoded shortcut that
        // gave a wrong answer two turns ago -- settles its fate.
        const int doomed = coop_near.reachable ? far : near;
        const int survivor = coop_near.reachable ? near : far;
        const SeatRoles single_roles = seat_roles_from_nil(survivor, false);
        SearchOptions single_opts = opts;
        single_opts.mode = MODE_FAST;
        single_opts.conjunction_seat = -1;
        Solution single_sol;
        if (!solve(pos, single_roles, single_opts, single_sol, sub_err)) {
            err = sub_err;
            return false;
        }
        total_nodes += single_sol.nodes;
        out.nils_set_mask = (1u << doomed) | (single_sol.nils_set ? (1u << survivor) : 0u);
        out.determined = true;
        out.nodes = total_nodes;
        return true;
    }

    // ---- the delicate cell: both bids individually reachable ---------------
    //
    // Resolved by the conjunction probe on each side.  C(side) asks "can this
    // side force ITS bid to live while the other's dies" -- rank 3, its best
    // outcome under either lean.  Every rung below is proved in the ROADMAP
    // entry and checked against the oracle's exhaustive backward induction.
    auto can_force = [&](int attacker_seat, bool& result) -> bool {
        SearchOptions conj_opts = opts;
        conj_opts.mode = MODE_FAST;
        conj_opts.conjunction_seat = attacker_seat;
        Solution conj_sol;
        if (!solve(pos, roles, conj_opts, conj_sol, sub_err)) return false;
        total_nodes += conj_sol.nodes;
        result = conj_sol.conjunction;
        return true;
    };

    bool near_forces = false;
    bool far_forces = false;
    if (!can_force(near, near_forces)) {
        err = sub_err;
        return false;
    }
    if (!can_force(far, far_forces)) {
        err = sub_err;
        return false;
    }

    // Rung 1: if a side can force rank 3, it takes it.  Both sides cannot --
    // rank 3 for one is rank 0 for the other, and each is a guarantee against
    // ANY play by the opponent, so the two cannot hold at once.
    if (near_forces && far_forces) {
        err = "internal inconsistency: both sides report forcing their own rank 3, which "
              "cannot both be true (" + describe_seat_roles(roles) + ")";
        return false;
    }
    if (near_forces) {
        out.nils_set_mask = 1u << far;
        out.determined = true;
        out.nodes = total_nodes;
        return true;
    }
    if (far_forces) {
        out.nils_set_mask = 1u << near;
        out.determined = true;
        out.nodes = total_nodes;
        return true;
    }

    // Rung 2: neither side can force the trade.  UNRESOLVED, and the rule
    // that was going to go here is DISPROVED rather than merely unwritten.
    //
    // What survives: the equilibrium is confined to the two MIDDLE outcomes
    // (both bids live, or both die) -- each side has a strategy preventing
    // the other's rank 3, and rank 0 is worse than every alternative, so no
    // optimal side declines that guarantee.  Measured: across 232 delicate
    // cells at 4 cards the oracle's own equilibrium is (live,live) or
    // (die,die) every time, never a mixed outcome.
    //
    // What does NOT survive: "both leans agree which middle they prefer, so
    // the preferred one wins whenever it is REACHABLE."  Cooperative
    // reachability is the wrong criterion, and measurably so -- 48 of 116
    // wrong under the OPPONENT lean, 5 of 116 under COVER.  The mechanism is
    // clearest on the OPPONENT side, traced on a concrete deal: both sides
    // rank (die,die) above (live,live), and (die,die) IS cooperatively
    // reachable, yet the equilibrium is (live,live).  Getting there needs TWO
    // separate events -- each bidder taking a trick -- and between them the
    // position is asymmetric: whichever bid is still alive now has rank 3
    // available (mine lives, theirs already dead), which beats the rank 2 it
    // was cooperating toward.  So that side defects, and the side that went
    // first lands on rank 0.  Anticipating it, neither goes first.  A
    // cooperative probe cannot see that, because cooperation is exactly the
    // assumption it makes.
    //
    // So this cell needs a criterion that models DEFECTION, not reachability.
    // Refused here rather than guessed at; `determined` stays false and
    // solve() reports it as unhandled.
    out.determined = false;
    out.nodes = total_nodes;
    return true;
}

}  // namespace nil
