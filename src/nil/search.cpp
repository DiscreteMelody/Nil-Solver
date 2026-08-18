#include "nil/search.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "nil/bounds.hpp"
#include "nil/rules.hpp"
#include "nil/statekey.hpp"
#include "nil/tt.hpp"

namespace nil {
namespace {

struct State {
    Hand hands[4];
    int leader;
    CardId trick[3];
    int trick_len;
    bool broken;

    bool empty() const { return (hands[0] | hands[1] | hands[2] | hands[3]) == 0; }
    int to_play() const { return (leader + trick_len) & 3; }
    int led_suit() const { return trick_len ? card_suit(trick[0]) : -1; }
};

// One table per thread, reused across solves and invalidated by generation
// rather than by clearing.  See release_transposition_table().
TranspositionTable& shared_table() {
    static thread_local TranspositionTable table;
    return table;
}

// Stand-ins for "no window at all", used by MODE_FULL.  No value the objective
// can produce comes within several orders of magnitude of either, so every
// cutoff test in search() is dead on that path: full mode is still exhaustive
// minimax, and it is exhaustive by construction rather than by a flag anyone
// could forget to set.  Halved from INT_MAX so that shifting a window by one
// trick's gain cannot overflow.
constexpr int WINDOW_MIN = -(1 << 29);
constexpr int WINDOW_MAX = 1 << 29;

struct Ctx {
    int nil_seat = 0;
    int primary_weight = 0;    // K*K, or 0 when the nil is already set
    int secondary_weight = -1; // -K the nil side wants tricks, +K it wants rid of them
    int tertiary_weight = 0;   // 1 when the cover partner's share is what counts
    bool break_forced = false;
    bool collapse = true;      // one move per class of rank-equivalent cards
    bool last_trick = true;    // evaluate a forced final trick instead of searching it
    bool tt_boundaries_only = true;  // consult the table only at a trick boundary
    bool target_bounds = true;       // the arithmetic reach bound; MODE_FULL only
    // True when no remaining trick can lower the value, i.e. every weight is
    // non-negative.  That makes what is already banked a lower bound on the
    // whole subtree, which is the one fact the "already past beta" cutoff in
    // search() rests on.  Always true in MODE_FAST, where the weights are
    // (1, 0, 0) and the value is a count.
    bool gains_nonnegative = false;
    // True when a subtree's value is exactly the number of tricks the nil
    // bidder takes in it -- primary 1, nothing weighted above or below.  That
    // is what turns the two proofs in bounds.hpp from statements about the PLAY
    // into statements about the VALUE, and it is what confines them to
    // MODE_FAST: the full objective still owes its caller the pair's trick
    // total and the split between the two partners, and neither proof says
    // anything about either.  Read off the weights rather than off the mode,
    // like gains_nonnegative, because it is a fact about the weights.
    bool value_is_nil_tricks = false;
    bool static_bounds = true;  // the proofs in bounds.hpp; off is the control arm
    // Spend the proofs in MODE_FULL too, as bounds rather than as values.  Off
    // is the control arm, and off is also what every measurement recorded
    // before patch 29 was taken under.
    bool full_static_bounds = true;
    // True when the search may try moves in an order other than the canonical
    // one.  Item 6 reads this; nothing does yet, and patch 15 landed it inert
    // on purpose so that the flag, the ABI bit and the control-arm test all
    // exist before the first heuristic has anywhere to hide.
    //
    // NOTE ON THE GATE, because it departs from what gains_nonnegative and
    // value_is_nil_tricks do.  Those two are read off the WEIGHTS, deliberately,
    // so that no flag anyone could forget to set stands between MODE_FULL and
    // its guarantees.  This one is read off the MODE, and there is no honest
    // way to do otherwise: what makes reordering unsafe is not a property of
    // the objective but the fact that MODE_FULL's chosen move is an output the
    // oracle checks card for card, and MODE_FAST has no principal variation to
    // report.  That is a fact about which mode owes its caller what, so the
    // mode is the right thing to read.  Recorded rather than glossed, because
    // the pattern elsewhere in this file is the opposite one.
    bool order_moves = false;

    // Narrow the window as moves come back, which is what makes the fail-soft
    // cutoff reachable in MODE_FULL.  Answer-neutral in both modes; see
    // SearchOptions::narrow_window for why, and for why fast mode is unchanged
    // node for node rather than merely unchanged in its answers.
    bool narrow_window = true;

