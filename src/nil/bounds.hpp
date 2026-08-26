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
// HOW SHORT OF COVERS IS A HOLDING
// ---------------------------------------------------------------------------
// Shared by the proof below and by move ordering (ROADMAP items 6b and 6d),
// which is the whole reason it is a function rather than a loop inside
// nil_must_take_a_trick.  There is one definition in this solver of "can this
// holding be covered", and this is it.
//
// `mine` and `theirs` are one suit's worth of cards: the holder's, and
// everybody else's still live.  Number the holder's cards from the top,
// j = 1, 2, ..., m, and let above_j be how many of `theirs` sit strictly above
// the j-th.  The holder can duck its way out of the suit only if every one of
// its top j cards can be matched to a distinct higher card outside -- Hall's
// condition, above_j >= j for all j.  Where that fails, one card gets through
// and wins a trick.
//
// Returns how many of the holder's OWN cards sit below the first failure, which
// is how many times the suit must be led before the trouble is reached:
//
//     depth = min { m - j : above_j < j }
//
// and SUIT_COVERED when there is no failure at all.  Smaller is worse for the
// holder.  A singleton nobody can beat returns 0 -- trouble on the very next
// lead of the suit.  An ace held behind three small ones returns 3, because the
// suit has to be led four times before the ace is stranded.
//
// EXACT IN SPADES, CONSERVATIVE ELSEWHERE.  The argument needs every card to be
// played and a card to lose only to a higher card of the same suit.  Both hold
// for trumps.  In a side suit a card can be ruffed, and the suit may simply
// never be led often enough to reach the failure, so a holding this call marks
// as short may never actually cost a trick.  Both escapes help the holder, so
// off-trump this OVERSTATES the trouble -- which is why nil_must_take_a_trick
// asks it only about spades, and why the ordering heuristics may ask it about
// anything: a heuristic that overstates can misorder, and cannot miscount.
constexpr int SUIT_COVERED = 64;

