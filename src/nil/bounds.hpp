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
// HOW MANY TRICKS CAN THE NIL DUCK UNDER ITS OWN PARTNER
// ---------------------------------------------------------------------------
// MOVE_ORDERING.md item C0, and a prerequisite rather than a heuristic: three
// of the four cover-partner rules want this one number, so it is built and
// tested once, on its own, before anything reads it.
//
// In one suit: how many times can the nil bidder play a card underneath a card
// of its PARTNER'S?  `nil_cards` and `cover_cards` are one suit's worth each,
// and the answer is the size of the largest set of pairs that can be formed
// with each of the nil bidder's cards matched to a DISTINCT strictly higher
// card of the cover partner's.  Distinct is the whole difficulty: one high
// cover card shelters one nil card, not all of them.
//
//     cover JT87, nil 954  ->  3   (9 under T, 5 under 8, 4 under 7)
//     cover KQ4,  nil J96  ->  2   (J under K, 9 under Q; nothing left for 6)
//
// NOT THE SAME QUESTION AS cover_deficit_depth, though it is the same
// combinatorics read the other way up.  That one asks where a holding first
// runs SHORT of covers, measured against every card outstanding, and answers
// with how many leads it takes to reach the trouble.  This one asks how much
// cover a NAMED HAND can actually supply, and answers with a count of tricks.
// Hall`s theorem connects them: the deficiency version says the largest
// matching is m - max_j(j - above_j), so a holding this call matches in full is
// exactly one the other call reports as covered.  Two readings, one definition
// of "can this be covered", which is the same reason cover_deficit_depth is a
// function rather than a loop inside nil_must_take_a_trick.
//
// THE WALK IS A GREEDY ONE AND IT IS EXACT.  Descend the ranks carrying
// `spare`, the cover cards seen so far that are not yet spent; each of the nil
// bidder`s cards takes one if there is one.  Every cover card above a given nil
// card is interchangeable as far as that card is concerned -- any of them
// beats it -- and a card taken high is never needed lower down, because
// anything a lower nil card could use is also above it.  So there is no choice
// to get wrong and no need to search.  `tools/duck_depth_property.cpp` checks
// this against an unconstrained maximum-matching search over ALL 3^13 ways to
// deal one suit between the two hands, in each of the four suits.
//
// WHAT IT DOES NOT CLAIM.  Not that these tricks will happen: the suit may
// never be led that often, an opponent may win the trick over both of them, and
// the cover partner may have better uses for the card.  It is a measure of
// SUPPLY, in the same spirit as cover_deficit_depth, and like that one it is
// exact arithmetic feeding a heuristic rather than a proof about the play.
// Ruffs are outside it entirely, because the question is asked per suit.
//
// PRECONDITION: the two masks are disjoint, which they are whenever they come
// from two different hands.  Cards of other suits are harmless -- the walk
// never looks outside `suit` -- so callers may mask or not, as cover_deficit_depth's
// callers do.
//
// The walk is the plain thirteen-rank one rather than a tightened version
// bounded by the two holdings, deliberately: nothing calls this yet, so a
// faster loop could not be measured, and MOVE_ORDERING.md is emphatic that
// ordering work on this solver lives or dies on throughput.  Tighten it in C1,
// where there is a consumer to measure it against.
inline int duck_depth(Hand nil_cards, Hand cover_cards, int suit) {
    // Not a case, just an early-out: a void hand on either side supplies or
    // demands nothing, and the walk below would return 0 anyway.
    if (!nil_cards || !cover_cards) return 0;
    const Hand top = card_bit(make_card(suit, 14));
    const Hand bottom = card_bit(make_card(suit, 2));
    int spare = 0;  // cover cards seen and not yet spent
    int ducks = 0;
    for (Hand bit = top; bit >= bottom; bit >>= 1) {
        if (cover_cards & bit) {
            ++spare;
        } else if (nil_cards & bit) {
            if (spare > 0) {
                --spare;
                ++ducks;
            }
        }
    }
    return ducks;
}