    // Re-derive the reported move canonically after the search has found the
    // value.  Set when ordering runs in MODE_FULL, which is the only situation
    // where the two can disagree.
    bool canonicalise = false;
    std::uint8_t tt_tag = TAG_NONE;  // which objective this solve's values are on
    std::uint64_t nodes = 0;
    TranspositionTable* tt = nullptr;  // null when the caller turned it off
};

// Any legal move, for a static cutoff to hand back.  Both proofs in bounds.hpp
// hold down EVERY line from the position, so no move is better placed than any
// other to carry the bound they return -- there is nothing to choose between
// them, and the canonically lowest is what the tie-break picks among equals
// anyway.  Nothing downstream reads it as a recommendation: the static bounds
// only run in MODE_FAST, which reports no principal variation.  What it is for
// is solve()'s invariant that a position with cards in it yields a move.
CardId first_legal_move(const State& st) {
    const int seat = st.to_play();
    const Hand moves = legal_moves(st.hands[seat], st.trick_len, st.led_suit(), st.broken);
    return moves ? lowest_card(moves) : NO_CARD;
}

// ROADMAP ITEM 6a: THE NIL BIDDER, FOLLOWING SUIT.
//
// Returns the move to try before the canonical enumeration, or NO_CARD to leave
// the order alone.
//
// The rule is the nil bidder's whole strategy in one line: play *the highest
// card that can still lose*.  A card below the current best loses this trick
// whatever anyone does afterwards, so the highest such card is a shed that is
// free -- it sheds more danger than anything under it and costs nothing this
// trick.  The nil bidder minimises against `[0, 1]` and cuts the moment a move
// comes back zero, so the move most likely to come back zero is the one to ask
// about first.
//
// WHY THIS IS THE WHOLE OF 6a, and what it deliberately leaves out.
//
//   - *Only when following.* Void in the led suit the nil bidder is
//     discarding, and "most dangerous" is then a comparison ACROSS suits that
//     needs a per-suit count rather than a bit scan.  That is item 6d, and
//     folding it in here would mean this patch's number covered two heuristics.
//   - *Only losing cards.* A card ABOVE the current best sheds more still, and
//     loses if a later seat covers it -- but a later OPPONENT will decline to
//     cover, because letting the nil bidder win is the opponent's whole
//     objective.  So the speculative shed is only good when the covering
//     partner is still to play, which is a lookahead this does not do.  Left
//     out on purpose; see the entry in ROADMAP.md.
//   - *No demotion.* Cards that certainly win want to be searched last, and
//     already are: they are the high cards of the suit, and the canonical
//     order is ascending, so it puts them at the back for free.
//
// `moves` is the REDUCED move set, so the card returned is a class
// representative and is a legal move by construction -- which is what keeps
// this compatible with the equivalent-card reduction rather than fighting it.
CardId nil_bidder_shed(const State& st, Hand moves) {
    const CardId best = trick_best_card(st.trick, st.trick_len);
    if (best == NO_CARD) return NO_CARD;  // on lead: no trick to lose to yet
    const int led = card_suit(st.trick[0]);
    // Following suit means every legal move is in the led suit; being void
    // means the whole hand is legal, and that is 6d's case rather than this
    // one.  Testing the mask is cheaper than testing the hand.
    if (moves & ~suit_mask(led)) return NO_CARD;

    Hand losing;
    if (card_suit(best) != led) {
        // A ruff is already on the trick and the nil bidder is following the
        // led suit, so nothing it can play beats anything: every move loses.
        losing = moves;
    } else {
        // Same suit, so bit order is rank order and everything below the
        // winning card loses to it.
        losing = moves & (card_bit(best) - 1);
    }
    if (!losing) return NO_CARD;  // every legal card wins the trick as it stands
    return highest_card(losing);
}


//
// FAIL-SOFT ALPHA-BETA.  The return is exact when it lands strictly inside
// [alpha, beta]; at or below alpha it is an upper bound on the true value, at
// or above beta a lower bound.  MODE_FULL passes [WINDOW_MIN, WINDOW_MAX],
// which no value can reach, so its returns are always exact and its node count,
// its move choices and its principal variation are exactly what they were
// before alpha-beta existed.  MODE_FAST passes [0, 1].
//
// WHY THE WINDOW IS NEVER NARROWED.  The usual `alpha = max(alpha, best)` is
// missing on purpose, and nothing is lost by it.  MODE_FAST's window is null --
// beta is alpha + 1 -- so for a max node to narrow alpha it would need a value
// strictly between alpha and beta, and there are no integers there; it either
// leaves alpha alone or it cuts.  The shifted windows below are null too, since
// shifting moves both ends equally.  So every window in a fast search is
// already minimal, and the whole search is the AND-OR / null-window search the
// boolean question wants: the opponents need ONE line that forces a trick onto
// the nil bidder, the nil side needs EVERY opponent line to fail, and the first
// answer either way ends the node.
//
// That is also what keeps MODE_FULL exhaustive.  An infinite window still
// prunes if it is narrowed -- alpha rises to the best value so far and the
// children inherit it -- so leaving it alone is not an optimisation forgone,
// it is the thing that makes "no cutoffs" true rather than merely intended.
//
// MOVE CHOICE.  Candidate moves are enumerated in canonical order and replace
// the incumbent only on a STRICT improvement, so among equal-valued moves the
// canonically lowest card wins.  Without cutoffs that makes the PV reproducible
// and identical to nil_oracle.py's; with them, `best_move` at a node that cut
// off is merely the move that caused the cut.  That is fine where it is used --
// MODE_FAST reports no PV, and MODE_FULL never cuts.
//
// The one thing that is not a plain enumeration is the equivalent-card
// reduction (rules.hpp), which drops candidates that are a lower candidate
// played under a different name.  It is a statement about the game tree rather
// than about the search, so it is unaffected by pruning.
// ROADMAP ITEM 6b: THE OPPONENTS, ON LEAD.
//
// Returns the card to try before the canonical enumeration, or NO_CARD.
//
// An opponent on lead is maximising against `[0, 1]` and cuts the moment a
// line comes back with the nil bidder taking a trick, so the lead to ask about
// first is the one most likely to force one.
//
// WHICH SUIT.  Not one the nil bidder is void in -- that is the lead it most
// wants, because a void is a free discard of whatever it is most afraid of.
// The suit to attack is the one whose holding runs short of covers soonest,
// which is what cover_deficit_depth measures: the number of the nil bidder's
// own cards below the point where Hall's condition fails, and so the number of
// times the suit must be led before it is stranded.  Depth 0 means the very
// next lead of that suit strands it.
//
// Where no suit is short of covers, decline rather than guess.  There is no
// ranking to be had in that case, and a promotion no better than the canonical
// order costs a branch and buys nothing.
//
// WHICH CARD.  The lowest of the chosen suit.  The point is to leave the nil
// bidder's card the one that wins; a high lead does its ducking for it.
//
// The deficit is read off the four hands, which makes it a trick-boundary
// measure -- and a lead is a trick boundary, so there is nothing to guard here.
CardId opponent_attack_lead(const State& st, int nil_seat, Hand moves) {
    const Hand nil_hand = st.hands[nil_seat];
    const Hand outstanding = relevant_cards(st.hands, NO_CARD) & ~nil_hand;

    int best_suit = -1;
    int best_depth = SUIT_COVERED;
    for (int suit = 0; suit < 4; ++suit) {
        const Hand mine = nil_hand & suit_mask(suit);
        if (!mine) continue;  // void: leading here hands it a free discard
        if (!(moves & suit_mask(suit))) continue;  // cannot legally lead this suit
        const int depth = cover_deficit_depth(mine, outstanding & suit_mask(suit), suit);
        if (depth < best_depth) {
            best_depth = depth;
            best_suit = suit;
        }
    }
    if (best_suit < 0) return NO_CARD;
    return lowest_card(moves & suit_mask(best_suit));
}


// ROADMAP ITEM 6d: THE NIL BIDDER, OFF SUIT.
//
// 6a handles the nil bidder when it must follow, where "the highest card that
// can still lose" is a bit scan against the card currently winning.  These two
// handle the cases where it may play anything, and there the same rule needs a
// comparison ACROSS suits -- which of two losing cards sheds more danger is not
// a question about rank.
//
// DANGER IS DUCKING SUPPLY, NOT RANK, and cover_deficit_depth in bounds.hpp is
// the measure: how many of the nil bidder's own cards sit below the point where
// its holding runs short of covers, and so how many times the suit must be led
// before it is stranded.  Small is dangerous.  A card nobody can beat scores 0
// whatever its rank; an ace behind three small ones scores 3.

// Discarding: void in the led suit, so anything is legal.  Shed the highest
// card that still loses, from whichever suit is closest to running out of
// covers.
//
// The losing filter is not decoration.  With a void the hand includes trumps,
// and a spade thrown on a side-suit lead RUFFS -- it takes the trick, which is
// the one outcome the nil bidder is trying to avoid.  Filtering to cards that
// cannot win the trick as it stands keeps that out, and costs one pass.
CardId nil_bidder_discard(const State& st, int nil_seat, Hand moves) {
    const CardId best = trick_best_card(st.trick, st.trick_len);
    Hand losing = 0;
    for (Hand rest = moves; rest;) {
        const CardId c = take_lowest(rest);
        if (!beats(c, best)) losing |= card_bit(c);
    }
    if (!losing) return NO_CARD;  // everything wins; nothing to be done here

    const Hand nil_hand = st.hands[nil_seat];
    const Hand outstanding = relevant_cards(st.hands, NO_CARD) & ~nil_hand;
    int best_suit = -1;
    int best_depth = SUIT_COVERED + 1;
    for (int suit = 0; suit < 4; ++suit) {
        if (!(losing & suit_mask(suit))) continue;
        const int depth =
            cover_deficit_depth(nil_hand & suit_mask(suit), outstanding & suit_mask(suit), suit);
        if (depth < best_depth) {
            best_depth = depth;
            best_suit = suit;
        }
    }
    if (best_suit < 0) return NO_CARD;
    return highest_card(losing & suit_mask(best_suit));
}

int search(Ctx& ctx, const State& st, CardId& best_move, int alpha, int beta);

// Apply `card` at `st`.  Returns what the trick banked -- zero unless this card
// completed one -- and writes the child position to `next`.
//
// Split out of search()'s loop so that solve_moves() below runs the identical
// transition rather than a second copy of it.  Two copies of this arithmetic is
// how a move list would come to disagree with the search that produced it.
int advance(const Ctx& ctx, const State& st, CardId card, State& next) {
    const int seat = st.to_play();
    const int led = st.led_suit();

    next = st;
    next.hands[seat] &= ~card_bit(card);
    next.broken = spades_broken_after(st.broken, st.trick_len, led, card_suit(card),
                                      ctx.break_forced);

    if (st.trick_len < 3) {
        next.trick[st.trick_len] = card;
        next.trick_len = st.trick_len + 1;
        return 0;
    }

    const CardId played[4] = {st.trick[0], st.trick[1], st.trick[2], card};
    const int winner = trick_winner(st.leader, played, 4);
    next.leader = winner;
    next.trick_len = 0;
    int gained = 0;
    if (winner == ctx.nil_seat) gained += ctx.primary_weight + ctx.tertiary_weight;
    if (((winner ^ ctx.nil_seat) & 1) == 0) gained += ctx.secondary_weight;
    return gained;
}

// The value of playing `card` at `st`, against the window `st` was given.
//
// Mid-trick `gained` is zero, so the shifted window below is the unshifted one
// and the two cases collapse into one line.  The early return stays guarded on
// the trick actually completing, because that is the condition its argument is
// written for and folding it in would rest on beta never reaching zero.
int value_after(Ctx& ctx, const State& st, CardId card, int alpha, int beta, State* next_out) {
    State next;
    const int gained = advance(ctx, st, card, next);
    if (next_out) *next_out = next;

    if (st.trick_len == 3 && ctx.gains_nonnegative && gained >= beta) {
        // This trick alone has already carried the line to beta, and no later
        // trick can take it back, so the rest of the hand cannot change which
        // side of the window the value falls on.  `gained` is a lower bound on
        // it, which is all a fail-high owes the caller.  Chang's
        // `if (goal <= 0) return 1`, arrived at from the window rather than
        // from the rules.
        //
        // It also has a second effect worth knowing about.  In MODE_FAST the
        // only non-zero gain is the nil bidder taking a trick, which is worth
        // exactly 1, and beta is exactly 1 -- so this branch intercepts every
        // gain that could have shifted the window, and the shifted call below
        // never sees a non-zero shift.  Every node of a fast search therefore
        // sees the same window [0, 1], every entry it stores is a bound on that
        // window, and no probe that finds an entry ever fails to be answered by
        // it.  The pruning is free of the usual cost of bounded entries, and
        // Solution::tt_partial staying at zero is what checks it.
        return gained;
    }

    CardId ignored;
    // The child is asked about the value of the REST of the hand, so the window
    // it has to beat is this one less what the trick just banked.
    return gained + search(ctx, next, ignored, alpha - gained, beta - gained);
}

int search(Ctx& ctx, const State& st, CardId& best_move, int alpha, int beta) {
    ++ctx.nodes;    best_move = NO_CARD;
    if (st.empty()) return 0;

    // LAST TRICK.  Chang's `if (tricks_left == 1) return LastTrick(sp)`, which
    // this search did not have: it recursed to the bottom like any other trick
    // and spent five nodes doing it -- one per ply, plus the terminal that
    // returns zero -- on a trick where nobody has a decision to make.
    //
    // At a trick boundary all four hands are the same size, so one card in the
    // leader's hand means one card in every hand: the trick is fully determined
    // before it starts.  There is no move to order, no window to test and no
    // cutoff available, because there is no branching to cut.  Playing it out
    // is arithmetic, and this is that arithmetic.
    //
    // EXACT, NOT A BOUND.  Nothing here depends on alpha or beta, and the value
    // returned is the value of the subtree rather than a claim about where it
    // sits relative to a window -- the same standing as the `nil_cannot_be_forced`
    // return below, and for the same reason: the line is forced.
    //
    // The broken flag is not updated because nothing reads it again; legality
    // is a constraint on later tricks and there are none.  Like the static
    // bounds, this sits ahead of the transposition probe and neither probes nor
    // stores: a node answered by four bit scans and a comparison is cheaper to
    // redo than to remember.
    if (ctx.last_trick && st.trick_len == 0) {
        const Hand lead_hand = st.hands[st.leader];
        if ((lead_hand & (lead_hand - 1)) == 0) {
            const CardId played[4] = {lowest_card(lead_hand),
                                      lowest_card(st.hands[(st.leader + 1) & 3]),
                                      lowest_card(st.hands[(st.leader + 2) & 3]),
                                      lowest_card(st.hands[(st.leader + 3) & 3])};
            const int winner = trick_winner(st.leader, played, 4);
            int gained = 0;
            if (winner == ctx.nil_seat) gained += ctx.primary_weight + ctx.tertiary_weight;
            if (((winner ^ ctx.nil_seat) & 1) == 0) gained += ctx.secondary_weight;
            best_move = played[0];
            return gained;
        }
    }

    // STATIC BOUNDS.  Two proofs that settle the position outright; see
    // bounds.hpp for both, and for why only one of them survives mid-trick.
    // They sit ahead of the transposition probe because they are cheaper than
    // encoding a key and hashing it, and a node they answer wants neither a
    // probe nor a store: it is not work the table needs to remember, because
    // reaching this position again costs the same few mask tests.
    if (ctx.static_bounds && ctx.value_is_nil_tricks && st.trick_len == 0) {
        // Whether the nil bidder still holds a spade is the cheap gate on both
        // proofs, and they want opposite answers to it -- a spade is what makes
        // safety unprovable and what makes a forced trick provable -- so it is
        // asked once here instead of twice inside them.  It also keeps the work
        // off the common path: a node that fails this test does no more than
        // one mask AND.
        if ((st.hands[ctx.nil_seat] & suit_mask(SUIT_SPADES)) == 0) {
            if (nil_cannot_be_forced(st.hands, ctx.nil_seat, st.leader == ctx.nil_seat)) {
                // Not a bound.  The nil bidder takes no trick down any line, so
                // the value of this subtree is zero exactly, and it is returned
                // as an exact value whatever window was asked about.
                best_move = first_legal_move(st);
                return 0;
            }
        } else if (beta <= 1 && nil_must_take_a_trick(st.hands, ctx.nil_seat)) {
            // A lower bound of one is only a legal fail-soft return when one is
            // at or above beta; below it the caller is entitled to an exact
            // value, and a nil bidder about to take three tricks would be
            // reported as taking one.  In MODE_FAST beta is 1 at every node --
            // see the cutoff further down -- so the guard never costs a cutoff.
            // It is here so that the correctness of this line is a property of
            // the code rather than of a fact about the window that some later
            // item could quietly change.
            best_move = first_legal_move(st);
            return 1;
        }
    }

    // TARGET REACHED.  The other half of Chang's and DDS's check, which this
    // search has only ever had one side of.  DDS runs it at the start of every
    // trick and tests both directions -- tricks already won against the target,
    // and tricks already won PLUS tricks left to play against the target -- and
    // returns without looking at a card either way.
    //
    // What this solver had is the first direction only, and only at the last
    // ply: the `gained >= beta` return in value_after().  The second direction
    // is the one that says "even the best case from here cannot reach the
    // window", and it needs no cards at all.
    //
    // THE BOUND.  From advance(): a trick won by the nil bidder scores
    // primary + tertiary + secondary, one won by the cover partner scores
    // secondary, and a trick won by either opponent scores nothing.  So with n
    // tricks to the nil bidder and p to the partner the subtree is worth
    //
    //     value = per_nil * n + per_partner * p,   n >= 0, p >= 0, n + p <= t
    //
    // which is linear over a simplex, so its extremes are at the three
    // vertices: (0,0), (t,0) and (0,t).  Nothing about the deal enters into it.
    // The window arriving here is already residual -- value_after() shifts it
    // by what the path has banked -- so comparing against it IS Chang's
    // "currently won plus tricks left against target", with the subtraction
    // done on the way down instead of the way up.
    //
    // FAIL-SOFT AND ONE-SIDED.  `hi` is an upper bound on the value and is
    // returned only when it is at or below alpha, `lo` a lower bound returned
    // only at or above beta; both are exactly what a fail-soft cutoff owes its
    // caller.  A node that fires is a node whose parent had already found
    // something this subtree cannot beat.
    //
    // WHY IT IS AHEAD OF THE PROOFS.  It is cheaper than either of them -- one
    // popcount and four comparisons against no cards -- and a node it answers
    // wants neither a probe nor a store, for the same reason the proofs below
    // do not take one.
    //
    // INERT IN MODE_FAST, and provably.  There the value is the nil bidder's
    // trick count, so per_nil is 1 and per_partner is 0: `hi` is t and `lo` is
    // 0 against a window that is [0, 1] at every node.  `hi <= alpha` needs
    // t <= 0, which is the empty position handled at the top, and `lo >= beta`
    // needs 0 >= 1.  The gate is therefore the mode rather than a measurement.
    if (ctx.target_bounds && !ctx.value_is_nil_tricks && st.trick_len == 0) {
        const int t = count_cards(st.hands[ctx.nil_seat]);
        const int all_nil = (ctx.primary_weight + ctx.tertiary_weight + ctx.secondary_weight) * t;
        const int all_partner = ctx.secondary_weight * t;
        int hi = 0;
        if (all_nil > hi) hi = all_nil;
        if (all_partner > hi) hi = all_partner;
        int lo = 0;
        if (all_nil < lo) lo = all_nil;
        if (all_partner < lo) lo = all_partner;
        if (hi <= alpha) {
            best_move = first_legal_move(st);
            return hi;
        }
        if (lo >= beta) {
            best_move = first_legal_move(st);
            return lo;
        }
    }

    // THE SAME TWO PROOFS, SPENT IN MODE_FULL (patch 29).
    //
    // The proofs are about the PLAY -- how many tricks the nil bidder takes --
    // and MODE_FAST could return them as values because there its value IS that
    // count.  MODE_FULL's value also carries the pair's tricks, so a proof
    // settles only part of it and what comes back is a BOUND.  That is the
    // whole difference, and it is why this arm is separate rather than a
    // relaxed gate on the one above.
    //
    // WHAT THIS COSTS.  MODE_FULL's node count has been a fixed point since
    // patch 8 and this spends it: a full search now prunes, so its counts move
    // and are no longer comparable with anything recorded before this patch.
    // The differential oracle is unaffected -- values and principal variations
    // are what it checks, and a fail-soft bound returned only on a cutoff
    // changes neither.
    //
    // THE ARITHMETIC.  From advance(): a trick won by the nil bidder is worth
    // primary + tertiary + secondary, one won by the partner is worth
    // secondary, and nothing else scores.  So with n tricks to the nil bidder
    // and p to the partner,
    //
    //     value = (primary + tertiary + secondary) * n + secondary * p
    //
    // and each proof pins n.  `t` is tricks remaining, which at a trick
    // boundary is the size of any one hand.
    if (ctx.static_bounds && ctx.full_static_bounds && !ctx.value_is_nil_tricks &&
        st.trick_len == 0) {
        const int t = count_cards(st.hands[ctx.nil_seat]);
        if ((st.hands[ctx.nil_seat] & suit_mask(SUIT_SPADES)) == 0) {
            if (nil_cannot_be_forced(st.hands, ctx.nil_seat, st.leader == ctx.nil_seat)) {
                // n = 0 exactly, so the primary and tertiary terms vanish and
                // the value is secondary * p for some p in [0, t].  Two ends,
                // ordered by the sign of the weight rather than assumed.
                const int span = ctx.secondary_weight * t;
                const int lo = span < 0 ? span : 0;
                const int hi = span < 0 ? 0 : span;
                if (hi <= alpha) {
                    best_move = first_legal_move(st);
                    return hi;
                }
                if (lo >= beta) {
                    best_move = first_legal_move(st);
                    return lo;
                }
            }
        } else {
            // n >= 1.  The value is smallest when n is exactly 1 and the
            // partner's tricks push as far down as their weight allows, which
            // is p = t - 1 when the secondary is negative and p = 0 when it is
            // not.  Guarded on the nil trick being worth something positive, so
            // that the bound is a lower bound for the reason stated rather than
            // for a reason that happens to hold for today's weights.
            const int per_nil = ctx.primary_weight + ctx.tertiary_weight + ctx.secondary_weight;
            if (per_nil > 0 && nil_must_take_a_trick(st.hands, ctx.nil_seat)) {
                const int worst_partner =
                    ctx.secondary_weight < 0 ? ctx.secondary_weight * (t - 1) : 0;
                const int lo = per_nil + worst_partner;
                if (lo >= beta) {
                    best_move = first_legal_move(st);
                    return lo;
                }
            }
        }
    }

    // The key describes the position up to a relabelling of ranks, so the move
    // that comes back out of the table is a slot number rather than a card and
    // has to be read against THIS position's live cards.  That relabelling is
    // order preserving -- it never moves a card across a suit and never
    // reorders two cards within one -- so the canonically lowest of several
    // equally good moves is still the canonically lowest one after it, and the
    // principal variation is the same one an uncached search would produce.
    //
    // AND ONLY AT A TRICK BOUNDARY, by default.  Building the key is O(live
    // cards) and three nodes in four are mid-trick, so the table's own cost is
    // most of the per-node cost of the search; the hit rates that cost buys are
    // 62-70% at a boundary against 5-11% one ply in.  See
    // SearchOptions::tt_boundaries_only for the ply sweep and for why ply 1 in
    // particular cannot repay it: nothing walks a ply-1 node twice, because the
    // boundary parent that is its only route is answered by the table first.
    // `--tt-all-plies` restores the old behaviour as a control arm.
    StateKey key;
    SuitProfile profile;
    std::uint64_t hash = 0;
    bool keyed = false;
    if (ctx.tt && (st.trick_len == 0 || !ctx.tt_boundaries_only)) {
        keyed = encode_state_key(st.hands, st.leader, st.broken, st.trick, st.trick_len, key,
                                 profile);
        if (keyed) {
            hash = mix_key(key);
            // The table hands back an entry only when it settles this window;
            // see tt.hpp.  A bound recorded under a wider window is still a
            // fact about the position, so it is reusable -- what changes with
            // pruning is that not every stored fact is enough.
            if (const TTEntry* hit = ctx.tt->probe(key, hash, ctx.tt_tag, alpha, beta)) {
                best_move = from_relative(hit->move, profile);
                return hit->value;
            }
        }
    }

    const int seat = st.to_play();
    // The nil bidder and its partner minimise; the two opponents maximise.
    const bool maximizing = ((seat ^ ctx.nil_seat) & 1) != 0;
    const int led = st.led_suit();

    Hand moves = legal_moves(st.hands[seat], st.trick_len, led, st.broken);
    // A lone legal card is its own class, so the deep endgame -- where most of
    // the nodes are and where forced plays are common -- skips the work.  The
    // test is `two or more bits set`.
    if (ctx.collapse && (moves & (moves - 1)) != 0) {
        moves = distinct_moves(
            moves, relevant_cards(st.hands, trick_best_card(st.trick, st.trick_len)));
    }
    // The window this node was ASKED about, kept because the loop below may
    // narrow the live one.  Which of the three bounds the result earns is a
    // statement about the caller's window, not about whatever the node talked
    // itself into partway through: classifying `best <= alpha` against a
    // narrowed alpha would call every max node an upper bound, since narrowing
    // sets alpha to best exactly.
    const int alpha_asked = alpha;
    const int beta_asked = beta;

    int best = 0;
    bool have_best = false;

    // ORDERING (item 6a).  One card lifted out of the mask and searched first;
    // everything else keeps the canonical ascending order it always had.  That
    // is the whole mechanism, and it is deliberately not a sort: the loop below
    // costs a `take_lowest` per move today, and both optimisations this project
    // has rejected lost on throughput rather than on nodes.
    CardId promoted = NO_CARD;
    if (ctx.order_moves) {
        if (seat == ctx.nil_seat) {
            // 6a.  Off-suit is 6d and is not written yet, so a discarding nil
            // bidder keeps the canonical order.
            // On lead the canonical order stands: leading was built and
            // measured with the rest of 6d and does nothing.  See ROADMAP.md.
            if (st.trick_len > 0) {
                promoted = (moves & ~suit_mask(card_suit(st.trick[0])))
                               ? nil_bidder_discard(st, ctx.nil_seat, moves)  // 6d
                               : nil_bidder_shed(st, moves);                  // 6a
            }
        } else if (maximizing && st.trick_len == 0) {
            // 6b.  `maximizing` is the test for an opponent, so the covering
            // partner falls through to the canonical order until 6c.
            promoted = opponent_attack_lead(st, ctx.nil_seat, moves);
        }
        if (promoted != NO_CARD) moves &= ~card_bit(promoted);
    }

    while (promoted != NO_CARD || moves) {
        CardId card;
        if (promoted != NO_CARD) {
            card = promoted;
            promoted = NO_CARD;
        } else {
            card = take_lowest(moves);
        }

        const int value = value_after(ctx, st, card, alpha, beta, nullptr);

        if (!have_best || (maximizing ? value > best : value < best)) {
            have_best = true;
            best = value;
            best_move = card;
        }
        // WINDOW NARROWING.  The other half of alpha-beta, and the half that
        // makes the cutoff below reachable at all in MODE_FULL.  Note which
        // bound moves: a maximiser raises its own alpha and leaves beta alone,
        // so the cutoff test on the next line still reads the bound it was
        // written against.  Inert in MODE_FAST -- see SearchOptions.
        if (ctx.narrow_window) {
            if (maximizing) {
                if (best > alpha) alpha = best;
            } else {
                if (best < beta) beta = best;
            }
        }
        // Fail-soft cutoff.  A maximiser that has already reached beta cannot
        // be talked down by its own remaining moves, and the minimising parent
        // will never choose this node once it is this bad; symmetrically at a
        // minimiser reaching alpha.  Either way the moves not looked at cannot
        // change the answer the parent came for.
        if (maximizing ? best >= beta : best <= alpha) break;
    }

    if (keyed) {
        const RelMove rel = best_move == NO_CARD ? REL_NO_MOVE : to_relative(best_move, profile);
        // Which of these three the node earned follows from where `best` landed
        // relative to the window it was GIVEN -- alpha_asked and beta_asked,
        // not the live pair, which narrowing may have moved underneath it.
        // Breaking out of the loop above implies best >= beta_asked at a
        // maximiser (narrowing never moves beta there) and best <= alpha_asked
        // at a minimiser, so a cut node can never be recorded as the bound
        // belonging to the other side.
        const std::uint8_t bound = best <= alpha_asked  ? BOUND_UPPER
                                   : best >= beta_asked ? BOUND_LOWER
                                                        : BOUND_EXACT;
        ctx.tt->store(key, hash, best, rel, profile.total, bound, ctx.tt_tag);
    }
    return best;
}

// Both modes set up identically: weights into the context, then the shared
// table attached unless the caller turned it off.
//
// The value an entry holds is relative to the weights that produced it, and the
// weights differ between the modes -- a fast-mode 1 and a full-mode 1 are not
// the same number.  new_search() on every solve is what keeps one mode's
// entries out of the other's search, and it is not optional.
void configure(Ctx& ctx, int nil_seat, const SearchOptions& opts,
               const ObjectiveWeights& weights) {
    ctx.nil_seat = nil_seat;
    ctx.primary_weight = weights.primary;
    ctx.secondary_weight = weights.secondary;
    ctx.tertiary_weight = weights.tertiary;
    ctx.break_forced = opts.break_on_forced_spade_lead;
    ctx.collapse = opts.collapse_equivalents;
    // Read off the weights rather than off the mode, because it is a fact about
    // the weights: with any of them negative a later trick could pull the value
    // back down, and what is banked so far would bound nothing.
    ctx.gains_nonnegative =
        weights.primary >= 0 && weights.secondary >= 0 && weights.tertiary >= 0;
    // Likewise a fact about the weights.  In MODE_FULL the primary is K*K and
    // the secondary is +/-K, so this is false for every K -- including K = 1,
    // where the primary is 1 but the secondary is not zero.  MODE_FULL
    // therefore never takes a static cutoff, and its node counts stay the fixed
    // point they have been since patch 8.
    ctx.value_is_nil_tricks =
        weights.primary == 1 && weights.secondary == 0 && weights.tertiary == 0;
    ctx.static_bounds = opts.use_static_bounds;
    ctx.full_static_bounds = opts.full_static_bounds;
    // Both halves matter.  MODE_FULL never reorders whatever the caller asked
    // for, because it cannot gain from it and would lose the oracle check by
    // doing it; and within MODE_FAST the caller can still switch ordering off
    // to get the control arm.
    // Ordering is answer-neutral in both modes.  What it costs in MODE_FULL is the
    // canonical tie-break on the principal variation, so it runs unless a caller
    // has asked for that line specifically.  See SearchOptions::canonical_pv.
    // The third condition is not about speed.  With nil_already_set the primary
    // weight is zero, and with minimise_own_tricks the tertiary is zero too, so
    // the value reduces to secondary * nil_side_tricks and says nothing about
    // how that total splits between the nil bidder and its partner.  The two
    // are genuinely interchangeable there, `nil_tricks` is decided by whichever
    // equally-optimal line the search happens to walk, and reordering picks a
    // different one.  Nothing is wrong with either answer -- but the corpus
    // pins the canonical one, so this is a field that must not be allowed to
    // move.  Reordering stays off for the combination that makes it float.
    // Ordering is a pure reorder and never moves a value.  What it used to cost
    // was the tie-break -- which of several equally-optimal moves gets reported
    // -- and canonical_move_for buys that back, so MODE_FULL may now order even
    // when the caller wants the canonical line.  The nil_already_set exclusion
    // patch 24 needed is gone with it: that case floated because the value does
    // not constrain how nil_side_tricks splits, and re-deriving the LINE pins
    // the split regardless of what the value says.
    ctx.order_moves = opts.order_moves;
    ctx.last_trick = opts.last_trick_eval;
    ctx.tt_boundaries_only = opts.tt_boundaries_only;
    ctx.target_bounds = opts.target_bounds;
    // Canonicalise whenever the caller wants the canonical line -- and also,
    // whether they asked or not, whenever the value cannot pin nil_tricks on its
    // own.  That is exactly `primary + tertiary == 0`: the coefficient the value
    // gives a nil trick.  It is zero only with nil_already_set and a minimising
    // tie-break, and there the value reduces to secondary * nil_side_tricks and
    // says nothing about how that total splits between the bidder and its
    // partner.  Re-deriving the line is then the only thing that makes the split
    // deterministic, so it is not the caller's to decline.
    const bool value_pins_nil_tricks = (weights.primary + weights.tertiary) != 0;
    ctx.canonicalise = ctx.order_moves && opts.mode == MODE_FULL &&
                       (opts.canonical_pv || !value_pins_nil_tricks);
    // Unlike ordering, this one is NOT restricted to fast mode -- full mode is
    // the only mode it can do anything for.  Fast mode's window is null, so
    // every narrowing it performs is immediately followed by the cutoff that
    // makes it moot.
    ctx.narrow_window = opts.narrow_window;
    ctx.tt_tag = opts.mode == MODE_FAST ? TAG_FAST : TAG_FULL;

    const std::size_t table_mb =
        opts.tt_megabytes == TT_AUTO ? TT_DEFAULT_MEGABYTES : opts.tt_megabytes;
    if (opts.use_memo && table_mb > 0) {
        TranspositionTable& table = shared_table();
        table.resize(table_mb);  // a no-op at the size it already is
        table.new_search();               // this solve may not see the last one's values
        ctx.tt = &table;
    }
}

// Walk the chosen moves down from `st`, starting with `first`, recovering the
// line the search picked.  MODE_FULL only: it searches between sentinels, so
// every node it revisits here answers exactly, and with the table on this is a
// handful of lookups rather than a re-search.
//
// Shared by solve() and by solve_moves(), which needs one of these per root
// move to recover that move's trick counts.
// The window carries in from the caller so that a PV step is asked the same
// question the root was.  Asking wide here against a table filled under a
// narrow window is not wrong, but it is a re-search of everything.
// The canonically lowest move achieving a value the caller already knows to be
// exact.
//
// Move ordering is a pure reorder, so it cannot change what a node is worth --
// but it does change which of several equally-worthy moves the node happens to
// record, and that recorded move is what a principal variation is built out of.
// This buys the tie-break back: enumerate in the order the unordered search
// would have used and stop at the first move that matches.
//
// Every child here is scored against the window the PARENT was asked about, and
// that window has WINDOW_MIN beneath it at every point a caller reaches this
// function, so no child can fail low and every value compared is exact.  The
// match is therefore a real tie and not two bounds that happen to coincide.
//
// Cost is the index of the answer, not the width of the node: the loop stops at
// the first match, so a node whose canonical move is also its promoted one pays
// for a single lookup, and the table is warm from the search that just ran.
CardId canonical_move_for(Ctx& ctx, const State& st, int value, int alpha, int beta) {
    const int seat = st.to_play();
    Hand moves = legal_moves(st.hands[seat], st.trick_len, st.led_suit(), st.broken);
    if (ctx.collapse && (moves & (moves - 1)) != 0) {
        moves = distinct_moves(moves,
                               relevant_cards(st.hands, trick_best_card(st.trick, st.trick_len)));
    }
    for (Hand h = moves; h;) {
        const CardId card = take_lowest(h);
        if (value_after(ctx, st, card, alpha, beta, nullptr) == value) return card;
    }
    return NO_CARD;  // caller keeps whatever the search recorded
}

bool walk_pv(Ctx& ctx, State st, CardId first, std::vector<Play>& pv_out, std::string& err,
             int pv_alpha = WINDOW_MIN, int pv_beta = WINDOW_MAX) {
    CardId move = first;
    while (!st.empty()) {
        if (move == NO_CARD) {
            err = "internal error: no move available at a non-terminal position";
            return false;
        }
        pv_out.push_back(Play{st.to_play(), move});
        State next;
        advance(ctx, st, move, next);
        st = next;
        if (st.empty()) break;
        const int v = search(ctx, st, move, pv_alpha, pv_beta);
        if (ctx.canonicalise) {
            const CardId c = canonical_move_for(ctx, st, v, pv_alpha, pv_beta);
            if (c != NO_CARD) move = c;
        }
    }
    return true;
}

State state_of(const Position& pos) {
    State st;
    for (int s = 0; s < 4; ++s) st.hands[s] = pos.hands[s];
    st.leader = pos.leader;
    st.trick_len = pos.trick_len;
    for (int i = 0; i < 3; ++i) st.trick[i] = pos.trick[i];
    st.broken = pos.spades_broken;
    return st;
}

std::string with_commas(std::uint64_t n) {
    std::string digits = std::to_string(n);
    std::string out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count && count % 3 == 0) out += ',';
        out += *it;
        ++count;
    }
    return std::string(out.rbegin(), out.rend());
}

}  // namespace

