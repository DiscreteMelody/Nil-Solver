#include "nil/outcome_constraint.hpp"

#include <unordered_map>

#include "nil/rules.hpp"

namespace nil {
namespace {

// Literal-state key plus `satisfied`: which of require_set_mask's seats have
// already taken a trick on this line.  Needed for the same reason
// cooperative.cpp's FailKey needs it -- "has this seat's requirement already
// been met" is not implied by the cards, and only ever grows along a line, so
// it is sound to fold into the key rather than re-derive.
//
// require_live_mask needs NO bit here: a violating line is abandoned at the
// violation, so no node past that point exists to key.
struct ConstraintKey {
    Hand hands[4];
    CardId trick[3];
    std::int8_t trick_len;
    std::int8_t leader;
    bool broken;
    std::uint8_t satisfied;

    bool operator==(const ConstraintKey& o) const {
        return hands[0] == o.hands[0] && hands[1] == o.hands[1] && hands[2] == o.hands[2] &&
               hands[3] == o.hands[3] && trick[0] == o.trick[0] && trick[1] == o.trick[1] &&
               trick[2] == o.trick[2] && trick_len == o.trick_len && leader == o.leader &&
               broken == o.broken && satisfied == o.satisfied;
    }
};

struct ConstraintKeyHash {
    std::size_t operator()(const ConstraintKey& k) const noexcept {
        std::uint64_t h = 1469598103934665603ull;
        auto mix = [&h](std::uint64_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(k.hands[0]);
        mix(k.hands[1]);
        mix(k.hands[2]);
        mix(k.hands[3]);
        mix(static_cast<std::uint64_t>(k.trick[0] + 1));
        mix(static_cast<std::uint64_t>(k.trick[1] + 1));
        mix(static_cast<std::uint64_t>(k.trick[2] + 1));
        mix((static_cast<std::uint64_t>(k.trick_len) << 8) |
            (static_cast<std::uint64_t>(k.leader) << 2) | (k.broken ? 1u : 0u));
        mix(static_cast<std::uint64_t>(k.satisfied));
        return static_cast<std::size_t>(h);
    }
};

using ConstraintMemo = std::unordered_map<ConstraintKey, bool, ConstraintKeyHash>;

struct ConstraintCtx {
    unsigned require_live = 0;
    unsigned require_set = 0;
    bool collapse = true;
    ConstraintMemo* memo = nullptr;
    std::uint64_t nodes = 0;
    std::uint64_t live_prunes = 0;
};

bool search_constrained(Hand hands[4], int leader, const CardId* trick, int trick_len,
                        bool broken, unsigned satisfied, ConstraintCtx& ctx) {
    ++ctx.nodes;

    if (!(hands[0] | hands[1] | hands[2] | hands[3])) {
        // The must-make half needs no check here: a line that violated it was
        // abandoned at the violation and never reached this point.  The
        // must-be-set half is checked here and only here -- see the header.
        return satisfied == ctx.require_set;
    }

    ConstraintKey key;
    const bool have_key = ctx.memo != nullptr;
    if (have_key) {
        key.hands[0] = hands[0];
        key.hands[1] = hands[1];
        key.hands[2] = hands[2];
        key.hands[3] = hands[3];
        key.trick[0] = trick_len > 0 ? trick[0] : NO_CARD;
        key.trick[1] = trick_len > 1 ? trick[1] : NO_CARD;
        key.trick[2] = trick_len > 2 ? trick[2] : NO_CARD;
        key.trick_len = static_cast<std::int8_t>(trick_len);
        key.leader = static_cast<std::int8_t>(leader);
        key.broken = broken;
        key.satisfied = static_cast<std::uint8_t>(satisfied);
        const auto it = ctx.memo->find(key);
        if (it != ctx.memo->end()) return it->second;
    }

    const int seat = (leader + trick_len) & 3;
    const int led_suit = trick_len ? card_suit(trick[0]) : -1;
    Hand moves = legal_moves(hands[seat], trick_len, led_suit, broken);

    if (ctx.collapse && (moves & (moves - 1)) != 0) {
        const CardId winning_now = trick_best_card(trick, trick_len);
        const Hand relevant = relevant_cards(hands, winning_now);
        moves = distinct_moves(moves, relevant);
    }

    bool result = false;
    for (Hand rest = moves; rest;) {
        const CardId card = take_lowest(rest);

        const Hand saved = hands[seat];
        hands[seat] &= ~card_bit(card);
        const bool next_broken = spades_broken_after(broken, card_suit(card));

        bool found;
        if (trick_len == 3) {
            const CardId played[4] = {trick[0], trick[1], trick[2], card};
            const int winner = trick_winner(leader, played, 4);
            if (ctx.require_live & (1u << winner)) {
                // The must-make half, violated.  A bid never un-breaks, so
                // nothing below can repair it: abandon without recursing.
                ++ctx.live_prunes;
                found = false;
            } else {
                const unsigned next_satisfied =
                    satisfied | (ctx.require_set & (1u << winner));
                found = search_constrained(hands, winner, trick, 0, next_broken,
                                           next_satisfied, ctx);
            }
        } else {
            CardId next_trick[3];
            for (int i = 0; i < trick_len; ++i) next_trick[i] = trick[i];
            next_trick[trick_len] = card;
            found = search_constrained(hands, leader, next_trick, trick_len + 1, next_broken,
                                       satisfied, ctx);
        }

        hands[seat] = saved;

        if (found) {
            result = true;
            break;
        }
    }

    if (have_key) (*ctx.memo)[key] = result;
    return result;
}

}  // namespace

bool solve_constrained_line(const Position& pos, const SeatRoles& roles,
                            unsigned require_live_mask, unsigned require_set_mask,
                            bool use_memo, bool collapse_equivalents,
                            ConstrainedLineSolution& out, std::string& err) {
    if (!validate(pos, err)) return false;

    if ((require_live_mask | require_set_mask) & ~0xFu) {
        err = "a constraint mask has bits set outside the four seats";
        return false;
    }
    if (require_live_mask & require_set_mask) {
        err = "a seat cannot be required both to make and to be set";
        return false;
    }
    for (int seat = 0; seat < 4; ++seat) {
        const unsigned bit = 1u << seat;
        if (!((require_live_mask | require_set_mask) & bit)) continue;
        if (roles[seat] == ROLE_NIL_SET) {
            err = std::string("seat ") + SEAT_CHARS[seat] +
                  " was declared already down, so constraining its bid is either a "
                  "contradiction or a tautology; drop it from the masks";
            return false;
        }
        if (roles[seat] != ROLE_NIL) {
            err = std::string("seat ") + SEAT_CHARS[seat] +
                  " did not bid nil, so there is no bid there to constrain";
            return false;
        }
    }

    ConstraintCtx ctx;
    ctx.require_live = require_live_mask;
    ctx.require_set = require_set_mask;
    ctx.collapse = collapse_equivalents;
    ConstraintMemo memo;
    ctx.memo = use_memo ? &memo : nullptr;

    Hand hands[4] = {pos.hands[0], pos.hands[1], pos.hands[2], pos.hands[3]};
    const CardId trick[3] = {pos.trick[0], pos.trick[1], pos.trick[2]};

    out.require_live_mask = require_live_mask;
    out.require_set_mask = require_set_mask;
    out.satisfiable = search_constrained(hands, pos.leader, trick, pos.trick_len,
                                         pos.spades_broken, 0u, ctx);
    out.nodes = ctx.nodes;
    out.live_prunes = ctx.live_prunes;
    return true;
}

namespace {

// ---- step 4: the same constraint, with the priority optimised inside it ----

// Suffix trick counts, or "no valid line from here".  `valid` is a SEPARATE
// channel from the counts on purpose -- see the header's note on why a
// sentinel value cannot do this job.
struct TrickVal {
    bool valid = false;
    std::int8_t t[4] = {0, 0, 0, 0};
};

// The memo is a pure cache -- dropping an entry costs time, never accuracy --
// so it is safe to stop growing it at a bound.  An UNBOUNDED memo here is not
// a performance question but a robustness bug: at 13 cards the first draft of
// this search was OOM-killed by the host rather than returning anything, and
// a library that can kill the calling process is broken regardless of how
// fast it is on hands that fit.  Roughly 300 MB of entries; past that the
// search still finishes, just without new memoisation.
constexpr std::size_t TRICKS_MEMO_MAX_ENTRIES = 4u << 20;

struct TricksCtx {
    unsigned require_live = 0;
    unsigned require_set = 0;
    int nil_of_side[2] = {-1, -1};
    int cover_of_side[2] = {-1, -1};
    bool collapse = true;
    std::unordered_map<ConstraintKey, TrickVal, ConstraintKeyHash>* memo = nullptr;
    std::uint64_t nodes = 0;
    std::uint64_t budget = 0;  // 0 = unlimited
    bool aborted = false;
};

// Is `a` better than `b` for the side to which `seat` belongs?  T's order:
// own cover hand up, other cover hand down, other nil up.
inline bool better_for(const TricksCtx& ctx, int seat, const TrickVal& a, const TrickVal& b) {
    const int side = seat & 1;
    const int mine = ctx.cover_of_side[side];
    const int theirs = ctx.cover_of_side[side ^ 1];
    const int their_nil = ctx.nil_of_side[side ^ 1];
    if (a.t[mine] != b.t[mine]) return a.t[mine] > b.t[mine];
    if (a.t[theirs] != b.t[theirs]) return a.t[theirs] < b.t[theirs];
    return a.t[their_nil] > b.t[their_nil];
}

TrickVal search_constrained_tricks(Hand hands[4], int leader, const CardId* trick,
                                   int trick_len, bool broken, unsigned satisfied,
                                   TricksCtx& ctx) {
    ++ctx.nodes;
    if (ctx.budget && ctx.nodes > ctx.budget) {
        // Out of budget.  Returning an invalid TrickVal unwinds the whole
        // search cheaply; `aborted` is what tells the caller that the
        // invalidity means "gave up", not "no such line".  The two must not
        // be confused, which is why they are separate channels.
        ctx.aborted = true;
        return TrickVal();
    }

    TrickVal result;
    if (!(hands[0] | hands[1] | hands[2] | hands[3])) {
        result.valid = (satisfied == ctx.require_set);
        return result;  // all-zero suffix counts, which is correct at the end
    }

    ConstraintKey key;
    const bool have_key = ctx.memo != nullptr;
    if (have_key) {
        key.hands[0] = hands[0];
        key.hands[1] = hands[1];
        key.hands[2] = hands[2];
        key.hands[3] = hands[3];
        key.trick[0] = trick_len > 0 ? trick[0] : NO_CARD;
        key.trick[1] = trick_len > 1 ? trick[1] : NO_CARD;
        key.trick[2] = trick_len > 2 ? trick[2] : NO_CARD;
        key.trick_len = static_cast<std::int8_t>(trick_len);
        key.leader = static_cast<std::int8_t>(leader);
        key.broken = broken;
        key.satisfied = static_cast<std::uint8_t>(satisfied);
        const auto it = ctx.memo->find(key);
        if (it != ctx.memo->end()) return it->second;
    }

    const int seat = (leader + trick_len) & 3;
    const int led_suit = trick_len ? card_suit(trick[0]) : -1;
    Hand moves = legal_moves(hands[seat], trick_len, led_suit, broken);

    // COLLAPSED, and the first draft of this file did NOT collapse, on the
    // worry that cards equivalent for winning THIS trick might not be
    // equivalent for who wins the later ones.  That worry was wrong, and
    // measuring it is what showed so.  `relevant_cards` keeps exactly the
    // cards the rest of the deal can still tell apart, so playing either of
    // two collapsed cards leaves positions identical up to relabelling those
    // cards -- and a relabelling permutes card identities, never which SEAT
    // takes a trick.  Per-seat trick counts are therefore invariant under it,
    // which is all this search reports.  Verified rather than argued: the
    // crosscheck's splits are unchanged with collapsing on, and the cost
    // difference is the difference between finishing at 13 cards and not.
    if (ctx.collapse && (moves & (moves - 1)) != 0) {
        const CardId winning_now = trick_best_card(trick, trick_len);
        const Hand relevant = relevant_cards(hands, winning_now);
        moves = distinct_moves(moves, relevant);
    }

    bool have_best = false;
    TrickVal best;
    for (Hand rest = moves; rest;) {
        const CardId card = take_lowest(rest);

        const Hand saved = hands[seat];
        hands[seat] &= ~card_bit(card);
        const bool next_broken = spades_broken_after(broken, card_suit(card));

        TrickVal child;
        int winner = -1;
        if (trick_len == 3) {
            const CardId played[4] = {trick[0], trick[1], trick[2], card};
            winner = trick_winner(leader, played, 4);
            if (ctx.require_live & (1u << winner)) {
                child.valid = false;  // must-make violated: abandon, no recursion
            } else {
                const unsigned next_satisfied = satisfied | (ctx.require_set & (1u << winner));
                child = search_constrained_tricks(hands, winner, trick, 0, next_broken,
                                                  next_satisfied, ctx);
                if (child.valid) ++child.t[winner];  // this trick belongs to the suffix
            }
        } else {
            CardId next_trick[3];
            for (int i = 0; i < trick_len; ++i) next_trick[i] = trick[i];
            next_trick[trick_len] = card;
            child = search_constrained_tricks(hands, leader, next_trick, trick_len + 1,
                                              next_broken, satisfied, ctx);
        }

        hands[seat] = saved;

        if (!child.valid) continue;  // skipped, never scored -- see the header
        if (!have_best || better_for(ctx, seat, child, best)) {
            best = child;
            have_best = true;
        }
    }

    if (have_best) result = best;
    if (have_key && ctx.memo->size() < TRICKS_MEMO_MAX_ENTRIES) (*ctx.memo)[key] = result;
    return result;
}

}  // namespace

bool solve_constrained_tricks(const Position& pos, const SeatRoles& roles,
                              unsigned require_live_mask, unsigned require_set_mask,
                              bool use_memo, bool collapse_equivalents,
                              std::uint64_t node_budget,
                              ConstrainedTricksSolution& out, std::string& err) {
    ConstrainedLineSolution shape_check;
    // Reuse the filter's own validation verbatim rather than a second copy of
    // it: same masks, same rules, so a divergence between the two would be a
    // bug waiting to happen.
    if (!solve_constrained_line(pos, roles, require_live_mask, require_set_mask,
                                 /*use_memo=*/true, /*collapse_equivalents=*/true,
                                 shape_check, err)) {
        return false;
    }

    TricksCtx ctx;
    ctx.require_live = require_live_mask;
    ctx.require_set = require_set_mask;
    ctx.collapse = collapse_equivalents;
    ctx.budget = node_budget;
    for (int side = 0; side < 2; ++side) {
        int found = -1;
        for (int seat = side; seat < 4; seat += 2) {
            if (!roles.is_nil(seat)) continue;
            if (found >= 0) {
                err = "each side must hold exactly one live bid for the trick priority to be "
                      "written (" + describe_seat_roles(roles) + ")";
                return false;
            }
            found = seat;
        }
        if (found < 0) {
            err = "each side must hold exactly one live bid for the trick priority to be "
                  "written (" + describe_seat_roles(roles) + ")";
            return false;
        }
        ctx.nil_of_side[side] = found;
        ctx.cover_of_side[side] = (found + 2) & 3;
    }

    std::unordered_map<ConstraintKey, TrickVal, ConstraintKeyHash> memo;
    ctx.memo = use_memo ? &memo : nullptr;

    Hand hands[4] = {pos.hands[0], pos.hands[1], pos.hands[2], pos.hands[3]};
    const CardId trick[3] = {pos.trick[0], pos.trick[1], pos.trick[2]};

    const TrickVal v = search_constrained_tricks(hands, pos.leader, trick, pos.trick_len,
                                                 pos.spades_broken, 0u, ctx);
    out.require_live_mask = require_live_mask;
    out.require_set_mask = require_set_mask;
    out.exhausted = ctx.aborted;
    out.satisfiable = v.valid && !ctx.aborted;
    for (int s = 0; s < 4; ++s) out.seat_tricks[s] = ctx.aborted ? 0 : v.t[s];
    out.nodes = ctx.nodes;
    return true;
}

}  // namespace nil
