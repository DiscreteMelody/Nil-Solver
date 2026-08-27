// What each of the four seats is doing in this deal.
//
// WHY THIS REPLACED `nil_seat` AND `nil_already_set`
// --------------------------------------------------
// The solver used to describe the objective with two scalars: which seat bid
// nil, and whether that nil had already been broken.  Two scalars can only ever
// describe ONE nil.  Real spades puts two on the table often enough -- both
// pairs bid one, or a pair bids nil and its opponents do too -- and the optimal
// line changes when it happens, because a seat that is both defending its own
// nil and attacking another's has an objective neither scalar can express.
//
// So the objective is now a role per seat, which is the shape the multi-nil
// question already has.  This is phase one: the representation changes and the
// search does not.  `validate_seat_roles` accepts exactly the arrangements the
// two scalars could describe and refuses the rest by name, so a caller that
// asks for two nils gets told the truth instead of a confidently wrong answer.
//
// ABSOLUTE INSIDE, ANCHORED ON THE WIRE
// -------------------------------------
// `SeatRoles::role` is indexed by ABSOLUTE seat -- N=0, E=1, S=2, W=3 -- the
// same indexing as `Position::hands`, `Position::leader` and the C ABI's
// NIL_SEAT_*.  Nothing inside the solver has to think about rotation.
//
// The TEXT form is anchored, exactly as a PBN deal string is: the four values
// run clockwise from a named seat, and that seat is the one the accompanying
// PBN string names.  'W:...' with `--seats 0 3 2 3` reads West, North, East,
// South, so West bid the nil and East is covering it.  Parsing therefore needs
// the anchor, which is why `parse_seat_roles` takes one and why every caller
// resolves the PBN first.
#ifndef NIL_SEATS_HPP
#define NIL_SEATS_HPP

#include <string>

#include "nil/cards.hpp"

namespace nil {

// The four roles, and the wire values they carry.  These numbers are part of
// the corpus format and of the C ABI, so they do not get reordered.
enum SeatRole : int {
    // A nil bidder that has not yet taken a trick.  Minimises its own tricks;
    // the opponents are trying to force one onto it.
    ROLE_NIL = 0,
    // A nil bidder whose nil is already broken.  The primary objective is off
    // for that seat: there is nothing left to protect or to attack, and only
    // the tie-break matters.  This is what `nil_already_set` used to say.
    ROLE_NIL_SET = 1,
    // The nil bidder's partner, covering it.  Its own tricks still count for
    // the pair, so it takes them freely; what it must not do is hand one over.
    ROLE_COVER = 2,
    // A seat on a side with no nil bid: an antagonist, trying to set as many of
    // the other side's nils as it can.
    ROLE_OPPONENT = 3,
};

// "nil", "nil-set", "cover", "opponent"; "?" for anything else.
const char* role_name(int role);

struct SeatRoles {
    // Indexed by absolute seat.  The default is the old default call: North bid
    // a live nil, South is covering, East and West are the opponents.
    SeatRole role[4] = {ROLE_NIL, ROLE_OPPONENT, ROLE_COVER, ROLE_OPPONENT};

    SeatRole operator[](int seat) const { return role[seat & 3]; }
    SeatRole& operator[](int seat) { return role[seat & 3]; }

    bool is_nil(int seat) const {
        const SeatRole r = role[seat & 3];
        return r == ROLE_NIL || r == ROLE_NIL_SET;
    }
    // The seat holding a nil, or -1.  Well defined only on an arrangement
    // `validate_seat_roles` accepted; it returns the first one otherwise.
    int nil_seat() const;
    int cover_seat() const;
    bool nil_already_set() const;
    // Is this seat on the side that is protecting a nil?  Under the phase-one
    // shape this is the nil bidder and its cover, which is the same partition
    // seat parity gives.
    bool on_nil_side(int seat) const { return role[seat & 3] != ROLE_OPPONENT; }

    bool operator==(const SeatRoles& other) const {
        for (int s = 0; s < 4; ++s) {
            if (role[s] != other.role[s]) return false;
        }
        return true;
    }
    bool operator!=(const SeatRoles& other) const { return !(*this == other); }
};

// The arrangement the two old scalars described: one nil, its partner covering,
// the other pair opposing.
SeatRoles seat_roles_from_nil(int nil_seat, bool already_set);

// PHASE ONE'S SHAPE CHECK.  Exactly one seat holds a nil, exactly one covers
// it, the cover is that seat's partner, and the remaining two oppose.  Sets
// `err` and returns false otherwise, naming what is wrong -- in particular, an
// arrangement with two nils is refused as unsupported rather than as malformed,
// because that is what it is.
bool validate_seat_roles(const SeatRoles& roles, std::string& err);

// Text form, ANCHORED: four values running clockwise from `anchor`, matching
// the hand order of a PBN string named for the same seat.  Accepts them
// separated by whitespace or commas, or run together as four digits.
bool parse_seat_roles(const std::string& text, int anchor, SeatRoles& out, std::string& err);

// The inverse: "0 3 2 3", clockwise from `anchor`.
std::string seat_roles_to_string(const SeatRoles& roles, int anchor);

// Absolute and unambiguous, for anything a human reads:
// "N=nil E=opponent S=cover W=opponent".
std::string describe_seat_roles(const SeatRoles& roles);

}  // namespace nil

#endif  // NIL_SEATS_HPP