ObjectiveWeights objective_weights(int tricks_remaining, const SearchOptions& opts) {
    if (opts.mode == MODE_FAST) {
        // Nothing packed above or below the nil bidder's trick count, so the
        // value IS that count and the window is [0, 1].  minimise_own_tricks is
        // inert here by construction -- there is no secondary level for it to
        // point at -- and nil_already_set never reaches this function, because
        // solve() answers that combination without searching.
        ObjectiveWeights w;
        w.primary = 1;
        w.secondary = 0;
        w.tertiary = 0;
        return w;
    }

    const int k = tricks_remaining + 1;
    ObjectiveWeights w;
    w.primary = opts.nil_already_set ? 0 : k * k;
    w.secondary = opts.minimise_own_tricks ? k : -k;
    w.tertiary = opts.minimise_own_tricks ? 0 : 1;
    return w;
}

// The largest packed value a position can score while the nil bidder takes no
// trick.  Item 23's bound rests on this being strictly below the smallest value
// a position can score while it takes one, which holds for both objectives:
//
//   minimise_own_tricks = false   secondary = -k, so a safe position scores
//                                 -k * cover <= 0, and a failing one scores at
//                                 least (k*k + 1 - k) - k*(k - 2) = k + 1.
//   minimise_own_tricks = true    secondary = +k, so a safe position scores at
//                                 most k * (k - 1) = k*k - k, and a failing one
//                                 at least k*k + k.
//
// Both gaps are 2k wide and neither depends on the deal.  Undefined when
// nil_already_set, where primary is zero and the two halves collapse into each
// other -- callers must not ask, and solve() does not.
// Below this many tricks the presolve is not worth its own setup.  It is not
// that the fast search gets relatively dearer -- it does not -- but that the
// full search it is meant to bound is already finished in well under a
// millisecond, so there is nothing left for a tighter window to save and the
// per-call cost of a second solve() is all that is left.  Measured on the
// 560-position corpus, which is four to six tricks: presolving everything cost
// 5.9% more nodes and 32% more wall there and returned nothing.
//
// Hand size is the only estimate of a search's cost available before running
// it, which is the whole reason this is a constant rather than a policy.
constexpr int PRESOLVE_MIN_TRICKS = 8;