// ---------------------------------------------------------------------------
// THE CHEAPEST CARD THAT COVERS A GIVEN ONE
// ---------------------------------------------------------------------------
// The companion to duck_depth, and the second half of MOVE_ORDERING.md item C0:
// that one counts how much shelter a hand can supply over a whole suit, this
// one names the card to spend on ONE trick.
//
// `cards` must be a single suit's worth and `target` a card of that same suit,
// which is what makes the bit comparison meaningful -- the suits sit sixteen
// bits apart, so a mask against a card of another suit compares nothing.  At a
// following-suit node the legal moves are all one suit by construction, and the
// caller checks the target the same way; see cover_partner_follow.
//
// NO_CARD when nothing covers it, which is a real answer rather than an error:
// a cover partner holding nothing above the nil bidder's card has no choice to
// make and should be left in the canonical order.
inline CardId cheapest_cover_above(Hand cards, CardId target) {
    const Hand above = cards & ~((card_bit(target) << 1) - 1);
    return above ? lowest_card(above) : NO_CARD;
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
inline void cash_tricks(const Hand hands[4], int seat, int& sum, int& best) {
    sum = 0;
    best = 0;
    const int lho = (seat + 1) & 3;
    const int rho = (seat + 3) & 3;
    const Hand all = hands[0] | hands[1] | hands[2] | hands[3];
    const bool lho_trumps = (hands[lho] & suit_mask(SUIT_SPADES)) != 0;
    const bool rho_trumps = (hands[rho] & suit_mask(SUIT_SPADES)) != 0;
    for (int suit = 0; suit < 4; ++suit) {
        Hand outstanding = all & suit_mask(suit);
        if (!outstanding) continue;
        const Hand mine = hands[seat] & suit_mask(suit);
        if (!mine) continue;
        int run = 0;
        while (outstanding) {
            const CardId c = highest_card(outstanding);
            if (!(mine & card_bit(c))) break;
            ++run;
            outstanding &= ~card_bit(c);
        }
        if (run == 0) continue;
        if (suit != SUIT_SPADES && (lho_trumps || rho_trumps)) {
            // An opponent void in the suit and holding a spade ruffs, so only
            // the rounds where BOTH must still follow are safe.
            const int a = count_cards(hands[lho] & suit_mask(suit));
            const int b = count_cards(hands[rho] & suit_mask(suit));
            const int guard = a < b ? a : b;
            if (run > guard) run = guard;
        }
        sum += run;
        if (run > best) best = run;
    }
}

// Extremes of per_nil * n + per_partner * p over the triangle
//
//     n >= kn,   p >= kp,   n + p <= room
//
// A linear function over a triangle takes its extremes at the vertices, so
// three evaluations settle it and nothing here searches.  Returns false when
// the triangle is empty, which every caller reads as "make no claim".
//
// Signs are never assumed.  The two weights change sign with the tie-break
// direction, so `hi` and `lo` are picked by comparison rather than by knowing
// which vertex ought to win.
inline bool triangle_bounds(int per_nil, int per_partner, int kn, int kp, int room, int& hi,
                            int& lo) {
    if (room < kn + kp) return false;
    const int v0 = per_nil * kn + per_partner * kp;
    const int v1 = per_nil * (room - kp) + per_partner * kp;
    const int v2 = per_nil * kn + per_partner * (room - kn);
    hi = v0;
    lo = v0;
    if (v1 > hi) hi = v1;
    if (v2 > hi) hi = v2;
    if (v1 < lo) lo = v1;
    if (v2 < lo) lo = v2;
    return true;
}

// The lower end of the same objective over n + p <= room, with no floor on
// either.  What the opponents' can-cash count buys: they take at least c, so
// the nil side splits at most room = t - c, and the value cannot be below the
// worst corner of that.
inline int split_floor(int per_nil, int per_partner, int room) {
    int lo = 0;
    const int a = per_nil * room;
    const int b = per_partner * room;
    if (a < lo) lo = a;
    if (b < lo) lo = b;
    return lo;
}

// DDS section 3's count, for the OPPONENTS of the nil bidder.
//
// `leader` gains the lead and runs its winners from the top.  Per suit it holds
// a run of r cards from the top of what is outstanding.  In the trump suit all
// r cash -- the only spades above them are its own.  In a side suit a hand void
// in the suit and holding a spade RUFFS, so only the rounds where both members
// of the OTHER side must still follow are safe:
//
//     r' = r                                  if the other side holds no spade
//     r' = min(r, shorter opposing length)    otherwise
//
// The leader's own partner is not consulted: a partner who ruffs still wins the
// trick for this side, and the claim being made is about the SIDE's total.
// That is the whole reason this direction is safe and the mirror image is not
// -- see the note on the cover partner below.
//
// ONE SUIT ONLY, deliberately.  Cashing a first suit can force a hand void in
// it to discard from a second, shortening the guard the second suit's count was
// computed against, so summing across suits is optimistic.  Patch 48 measured
// both and the sum is not worth the argument it would need: cash one suit and
// stop is unconditionally sound.
//
// A CAN-CASH COUNT, not a forced one.  It is a claim about one strategy and it
// bounds a node only from the side that owns the strategy, so it is spent
// against beta at a node where these hands are ON LEAD and nowhere else.
inline int side_cashable_tricks(const Hand hands[4], int leader) {
    const int a = (leader + 1) & 3;
    const int b = (leader + 3) & 3;
    const Hand theirs = hands[a] | hands[b];
    const Hand live = hands[0] | hands[1] | hands[2] | hands[3];
    const bool they_have_trumps = (theirs & suit_mask(SUIT_SPADES)) != 0;
    int best = 0;
    for (int suit = 0; suit < 4; ++suit) {
        const Hand mask = suit_mask(suit);
        const Hand mine = hands[leader] & mask;
        if (!mine) continue;
        Hand outstanding = live & mask;
        int run = 0;
        while (outstanding) {
            const CardId c = highest_card(outstanding);
            if (!(mine & card_bit(c))) break;
            ++run;
            outstanding &= ~card_bit(c);
        }
        if (run == 0) continue;
        if (suit != SUIT_SPADES && they_have_trumps) {
            const int la = count_cards(hands[a] & mask);
            const int lb = count_cards(hands[b] & mask);
            const int shorter = la < lb ? la : lb;
            if (shorter < run) run = shorter;
        }
        if (run > best) best = run;
    }
    return best;
}

// WHY THE COVER PARTNER'S CASH COUNT IS NOT HERE.
//
// The mirror of the above would let a cover partner on lead cash b tricks and
// claim p >= b, bounding the value from above.  Patch 48 measured that
// population alongside this one and then found the hole: the claim is about a
// NAMED HAND rather than about a side, and if the nil bidder is void in the
// cashing suit and holds nothing but spades it is FORCED to ruff its own
// partner's winner.  The trick moves from p to n and the bound is wrong.
//
// The opponents' version above does not have the hole, because a partner who
// ruffs is still on the same side and the constraint is n + p <= t - c either
// way.  Closing the hole needs a guard -- the nil bidder holding at least b
// cards in the cashed suit, or no spades at all -- which shrinks the population
// that made the branch look worth having.  Deferred rather than guessed at; the
// guard and the re-measurement are written up in ROADMAP.md item 43.

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

// ===========================================================================
// ADVERSARIAL PROOFS (Sept 2026 nil-rules research)
// ===========================================================================
// Everything above this line is an EVERY-LINE proof: nil_cannot_be_forced says
// no line of play at all gives the nil bidder a trick, nil_must_take_a_trick
// says every line does.  That is the strongest kind of fact and the one the
// double-dummy handoff (dd_pinned_offset) and doom-charging need.
//
// It is also nearly exhausted.  Measured over 45,000 random endings with the
// two cooperative probes as ground truth: from six cards a hand on, positions
// that are safe or doomed on EVERY line and that the two proofs above miss are
// under 1% of deals, and every-line safety beyond nil_cannot_be_forced is zero
// by nine cards.  There is little left there.
//
// What the search actually spends its nodes on is the ADVERSARIAL question --
// can the nil side keep the nil clean against any defence, can the opponents
// force a trick against any defence -- and the three functions below answer it
// by proof.  They are weaker facts than the ones above in exactly one respect:
// they hold under the right strategy, not under every line.  So:
//
//   * MODE_FAST: the value IS the adversarial verdict, so a proof settles the
//     node outright, exactly as the two every-line proofs do.
//   * MODE_FULL: the primary weight dominates the trick term, so under optimal
//     play the nil's outcome is the one the proof guarantees and the value lies
//     in the same band an every-line proof would put it in.  The same fail-soft
//     bounds apply, and nothing else does.
//   * NOT dd_pinned_offset and NOT doom_charge.  Both need the outcome pinned
//     on EVERY line -- a misdefence could still save a nil that D1 below proves
//     the opponents can break, so charging it on arrival would be wrong.
//
// How they were checked, since each is a proof and a proof is only as good as
// its weakest case (full write-up in the project's nil-rules research notes):
// agreement with the search on every trick-boundary position with one or two
// cards a hand (3.3M positions), on ~650k positions from adversarial generators
// and a mutation walk inside each rule's firing region, and on ~540M firings
// inside real 13-card searches; and, for the SAFE proof, a solver-free check
// that it is an INDUCTIVE INVARIANT -- wherever it fires, the nil side can play
// the next trick so that the nil does not win it and the proof fires again --
// on 6.8M positions.  Loosening any single clause below by one step is refuted
// by a one- or two-card counterexample, which is the evidence that each clause
// is doing work.
// ===========================================================================

// ---------------------------------------------------------------------------
// A DUCKABLE SUIT
// ---------------------------------------------------------------------------
// `mine` is the nil bidder's cards of a side suit, `others` everybody else's.
// True when the nil bidder's j-th LOWEST card is below the j-th lowest of
// `others`, for every j up to the shorter holding.
//
// WHY THAT IS ENOUGH TO NEVER LOSE THE SUIT.  Whatever card of the suit is led,
// it is some o(i), and the condition gives the nil bidder at least min(i, |N|)
// cards below it -- so it ducks, playing its HIGHEST card below the lead.  The
// condition survives the trick:
//
//   * other players' cards leaving only push o(j) upward;
//   * the duck removes n(m) with m >= i: for j < i nothing changes, for
//     i <= j < m it is n(j) < o(j) < o(j+1), and for j >= m it is
//     n(j+1) < o(j+1) -- which is the old condition one index up;
//   * a discard of the nil bidder's HIGHEST card of the suit leaves every
//     n(j) with j < |N| where it was.
//
// This is condition 2 of nil_cannot_be_forced read the other way: that one
// asks for every card below every outstanding card, which is the case where
// ducking needs no strategy at all.  This one needs the nil bidder to duck
// correctly, which is exactly the difference between an every-line fact and an
// adversarial one.
//
// Walked upwards through the ranks of the suit: at the j-th card of `others`
// at least min(j, |N|) of the nil bidder's must already have been seen.
inline bool duckable_suit(Hand mine, Hand others, int suit) {
    if (!mine || !others) return true;
    const int need_all = count_cards(mine);
    int seen_mine = 0;
    int seen_others = 0;
    const int shift = suit * 16;
    Hand bits = (mine | others) >> shift;
    const Hand m = mine >> shift;
    while (bits) {
        const Hand low = bits & (~bits + 1);
        bits &= bits - 1;
        if (m & low) {
            if (++seen_mine >= need_all) return true;
        } else if (++seen_others > seen_mine) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// NIL PROVABLY SAFE AGAINST ANY DEFENCE: duck or cover
// ---------------------------------------------------------------------------
// True when the nil side has a strategy that keeps the nil bidder off every
// remaining trick.  The nil bidder must not be on lead: below the root it
// never is (it reaches the lead only by winning a trick), and at the root the
// search can find its own safe lead.
//
// THE CONDITIONS.  Per side suit s, call L the nil bidder's cards below every
// other outstanding card of s (they lose to any card of s that is led) and H
// the rest.
//
//   Spades.  The cover holds at least as many spades as the nil bidder, and
//   its top |N_S| spades beat the nil bidder's top-vs-top -- duck_depth()
//   matches every one of them.  The cover's other spades are SPARE.
//
//   A side suit that is duckable (above) needs nothing.  Otherwise:
//
//     len = the longest holding of s among the other three hands.  Every
//           round of s is led by one of them and every holder follows, so
//           s can be led at most len more times, and the nil bidder only has
//           to survive R = min(|N_s|, len) of them.
//     a   = min(R, |C_s|), the rounds in which the cover must follow too.
//     M   = duck_depth(H, C_s), how many H cards distinct higher cover cards
//           can shelter.
//     FEASIBLE: M + |L| >= a.  A round in which the cover must follow needs a
//           matched H card under its cover card, or an L card; nothing else
//           is safe while the cover cannot ruff.
//     need = R - |L| - min(M, a), the H cards left once the cover is void in
//           s, each of which costs one ruff from a spare spade.
//     excess = |N_s| - R, the nil bidder's cards of s that can never be
//           forced because the suit runs out first.
//
//   Fires when every side suit is feasible and
//
//       sum(need) + min(SL, sum of excess over suits with need > 0) <= spare
//
//   where SL is the longer OPPONENT spade holding.
//
// WHY THE EXCESS IS CHARGED, and why only against SL.  Every spade lead while
// the nil bidder has no spade costs the cover a spare spade, and the nil
// bidder's discard pays it back only if it comes out of a suit that still
// needs ruffs AND has no excess -- discarding a card the suit would never have
// forced reduces nothing.  So an excess card is worth one spade lead to the
// opponents, and they can make at most SL of those.  Dropping the charge is
// refuted in two cards: N:.98.. 2.7.. T..Q. J.6.., East on lead, spades broken
// -- the spade lead strips the cover's only trump, the nil bidder's discard
// does not help, and the next heart lead finds it with nothing to duck under.
//
// THE STRATEGY THE PROOF RUNS, in one paragraph.  In a duckable suit, duck as
// above.  In a round where the cover follows, play a matched pair (the nil
// bidder's H card under the cover's partner card) while there are enough of
// them, otherwise an L card with an unmatched cover card.  Once the cover is
// void, ruff the nil bidder's unmatched H card with the cover's LOWEST spare
// spade.  Spades led: with spades, both play their top one (domination is
// kept); without, the cover follows with a spare spade and the nil bidder
// discards where it pays the most.  A forced ruff by an all-spade nil bidder
// is overruffed, because the cover holds at least as many spades and so is
// then all spades too.  Each case keeps the condition true at the next trick
// boundary, so the proof re-fires there and the argument is an induction on
// tricks left.  That induction step is what the lab's closure check verified
// mechanically.
//
// NOT THE SAME QUESTION AS cover_deficit_depth OR duck_depth, though it is
// built from both: those measure one suit's supply of covers; this decides
// whether the supplies of all four suits, plus the cover's trumps, add up to
// a strategy.
inline bool nil_duck_or_cover(const Hand hands[4], int nil_seat, bool nil_on_lead) {
    if (nil_on_lead) return false;
    const int cov = (nil_seat + 2) & 3;
    const int lho = (nil_seat + 1) & 3;
    const int rho = (nil_seat + 3) & 3;
    const Hand spades = suit_mask(SUIT_SPADES);
    const Hand nil_sp = hands[nil_seat] & spades;
    const Hand cov_sp = hands[cov] & spades;
    const int n_nil_sp = count_cards(nil_sp);
    const int n_cov_sp = count_cards(cov_sp);
    // The cheap gate, and the common failure: a nil spade the cover cannot
    // shelter one-for-one.
    if (n_cov_sp < n_nil_sp) return false;
    if (nil_sp && duck_depth(nil_sp, cov_sp, SUIT_SPADES) != n_nil_sp) return false;
    const int spare = n_cov_sp - n_nil_sp;

    int need = 0;
    int excess = 0;
    for (int suit = SUIT_HEARTS; suit <= SUIT_CLUBS; ++suit) {
        const Hand sm = suit_mask(suit);
        const Hand mine = hands[nil_seat] & sm;
        if (!mine) continue;
        const Hand h_lho = hands[lho] & sm;
        const Hand h_cov = hands[cov] & sm;
        const Hand h_rho = hands[rho] & sm;
        const Hand others = h_lho | h_cov | h_rho;
        // Nobody else holds the suit: nobody can lead it, and the nil bidder
        // is not on lead.  Its cards of the suit will only ever be discards.
        if (!others) continue;
        const Hand low = mine & ((others & (~others + 1)) - 1);
        if (low == mine) continue;  // all L: they lose to any card led
        if (duckable_suit(mine, others, suit)) continue;
        const Hand high = mine & ~low;
        const int n = count_cards(mine);
        const int n_low = count_cards(low);
        const int n_cov = count_cards(h_cov);
        int len = count_cards(h_lho);
        if (n_cov > len) len = n_cov;
        const int n_rho = count_cards(h_rho);
        if (n_rho > len) len = n_rho;
        const int rounds = n < len ? n : len;
        const int cover_rounds = rounds < n_cov ? rounds : n_cov;
        const int matched = h_cov ? duck_depth(high, h_cov, suit) : 0;
        if (matched + n_low < cover_rounds) return false;
        const int sheltered = matched < cover_rounds ? matched : cover_rounds;
        const int ruffs = rounds - n_low - sheltered;
        if (ruffs > 0) {
            need += ruffs;
            if (need > spare) return false;
            excess += n - rounds;
        }
    }
    if (excess) {
        const int sl_lho = count_cards(hands[lho] & spades);
        const int sl_rho = count_cards(hands[rho] & spades);
        const int sl = sl_lho > sl_rho ? sl_lho : sl_rho;
        need += excess < sl ? excess : sl;
    }
    return need <= spare;
}

// ---------------------------------------------------------------------------
// NIL PROVABLY FORCED THIS TRICK: the forcing lead (D1) and the forced ruff (D1b)
// ---------------------------------------------------------------------------
// The opponents' side of the same question, for a position where one of them
// is on lead.  Both are one-trick arguments: they name a lead and show that
// every reply by the nil side leaves the nil bidder winning the trick.  On
// in-search FAILS positions the two together reach 8,198 of the 8,203 that an
// exact one-trick minimax finds, so there is essentially nothing left to find
// at this depth.
//
// D1, THE FORCING LEAD.  Returns the suit s, or -1.  The leader X may legally
// lead s, and:
//
//   * the nil bidder holds s, and its LOWEST card of s beats X's lowest;
//   * the cover holds s and every card of s it holds is below that nil card,
//     or it is void in s and holds no spade (so it cannot ruff);
//   * X's partner holds a card of s below that nil card, or is void in s and
//     holds a non-spade to discard.
//
// X leads its lowest card of s.  Every card that reaches the trick is then
// below the nil bidder's lowest card of s, which it must play or beat, and no
// spade can arrive.  The last condition is ROADMAP item 32's rescue: an
// opponent void in s holding only spades is forced to ruff, and the ruff saves
// the nil -- N:.T.. 9... .7.. .4.., West on lead and spades broken, is exactly
// that trick.
//
// Spades as s need spades broken or a leader holding nothing else; a void in
// spades then discards, whoever it is.
inline int forcing_lead_suit(const Hand hands[4], int nil_seat, int leader, bool broken) {
    if (((leader ^ nil_seat) & 1) == 0) return -1;  // an opponent must be on lead
    const int cov = (nil_seat + 2) & 3;
    const int partner = (leader + 2) & 3;
    const Hand spades = suit_mask(SUIT_SPADES);
    const Hand lead_hand = hands[leader];
    const bool leader_only_spades = (lead_hand & ~spades) == 0;
    for (int suit = SUIT_SPADES; suit <= SUIT_CLUBS; ++suit) {
        const Hand sm = suit_mask(suit);
        const Hand xs = lead_hand & sm;
        if (!xs) continue;
        const Hand ns = hands[nil_seat] & sm;
        if (!ns) continue;
        if (suit == SUIT_SPADES && !broken && !leader_only_spades) continue;
        const CardId nil_low = static_cast<CardId>(lowest_card(ns));
        if (lowest_card(xs) > nil_low) continue;
        const Hand cs = hands[cov] & sm;
        if (cs) {
            if (highest_card(cs) > nil_low) continue;
        } else if (suit != SUIT_SPADES && (hands[cov] & spades)) {
            continue;  // the cover can ruff
        }
        const Hand ys = hands[partner] & sm;
        if (ys) {
            if (lowest_card(ys) > nil_low) continue;  // forced to play over the nil
        } else if (suit != SUIT_SPADES && (hands[partner] & ~spades) == 0) {
            continue;  // nothing but spades: a forced ruff rescues the nil
        }
        return suit;
    }
    return -1;
}

// D1b, THE FORCED RUFF.  The nil bidder holds nothing but spades, and the
// opponent on lead holds a side suit t the nil bidder is void in.  Led, t makes
// the nil bidder ruff, and the ruff wins unless
//
//   * the cover, also void in t, holds a spade above the nil bidder's lowest
//     (it ruffs first and the nil bidder underruffs, or it overruffs), or
//   * the leader's partner, void in t, holds nothing but spades (it is forced
//     to ruff, and might ruff higher).
//
// Rare -- 22 of 26,090 in-search FAILS samples -- but it is the one-trick case
// D1 cannot see, because D1 asks about suits the nil bidder holds.  It is also
// the case that makes the spade gate on nil_cannot_be_forced exact rather than
// conservative (ROADMAP, "Weakening condition 1").
inline bool forced_ruff_lead(const Hand hands[4], int nil_seat, int leader) {
    if (((leader ^ nil_seat) & 1) == 0) return false;
    const Hand spades = suit_mask(SUIT_SPADES);
    const Hand nil_hand = hands[nil_seat];
    if (!nil_hand || (nil_hand & ~spades)) return false;
    const int cov = (nil_seat + 2) & 3;
    const int partner = (leader + 2) & 3;
    const Hand above_nil_low = ~((card_bit(static_cast<CardId>(lowest_card(nil_hand))) << 1) - 1);
    for (int suit = SUIT_HEARTS; suit <= SUIT_CLUBS; ++suit) {
        const Hand sm = suit_mask(suit);
        if (!(hands[leader] & sm)) continue;
        if (!(hands[cov] & sm) && (hands[cov] & spades & above_nil_low)) continue;
        if (!(hands[partner] & sm) && !(hands[partner] & ~spades)) continue;
        return true;
    }
    return false;
}

}  // namespace nil

#endif  // NIL_BOUNDS_HPP
