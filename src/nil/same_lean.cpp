#include "nil/same_lean.hpp"

#include <unordered_map>

#include "nil/cooperative.hpp"
#include "nil/rules.hpp"

namespace nil {

namespace {

// ---- the delicate cell, resolved by backward induction ---------------------
//
// WHY A SEARCH AND NOT A PROBE.  Every probe in this codebase asks either a
// fully cooperative question ("does a line exist") or a one-sided forcing one
// ("can this side guarantee X").  The open cell is neither: both sides prefer
// the SAME outcome -- same lean, so they agree which of the two middles they
// want -- yet they cannot simply take it, because the path there can pass
// through a state where one side prefers to defect to its rank 3.  Patch 100
// measured what ignoring that costs: cooperative reachability gets this cell
// wrong on 48 of 116 OPPONENT-lean deals.  Defection is a property of the
// TREE, so it takes a tree walk.
//
// WHAT MAKES IT TRACTABLE, and it is a genuine simplification rather than an
// approximation: `side_rank` is a BIJECTION from the four outcomes onto
// {0,1,2,3} under either lean (see seats.cpp).  So a mover's preference
// totally orders the outcomes with no ties, and two children that give the
// mover equal rank give the IDENTICAL outcome.  The equilibrium OUTCOME is
// therefore well defined without any tiebreak rule -- which line reaches it
// is not, and is not reported.  That is what lets this be an ordinary
// backward induction returning a 2-bit outcome rather than a maxn search
// carrying tuples it would have to break ties between.
//
// Bit 0 is the near bid broken, bit 1 the far bid broken.
struct EqResult {
    bool ok = false;       // false means the budget ran out, NOT an outcome
    unsigned broken = 0;
};

struct EqKey {
    Hand hands[4];
    CardId trick[3];
    std::int8_t trick_len;
    std::int8_t leader;
    bool spades_broken;
    std::uint8_t nils_broken;