int max_value_if_nil_safe(int tricks_remaining, const ObjectiveWeights& w) {
    const int cover_only = w.secondary * tricks_remaining;
    return cover_only > 0 ? cover_only : 0;
}

bool solve(const Position& pos, int nil_seat, const SearchOptions& opts, Solution& out,
           std::string& err) {
    if (nil_seat < 0 || nil_seat > 3) {
        err = "nil seat out of range";
        return false;
    }
    if (!validate(pos, err)) return false;

    const ObjectiveWeights weights = objective_weights(pos.tricks_remaining(), opts);

    // ---- fast mode: the nil question, and nothing else ---------------------
    if (opts.mode == MODE_FAST) {
        out = Solution();
        out.nil_seat = nil_seat;
        out.mode = MODE_FAST;
        out.nil_tricks = TRICKS_NOT_COMPUTED;
        out.nil_side_tricks = TRICKS_NOT_COMPUTED;
        out.opponent_tricks = TRICKS_NOT_COMPUTED;

        // The caller has asserted the very thing this mode computes.  There is
        // nothing to search: the secondary objective that nil_already_set
        // exists to expose has no output to land in here, because fast mode
        // reports no trick counts.  Ask in full mode if you want those numbers.
        if (opts.nil_already_set) {
            out.nil_fails = true;
            return true;
        }

        Ctx fast_ctx;
        configure(fast_ctx, nil_seat, opts, weights);
        State root = state_of(pos);
        CardId root_move = NO_CARD;
        // The whole question is "is the nil bidder's trick count at least one",
        // so [0, 1] is not a window onto the answer -- it IS the answer, and the
        // search returns a bound on whichever side settles it.  There are no
        // integers strictly inside a window of width one, so the root always
        // fails one way or the other and never comes back exact.  That is the
        // expected shape of a boolean search, not a loss of information.
        const int fast_value = search(fast_ctx, root, root_move, 0, 1);
        const TTStats fast_stats = fast_ctx.tt ? fast_ctx.tt->stats() : TTStats();

        // No principal variation means no replay, so these two are what is left
        // of the self-check: the value has to be a trick count that the
        // position could actually produce, and a position with cards still in
        // it has to have yielded a move.  Pruning does not weaken the first --
        // a bound out of this search is still assembled from real tricks, so it
        // still lies between zero and the number of them.  The real check on
        // this mode is that it agrees with full mode -- see nil_bench --mode
        // both, and the corpus_modes test that runs it on every build.
        if (fast_value < 0 || fast_value > pos.tricks_remaining()) {
            std::ostringstream os;
            os << "internal inconsistency: fast mode returned " << fast_value
               << " for a position with " << pos.tricks_remaining() << " trick(s) left";
            err = os.str();
            return false;
        }
        if (root_move == NO_CARD && !root.empty()) {
            err = "internal error: no move available at a non-terminal position";
            return false;
        }

        out.nil_fails = fast_value > 0;
        out.value = fast_value;
        out.nodes = fast_ctx.nodes;
        out.tt_probes = fast_stats.probes;
        out.tt_hits = fast_stats.hits;
        out.tt_partial = fast_stats.partial;
        out.tt_stores = fast_stats.stores;
        out.tt_evictions = fast_stats.evictions;
        return true;
    }

    // ---- item 23: buy a root window with a MODE_FAST presolve --------------
    //
    // Full mode's root window has been [WINDOW_MIN, WINDOW_MAX] since patch 8,
    // and patch 22 made narrowing reachable underneath it -- but the FIRST
    // child of the root still gets a window as open as the sentinels allow,
    // because there is no sibling yet to have raised alpha.  On a position
    // whose moves are close in value that is most of the search.
    //
    // The bit that closes it is one fast search away.  If the nil bidder cannot
    // be forced to take a trick then the value is at most
    // max_value_if_nil_safe(), so a beta one above that contains the answer and
    // refutes every line that tries to force a trick the moment it succeeds.
    //
    // Skipped when nil_already_set, where the primary weight is zero and the
    // two halves of the value range are not separated at all.
    int root_alpha = WINDOW_MIN;
    int root_beta = WINDOW_MAX;
    std::uint64_t presolve_nodes = 0;
    TTStats presolve_stats;
    if (opts.presolve_window && !opts.nil_already_set &&
        pos.tricks_remaining() >= PRESOLVE_MIN_TRICKS) {
        SearchOptions probe_opts = opts;
        probe_opts.mode = MODE_FAST;
        // The nested solve inherits the parent's size, which is what
        // `probe_opts = opts` already does now that TT_AUTO is one number for
        // both modes.  It used to have to be pinned by hand: the presolve would
        // otherwise resolve to the MODE_FAST size, and a resize discards the
        // table, so the presolve handed the search it is meant to help a cold
        // table and a reallocation.  That whole hazard is gone with the
        // schedule.
        Solution probe;
        std::string probe_err;
        // A failed presolve is not a failed solve.  It is an optimisation, and
        // the full search below is complete without it, so anything that goes
        // wrong here is dropped and the sentinels stand.
        if (solve(pos, nil_seat, probe_opts, probe, probe_err) && !probe.nil_fails) {
            root_beta = max_value_if_nil_safe(pos.tricks_remaining(), weights) + 1;
        }
        presolve_nodes = probe.nodes;
        presolve_stats.probes = probe.tt_probes;
        presolve_stats.hits = probe.tt_hits;
        presolve_stats.partial = probe.tt_partial;
        presolve_stats.stores = probe.tt_stores;
        presolve_stats.evictions = probe.tt_evictions;
    }

    Ctx ctx;
    configure(ctx, nil_seat, opts, weights);

    State st = state_of(pos);
    CardId move = NO_CARD;
    // The window is the presolve's, or the sentinels if there was none.  Either
    // way the true value is inside it, so what comes back is exact rather than
    // a bound -- which is what the trick counts, the principal variation and
    // the replay check below all require.
    const int value = search(ctx, st, move, root_alpha, root_beta);
    // The root's own move is the head of the line and needs the same treatment
    // as every step below it.
    if (ctx.canonicalise) {
        const CardId c = canonical_move_for(ctx, st, value, root_alpha, root_beta);
        if (c != NO_CARD) move = c;
    }
    const std::uint64_t search_nodes = ctx.nodes + presolve_nodes;
    // Snapshot before the PV walk below, which probes the table again and would
    // otherwise inflate the hit count with lookups that did no search work.
    const TTStats table_stats = ctx.tt ? ctx.tt->stats() : TTStats();

    // Walk the chosen moves to recover the principal variation.  With the memo
    // on this is a handful of lookups; with it off each step re-searches a
    // strictly smaller subtree.  Either way the moves are the same ones the
    // search picked, so the PV matches the oracle's play for play.
    out.pv.clear();
    if (!walk_pv(ctx, st, move, out.pv, err, root_alpha, root_beta)) return false;

    // A solver that lies is worse than no solver.  Replaying recovers the trick
    // counts independently; re-packing them must land back on the search value.
    Tally tally;
    if (!replay_pv(pos, out.pv, nil_seat, opts.break_on_forced_spade_lead, tally, err)) {
        err = "internal inconsistency: " + err;
        return false;
    }
    const int replayed = (weights.primary + weights.tertiary) * tally.nil_tricks +
                         weights.secondary * tally.nil_side_tricks;
    if (replayed != value) {
        std::ostringstream os;
        os << "internal inconsistency: search says " << value << ", replaying the PV gives "
           << replayed << " (nil=" << tally.nil_tricks << ", side=" << tally.nil_side_tricks
           << ")";
        err = os.str();
        return false;
    }

    out.nil_tricks = tally.nil_tricks;
    out.nil_side_tricks = tally.nil_side_tricks;
    out.opponent_tricks = tally.opponent_tricks;
    // When the caller has told us the nil is already broken, that is a fact
    // about the game, not something for the search to rediscover.
    out.nil_fails = opts.nil_already_set || tally.nil_tricks > 0;
    out.value = value;
    out.nil_seat = nil_seat;
    out.mode = MODE_FULL;
    // The presolve's work is this solve's work.  Reporting the full search
    // alone would make item 23 look free, and it is not free -- it is cheap.
    out.nodes = search_nodes;
    out.tt_probes = table_stats.probes + presolve_stats.probes;
    out.tt_hits = table_stats.hits + presolve_stats.hits;
    out.tt_partial = table_stats.partial + presolve_stats.partial;
    out.tt_stores = table_stats.stores + presolve_stats.stores;
    out.tt_evictions = table_stats.evictions + presolve_stats.evictions;
    return true;
}

