// The rules of Spades card play, one small function each.
//
// These are deliberately one-to-one with nil_oracle.py's legal_moves,
// spades_broken_after, _beats and trick_winner so that a divergence can be
// localised to a single rule rather than to "the search".
#ifndef NIL_RULES_HPP
#define NIL_RULES_HPP

#include "nil/cards.hpp"

namespace nil {

// Legal plays for `hand`.  `trick_len` is how many cards are already on the
// trick; `led_suit` is the suit of the led card (ignored when trick_len == 0).
inline Hand legal_moves(Hand hand, int trick_len, int led_suit, bool spades_broken) {
    if (trick_len == 0) {
        // Leading: may not lead spades until broken, unless spades are all
        // that remain.
        const Hand non_spades = hand & ~suit_mask(SUIT_SPADES);
        if (!spades_broken && non_spades) return non_spades;
        return hand;
    }
    const Hand follow = hand & suit_mask(led_suit);
    return follow ? follow : hand;
}

// Updated "spades are broken" flag after `card_suit` is played onto a trick
// that already holds `trick_len` cards of led suit `led_suit`.
//
// The forced-spade-lead case is the one genuine rule ambiguity; see the README.
// `break_on_forced_lead == false` is the literal reading and matches the
// oracle's default.
inline bool spades_broken_after(bool spades_broken, int trick_len, int led_suit,
                                int card_suit_, bool break_on_forced_lead) {
    if (spades_broken || card_suit_ != SUIT_SPADES) return spades_broken;
    if (trick_len == 0) return break_on_forced_lead;
    // legal_moves guarantees a spade on a non-spade lead means a void.
    return led_suit != SUIT_SPADES;
}

// Does `candidate` beat the current best card on the trick?  `incumbent` is
// always either the led card or a spade, so any third suit is a discard.
inline bool beats(CardId candidate, CardId incumbent) {
    const int cs = card_suit(candidate);
    const int is = card_suit(incumbent);
    if (cs == SUIT_SPADES && is != SUIT_SPADES) return true;
    if (cs != is) return false;
    return candidate > incumbent;  // same suit => bit index orders by rank
}

// Seat that wins a trick led by `leader`, cards given in play order.
inline int trick_winner(int leader, const CardId* cards, int n) {
    int best = 0;
    for (int i = 1; i < n; ++i) {
        if (beats(cards[i], cards[best])) best = i;
    }
    return (leader + best) & 3;
}

// The card currently winning a trick in progress, or NO_CARD if it is empty.
// `beats` is only ever asked about the running best, so this one card is the
// whole of what the trick so far can still do.
inline CardId trick_best_card(const CardId* cards, int n) {
    if (n <= 0) return NO_CARD;
    CardId best = cards[0];
    for (int i = 1; i < n; ++i) {
        if (beats(cards[i], best)) best = cards[i];
    }
    return best;
}

// EQUIVALENT-CARD REDUCTION
// -------------------------
// Holding SK and SQ with the jack already gone, the king and the queen are not
// two moves.  They are one move played under two names: every card still in
// existence is either above both or below both, so playing one and playing the
// other reach positions that differ only by swapping the two labels.  Searching
// both subtrees searches the same tree twice.
//
// `relevant_cards` is the set of cards whose rank the rest of the deal can
// still tell apart, and `distinct_moves` keeps one representative per run of
// legal cards that is contiguous in it.
//
// WHY THIS IS EXACT.  Let X > Y be two cards of one suit in the mover's hand
// with no relevant card ranked between them, and let s be the relabelling that
// swaps X and Y.  Because nothing relevant separates them, s preserves the
// order of every card the game can still compare, and it maps the position
// after playing X onto the position after playing Y.  The rules read nothing
// else -- follow-suit sees suits, the break rule sees suits, and who wins a
// trick is decided by comparisons alone -- so the two positions have the same
// value, and dropping either candidate loses nothing.
//
// WHY THE PRINCIPAL VARIATION SURVIVES.  Candidates are enumerated from the
// bottom and replace the incumbent only on a strict improvement, so among
// equal-valued moves the canonically lowest already wins.  The card dropped
// here is always the higher member of a pair that scores identically, which
// could never have displaced it.  The search picks the same cards it picked
// before; only the work changes.
//
// WHY THE TRANSPOSITION TABLE STAYS CONSISTENT.  A stored move is a slot index
// read back against a possibly different position with the same key, so the
// reduction has to be a function of the key rather than of the literal cards.
// It is: the key already records the live cards of each suit in order, and the
// winning card of the trick as `gap`, its position among them -- which is
// exactly the two ingredients below.

// The cards whose ranks still mean something.
//
// Cards from finished tricks are gone.  So are the LOSING cards of the trick in
// progress: nothing compares against them ever again, because `beats` is only
// asked about the running best.  The winning card is the exception -- a hand
// card above it takes the trick and one below it does not -- so it stays in,
// and it is the only card outside the four hands that does.
inline Hand relevant_cards(const Hand hands[4], CardId winning_card) {
    const Hand live = hands[0] | hands[1] | hands[2] | hands[3];
    return winning_card == NO_CARD ? live : live | card_bit(winning_card);
}

// One representative -- the canonically lowest -- per equivalence class of
// `moves`.  `moves` must be a subset of `relevant`, which every legal move is.
//
// The classes are runs of `moves` that are contiguous within `relevant`, so a
// card is redundant exactly when the next relevant card BELOW it is also a
// legal move.  Reading that downwards needs a search per card; reading it
// upwards is a flood that fills from every move through the ranks no relevant
// card occupies, and then steps once more.  Whatever it lands on is the first
// relevant card above that move, and a move landed on this way has an equal,
// lower twin already in the set.
//
// The four widening steps carry a bit up to fifteen ranks, past the widest gap
// a thirteen-rank suit can have, and SUIT_PADDING keeps the fill inside its own
// suit.
inline Hand distinct_moves(Hand moves, Hand relevant) {
    Hand gap = ~(relevant | SUIT_PADDING);
    Hand flood = moves;
    flood |= gap & (flood << 1);
    gap &= gap << 1;
    flood |= gap & (flood << 2);
    gap &= gap << 2;
    flood |= gap & (flood << 4);
    gap &= gap << 4;
    flood |= gap & (flood << 8);
    return moves & ~(flood << 1);
}

}  // namespace nil

#endif  // NIL_RULES_HPP
