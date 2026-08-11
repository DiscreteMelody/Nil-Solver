// Static bounds on the nil question: two facts about a position that settle it
// without searching it at all.
//
// This is Chang's quick-trick check asked in the shape the nil question wants.
// Chang skips the search when "the side to play would win at least one trick",
// which bounds how many tricks a SIDE takes; the nil question is whether one
// TRICK can be forced onto one SEAT, so neither of his two tests transfers
// directly.  What transfers is the idea: a cheap sufficient condition, checked
// before recursing, that answers the boolean outright.  He measured over half
// the expanded nodes gone on ten-trick deals.
//
// Both tests are one-sided.  Each proves its answer when it fires and says
// nothing when it does not, so a false is never evidence of anything and the
// search proceeds exactly as it would have.  That is what makes them safe to
// bolt onto a search that is already trusted: they can cost time, and they
// cannot change an answer.
//
// ONLY AT A TRICK BOUNDARY.  Both proofs count the cards still in the four
// hands.  A card already played to the trick in progress is in neither
// category -- it is out of its owner's hand, but it has not finished doing its
// work.  For the second proof that is fatal, and the reason is spelled out at
// nil_must_take_a_trick.  For the first it is not: `relevant_cards` already
// keeps the one played card that can still decide anything, and the argument
// goes through mid-trick with one extra condition.  That version was built and
// measured, and it lost -- see "Evaluated and rejected" in ROADMAP.md.  So
// neither function takes a trick parameter, which is the cheapest way to keep
// the caller honest.  (Chang probes his hash table only at the start of a trick
// for a related reason: a position is fully described by the four hands only
// there.)
#ifndef NIL_BOUNDS_HPP
#define NIL_BOUNDS_HPP

#include "nil/cards.hpp"
#include "nil/rules.hpp"

