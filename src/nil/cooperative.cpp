#include "nil/cooperative.hpp"

#include <unordered_map>

#include "nil/rules.hpp"

namespace nil {
namespace {

// The literal-state key nil_oracle.py's `_CoopCtx.memo` uses, and the one
// statekey.hpp's own header comment describes the main table as having
// replaced: four raw hand masks, the trick in progress, the leader and the
// spades-broken flag.  See cooperative.hpp for why that is a faithful port
// rather than a missed opportunity.
//
// NO PROTECT MASK IN THE KEY, and that is a property of the question rather
// than an oversight -- ported comment for ported reasoning.  A protected
// seat's bid ends the line the instant it wins a trick, so recursion is never
// entered past that point; every node that reaches the memo therefore has the
// same history in the only respect that matters (no protected bid down yet),
// and `protect_mask` is fixed for the whole call in CoopCtx rather than
// varying per key.
struct CoopKey {
    Hand hands[4];
    CardId trick[3];
    std::int8_t trick_len;
    std::int8_t leader;
    bool broken;

    bool operator==(const CoopKey& o) const {
        return hands[0] == o.hands[0] && hands[1] == o.hands[1] && hands[2] == o.hands[2] &&
               hands[3] == o.hands[3] && trick[0] == o.trick[0] && trick[1] == o.trick[1] &&
               trick[2] == o.trick[2] && trick_len == o.trick_len && leader == o.leader &&
               broken == o.broken;
    }
};

struct CoopKeyHash {
    std::size_t operator()(const CoopKey& k) const noexcept {
        std::uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
        auto mix = [&h](std::uint64_t v) {
            h ^= v;
            h *= 1099511628211ull;  // FNV-1a prime
        };
        mix(k.hands[0]);
        mix(k.hands[1]);
        mix(k.hands[2]);
        mix(k.hands[3]);
        // Offset by one so NO_CARD (-1) does not collide with a real card 0.
        mix(static_cast<std::uint64_t>(k.trick[0] + 1));
        mix(static_cast<std::uint64_t>(k.trick[1] + 1));
        mix(static_cast<std::uint64_t>(k.trick[2] + 1));
        mix((static_cast<std::uint64_t>(k.trick_len) << 8) |
            (static_cast<std::uint64_t>(k.leader) << 2) | (k.broken ? 1u : 0u));
        return static_cast<std::size_t>(h);
    }
};

using CoopMemo = std::unordered_map<CoopKey, bool, CoopKeyHash>;

struct CoopCtx {
    unsigned protect_mask = 0;
    bool collapse = true;
    CoopMemo* memo = nullptr;  // null when the caller asked for no memo
    std::uint64_t nodes = 0;
};

// Mirrors nil_oracle.py's _search_cooperative node for node: is there ANY
// line from here on which no seat in ctx.protect_mask ever wins a trick?
//
// `hands` is mutated and restored (play, recurse, undo) rather than copied
// per call, which nil_oracle.py cannot do -- Python hands are immutable
// tuples there, rebuilt per move, and that rebuild is not what this port is
// trying to reproduce.  The state a caller must see identically is the
// ANSWER and the NODE COUNT, both of which are unaffected by how a card's
// removal is represented.
bool search_cooperative(Hand hands[4], int leader, const CardId* trick, int trick_len,
                        bool broken, CoopCtx& ctx) {
    ++ctx.nodes;

    if (!(hands[0] | hands[1] | hands[2] | hands[3])) {
        // Every card played, and nothing pruned this line along the way --
        // see below, a protected seat winning ends the line immediately
        // rather than reaching this check with a dead protected bid.
        return true;
    }

    CoopKey key;
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
        const auto it = ctx.memo->find(key);
        if (it != ctx.memo->end()) return it->second;
    }

    const int seat = (leader + trick_len) & 3;
    const int led_suit = trick_len ? card_suit(trick[0]) : -1;
    Hand moves = legal_moves(hands[seat], trick_len, led_suit, broken);

