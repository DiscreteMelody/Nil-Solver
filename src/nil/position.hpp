#ifndef NIL_POSITION_HPP
#define NIL_POSITION_HPP

#include <string>

#include "nil/cards.hpp"

namespace nil {

// A position at any point in a hand.
//
//   hands         : indexed by absolute seat (N=0, E=1, S=2, W=3).  Cards
//                   already played to the current trick must NOT appear here.
//   leader        : seat that led the current trick.
//   spades_broken : as of this position.
//   trick         : cards already played to the in-progress trick, in play
//                   order starting from `leader`.  0..3 of them.
struct Position {
    Hand hands[4] = {0, 0, 0, 0};
    int leader = SEAT_NORTH;
    bool spades_broken = false;
    CardId trick[3] = {NO_CARD, NO_CARD, NO_CARD};
    int trick_len = 0;

    int cards_per_hand() const;
    int tricks_remaining() const { return cards_per_hand(); }
    int to_play() const { return (leader + trick_len) & 3; }
    int led_suit() const { return trick_len ? card_suit(trick[0]) : -1; }
};

// 'N:8.K5.KT2.KQT9762 AQT643.T.QJ864.8 J9.A943.73.AJ543 K752.QJ8762.A95.'
// Hands run clockwise from the named seat.  Shortened (mid-play) hands and
// '-' for a void or for a whole hand are accepted, matching the oracle.
bool parse_pbn(const std::string& text, Hand hands[4], std::string& err);

// The seat a PBN string is named for, or -1 if it does not name one.  The hands
// run clockwise from it, and so does the `--seats` role list that travels
// beside it -- see nil/seats.hpp.  Cheap enough to call before parsing the
// deal, which is what callers that must resolve the roles first do.
int pbn_anchor(const std::string& text);

// One PBN hand group, e.g. 'AQT643.T.QJ864.8'.
bool parse_pbn_hand(const std::string& text, Hand& out, std::string& err);

std::string deal_to_pbn(const Hand hands[4], int first_seat = SEAT_NORTH);

// Checks every invariant the oracle's Position.validate checks: no duplicate
// cards, trick cards absent from the hands, consistent hand sizes for the
// number of cards already on the trick, and follow-suit consistency for the
// cards already played to it.
bool validate(const Position& pos, std::string& err);

std::string format_hands(const Position& pos);  // one line per seat

}  // namespace nil

#endif  // NIL_POSITION_HPP
