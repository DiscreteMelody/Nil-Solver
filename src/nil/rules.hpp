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

}  // namespace nil

#endif  // NIL_RULES_HPP