void release_transposition_table() { shared_table().resize(0); }

bool solve_moves(const Position& pos, int nil_seat, const SearchOptions& opts, Solution& out,
                 std::vector<MoveScore>& moves_out, std::string& err) {
    moves_out.clear();
    if (nil_seat < 0 || nil_seat > 3) {
        err = "nil seat out of range";
        return false;
    }
    if (!validate(pos, err)) return false;

    out = Solution();
    out.nil_seat = nil_seat;
    out.mode = opts.mode;

    const int tricks_remaining = pos.tricks_remaining();
    const ObjectiveWeights weights = objective_weights(tricks_remaining, opts);
    const bool fast = opts.mode == MODE_FAST;

    State root = state_of(pos);
    if (root.empty()) {
        // Nothing to play, so there is no move list to give and no search to
        // run.  Not an error: an exhausted position is a legal thing to ask
        // about, and the empty list is the honest answer.
        out.nil_tricks = fast ? TRICKS_NOT_COMPUTED : 0;
        out.nil_side_tricks = fast ? TRICKS_NOT_COMPUTED : 0;
        out.opponent_tricks = fast ? TRICKS_NOT_COMPUTED : 0;
        out.nil_fails = opts.nil_already_set;
        return true;
    }

    const int seat = root.to_play();
    const bool maximizing = ((seat ^ nil_seat) & 1) != 0;

    // Enumerate the root's legal cards, and record which of them are the same
    // move under another name BEFORE any of them is searched -- the classes are
    // a property of this position, and playing a card changes it.
    const Hand legal = legal_moves(root.hands[seat], root.trick_len, root.led_suit(), root.broken);
    const Hand relevant =
        relevant_cards(root.hands, trick_best_card(root.trick, root.trick_len));
    Hand reps = legal;
    if (opts.collapse_equivalents && (legal & (legal - 1)) != 0) {
        reps = distinct_moves(legal, relevant);
    }

    // Fast mode with the nil already set answers without looking at a card, the
    // same way solve() does -- the caller has asserted the only thing this mode
    // computes.  Every legal card gets the same answer, because it is not an
    // answer about the cards.
    if (fast && opts.nil_already_set) {
        out.nil_fails = true;
        out.nil_tricks = TRICKS_NOT_COMPUTED;
        out.nil_side_tricks = TRICKS_NOT_COMPUTED;
        out.opponent_tricks = TRICKS_NOT_COMPUTED;
        for (Hand h = reps; h;) {
            const CardId card = take_lowest(h);
            MoveScore ms;
            ms.card = card;
            ms.equals = opts.collapse_equivalents ? equivalent_moves(card, legal, relevant)
                                                  : card_bit(card);
            ms.nil_fails = true;
            ms.is_best = true;
            moves_out.push_back(ms);
        }
        return true;
    }

    Ctx ctx;
    configure(ctx, nil_seat, opts, weights);

    std::uint64_t presolve_nodes = 0;
    const int alpha = fast ? 0 : WINDOW_MIN;
    int beta = fast ? 1 : WINDOW_MAX;

    // Item 23, per-card.  The same presolve bound the single-answer path takes,
    // with one extra obligation: a row owes its own exact value, and a card
    // that loses the nil scores on the far side of the threshold, so the tight
    // window would hand back a bound for exactly the rows a caller most wants
    // a number on.  Those rows -- and only those -- are re-searched wide below.
    int tight_beta = beta;
    if (!fast && opts.presolve_window && !opts.nil_already_set &&
        pos.tricks_remaining() >= PRESOLVE_MIN_TRICKS) {
        SearchOptions probe_opts = opts;
        probe_opts.mode = MODE_FAST;
        // The nested solve inherits the parent's size, which is what
        // `probe_opts = opts` already does now that TT_AUTO is one number for
        // both modes.  It used to have to be pinned by hand: the presolve would
        // otherwise resolve to the MODE_FAST size, and a resize discards the
        // table, so the presolve handed the search it is meant to help a cold
        // table and a reallocation.  That whole hazard is gone with the
        // schedule.
        Solution probe;
        std::string probe_err;
        if (solve(pos, nil_seat, probe_opts, probe, probe_err) && !probe.nil_fails) {
            tight_beta = max_value_if_nil_safe(pos.tricks_remaining(), weights) + 1;
        }
        presolve_nodes = probe.nodes;
    }
    beta = tight_beta;

    // The root search first, for two reasons.  It is the cheapest way to the
    // position's own answer -- ordering and cutoffs still apply to it -- and it
    // warms the shared table, so the per-move searches below spend much of
    // their time reading back work this one already did.
    CardId root_move = NO_CARD;
    const int root_value = search(ctx, root, root_move, alpha, beta);

    int best_value = 0;
    bool have_best = false;
    for (Hand h = reps; h;) {
        const CardId card = take_lowest(h);
        MoveScore ms;
        ms.card = card;
        ms.equals =
            opts.collapse_equivalents ? equivalent_moves(card, legal, relevant) : card_bit(card);
        // Same window every move, deliberately.  Narrowing it as the loop went
        // would make later entries bounds relative to earlier ones rather than
        // answers about themselves, and a move list whose entries are not
        // comparable with each other is not a move list.
        ms.value = value_after(ctx, root, card, alpha, beta, nullptr);
        // Fail-high means this card is on the far side of the presolve's
        // threshold -- it loses the nil -- so the number above is a bound and
        // not this row's answer.  Re-search it against the sentinels.  The
        // table is warm by now, and there is one of these per losing card
        // rather than one per card.
        if (beta != WINDOW_MAX && ms.value >= beta) {
            ms.value = value_after(ctx, root, card, WINDOW_MIN, WINDOW_MAX, nullptr);
        }
        if (!have_best || (maximizing ? ms.value > best_value : ms.value < best_value)) {
            have_best = true;
            best_value = ms.value;
        }
        moves_out.push_back(ms);
    }

    if (moves_out.empty()) {
        err = "internal error: no legal move at a non-terminal position";
        return false;
    }

    // The cross-check this entry point gets in place of a principal variation
    // replay: scoring every move and taking the extremum must land where the
    // ordinary search landed.  In MODE_FULL that is exact, because nothing
    // cuts.  In MODE_FAST the root may have stopped at the first card that
    // settled the window, so its value is a bound and only the BOOLEAN is
    // required to agree -- which is the whole of what that mode claims.
    if (fast) {
        if ((best_value > 0) != (root_value > 0)) {
            std::ostringstream os;
            os << "internal inconsistency: the position says nil_fails=" << (root_value > 0)
               << " and the best of its " << moves_out.size() << " move(s) says "
               << (best_value > 0);
            err = os.str();
            return false;
        }
    } else if (best_value != root_value) {
        std::ostringstream os;
        os << "internal inconsistency: the position scores " << root_value
           << " and the best of its " << moves_out.size() << " move(s) scores " << best_value;
        err = os.str();
        return false;
    }

    for (MoveScore& ms : moves_out) {
        ms.is_best = ms.value == best_value;
        if (fast) {
            ms.nil_fails = ms.value > 0;
            continue;
        }
        // MODE_FULL owes each move its trick counts, and they are recovered the
        // way solve() recovers the position's: walk the line this move leads
        // to, replay it from the ORIGINAL position with the move at its head,
        // and read the tally off the replay.  That keeps one implementation of
        // the counting -- replay_pv -- rather than adding a second that would
        // have to remember to credit the trick this card completes.
        State child;
        advance(ctx, root, ms.card, child);
        CardId next_move = NO_CARD;
        if (!child.empty()) {
            const int cv = search(ctx, child, next_move, WINDOW_MIN, WINDOW_MAX);
            if (ctx.canonicalise) {
                const CardId c = canonical_move_for(ctx, child, cv, WINDOW_MIN, WINDOW_MAX);
                if (c != NO_CARD) next_move = c;
            }
        }

        std::vector<Play> line;
        line.push_back(Play{seat, ms.card});
        if (!child.empty() && !walk_pv(ctx, child, next_move, line, err)) return false;

        Tally tally;
        if (!replay_pv(pos, line, nil_seat, opts.break_on_forced_spade_lead, tally, err)) {
            err = "internal inconsistency replaying " + card_to_string(ms.card) + ": " + err;
            return false;
        }
        const int replayed = (weights.primary + weights.tertiary) * tally.nil_tricks +
                             weights.secondary * tally.nil_side_tricks;
        if (replayed != ms.value) {
            std::ostringstream os;
            os << "internal inconsistency: " << card_to_string(ms.card) << " scores " << ms.value
               << ", replaying its line gives " << replayed << " (nil=" << tally.nil_tricks
               << ", side=" << tally.nil_side_tricks << ")";
            err = os.str();
            return false;
        }
        ms.nil_tricks = tally.nil_tricks;
        ms.nil_side_tricks = tally.nil_side_tricks;
        ms.opponent_tricks = tally.opponent_tricks;
        ms.nil_fails = opts.nil_already_set || tally.nil_tricks > 0;

        // The first best move in canonical order is the one solve() would have
        // picked -- it enumerates from the bottom and replaces the incumbent
        // only on a strict improvement -- so keeping its line gives this entry
        // point the same principal variation, already replay-checked above.
        if (out.pv.empty() && ms.value == best_value) out.pv = line;
    }

    // The position's own answer, taken from a best move rather than searched
    // for a second time.
    const MoveScore* best = nullptr;
    for (const MoveScore& ms : moves_out) {
        if (ms.is_best) {
            best = &ms;
            break;
        }
    }
    out.value = fast ? root_value : best_value;
    out.nil_fails = fast ? root_value > 0 : best->nil_fails;
    out.nil_tricks = best->nil_tricks;
    out.nil_side_tricks = best->nil_side_tricks;
    out.opponent_tricks = best->opponent_tricks;
    out.nodes = ctx.nodes + presolve_nodes;

    const TTStats stats = ctx.tt ? ctx.tt->stats() : TTStats();
    out.tt_probes = stats.probes;
    out.tt_hits = stats.hits;
    out.tt_partial = stats.partial;
    out.tt_stores = stats.stores;
    out.tt_evictions = stats.evictions;
    return true;
}
bool replay_pv(const Position& pos, const std::vector<Play>& pv, int nil_seat,
               bool break_on_forced_spade_lead, Tally& tally_out, std::string& err) {
    Hand hands[4];
    for (int s = 0; s < 4; ++s) hands[s] = pos.hands[s];
    int leader = pos.leader;
    bool broken = pos.spades_broken;
    CardId trick[4];
    int trick_len = pos.trick_len;
    for (int i = 0; i < pos.trick_len; ++i) trick[i] = pos.trick[i];
    tally_out = Tally{};

    for (std::size_t ply = 0; ply < pv.size(); ++ply) {
        const int seat = pv[ply].seat;
        const CardId card = pv[ply].card;
        const int expected = (leader + trick_len) & 3;
        std::ostringstream os;
        if (seat < 0 || seat > 3) {
            os << "ply " << ply << ": seat out of range";
            err = os.str();
            return false;
        }
        if (seat != expected) {
            os << "ply " << ply << ": " << SEAT_CHARS[seat] << " played out of turn (expected "
               << SEAT_CHARS[expected] << ")";
            err = os.str();
            return false;
        }
        if (card < 0 || card > 63 || !(hands[seat] & card_bit(card))) {
            os << "ply " << ply << ": " << SEAT_CHARS[seat] << " does not hold "
               << (card >= 0 && card <= 63 ? card_to_string(card) : std::string("?"));
            err = os.str();
            return false;
        }
        const int led = trick_len ? card_suit(trick[0]) : -1;
        const Hand allowed = legal_moves(hands[seat], trick_len, led, broken);
        if (!(allowed & card_bit(card))) {
            os << "ply " << ply << ": " << SEAT_CHARS[seat] << " played " << card_to_string(card)
               << ", which is illegal; legal: " << hand_to_string(allowed);
            err = os.str();
            return false;
        }
        hands[seat] &= ~card_bit(card);
        broken = spades_broken_after(broken, trick_len, led, card_suit(card),
                                     break_on_forced_spade_lead);
        trick[trick_len++] = card;
        if (trick_len == 4) {
            const int winner = trick_winner(leader, trick, 4);
            if (winner == nil_seat) ++tally_out.nil_tricks;
            if (((winner ^ nil_seat) & 1) == 0) {
                ++tally_out.nil_side_tricks;
            } else {
                ++tally_out.opponent_tricks;
            }
            leader = winner;
            trick_len = 0;
        }
    }

    if (trick_len) {
        err = "PV ends mid-trick";
        return false;
    }
    if (hands[0] | hands[1] | hands[2] | hands[3]) {
        err = "PV does not play out every card";
        return false;
    }
    return true;
}

