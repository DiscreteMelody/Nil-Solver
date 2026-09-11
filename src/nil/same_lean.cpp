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
                           coop_near, sub_err)) {
        err = sub_err;
        return false;
    }
    if (!solve_cooperative(pos, roles, 1u << far, opts.use_memo, opts.collapse_equivalents,
                           coop_far, sub_err)) {
        err = sub_err;
        return false;
    }
    total_nodes += coop_near.nodes + coop_far.nodes;

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

    // Both individually reachable: step 2's branch, not this function's.
    out.determined = false;
    out.nodes = total_nodes;
    return true;
}

}  // namespace nil