inline int cover_deficit_depth(Hand mine, Hand theirs, int suit) {
    if (!mine) return SUIT_COVERED;
    const int m = count_cards(mine);
    const Hand top = card_bit(make_card(suit, 14));
    const Hand bottom = card_bit(make_card(suit, 2));
    int held = 0;   // j
    int above = 0;  // above_j, once the j-th has been reached
    for (Hand bit = top; bit >= bottom; bit >>= 1) {
        if (mine & bit) {
            if (++held > above) return m - held;
        } else if (theirs & bit) {
            ++above;
        }
    }
    return SUIT_COVERED;
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
// MEASUREMENT ONLY (roadmap item 32).  Deliberately permissive: this is a
// CEILING on how often an adversarial nil-set proof could fire, not the proof.
//
// nil_must_take_a_trick counts covers by Hall: the j-th of the nil bidder's
// spades from the top needs j outstanding spades above it, and every spade not
// in the nil bidder's hand counts the same regardless of who holds it.  That is
// the wrong question.  A cover only covers if it lands on the SAME TRICK as the
// card it beats, and the hand holding it chooses when to spend it -- an
// opponent holding ♠AQJ against ♠K2 will never lead the ace, because the nil
// bidder would simply throw the king under it.  It leads the jack, takes the
// deuce, then leads the queen into a bare king.  Hall calls that holding
// covered.  It is forced.
//
// So ask it the way DDS §4 asks LaterTricks: can the side that did not lead
// force a trick later, against best defence.  The opponents lead their m
// smallest spades in increasing order; the nil bidder must follow each time and
// survives the j-th lead only if it still holds a spade below it, which needs
// at least j of its spades below that lead.
//
// WHY THIS OVER-FIRES, and why that is the point.  It ignores the partner's
// spades entirely, and the partner is cooperative -- it can overtake and rescue
// a trick the nil bidder would otherwise win.  It also ignores whether the
// opponents can actually get the lead often enough, and whether spades are
// broken.  Every one of those makes it claim "forced" where the real proof
// could not, so the count it produces is an UPPER BOUND on the population.  If
// the ceiling is small the item closes without anyone writing the real proof;
// only if the ceiling is large is the sound version worth the work.
inline bool nil_forced_ceiling(const Hand hands[4], int nil_seat) {
    const Hand mine = hands[nil_seat] & suit_mask(SUIT_SPADES);
    if (!mine) return false;
    const int m = count_cards(mine);
    const Hand opp =
        (hands[(nil_seat + 1) & 3] | hands[(nil_seat + 3) & 3]) & suit_mask(SUIT_SPADES);
    int j = 0;
    for (Hand bit = card_bit(make_card(SUIT_SPADES, 2));
         bit <= card_bit(make_card(SUIT_SPADES, 14)); bit <<= 1) {
        if (!(opp & bit)) continue;
        if (++j > m) break;  // the nil bidder has run out of spades to be forced with
        if (count_cards(mine & (bit - 1)) < j) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// TRICKS ONE HAND CANNOT BE DENIED
// ---------------------------------------------------------------------------
// Returns the seat holding the top outstanding spade and, in `run`, how many of
// the top outstanding spades it holds CONSECUTIVELY from the top.  That hand
// wins at least `run` of the remaining tricks in EVERY line of play -- not
// "can force `run`", which is a claim about optimal play and is the wrong shape
// for what reads this.  The reach bound in search_impl() ranges over the whole
// simplex n + p <= t without assuming anybody plays well, so what it can
// consume is a floor that holds down every branch, good and bad alike.
//
// WHY THE RUN IS WON.  Let the outstanding spades be s1 > s2 > ... and let one
// hand hold s1..sk from the top.  Then:
//
//   * every card is played eventually, so each si is played at some trick Ti;
//   * one hand plays one card per trick, so T1..Tk are k DISTINCT tricks;
//   * spades is trump, so a trick carrying a spade is won by the highest spade
//     on it;
//   * the only spades above si are s1..s(i-1), which are in this same hand and
//     therefore not on trick Ti.
//
// So si wins Ti outright, and the k tricks are distinct.
//
// ONE HAND, ANCHORED AT THE TOP, and both halves are load-bearing.  s1 and s2
// split across two hands of the same side can legally land on the SAME trick --
// both void, both ruffing -- and collapse to one trick, so a side-wide count is
// conditional on play and unusable here.  That is the concentration failure
// item 32 died on, and confining the count to a single hand is what steps
// around it rather than into it.  Un-anchored is worse: a hand holding s2 but
// not s1 wins nothing it can rely on, because s1 may be played over it.
//
// This is DDS section 4's LaterTricks rules 1 and 2 in the only form the nil
// question can use.  Section 4 asks about the opponents OF THE TRICK-LEADING
// HAND, which changes seat every trick; the caller needs a claim about a fixed
// hand, and the argument above is leader-independent, so nothing has to know
// who is on lead.
//
// NOT item 32, and the difference is what makes this sound.  That item had to
// prove a named SEAT takes a trick from a POOLED count and no such count
// reached it -- its counterexample is an opponent compelled to play a high
// spade and rescue the nil.  A rescuing spade still wins the trick for the hand
// that played it, which is all this counts.
//
// A TRICK BOUNDARY ONLY.  Reads the four hands as the whole of the live cards,
// which is true only when no card sits half-played on the table.
//
// Returns -1 when no spade is left, which is the common late-hand case and one
// mask test.
inline int top_spade_run(const Hand hands[4], int& run) {
    run = 0;
    Hand outstanding =
        (hands[0] | hands[1] | hands[2] | hands[3]) & suit_mask(SUIT_SPADES);
    if (!outstanding) return -1;
    const CardId top = highest_card(outstanding);
    int owner = 0;
    while (owner < 4 && !(hands[owner] & card_bit(top))) ++owner;
    if (owner == 4) return -1;
    const Hand theirs = hands[owner];
    do {
        const CardId c = highest_card(outstanding);
        if (!(theirs & card_bit(c))) break;
        ++run;
        outstanding &= ~card_bit(c);
    } while (outstanding);
    return owner;
}

// ---------------------------------------------------------------------------
// QUICK TRICKS (DDS section 3), and the two shapes it comes in
// ---------------------------------------------------------------------------
// Section 3 counts the tricks the side about to lead can take IMMEDIATELY, by
// cashing from the top.  That is a CAN-CASH count: it presumes the side is on
// lead and chooses to cash.  DDS can spend it directly because its value is the
// side's own trick count and its side to move is maximising exactly that.
//
// This search cannot, and the reason is worth stating once.  The reach bound in
// search_impl() ranges over the whole simplex n + p <= t WITHOUT assuming
// anybody plays well, so what it consumes is a floor that holds down every
// line.  A can-cash count is a statement about one strategy, and it bounds the
// node's value only from the side whose strategy it is -- an upper bound when a
// minimiser holds the option, a lower bound when a maximiser does.  The two
// therefore plug in at different places and fire against different thresholds,
// which is why they are counted separately below rather than added up.
//
// FORCED, by contrast, is what top_spade_run() gives and what the bound wants.
// forced_spade_tricks() below is the closed form of that argument for every
// hand at once.

// Every hand's FORCED trump tricks, in one pass down the outstanding spades.
//
// With hand H holding spades r1 > r2 > ... > ra and o_i the number of spades
// OUTSIDE H ranked above r_i,
//
//     forced(H) = max over i of (i - o_i)
//
// and H wins at least that many of the remaining tricks in EVERY line.
//
// WHY.  Fix i and take H's top i spades.  They are played on i distinct tricks,
// because H plays one card per trick.  One of them is lost only if a HIGHER
// spade lands on that trick, and a higher spade in H itself cannot -- same
// hand, same trick, one card.  So the spoiler is a spade outside H ranked above
// some r_j with j <= i, hence above r_i, and there are o_i of those.  Each is
// played once, so each spoils at most one.  At least i - o_i survive.
//
// The max over i is free: every i gives a valid floor, so the largest is valid
// too.  At o_i = 0 this reproduces top_spade_run() exactly, so it is never
// weaker for the hand that function names, and it speaks about the other three
// besides.
//
// DDS SECTION 4'S RULES 2 AND 3 ARE THE SMALL CASES.  Rule 2 -- highest trump
// is one sure trick, highest plus second-highest is two -- is i = 1 and i = 2
// at o = 0.  Rule 3 -- "the second highest trump plus at least one trump more
// behind the hand with the highest trump" -- is i = 2, o_2 = 1.  The paper
// enumerates them; this is the form they are instances of.
//
// TWO HANDS' FLOORS MAY BE ADDED, which the top_spade_run() comment denies for
// a side-wide count.  What is unusable there is pooling a RUN across two hands,
// where s1 and s2 can land on one trick and collapse.  Nothing is pooled here:
// each floor is proved on its own hand, and two hands cannot win the same
// trick.  The formula prices the collapse in by itself -- give s1 to West and
// s2 to East and East scores 1 - 1 = 0.
//
// A TRICK BOUNDARY ONLY: it reads the four hands as the whole of the live
// cards.
inline void forced_spade_tricks(const Hand hands[4], int out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    Hand outstanding =
        (hands[0] | hands[1] | hands[2] | hands[3]) & suit_mask(SUIT_SPADES);
    int seen[4] = {0, 0, 0, 0};
    int total = 0;
    while (outstanding) {
        const CardId c = highest_card(outstanding);
        outstanding &= ~card_bit(c);
        int owner = 0;
        while (owner < 4 && !(hands[owner] & card_bit(c))) ++owner;
        if (owner == 4) continue;
        ++seen[owner];
        ++total;
        // o_i is every spade seen so far that is not this hand's.
        const int v = seen[owner] - (total - seen[owner]);
        if (v > out[owner]) out[owner] = v;
    }
}

// DDS section 3's count: tricks `seat` cashes if it gains the lead and runs its
// winners from the top.
//
// Per suit, `seat` holds a run of r cards from the top of what is outstanding.
// In the trump suit all r cash -- the only spades above them are its own.  In a
// side suit an opponent void in the suit and holding a spade RUFFS, so only the
// rounds where both opponents must still follow are safe:
//
//     r' = r                                   if neither opponent holds a spade
//     r' = min(r, shorter opponent's length)   otherwise
//
// The PARTNER's holding is not consulted: a partner who ruffs still wins the
// trick for this side, and a partner who over-takes still leaves the side on
// lead.  Only the two opponents can break the run.
//
// `sum` adds the per-suit counts and `best` takes the largest single suit.
// They bracket the truth rather than agreeing with it, and deliberately.  `best`
// is unconditionally sound -- cash one suit and stop.  `sum` is optimistic:
// cashing one suit can force an opponent void in it to DISCARD from another,
// shortening the guard that the next suit's count was computed against.  A
// population measured between the two is a population known to within the
// bracket, which is the honest thing to report before anything is wired to it.
struct QuickTricks {
    int best = 0;  // largest single suit -- sound
    int sum = 0;   // every suit added -- optimistic
};

inline QuickTricks cashable_tricks(const Hand hands[4], int seat) {
    QuickTricks q;
    const int lho = (seat + 1) & 3;
    const int rho = (seat + 3) & 3;
    const Hand live = hands[0] | hands[1] | hands[2] | hands[3];
    const bool opps_have_trumps =
        ((hands[lho] | hands[rho]) & suit_mask(SUIT_SPADES)) != 0;
    for (int suit = 0; suit < 4; ++suit) {
        const Hand mask = suit_mask(suit);
        Hand outstanding = live & mask;
        if (!outstanding) continue;
        const Hand mine = hands[seat] & mask;
        if (!mine) continue;
        int run = 0;
        while (outstanding) {
            const CardId c = highest_card(outstanding);
            if (!(mine & card_bit(c))) break;
            ++run;
            outstanding &= ~card_bit(c);
        }
        if (run == 0) continue;
        int cashes = run;
        if (suit != SUIT_SPADES && opps_have_trumps) {
            const int a = count_cards(hands[lho] & mask);
            const int b = count_cards(hands[rho] & mask);
            const int shorter = a < b ? a : b;
            if (shorter < cashes) cashes = shorter;
        }
        if (cashes <= 0) continue;
        q.sum += cashes;
        if (cashes > q.best) q.best = cashes;
    }
    return q;
}

inline bool nil_must_take_a_trick(const Hand hands[4], int nil_seat) {
    const Hand mine = hands[nil_seat] & suit_mask(SUIT_SPADES);
    // The overwhelmingly common case for a nil bidder, and one mask test.
    if (!mine) return false;
    const Hand theirs = relevant_cards(hands, NO_CARD) & suit_mask(SUIT_SPADES) & ~mine;
    // Exactly the walk this function used to do inline.  It is shared now
    // because move ordering wants the same count off-trump; the predicate is
    // "the holding runs short of covers somewhere", and where is what the
    // ordering wants and this does not.
    return cover_deficit_depth(mine, theirs, SUIT_SPADES) != SUIT_COVERED;
}

}  // namespace nil

#endif  // NIL_BOUNDS_HPP