std::string format_pv_compact(const Solution& sol) {
    std::ostringstream os;
    for (std::size_t i = 0; i < sol.pv.size(); ++i) {
        if (i) os << ' ';
        os << SEAT_CHARS[sol.pv[i].seat] << ':' << card_to_string(sol.pv[i].card);
    }
    return os.str();
}

std::string format_pv(const Position& pos, const Solution& sol) {
    struct Item {
        int seat;
        CardId card;
        bool from_pv;
    };
    std::vector<Item> plays;
    for (int i = 0; i < pos.trick_len; ++i)
        plays.push_back(Item{(pos.leader + i) & 3, pos.trick[i], false});
    for (const Play& p : sol.pv) plays.push_back(Item{p.seat, p.card, true});

    std::ostringstream os;
    int leader = pos.leader;
    int running = 0;
    bool first_line = true;
    for (std::size_t i = 0; i < plays.size(); i += 4) {
        const std::size_t n = std::min<std::size_t>(4, plays.size() - i);
        CardId cards[4];
        for (std::size_t j = 0; j < n; ++j) cards[j] = plays[i + j].card;
        const int winner = (n == 4) ? trick_winner(leader, cards, 4) : -1;
        if (winner == sol.nil_seat) ++running;  // primary counter only

        if (!first_line) os << '\n';
        first_line = false;
        os << "  T" << (i / 4 + 1) << " ";
        for (std::size_t j = 0; j < n; ++j) {
            std::string cell = std::string(1, SEAT_CHARS[plays[i + j].seat]) + ":" +
                               card_to_string(plays[i + j].card);
            while (cell.size() < 5) cell += ' ';
            os << ' ' << cell << (plays[i + j].from_pv ? ' ' : '*');
        }
        os << "  won by " << (winner >= 0 ? std::string(1, SEAT_CHARS[winner]) : std::string("?"))
           << "   [" << SEAT_CHARS[sol.nil_seat] << '=' << running << ']';
        if (winner == sol.nil_seat) os << " <-- nil takes a trick";
        if (winner < 0) break;
        leader = winner;
    }
    return os.str();
}