    // Item 35's reduction (rules.hpp), read for a reachability question rather
    // than a value one -- see cooperative.hpp for why the same argument holds.
    // Skipped outright with one legal card, matching the gate search.cpp uses
    // for the same computation.
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
            if (ctx.protect_mask & (1u << winner)) {
                // A protected bid just died.  A bid never un-breaks, so
                // nothing downstream can repair this line; drop it without
                // recursing, exactly as the oracle's `continue` does.
                found = false;
            } else {
                // trick_len=0 below means the callee reads none of `trick`,
                // so the completed trick's own storage (about to go out of
                // scope on return) is never touched through the new call.
                found = search_cooperative(hands, winner, trick, 0, next_broken, ctx);
            }
        } else {
            CardId next_trick[3];
            for (int i = 0; i < trick_len; ++i) next_trick[i] = trick[i];
            next_trick[trick_len] = card;
            found = search_cooperative(hands, leader, next_trick, trick_len + 1, next_broken,
                                       ctx);
        }

        hands[seat] = saved;  // undo, whether or not this line succeeded

        if (found) {
            // OR at every node: one surviving line is the whole answer.
            result = true;
            break;
        }
    }

    if (have_key) (*ctx.memo)[key] = result;
    return result;
}

// The dual question's key.  Same literal-state fields as CoopKey, plus
// `satisfied`: which of ctx.fail_mask's seats have already taken a trick on
// this line.  Needed here and not above because "has this seat's requirement
// already been met" is not implied by the other fields the way "is this
// protected seat still alive" is -- there, dying ends the line outright, so
// no node past that point exists to need a bit for it.
struct FailKey {
    Hand hands[4];
    CardId trick[3];
    std::int8_t trick_len;
    std::int8_t leader;
    bool broken;
    std::uint8_t satisfied;

    bool operator==(const FailKey& o) const {
        return hands[0] == o.hands[0] && hands[1] == o.hands[1] && hands[2] == o.hands[2] &&
               hands[3] == o.hands[3] && trick[0] == o.trick[0] && trick[1] == o.trick[1] &&
               trick[2] == o.trick[2] && trick_len == o.trick_len && leader == o.leader &&
               broken == o.broken && satisfied == o.satisfied;
    }
};

struct FailKeyHash {
    std::size_t operator()(const FailKey& k) const noexcept {
        std::uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
        auto mix = [&h](std::uint64_t v) {
            h ^= v;
            h *= 1099511628211ull;  // FNV-1a prime
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

using FailMemo = std::unordered_map<FailKey, bool, FailKeyHash>;

struct FailCtx {
    unsigned fail_mask = 0;
    bool collapse = true;
    FailMemo* memo = nullptr;
    std::uint64_t nodes = 0;
};

// Is there ANY line from here on which every seat in ctx.fail_mask ends up
// having taken at least one trick?  `satisfied` names which of them already
// have, as of entry to this call; it only ever grows along a line, never
// shrinks, which is what makes it sound to fold into the memo key rather than
// re-derive from the trick history on every lookup.
bool search_cooperative_fail(Hand hands[4], int leader, const CardId* trick, int trick_len,
                             bool broken, unsigned satisfied, FailCtx& ctx) {
    ++ctx.nodes;

    if (!(hands[0] | hands[1] | hands[2] | hands[3])) {
        // Every card played: did every required seat collect its trick along
        // the way?  `satisfied` already reflects the whole line, so there is
        // nothing left to check but the set itself.
        return satisfied == ctx.fail_mask;
    }

    FailKey key;
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

    // Same reduction, same argument as search_cooperative: which seat wins is
    // decided by the rules alone, and `satisfied` is a function of who wins,
    // so relabelling two rank-equivalent cards changes neither.
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
            // Unlike the protect side, a win here is progress, not a dead
            // end: fold it into `satisfied` (a no-op if already set, or if
            // this winner is not one of the seats being asked about) and
            // keep going -- there is no early success either, since every
            // OTHER named seat still has to collect its own trick too.
            const unsigned next_satisfied =
                satisfied | (ctx.fail_mask & (1u << winner));
            found = search_cooperative_fail(hands, winner, trick, 0, next_broken,
                                            next_satisfied, ctx);
        } else {
            CardId next_trick[3];
            for (int i = 0; i < trick_len; ++i) next_trick[i] = trick[i];
            next_trick[trick_len] = card;
            found = search_cooperative_fail(hands, leader, next_trick, trick_len + 1,
                                            next_broken, satisfied, ctx);
        }

        hands[seat] = saved;  // undo, whether or not this line succeeded

        if (found) {
            // Still OR at every node: one line where everyone asked-about
            // eventually takes a trick is the whole answer.
            result = true;
            break;
        }
    }

    if (have_key) (*ctx.memo)[key] = result;
    return result;
}

}  // namespace