    bool operator==(const EqKey& o) const {
        return hands[0] == o.hands[0] && hands[1] == o.hands[1] && hands[2] == o.hands[2] &&
               hands[3] == o.hands[3] && trick[0] == o.trick[0] && trick[1] == o.trick[1] &&
               trick[2] == o.trick[2] && trick_len == o.trick_len && leader == o.leader &&
               spades_broken == o.spades_broken && nils_broken == o.nils_broken;
    }
};

struct EqKeyHash {
    std::size_t operator()(const EqKey& k) const noexcept {
        std::uint64_t h = 1469598103934665603ull;
        auto mix = [&h](std::uint64_t v) { h ^= v; h *= 1099511628211ull; };
        mix(k.hands[0]); mix(k.hands[1]); mix(k.hands[2]); mix(k.hands[3]);
        mix(static_cast<std::uint64_t>(k.trick[0] + 1));
        mix(static_cast<std::uint64_t>(k.trick[1] + 1));
        mix(static_cast<std::uint64_t>(k.trick[2] + 1));
        mix((static_cast<std::uint64_t>(k.trick_len) << 8) |
            (static_cast<std::uint64_t>(k.leader) << 2) | (k.spades_broken ? 1u : 0u));
        mix(static_cast<std::uint64_t>(k.nils_broken));
        return static_cast<std::size_t>(h);
    }
};

struct EqCtx {
    int nil_of_side[2] = {-1, -1};
    SeatRole partner_role[2] = {ROLE_COVER, ROLE_COVER};
    bool collapse = true;
    std::unordered_map<EqKey, unsigned, EqKeyHash>* memo = nullptr;
    std::uint64_t nodes = 0;
    std::uint64_t budget = 0;
    bool aborted = false;
    std::size_t memo_cap = 4u << 20;
};

// Rank of `broken` from the point of view of the side to which `seat` belongs.
inline int rank_for(const EqCtx& ctx, int seat, unsigned broken) {
    const int side = seat & 1;
    const bool mine = (broken & (1u << side)) == 0;
    const bool theirs = (broken & (1u << (side ^ 1))) == 0;
    return side_rank(mine, theirs, ctx.partner_role[side]);
}

EqResult search_equilibrium(Hand hands[4], int leader, const CardId* trick, int trick_len,
                            bool spades_broken, unsigned nils_broken, EqCtx& ctx) {
    ++ctx.nodes;
    if (ctx.budget && ctx.nodes > ctx.budget) {
        ctx.aborted = true;
        return EqResult();
    }

    // Both bids already down: nothing either side does from here can change
    // the outcome, so the rest of the deal need not be played out.  This is
    // the one early exit available, and it is exact, not a heuristic.
    if (nils_broken == 0x3u) return EqResult{true, 0x3u};

    if (!(hands[0] | hands[1] | hands[2] | hands[3])) {
        return EqResult{true, nils_broken};
    }

    EqKey key;
    const bool have_key = ctx.memo != nullptr;
    if (have_key) {
        key.hands[0] = hands[0]; key.hands[1] = hands[1];
        key.hands[2] = hands[2]; key.hands[3] = hands[3];
        key.trick[0] = trick_len > 0 ? trick[0] : NO_CARD;
        key.trick[1] = trick_len > 1 ? trick[1] : NO_CARD;
        key.trick[2] = trick_len > 2 ? trick[2] : NO_CARD;
        key.trick_len = static_cast<std::int8_t>(trick_len);
        key.leader = static_cast<std::int8_t>(leader);
        key.spades_broken = spades_broken;
        key.nils_broken = static_cast<std::uint8_t>(nils_broken);
        const auto it = ctx.memo->find(key);
        if (it != ctx.memo->end()) return EqResult{true, it->second};
    }

    const int seat = (leader + trick_len) & 3;
    const int led_suit = trick_len ? card_suit(trick[0]) : -1;
    Hand moves = legal_moves(hands[seat], trick_len, led_suit, spades_broken);

    // Sound for the same reason as in outcome_constraint.cpp: a relabelling
    // of rank-equivalent cards permutes card identities, never which SEAT
    // takes a trick, and the outcome here is a function of which seats win.
    if (ctx.collapse && (moves & (moves - 1)) != 0) {
        const CardId winning_now = trick_best_card(trick, trick_len);
        const Hand relevant = relevant_cards(hands, winning_now);
        moves = distinct_moves(moves, relevant);
    }

    bool have_best = false;
    unsigned best = 0;
    for (Hand rest = moves; rest;) {
        const CardId card = take_lowest(rest);
        const Hand saved = hands[seat];
        hands[seat] &= ~card_bit(card);
        const bool next_spades = spades_broken_after(spades_broken, card_suit(card));

        EqResult child;
        if (trick_len == 3) {
            const CardId played[4] = {trick[0], trick[1], trick[2], card};
            const int winner = trick_winner(leader, played, 4);
            unsigned next_broken = nils_broken;
            for (int side = 0; side < 2; ++side) {
                if (winner == ctx.nil_of_side[side]) next_broken |= 1u << side;
            }
            child = search_equilibrium(hands, winner, trick, 0, next_spades, next_broken, ctx);
        } else {
            CardId next_trick[3];
            for (int i = 0; i < trick_len; ++i) next_trick[i] = trick[i];
            next_trick[trick_len] = card;
            child = search_equilibrium(hands, leader, next_trick, trick_len + 1, next_spades,
                                       nils_broken, ctx);
        }

        hands[seat] = saved;

        if (!child.ok) return EqResult();  // budget gone: abandon, report nothing
        if (!have_best || rank_for(ctx, seat, child.broken) > rank_for(ctx, seat, best)) {
            best = child.broken;
            have_best = true;
        }
    }

    if (have_key && ctx.memo->size() < ctx.memo_cap) (*ctx.memo)[key] = best;
    return EqResult{true, best};
}

}  // namespace

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
    // So this cell needs a criterion that models DEFECTION, not reachability
    // -- and that is what `search_equilibrium` above is.  It walks the tree
    // with each mover taking the child its OWN side_rank prefers, which is
    // exactly what a probe cannot express: the ability to see that a line
    // toward the shared preference passes through a node where the other
    // side would rather flip to its rank 3.
    EqCtx eq;
    eq.nil_of_side[0] = near;
    eq.nil_of_side[1] = far;
    eq.partner_role[0] = roles[(near + 2) & 3];
    eq.partner_role[1] = roles[(far + 2) & 3];
    eq.collapse = opts.collapse_equivalents;
    eq.budget = opts.same_lean_probe_budget;
    std::unordered_map<EqKey, unsigned, EqKeyHash> eq_memo;
    if (opts.use_memo) eq.memo = &eq_memo;

    Hand hands[4] = {pos.hands[0], pos.hands[1], pos.hands[2], pos.hands[3]};
    const CardId trick[3] = {pos.trick[0], pos.trick[1], pos.trick[2]};
    unsigned already = 0;
    for (int side = 0; side < 2; ++side) {
        if (roles[eq.nil_of_side[side]] == ROLE_NIL_SET) already |= 1u << side;
    }
    const EqResult eqr = search_equilibrium(hands, pos.leader, trick, pos.trick_len,
                                            pos.spades_broken, already, eq);
    total_nodes += eq.nodes;
    if (!eqr.ok) {
        out.determined = false;
        out.exhausted = true;
        out.nodes = total_nodes;
        return true;
    }
    // Translate the side-indexed outcome back to seat bits.
    out.nils_set_mask = 0;
    if (eqr.broken & 1u) out.nils_set_mask |= 1u << near;
    if (eqr.broken & 2u) out.nils_set_mask |= 1u << far;
    out.determined = true;
    out.nodes = total_nodes;
    return true;
}

}  // namespace nil