std::string format_solution(const Position& pos, const Solution& sol,
                            const SearchOptions& opts) {
    const bool nil_is_ns = (sol.nil_seat & 1) == 0;
    const bool fast = sol.mode == MODE_FAST;
    const char* side = nil_is_ns ? "NS" : "EW";
    const char* other = nil_is_ns ? "EW" : "NS";
    std::ostringstream os;
    os << "PBN            " << deal_to_pbn(pos.hands) << '\n'
       << format_hands(pos) << '\n'
       << "Leader         " << SEAT_CHARS[pos.leader] << '\n'
       << "Nil bidder     " << SEAT_CHARS[sol.nil_seat] << "  ("
       << (nil_is_ns ? "N/S minimise, E/W maximise" : "E/W minimise, N/S maximise") << ")\n"
       << "Objective      ";
    if (fast) {
        os << "fast mode: the nil question only, no trick counts and no PV\n";
    } else {
        os << (opts.nil_already_set ? "nil already set, so secondary only; "
                                    : "nil tricks first, then ")
           << (opts.minimise_own_tricks ? "each pair sheds what it can"
                                        : "each pair takes what it can")
           << '\n';
    }
    os << "Spades broken  " << (pos.spades_broken ? "yes" : "no") << '\n';
    if (pos.trick_len) {
        os << "On the trick   ";
        for (int i = 0; i < pos.trick_len; ++i) {
            if (i) os << ' ';
            os << card_to_string(pos.trick[i]);
        }
        os << "  (marked * below)\n";
    }
    if (!fast) {
        os << "Tricks for " << SEAT_CHARS[sol.nil_seat] << "   " << sol.nil_tricks << " of "
           << pos.tricks_remaining() << '\n'
           << "Side tricks    " << side << '=' << sol.nil_side_tricks << "  " << other << '='
           << sol.opponent_tricks << '\n';
    }
    if (opts.nil_already_set) {
        os << "Nil            ALREADY SET (told, not computed)\n";
    } else {
        os << "Nil            "
           << (sol.nil_fails ? "FAILS  (can be forced to take a trick)"
                             : "MAKES  (cannot be forced to take a trick)")
           << '\n';
    }
    os << "Nodes          " << with_commas(sol.nodes) << '\n';
    if (fast) {
        os << "(fast mode answers the nil question alone; run without --mode fast for\n"
              " trick counts and a principal variation)";
        return os.str();
    }
    os << "Principal variation:\n"
       << format_pv(pos, sol) << '\n'
       << "Compact PV:\n"
       << "  " << format_pv_compact(sol);
    return os.str();
}

}  // namespace nil
