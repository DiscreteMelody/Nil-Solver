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

// The arrangements the solver can answer, and the name each goes by.  Mirrors
// role_shape() in nil_oracle.py, which is the specification.
enum SeatShape : int {
    SHAPE_UNSUPPORTED = 0,
    // One nil, its partner covering, two opponents.
    SHAPE_SINGLE_NIL = 1,
    // Both members of one pair bid; the other two oppose and there is no cover.
    SHAPE_PARTNER_NILS = 2,
    // One bid per pair, each with a partner.  The role on that partner is not a
    // statement about teams -- both teams have a bid -- but about what the side
    // does when it cannot both save its own and set the other's.  See
    // side_rank().
    SHAPE_OPPOSING_NILS = 3,
};

// HOW GOOD AN OUTCOME IS FOR ONE SIDE, 3 best to 0 worst.
//
// Both sides agree on the ends: my bid surviving while theirs dies is best, and
// the reverse is worst.  They need not agree on the MIDDLE, and the role on a
// bidder's partner is what says which way that side leans -- ROLE_COVER saves
// our own bid first, ROLE_OPPONENT sets theirs first.
//
// Summing the two sides' ranks is the test for whether the deal is an ordinary
// two-team game.  When the partners lean OPPOSITE ways the sum is a constant 3,
// the rankings are exact reverses, and a single scalar describes the whole
// thing.  When they lean the SAME way the sum is not constant: both sides would
// rather have both bids live (two ROLE_COVERs) or both dead (two
// ROLE_OPPONENTs) than trade, which is a shared interest no scalar can express.
// Only the opposed case is accepted today; see seat_shape().
int side_rank(bool mine_survives, bool theirs_survives, int partner_role);

// Do the two sides' rankings sum to a constant?  A property of the roles alone,
// and the condition under which minimax applies.
bool strictly_opposed(const SeatRoles& roles);

// Name the arrangement, or SHAPE_UNSUPPORTED with `err` saying why.  An
// arrangement nobody has implemented is refused rather than answered as some
// adjacent question -- in particular nils on OPPOSING sides, which is a legal
// spades deal and not a malformed argument.
SeatShape seat_shape(const SeatRoles& roles, std::string& err);

// True for any shape the solver can answer.  See seat_shape.
bool validate_seat_roles(const SeatRoles& roles, std::string& err);

// How many seats hold a bid, live or already down: 1 or 2 on any accepted
// shape.
int nil_count(const SeatRoles& roles);

// How many bids the caller has already declared down.  Those carry no
// primary weight -- a bid cannot go down twice -- but they are still counted
// in the reported `nils_set`, which is how many are down and not how many
// the search knocked down.
int nil_set_count(const SeatRoles& roles);

// Bit `s` set for each seat holding a bid that is STILL LIVE.  This is what
// the search charges its primary weight against: an already-broken bid has
// nothing left to lose, so its seat plays exactly as a cover partner does --
// freely, its tricks still counting for the pair's secondary total.
unsigned live_nil_mask(const SeatRoles& roles);

// Bit `s` set for each seat the CALLER declared already down with
// ROLE_NIL_SET.  The mask counterpart of nil_set_count: those bids are down
// whatever the search finds, so they belong in a reported broken-bid mask on
// every line, including the lines where nothing else breaks.
unsigned nil_set_mask(const SeatRoles& roles);

// DOES THE OBJECTIVE PIN *WHICH* BIDS GO DOWN, or only how many?
//
// A reported broken-bid mask is an answer column, and patch 57 established
// what an answer column may hold: only what the objective pins.  Two equally
// optimal lines must not be able to disagree about it, or the field reports
// move ordering rather than the answer.
//
// The three shapes differ, and the difference is in the primary level:
//
//   SHAPE_SINGLE_NIL     one bidder, so the mask has two values and the
//                        reported count already distinguishes them.  Pinned.
//   SHAPE_OPPOSING_NILS  the primary is far_side_rank(mask), and under strict
//                        opposition -- the only opposed arrangement accepted --
//                        rank and mask are in bijection, so the value names the
//                        mask exactly.  Pinned.
//   SHAPE_PARTNER_NILS   the primary is a COUNT of bids down.  With one of two
//                        down the value cannot say WHICH, and measurement says
//                        it genuinely does not: tools/mask_determinacy.py finds
//                        11.67-20.00% of positions where two optimal lines
//                        break different bids for the same value.  NOT pinned.
//
// A property of the ROLES alone, so it costs nothing to answer and is the same
// for every row of a move list.  It is deliberately conservative on the twin
// shape: about five positions in six there are determined too, but saying so
// would need a second search to enumerate the optimal lines, and a field that
// occasionally overstates is worse than one that uniformly understates.
bool mask_determined_by_objective(const SeatRoles& roles);

// The same question asked of a position rather than of a shape, which is
// strictly tighter and still free.
//
// Even where the shape does not pin the mask, the COUNT may: the mask is a
// subset of the live bidders whose size is the number of live bids down, so
// when only ONE subset has that size there is nothing left to disagree about.
// With two partners bidding that leaves exactly one ambiguous case, count 1 --
// which is precisely where tools/mask_determinacy.py finds the disagreement,
// always as {N} against {S}.  Both down and neither down are determined.
//
// Prefer this to the shape predicate wherever a count is in hand.  It reads no
// cards and runs no search, so the tightening costs nothing.
bool mask_determined(const SeatRoles& roles, int nils_set);

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