namespace nil {

// ---------------------------------------------------------------------------
// NIL PROVABLY SAFE
// ---------------------------------------------------------------------------
// True when the nil bidder cannot win another trick, whatever all four players
// do.  The value of the subtree is then zero, exactly, and it is zero down
// every line -- so this is not a bound that happens to be tight, it is the
// answer.
//
// THE CONDITIONS
//
//   1. The nil bidder holds no spades.
//   2. In every other suit it holds, every card of its is below every
//      outstanding card of that suit -- or nobody else holds that suit at all.
//   3. The nil bidder is not on lead; or, if it is, condition 2 holds in the
//      first form, with somebody else holding every suit the nil bidder does.
//
// WHY IT IS EXACT.  Induction over the remaining tricks, with 1-3 as the
// hypothesis.
//
// The nil bidder is not the leader, so somebody else leads a suit L.
//
//   - Void in L, the nil bidder discards.  By (1) the discard is not a spade,
//     and an off-suit non-spade never wins a trick.
//   - Holding L, the nil bidder follows.  The leader's card is in L and came
//     out of a hand, so it was outstanding, so by (2) it is above every card
//     the nil bidder holds in L.  Whatever ends up winning the trick either is
//     that card or beats it, so it beats the nil bidder's card too.
//
// Either way the nil bidder does not win, so it does not lead the next trick
// and (3) survives.  (1) survives because a hand only shrinks.  (2) survives
// because the outstanding set only shrinks and the nil bidder's cards in a suit
// stay below whatever is left of it.  The hypothesis reproduces itself, and the
// nil bidder wins no trick at any depth.
//
// The base case is the whole of what condition 3 is for.  A nil bidder ON lead
// leads a card; if nobody else holds that suit, all three follow with discards
// or ruffs, and they are not obliged to ruff -- three discards hand the trick
// to the nil bidder's deuce.  Requiring somebody else to hold each of its suits
// closes that off: whoever holds L must follow, and by (2) whatever they follow
// with is higher.  Below the root the case does not arise at all, since a nil
// bidder reaches the lead only by winning a trick, and a fast search never
// recurses past that.
//
// WHY THE SPADE CONDITION IS NOT WEAKER.  A void is the thing that cannot be
// ruled out cheaply.  Give the nil bidder the deuce of spades and one heart,
// let hearts run out, and it must ruff -- the lowest trump in the deck wins a
// trick when the other three are following a suit they hold or discarding.  So
// any spade at all defeats the proof, and the test is a mask compare rather
// than a rank comparison.
//
// `hands` is the four hands at a trick boundary, `nil_seat` the nil bidder, and
// `nil_on_lead` whether the nil bidder is the one to lead.
inline bool nil_cannot_be_forced(const Hand hands[4], int nil_seat, bool nil_on_lead) {
    const Hand mine = hands[nil_seat];
    // Condition 1.
    if (mine & suit_mask(SUIT_SPADES)) return false;

    // The outstanding cards are the ones still in hands, which at a trick
    // boundary is exactly what relevant_cards computes with no winning card to
    // fold in.  Going through it rather than re-deriving the union is the point
    // -- there is one definition of "still live" in this solver and this is it.
    const Hand outstanding = relevant_cards(hands, NO_CARD) & ~mine;

    for (int suit = SUIT_HEARTS; suit <= SUIT_CLUBS; ++suit) {
        const Hand held = mine & suit_mask(suit);
        if (!held) continue;
        const Hand theirs = outstanding & suit_mask(suit);
        if (!theirs) {
            // Nobody else holds the suit, so nobody else can lead it and these
            // cards only ever fall as discards -- unless the nil bidder is the
            // one on lead, in which case it is three discards away from winning
            // a trick with the lowest of them.
            if (nil_on_lead) return false;
            continue;
        }
        // Condition 2: every card held is below the lowest outstanding one.
        // Within a suit the bits are ordered by rank, so "all of `held` sits
        // below the lowest bit of `theirs`" is one unsigned comparison against
        // that bit -- `held` is smaller than it exactly when no bit of `held`
        // is at or above it.
        const Hand lowest_outstanding = theirs & (~theirs + 1);
        if (held >= lowest_outstanding) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// NIL PROVABLY SET
// ---------------------------------------------------------------------------
// True when the nil bidder is guaranteed at least one more trick, whatever all
// four players do.  A lower bound of one, which is the only bound the boolean
// question needs on that side.
//
// THE PATTERN.  Rank the outstanding spades, 1 for the highest.  The nil bidder
// is set if it holds the 1st; or the 2nd and 3rd; or the 3rd, 4th and 5th; or
// the 4th through the 7th; and so on -- a block of k spades starting at rank k.
//
// WHY.  Every card is played before the hand ends, so every spade the nil
// bidder holds is played at some point, and a spade loses only to a HIGHER
// spade on the same trick.  Above a block of k spades starting at rank k there
// are exactly k-1 spades the nil bidder does not hold, each playable once and
// so able to bury at most one of the block.  k cards, k-1 covers: one gets
// through, and a spade that gets through wins its trick.  Nobody's intentions
// enter into it, which is what makes this a proof rather than a heuristic --
// the covering partner cannot save the nil and the opponents need do nothing
// clever.
//
// THE GENERAL FORM, which is what the loop below tests.  Let the nil bidder's
// spades sit at outstanding ranks r(1) < r(2) < ... < r(m).  Taking the top j
// of them as the block, the spades above it that the nil bidder does not hold
// number r(j) - j, and the block is forced through when r(j) - j < j.  Walking
// the spades downwards, r(j) - j is just the running count of higher spades
// held by somebody else, so the whole test is two counters and a comparison.
// The listed pattern is the tight case r(j) = 2j - 1; the loop also catches the
// slack ones, so holding the 2nd, 3rd and 9th is recognised on the 3rd.
//
// WHY A TRICK BOUNDARY IS REQUIRED.  The count of "spades above, held by
// somebody else" is taken over the four hands.  Mid-trick a spade already
// played is in nobody's hand, and if it is winning the trick it can still bury
// a spade the nil bidder is about to play under it -- a cover this function
// would not have counted.  Holding what is now the highest spade in any hand
// does not make the nil bidder set when a higher one is sitting on the table
// waiting to be dumped on.  Hence: trick boundaries only, and no trick to pass
// in.
inline bool nil_must_take_a_trick(const Hand hands[4], int nil_seat) {
    const Hand mine = hands[nil_seat] & suit_mask(SUIT_SPADES);
    // The overwhelmingly common case for a nil bidder, and one mask test.
    if (!mine) return false;
    const Hand theirs = relevant_cards(hands, NO_CARD) & suit_mask(SUIT_SPADES) & ~mine;

    int held = 0;   // j: the nil bidder's spades at or above the current rank
    int above = 0;  // r(j) - j: everyone else's, strictly above the j-th
    // Spades are suit 0, so they occupy bits 0 (the deuce) to 12 (the ace) and
    // walking a single bit down from the ace stays inside the suit.
    for (Hand bit = card_bit(make_card(SUIT_SPADES, 14)); bit; bit >>= 1) {
        if (mine & bit) {
            if (++held > above) return true;
        } else if (theirs & bit) {
            ++above;
        }
    }
    return false;
}

}  // namespace nil

#endif  // NIL_BOUNDS_HPP