bool solve_cooperative(const Position& pos, const SeatRoles& roles, unsigned protect_mask,
                       bool use_memo, bool collapse_equivalents, CooperativeSolution& out,
                       std::string& err) {
    if (!validate(pos, err)) return false;

    if (protect_mask & ~0xFu) {
        err = "protect_mask has bits set outside the four seats";
        return false;
    }
    for (int seat = 0; seat < 4; ++seat) {
        if (!(protect_mask & (1u << seat))) continue;
        if (roles[seat] == ROLE_NIL_SET) {
            err = std::string("seat ") + SEAT_CHARS[seat] +
                  " was declared already down, so its bid cannot be protected; drop it from "
                  "the protect set or change its role";
            return false;
        }
        if (roles[seat] != ROLE_NIL) {
            err = std::string("seat ") + SEAT_CHARS[seat] +
                  " did not bid nil, so there is nothing there to protect";
            return false;
        }
    }

    CoopCtx ctx;
    ctx.protect_mask = protect_mask;
    ctx.collapse = collapse_equivalents;
    CoopMemo memo;
    ctx.memo = use_memo ? &memo : nullptr;

    Hand hands[4] = {pos.hands[0], pos.hands[1], pos.hands[2], pos.hands[3]};
    const CardId trick[3] = {pos.trick[0], pos.trick[1], pos.trick[2]};

    out.protect_mask = protect_mask;
    out.reachable =
        search_cooperative(hands, pos.leader, trick, pos.trick_len, pos.spades_broken, ctx);
    out.nodes = ctx.nodes;
    return true;
}

bool solve_cooperative_fail(const Position& pos, const SeatRoles& roles, unsigned fail_mask,
                            bool use_memo, bool collapse_equivalents,
                            CooperativeFailSolution& out, std::string& err) {
    if (!validate(pos, err)) return false;

    if (fail_mask & ~0xFu) {
        err = "fail_mask has bits set outside the four seats";
        return false;
    }
    for (int seat = 0; seat < 4; ++seat) {
        if (!(fail_mask & (1u << seat))) continue;
        if (roles[seat] == ROLE_NIL_SET) {
            err = std::string("seat ") + SEAT_CHARS[seat] +
                  " was declared already down, so its bid has already failed by the caller's "
                  "own assertion; drop it from the fail set rather than asking this to "
                  "re-derive it";
            return false;
        }
        if (roles[seat] != ROLE_NIL) {
            err = std::string("seat ") + SEAT_CHARS[seat] +
                  " did not bid nil, so there is no bid there to set";
            return false;
        }
    }

    FailCtx ctx;
    ctx.fail_mask = fail_mask;
    ctx.collapse = collapse_equivalents;
    FailMemo memo;
    ctx.memo = use_memo ? &memo : nullptr;

    Hand hands[4] = {pos.hands[0], pos.hands[1], pos.hands[2], pos.hands[3]};
    const CardId trick[3] = {pos.trick[0], pos.trick[1], pos.trick[2]};

    out.fail_mask = fail_mask;
    // `satisfied` starts empty: no seat has taken a trick in the part of the
    // deal still to be played, which is the only part this search can see.
    out.reachable = search_cooperative_fail(hands, pos.leader, trick, pos.trick_len,
                                            pos.spades_broken, 0u, ctx);
    out.nodes = ctx.nodes;
    return true;
}

}  // namespace nil
