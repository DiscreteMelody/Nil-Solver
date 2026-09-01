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
    // Bit `s` set for each NIL BIDDER that has already taken a trick on the way
    // to this node.  Zero and untouched under a single nil.
    //
    // WHY THIS IS PATH STATE.  The single-nil objective is additive: every
    // completed trick contributes a fixed amount, so a subtree's value does not
    // depend on how it was reached.  "How many nils are set" is a step
    // function -- a bidder's FIRST trick costs the whole primary level and
    // every later one costs nothing -- so it does.  This is what carries that,
    // and it is why the transposition table is switched off for the shape: two
    // positions identical in cards but reached under different masks are not
    // the same position, and the key does not say so yet.  See ROADMAP item 61.
    unsigned char nils_broken;

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

// The winning-rank histogram, thread-local for the same reason the table is.
RankMaskStats& rank_mask_stats_storage() {
    static thread_local RankMaskStats stats;
    return stats;
}

NilSetStats& nil_set_stats_storage() {
    static thread_local NilSetStats stats;
    return stats;
}

OpposedStats& opposed_stats_storage() {
    static thread_local OpposedStats stats;
    return stats;
}

QuickTrickStats& quick_trick_stats_storage() {
    static thread_local QuickTrickStats stats;
    return stats;
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
    // A pair that both bid nil.  The primary level then counts BIDS DOWN rather
    // than weighting one seat's tricks, so `primary_weight` is charged once per
    // bidder on its first trick instead of on every trick it takes.
    bool multi_nil = false;
    // Bit `s` for each seat holding a bid.  Equal to (1 << nil_seat) unless
    // multi_nil.
    unsigned char nil_mask = 0;
    // One bid on EACH side, with the two partners leaning opposite ways.  The
    // value is then how well the side opposite `nil_seat` is doing: its outcome
    // rank, which the other side's rank mirrors exactly because the two sum to
    // a constant, plus its own trick count.
    bool opposing = false;
    // ITEM 78's PROBE, which rides on the opposing shape rather than replacing
    // it.  The value is the CONJUNCTION indicator -- 1 exactly when the far
    // side's bid is alive and the near side's is dead -- so the window is
    // [0, 1] and the search is the same AND-OR shape MODE_FAST is.
    //
    // WHY IT REUSES `nil_seat` FOR THE DEFENDER.  `maximizing` is
    // `(seat ^ ctx.nil_seat) & 1`, so pointing `nil_seat` at the DEFENDING
    // side's bidder makes the attacking side the maximiser with no new test on
    // the hot path -- the same trick the opposing shape already plays when it
    // writes its value from the far side.
    bool conjunction = false;
    bool settled_tricks = true;  // item 82's trick floor, where the rank is spent
    // That side's bidder, and the role of its partner, which is what decides
    // its ranking of the two middle outcomes.
    int far_nil_seat = 0;
    int far_partner_role = ROLE_OPPONENT;
    int primary_weight = 0;    // K*K, or 0 when the nil is already set
    int secondary_weight = -1; // -K the nil side wants tricks, +K it wants rid of them
    int tertiary_weight = 0;   // 1 when the cover partner's share is what counts
    bool collapse = true;      // one move per class of rank-equivalent cards
    bool last_trick = true;    // evaluate a forced final trick instead of searching it
    bool tt_boundaries_only = true;  // consult the table only at a trick boundary
    bool target_bounds = true;       // the arithmetic reach bound; MODE_FULL only
    bool later_tricks = true;        // tighten that bound by DDS s4; MODE_FULL only
    bool spade_matrix = false;       // ...for all four hands at once (item 44, opt-in)
    bool quick_tricks = true;        // ...plus DDS s3's can-cash floor (item 43)
    bool tt_narrow = true;           // spend a partial table match on the cutoff bound
    bool suit_mix = true;            // one card per suit at the head of the move list
    bool cover_duck_short = true;    // cover leads the cheapest duckable card, nil's shortest suit
    // True when no remaining trick can lower the value, i.e. every weight is
    // non-negative.  That makes what is already banked a lower bound on the
    // whole subtree, which is the one fact the "already past beta" cutoff in
    // search() rests on.  Always true in MODE_FAST, where the weights are
    // (1, 0, 0) and the value is a count.
    bool gains_nonnegative = false;
    // The same fact, but true only once every bid is already down.  With one
    // bid on each side the primary weight is positive and the QUANTITY it
    // multiplies -- outcome rank -- can still fall, which is what makes
    // `gains_nonnegative` false for that shape.  It cannot fall once no nil is
    // left to break, so the gain is non-negative from there on and the static
    // cutoff is sound again.  A fact about the POSITION rather than about the
    // weights, so it is tested per node.  Roadmap item 76.
    bool settled_gains = false;
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

    // WINNING-RANK BACKUP (roadmap item 31).  Off by default and inert when
    // off: the essential-set argument threaded through search() is null unless
    // this is set, and every merge is guarded on it.  See nil/ranks.hpp.
    bool track_ranks = false;
    RankMaskStats* rank_stats = nullptr;
    NilSetStats* nilset_stats = nullptr;  // roadmap item 32, measurement only
    OpposedStats* opposed_stats = nullptr;  // roadmap item 79, measurement only
    // Set once the value is known and the remaining work is recovering the
    // line, so item 79's sweep can keep those nodes out of the population.
    bool in_pv_walk = false;
    bool opposed_reach = true;  // item 79's bound, spent rather than counted
    // ITEM 79, PRECOMPUTED.  The rank term depends on the mask and on nothing
    // else, and there are four masks, so it is four pairs of numbers settled
    // once in configure() rather than a sixteen-way walk at every node.
    //
    // THE FIRST SPELLING COST 13.8% OF THROUGHPUT and gave back most of a 1.20x
    // node win.  It enumerated the reachable masks and popcounted each -- about
    // sixty-four iterations per node, to fire on a quarter of them.  That is
    // item 44's failure exactly, and unlike item 44 there was a cheaper
    // spelling sitting right there.
    //
    // Indexed by (near bid down) << 1 | (far bid down), already multiplied by
    // primary_weight, so a node adds only the trick span.
    int reach_lo[4] = {0, 0, 0, 0};
    int reach_hi[4] = {0, 0, 0, 0};
    // Which states can EVER fire, so the common case leaves after one test.
    // Measured on opposed13.txt: with both bids intact the reachable set spans
    // every rank and the bound answered 0 nodes of 68.5 million; with only the
    // near bid down it straddles the band and answered 0.41%.  Both are skipped
    // outright rather than computed and discarded.
    bool reach_useful[4] = {false, false, false, false};

    int reach_index(unsigned mask) const {
        return static_cast<int>((((mask >> nil_seat) & 1u) << 1) |
                                ((mask >> far_nil_seat) & 1u));
    }
    QuickTrickStats* quick_stats = nullptr;  // roadmap item 43, measurement only
};

// Roadmap item 32's population count.  Split out so the expression at the call
// site stays one line: the measurement must not reshape the branch it measures.
inline void count_nilset_outcome(NilSetStats& stats, const Hand hands[4], int nil_seat) {
    if (nil_must_take_a_trick(hands, nil_seat)) {
        ++stats.proof_fires;
    } else if (nil_forced_ceiling(hands, nil_seat)) {
        ++stats.ceiling_only;
    } else {
        ++stats.neither;
    }
}

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

// ---------------------------------------------------------------------------
// ROADMAP ITEM C5: THE COVER PARTNER ON LEAD, DUCKING THE NIL BIDDER SHORT.
// ---------------------------------------------------------------------------
// Tier three of the old four-tier lead rule, now its own item.
//
// Lead, into the nil bidder's SHORTEST suit, the cheapest card it can duck
// beneath.  Two things happen and both are wanted.  The nil bidder follows suit
// with its lowest card of that suit, which is under the card just led, so it
// cannot win the trick -- that much is a guarantee rather than a hope, since
// the card was chosen to sit above it.  And the suit gets one card shorter,
// which is the whole point: a suit the nil bidder is VOID in is a suit where
// every later lead hands it a free discard of its worst card elsewhere.  This
// rule is the one that manufactures the void that C3 then cashes into.
//
// Cheapest, not highest, for the reason 6c was rejected: spending the ace where
// the seven does the same job throws away a card that covers the nil bidder
// again later.
//
// The SHORTEST suit is the shortest NON-EMPTY one -- a void needs no shortening
// and is C3's case, not this one.  Ties go to the enumeration's own rotation
// order, which is a real choice and not an obvious one: the shortest suit is
// tied roughly 40% of the time.  Leaving ties with the incumbent keeps the rule
// as small a change as it can be.
CardId cover_partner_duck_short(const State& st, int nil_seat, Hand moves) {
    int shortest = -1, shortest_len = 99;
    for (int i = 1; i <= 4; ++i) {
        const int su = (3 + i) & 3;
        const Hand held = st.hands[nil_seat] & suit_mask(su);
        if (!held) continue;
        const int len = count_cards(held);
        if (len < shortest_len) {
            shortest_len = len;
            shortest = su;
        }
    }
    if (shortest < 0) return NO_CARD;  // the nil bidder holds nothing at all
    const Hand mine = moves & suit_mask(shortest);
    if (!mine) return NO_CARD;         // cannot lead that suit
    return cheapest_cover_above(mine, lowest_card(st.hands[nil_seat] & suit_mask(shortest)));
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

// TRACK is a template parameter rather than a flag, and that is a measurement
// rather than a preference.  Carried as a runtime pointer -- an extra argument
// on a hot recursive function, a null test at every return path and a
// zero-initialised Hand per move -- the backup cost 4-8% of wall time on three
// workloads WITH THE FEATURE OFF, which is a price the search gets nothing for.
// Two instantiations make the off path compile to exactly what it was.
template <bool TRACK>
int search_impl(Ctx& ctx, const State& st, CardId& best_move, int alpha, int beta,
                [[maybe_unused]] Hand* essential);
template <bool TRACK>
int value_after_impl(Ctx& ctx, const State& st, CardId card, int alpha, int beta,
                     State* next_out, [[maybe_unused]] Hand* essential);

// The dispatchers.  One branch, taken once per entry into the search rather
// than once per node: the recursion below calls its own instantiation directly.
int search(Ctx& ctx, const State& st, CardId& best_move, int alpha, int beta);
int value_after(Ctx& ctx, const State& st, CardId card, int alpha, int beta, State* next_out);

// ---------------------------------------------------------------------------
// WHAT THE STATIC RETURNS ACTUALLY READ
// ---------------------------------------------------------------------------
// Every path out of search() that does not recurse owes its caller an essential
// set: the cards whose ranks its answer depended on.  For the two proofs in
// bounds.hpp that is not "the whole position", and being exact about it matters
// -- a set that is too wide at a leaf is too wide at every ancestor.
//
// nil_cannot_be_forced reads, per non-spade suit the nil bidder holds, whether
// every card it holds is below every outstanding one.  Pin the OUTSTANDING
// cards of that suit and the condition survives: the nil bidder's own cards are
// then the only thing left to fill the unpinned slots below, and how many of
// them there are is the suit distribution, which statekey.hpp keeps exactly.
// Condition 1 (no spades) and the "nobody else holds it" branch of condition 2
// are distribution facts and need no rank pinned at all.
inline Hand ranks_read_by_safety_proof(const Hand hands[4], int nil_seat) {
    const Hand mine = hands[nil_seat];
    const Hand outstanding = relevant_cards(hands, NO_CARD) & ~mine;
    Hand read = 0;
    for (int suit = SUIT_HEARTS; suit <= SUIT_CLUBS; ++suit) {
        if (mine & suit_mask(suit)) read |= outstanding & suit_mask(suit);
    }
    return read;
}

// nil_must_take_a_trick walks the spades downwards and stops at the first of
// the nil bidder's cards that runs short of covers.  Nothing below that card is
// ever looked at, so nothing below it needs pinning.
inline Hand ranks_read_by_set_proof(const Hand hands[4], int nil_seat) {
    const Hand live = relevant_cards(hands, NO_CARD) & suit_mask(SUIT_SPADES);
    const Hand mine = hands[nil_seat] & suit_mask(SUIT_SPADES);
    const Hand theirs = live & ~mine;
    int held = 0;
    int above = 0;
    for (Hand bit = card_bit(make_card(SUIT_SPADES, 14)); bit; bit >>= 1) {
        if (mine & bit) {
            if (++held > above) return live & ~(bit - 1);  // this card and everything above
        } else if (theirs & bit) {
            ++above;
        }
    }
    return live;  // the proof did not fire; the caller does not use this
}

// Apply `card` at `st`.  Returns what the trick banked -- zero unless this card
// completed one -- and writes the child position to `next`.
//
// Split out of search()'s loop so that solve_moves() below runs the identical
// transition rather than a second copy of it.  Two copies of this arithmetic is
// The rank, for the side the value is written from -- the one opposite
// ctx.nil_seat.  A function of the mask alone.
// Item 78's probe as a function of the mask: 1 when the FAR side's bid is alive
// and the near side's is dead.
//
// THAT IS RANK 3 AND NOTHING ELSE, which is the whole reason this probe fits
// here.  `side_rank` gives 3 to exactly that outcome under BOTH partner leans --
// the leans only ever swap the two middle rungs -- so the indicator needs no
// lean and no role, only the two bidder seats.  `nil_oracle.py` proves the same
// identity from the other end: backward induction on the utility pair lands on
// rank 3 precisely when the far side can force it.
inline int conjunction_value(unsigned mask, const Ctx& ctx) {
    const bool far_alive = (mask & (1u << ctx.far_nil_seat)) == 0;
    const bool near_dead = (mask & (1u << ctx.nil_seat)) != 0;
    return (far_alive && near_dead) ? 1 : 0;
}

inline int far_side_rank(unsigned mask, const Ctx& ctx) {
    const bool far_survives = (mask & (1u << ctx.far_nil_seat)) == 0;
    const bool near_survives = (mask & (1u << ctx.nil_seat)) == 0;
    return side_rank(far_survives, near_survives, ctx.far_partner_role);
}


// ---- item 79: the reachable-rank bound, MEASURED but not spent -------------
//
// The rank is a step function of the broken-bid mask and a bid never un-breaks,
// so from mask `m` the reachable ranks are exactly those of the masks that
// contain it -- four of them, no cards read.  `score_trick` charges the rank as
// a DELTA against the mask the node arrived with, so a subtree's own value is
//
//     primary * (rank(final) - rank(m))  +  secondary * (far tricks from here)
//
// and both terms have a range computable from `m` and the tricks left.  That is
// the `target_bounds` this shape lost at patch 68 and has never had back.
//
// ONE REFINEMENT WORTH TAKING, and it is patch 66's `u >= d` edge again: a bid
// dies exactly when its own seat wins a trick, and no two bids die on the same
// trick, so at most `t` more bids can break with `t` tricks left.  It only bites
// in the last trick or two, which is also where the tree is widest.
void opposed_reach_bound(const Ctx& ctx, unsigned mask, int tricks_left, int& lo, int& hi) {
    const int idx = ctx.reach_index(mask);
    lo = ctx.reach_lo[idx] + (ctx.secondary_weight < 0 ? ctx.secondary_weight * tricks_left : 0);
    hi = ctx.reach_hi[idx] + (ctx.secondary_weight > 0 ? ctx.secondary_weight * tricks_left : 0);
}

// What a completed trick is worth, and what it does to the broken-nil mask.
//
// TWO OBJECTIVES, ONE FUNCTION.  Under a single nil the primary level weights
// the bidder's trick COUNT, so its weight is charged every time that seat wins.
// Under a pair that both bid it counts BIDS DOWN, so the weight is charged on a
// bidder's first trick and on none after it -- which is why the mask exists and
// why it has to travel with the state rather than being recomputed.
//
// The secondary level is the same in both: the bidding side's own tricks,
// wanted or shed according to the sign of the weight.  With two bidders every
// trick the side takes is taken BY a bidder, so the two levels pull against
// each other and the lexicographic order does real work -- having lost one bid,
// the pair funnels everything through the seat already broken.
inline int score_trick(const Ctx& ctx, const State& st, int winner, State& next) {
    next.nils_broken = st.nils_broken;
    int gained = 0;
    if (ctx.conjunction) {
        // A DELTA, exactly as the outcome rank is, and for the same reason: the
        // indicator is a step function of the mask, not something a trick earns
        // a share of.  Summed along a line the deltas telescope to
        // conj(final) - conj(nothing broken), and conj of an empty mask is zero,
        // so the accumulated value IS the answer rather than differing from it
        // by a constant.
        //
        // The delta is -1 as well as +1: the far bid dying after the near one
        // already has takes the indicator back down.  That is why
        // `gains_nonnegative` is false here and the static end-of-trick cutoff
        // stays off.
        const unsigned bit = 1u << winner;
        if ((ctx.nil_mask & bit) && !(st.nils_broken & bit)) {
            const unsigned after = st.nils_broken | bit;
            gained += conjunction_value(after, ctx) - conjunction_value(st.nils_broken, ctx);
            next.nils_broken = static_cast<unsigned char>(after);
        }
        return gained;  // no trick term: the probe is the boolean and nothing else
    }
    if (ctx.opposing) {
        // THE RANK IS A STEP FUNCTION OF THE MASK, so it cannot be weighted per
        // trick the way a single nil's count is.  It is charged as a DELTA
        // instead: when a bid dies the rank moves, and the move is worth
        // rank(after) - rank(before).  Summed along any line those deltas
        // telescope to rank(final) - rank(nothing broken), so the accumulated
        // value differs from the true rank by a constant -- which is invisible
        // to a search that only ever compares values within one position.
        const unsigned bit = 1u << winner;
        if ((ctx.nil_mask & bit) && !(st.nils_broken & bit)) {
            const unsigned after = st.nils_broken | bit;
            gained += ctx.primary_weight * (far_side_rank(after, ctx) -
                                            far_side_rank(st.nils_broken, ctx));
            next.nils_broken = static_cast<unsigned char>(after);
        }
        // The secondary is the FAR side's own tricks, so it is charged when the
        // far side wins.  Tricks are zero-sum between the two sides, so writing
        // it from one side is not a choice of favourite -- it is the same
        // objective either way, up to sign.
        if (((winner ^ ctx.nil_seat) & 1) != 0) gained += ctx.secondary_weight;
        return gained;
    }
    if (ctx.multi_nil) {
        const unsigned bit = 1u << winner;
        if ((ctx.nil_mask & bit) && !(st.nils_broken & bit)) {
            gained += ctx.primary_weight;
            next.nils_broken = static_cast<unsigned char>(st.nils_broken | bit);
        }
    } else if (winner == ctx.nil_seat) {
        gained += ctx.primary_weight + ctx.tertiary_weight;
    }
    if (((winner ^ ctx.nil_seat) & 1) == 0) gained += ctx.secondary_weight;
    return gained;
}


// how a move list would come to disagree with the search that produced it.
int advance(const Ctx& ctx, const State& st, CardId card, State& next) {
    const int seat = st.to_play();

    next = st;
    next.hands[seat] &= ~card_bit(card);
    next.broken = spades_broken_after(st.broken, card_suit(card));

    if (st.trick_len < 3) {
        next.trick[st.trick_len] = card;
        next.trick_len = st.trick_len + 1;
        return 0;
    }

    const CardId played[4] = {st.trick[0], st.trick[1], st.trick[2], card};
    const int winner = trick_winner(st.leader, played, 4);
    next.leader = winner;
    next.trick_len = 0;
    return score_trick(ctx, st, winner, next);
}

// The value of playing `card` at `st`, against the window `st` was given.
//
// Mid-trick `gained` is zero, so the shifted window below is the unshifted one
// and the two cases collapse into one line.  The early return stays guarded on
// the trick actually completing, because that is the condition its argument is
// written for and folding it in would rest on beta never reaching zero.
template <bool TRACK>
int value_after_impl(Ctx& ctx, const State& st, CardId card, int alpha, int beta, State* next_out,
                     [[maybe_unused]] Hand* essential) {
    State next;
    const int gained = advance(ctx, st, card, next);
    if (next_out) *next_out = next;

    // WON BY RANK (DDS 6.1).  A completed trick contributes its winner to the
    // essential set exactly when a second card of the WINNER'S suit was played
    // to it -- that is what makes the comparison a rank comparison.  A spade
    // ruffing alone wins whatever its rank; a lead nobody could follow wins
    // whatever its rank; both leave the essential set alone, and both are the
    // paper's own example.
    [[maybe_unused]] Hand trick_essential = 0;
    if constexpr (TRACK) {
      if (st.trick_len == 3) {
        const CardId played[4] = {st.trick[0], st.trick[1], st.trick[2], card};
        int win = 0;
        for (int i = 1; i < 4; ++i) {
            if (beats(played[i], played[win])) win = i;
        }
        const int win_suit = card_suit(played[win]);
        int followers = 0;
        for (int i = 0; i < 4; ++i) {
            if (card_suit(played[i]) == win_suit) ++followers;
        }
        if (followers >= 2) trick_essential = card_bit(played[win]);
      }
    }

    const bool gains_ok =
        ctx.gains_nonnegative ||
        (ctx.settled_gains && st.nils_broken == ctx.nil_mask);
    if (st.trick_len == 3 && gains_ok && gained >= beta) {
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
        //
        // The claim is "this trick alone has reached beta", and this trick is
        // the whole of what witnesses it.
        if constexpr (TRACK) *essential = trick_essential;
        return gained;
    }

    CardId ignored;
    // The child is asked about the value of the REST of the hand, so the window
    // it has to beat is this one less what the trick just banked.
    Hand child_essential = 0;
    const int value = gained + search_impl<TRACK>(ctx, next, ignored, alpha - gained,
                                                  beta - gained, &child_essential);
    if constexpr (TRACK) *essential = trick_essential | child_essential;
    return value;
}

template <bool TRACK>
int search_impl(Ctx& ctx, const State& st, CardId& best_move, int alpha, int beta,
                [[maybe_unused]] Hand* essential) {
    ++ctx.nodes;    best_move = NO_CARD;
    if constexpr (TRACK) *essential = 0;
    if (st.empty()) return 0;

    // Item 79's sweep.  Null unless --opposed-stats asked for it, so this is a
    // predictable branch and nothing else.  Counted BEFORE any of the bounds
    // below run, because the question is what the reachable-rank bound would
    // answer, not what is left after everything else has had a turn.
    if (ctx.opposed_stats) {
        OpposedStats& os = *ctx.opposed_stats;
        if (ctx.in_pv_walk) {
            ++os.pv_walk_nodes;
        } else {
        ++os.nodes;
        const unsigned near_bit = 1u << ctx.nil_seat;
        const unsigned far_bit = 1u << ctx.far_nil_seat;
        const bool near_down = (st.nils_broken & near_bit) != 0;
        const bool far_down = (st.nils_broken & far_bit) != 0;
        int lo = 0;
        int hi = 0;
        // The seat about to play has not played to this trick, so its card count is
        // the number of tricks still to come INCLUDING the current one -- correct
        // at any trick_len, where the leader's count is not.
        opposed_reach_bound(ctx, st.nils_broken, count_cards(st.hands[st.to_play()]), lo, hi);
        const bool answers = hi <= alpha || lo >= beta;
        if (near_down && far_down) {
            ++os.state_both_down;
            if (answers) ++os.would_answer_both_down;
        } else if (near_down) {
            ++os.state_near_down;
            if (answers) ++os.would_answer_near_down;
        } else if (far_down) {
            ++os.state_far_down;
            if (answers) ++os.would_answer_far_down;
        } else {
            ++os.state_intact;
            if (answers) ++os.would_answer_intact;
        }
        if (answers) ++os.would_answer;

        // Item 82.  Every bid down, at a trick boundary: the rank is settled and
        // the value is the far side's remaining tricks alone.  Measured for the
        // default tie-break only, where that term is positive; the mirrored one
        // is the same arithmetic with the signs turned over and is not worth a
        // second branch to count.
        if (near_down && far_down && st.trick_len == 0 && ctx.secondary_weight > 0) {
            ++os.settled_boundary;
            const int t = count_cards(st.hands[st.to_play()]);
            const int k = ctx.secondary_weight;
            // Failing low needs the far side held to `cap` tricks, which is a
            // claim about the NEAR side taking the rest.  Failing high needs the
            // far side to take `need_far`.
            const int cap = alpha >= 0 ? alpha / k : -1;
            const int need_far = (beta + k - 1) / k;
            int best = -1;
            if (cap >= 0 && cap < t) best = t - cap;
            if (need_far > 0 && need_far <= t && (best < 0 || need_far < best)) best = need_far;
            if (cap >= t || need_far <= 0) best = 0;   // already decided by arithmetic
            if (best < 0) {
                ++os.settled_hopeless;
            } else {
                ++os.settled_need[best > 6 ? 6 : best];
            }
            // Which SIDE has to prove what, and whether the spade run does it.
            // Failing low is a claim about the near side taking the rest;
            // failing high is a claim about the far side taking its own.
            if (best > 0) {
                const int need_near = (cap >= 0 && cap < t) ? t - cap : -1;
                const int need_of_far = (need_far > 0 && need_far <= t) ? need_far : -1;
                int run = 0;
                const int owner = top_spade_run(st.hands, run);
                if (owner < 0) {
                    ++os.settled_spade_wrong_side;
                } else {
                    const bool owner_is_near = ((owner ^ ctx.nil_seat) & 1) == 0;
                    const int want = owner_is_near ? need_near : need_of_far;
                    if (want < 0) {
                        ++os.settled_spade_wrong_side;
                    } else if (run >= want) {
                        ++os.settled_spade_proved;
                    } else {
                        ++os.settled_spade_short;
                    }
                }

                // The stronger proofs, asked of BOTH sides rather than only
                // whichever happens to hold the top spade.
                int forced[4] = {0, 0, 0, 0};
                forced_spade_tricks(st.hands, forced);
                const int near_seat_a = ctx.nil_seat;
                const int far_seat_a = ctx.far_nil_seat;
                const int floor_near =
                    forced[near_seat_a & 3] + forced[(near_seat_a + 2) & 3];
                const int floor_far = forced[far_seat_a & 3] + forced[(far_seat_a + 2) & 3];
                const bool by_forced =
                    (need_near >= 0 && floor_near >= need_near) ||
                    (need_of_far >= 0 && floor_far >= need_of_far);

                int cash_sum = 0;
                int cash_best = 0;
                cash_tricks(st.hands, st.leader, cash_sum, cash_best);
                const bool lead_is_near = ((st.leader ^ ctx.nil_seat) & 1) == 0;
                const int want_lead = lead_is_near ? need_near : need_of_far;
                const bool by_cash = want_lead >= 0 && cash_best >= want_lead;

                if (by_forced) ++os.settled_forced_proved;
                if (by_cash) ++os.settled_cash_proved;
                if (by_forced || by_cash) ++os.settled_either_proved;
            }
        }

        // Item 81.  Only at a trick boundary, which is where the proofs' own
        // preconditions hold, and only with exactly one bid still live.
        const bool one_down = near_down != far_down;
        if (one_down && st.trick_len == 0) {
            ++os.one_down_boundary;
            const int live = near_down ? ctx.far_nil_seat : ctx.nil_seat;
            const bool doomed = nil_must_take_a_trick(st.hands, live);
            const bool safe = !doomed &&
                              nil_cannot_be_forced(st.hands, live, st.leader == live);
            if (doomed) ++os.one_down_proof_doomed;
            if (safe) ++os.one_down_proof_safe;
            if (doomed || safe) {
                // The rank the proof pins the subtree to, and the bound that
                // follows: a single value where item 79 has to allow two.
                const unsigned final_mask =
                    doomed ? (st.nils_broken | (1u << live)) : st.nils_broken;
                const int step = ctx.primary_weight *
                                 (far_side_rank(final_mask, ctx) -
                                  far_side_rank(st.nils_broken, ctx));
                const int t = count_cards(st.hands[st.to_play()]);
                const int span = ctx.secondary_weight * t;
                const int plo = step + (span < 0 ? span : 0);
                const int phi = step + (span > 0 ? span : 0);
                if (answers) ++os.one_down_answered_now;
                if (phi <= alpha || plo >= beta) ++os.one_down_answered_pinned;
            }
        }
        }
    }

    // ---- item 79: answer the node from the mask, spending no cards ---------
    //
    // FAIL-SOFT AND EXACT ABOUT WHAT IT CLAIMS.  `hi` is an upper bound on this
    // subtree's value and `lo` a lower one, so returning `hi` at or below alpha
    // and `lo` at or above beta are both the ordinary fail-soft returns -- the
    // node is claiming which side of the window it falls on, which is all a
    // cutoff ever claims.
    //
    // NOTHING IS STORED.  Like the static bounds above, a node answered by a
    // popcount and two comparisons is cheaper to redo than to remember, and an
    // entry whose value came from the mask rather than the cards would be read
    // back by a search that arrived under a different mask.
    //
    // INERT ALONG THE PRINCIPAL VARIATION, and by arithmetic rather than by a
    // gate: the walk runs under the sentinels, where no finite range reaches
    // either end.  So the line the search chose is recovered by searching, not
    // by the bound that shortened the search.
    // ---- item 82: the rank is spent, so bound what is left ----------------
    //
    // With every bid down the rank cannot move again, so this subtree is worth
    // `secondary * (far tricks from here)` and nothing else -- an ordinary
    // double-dummy trick count, which is what DDS sections 3 and 4 bound and
    // what this shape has had switched off since patch 68.
    //
    // TWO FLOORS, BOTH SOUND, POINTING OPPOSITE WAYS.  A floor on the NEAR
    // side's tricks caps the far side's and so caps the value; a floor on the
    // FAR side's floors it.  `forced_spade_tricks` holds down every line and is
    // summed side-wide, which its own comment licenses.  `cash_tricks` is a
    // statement about one strategy and so speaks only for the side holding it --
    // enough in both directions here, because the far side maximises its own
    // tricks and the near side minimises them, so each one's cashing bounds the
    // value from its own end.
    //
    // GATED ON THE CLAIM BEING SMALL, and the gate is free: the size of the
    // claim needed falls out of the window and the tricks left with no proof
    // run at all.  71.88% of this region needs at most two tricks proven and
    // 0.47% needs four or more, so attempting a proof where it cannot succeed is
    // a cost with no matching yield -- which is exactly how item 79's first
    // spelling gave back a 1.20x node win, and item 44 before it.
    //
    // INERT ALONG THE PRINCIPAL VARIATION by arithmetic rather than by a gate:
    // that walk runs with WINDOW_MIN beneath it, so `cap` is -1 and `need_far`
    // exceeds the tricks left, and both claims come back unset.
    // `ctx.opposing` IS LOAD-BEARING, not decoration.  Without it the gate
    // `nils_broken == nil_mask` also opens on a SINGLE-nil deal the moment its
    // one bid breaks -- and there the value is not the far side's tricks, it is
    // `primary * nil_tricks + secondary * ...`, so the two floors below bound
    // the wrong quantity and the node returns a confident wrong number.  Caught
    // by the 560-position corpus: 80 failures and 278,059 nodes moved to
    // 270,562.  A shape test that reads as boilerplate is the one to check.
    if (ctx.settled_tricks && ctx.opposing && st.trick_len == 0 &&
        st.nils_broken == ctx.nil_mask && ctx.secondary_weight > 0 && !st.empty()) {
        const int t = count_cards(st.hands[st.to_play()]);
        const int k = ctx.secondary_weight;
        const int cap = alpha >= 0 ? alpha / k : -1;
        const int need_far_v = (beta + k - 1) / k;
        const int need_near = (cap >= 0 && cap < t) ? t - cap : -1;
        const int need_far = (need_far_v > 0 && need_far_v <= t) ? need_far_v : -1;
        const bool worth_asking = (need_near >= 1 && need_near <= 3) ||
                                  (need_far >= 1 && need_far <= 3);
        if (worth_asking) {
            int forced[4] = {0, 0, 0, 0};
            forced_spade_tricks(st.hands, forced);
            int floor_near = forced[ctx.nil_seat] + forced[(ctx.nil_seat + 2) & 3];
            int floor_far = forced[ctx.far_nil_seat] + forced[(ctx.far_nil_seat + 2) & 3];
            int cash_sum = 0;
            int cash_best = 0;
            cash_tricks(st.hands, st.leader, cash_sum, cash_best);
            if (((st.leader ^ ctx.nil_seat) & 1) == 0) {
                if (cash_best > floor_near) floor_near = cash_best;
            } else {
                if (cash_best > floor_far) floor_far = cash_best;
            }
            // Fail low: the near side takes at least `floor_near`, so the far
            // side takes at most the rest and the value cannot reach alpha.
            if (need_near >= 0 && floor_near >= need_near) return k * (t - floor_near);
            // Fail high: the far side takes at least `floor_far` outright.
            if (need_far >= 0 && floor_far >= need_far) return k * floor_far;
        }
    }

    if (ctx.opposed_reach && st.nils_broken != 0) {
        const int idx = ctx.reach_index(st.nils_broken);
        if (ctx.reach_useful[idx]) {
            const int t = count_cards(st.hands[st.to_play()]);
            const int span = ctx.secondary_weight * t;
            const int lo = ctx.reach_lo[idx] + (span < 0 ? span : 0);
            const int hi = ctx.reach_hi[idx] + (span > 0 ? span : 0);
            if (hi <= alpha) return hi;
            if (lo >= beta) return lo;
        }
    }

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
            // Nothing follows the last trick, so the updated mask is discarded.
            State ignored_next;
            const int gained = score_trick(ctx, st, winner, ignored_next);
            best_move = played[0];
            // The same by-rank test value_after() applies to every other trick.
            // `winner` is a seat, so the winning CARD is the one at that seat's
            // offset from the leader.
            if constexpr (TRACK) {
                const CardId won = played[(winner - st.leader) & 3];
                const int win_suit = card_suit(won);
                int followers = 0;
                for (int i = 0; i < 4; ++i) {
                    if (card_suit(played[i]) == win_suit) ++followers;
                }
                if (followers >= 2) *essential = card_bit(won);
            }
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
                if constexpr (TRACK)
                    *essential = ranks_read_by_safety_proof(st.hands, ctx.nil_seat);
                return 0;
            }
        } else if (ctx.nilset_stats
                       ? (++ctx.nilset_stats->boundaries,
                          count_nilset_outcome(*ctx.nilset_stats, st.hands, ctx.nil_seat),
                          beta <= 1 && nil_must_take_a_trick(st.hands, ctx.nil_seat))
                       : (beta <= 1 && nil_must_take_a_trick(st.hands, ctx.nil_seat))) {
            // A lower bound of one is only a legal fail-soft return when one is
            // at or above beta; below it the caller is entitled to an exact
            // value, and a nil bidder about to take three tricks would be
            // reported as taking one.  In MODE_FAST beta is 1 at every node --
            // see the cutoff further down -- so the guard never costs a cutoff.
            // It is here so that the correctness of this line is a property of
            // the code rather than of a fact about the window that some later
            // item could quietly change.
            best_move = first_legal_move(st);
            if constexpr (TRACK) *essential = ranks_read_by_set_proof(st.hands, ctx.nil_seat);
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
        int hi = 0;
        int lo = 0;
        if (ctx.multi_nil) {
            // THE SAME IDEA, A DIFFERENT REGION.  With a pair that both bid,
            // the subtree is worth
            //
            //     value = primary * d + secondary * u
            //
            // for d bids going down from here and u tricks to the pair from
            // here.  The single-nil derivation above does not port, because its
            // primary weights a trick COUNT and this one counts BIDS: d is a
            // step function of who wins what, not a linear function of it.
            //
            // What DOES port is the method.  The reachable region is still a
            // convex polygon and the function is still linear over it, so the
            // extremes are still at the vertices.  Only the polygon changes:
            //
            //     0 <= d <= D        D = min(live bids still standing, t)
            //     d <= u <= t        EACH BID THAT GOES DOWN COSTS THE PAIR A
            //                        TRICK, because a bid dies exactly when its
            //                        own seat wins one, and no two bids can die
            //                        on the same trick
            //
            // That is a trapezoid rather than a simplex -- the u >= d edge is
            // the whole difference -- with vertices (0,0), (0,t), (D,D) and
            // (D,t).  Four evaluations instead of three, still reading no cards.
            //
            // The u >= d edge is what makes this worth having rather than a
            // bounding box: without it the maximiser's bound would have to
            // allow d bids down AND the pair taking nothing for them, which no
            // line can do.
            //
            // Over-approximating the region stays sound in both directions --
            // hi only grows, lo only shrinks -- so the coupling being an
            // inequality rather than an equality costs tightness and never
            // correctness.
            int standing = 0;
            for (int seat = 0; seat < 4; ++seat) {
                const unsigned bit = 1u << seat;
                if ((ctx.nil_mask & bit) && !(st.nils_broken & bit)) ++standing;
            }
            const int d_max = standing < t ? standing : t;
            const int corners[4] = {
                0,                                                  // (0, 0)
                ctx.secondary_weight * t,                           // (0, t)
                (ctx.primary_weight + ctx.secondary_weight) * d_max,  // (D, D)
                ctx.primary_weight * d_max + ctx.secondary_weight * t,  // (D, t)
            };
            hi = corners[0];
            lo = corners[0];
            for (int i = 1; i < 4; ++i) {
                if (corners[i] > hi) hi = corners[i];
                if (corners[i] < lo) lo = corners[i];
            }
        } else {
            const int all_nil =
                (ctx.primary_weight + ctx.tertiary_weight + ctx.secondary_weight) * t;
            const int all_partner = ctx.secondary_weight * t;
            if (all_nil > hi) hi = all_nil;
            if (all_partner > hi) hi = all_partner;
            if (all_nil < lo) lo = all_nil;
            if (all_partner < lo) lo = all_partner;
        }
        if (hi <= alpha) {
            best_move = first_legal_move(st);
            return hi;
        }
        if (lo >= beta) {
            best_move = first_legal_move(st);
            return lo;
        }

        // LATER TRICKS (DDS section 4).  The bound above reads no cards, which
        // is its virtue and its ceiling: it has to allow every one of the t
        // remaining tricks to fall wherever the simplex permits, and on most
        // positions some of them provably cannot move at all.
        //
        // top_spade_run() names one hand and a number of tricks that hand wins
        // down EVERY line.  Which constraint that adds depends on whose hand it
        // is, and the three cases are three different simplices -- but each is
        // still a linear function over a triangle, so each is still three
        // vertex evaluations and a min and a max.  Nothing here searches.
        //
        //   opponent   opponents take k, so the nil side splits at most t - k:
        //              n + p <= t - k.  Vertices (0,0), (t-k,0), (0,t-k).
        //   nil bidder n >= k.  Vertices (k,0), (k,t-k), (t,0).
        //   partner    p >= k.  Vertices (0,k), (0,t), (t-k,k).
        //
        // The nil-bidder case overlaps `nil_must_take_a_trick` below, which
        // pins n >= 1; this pins n >= k, so it is strictly stronger at k >= 2
        // and the same claim at k = 1.  It sits here rather than there because
        // it comes free with a scan the other two cases are paying for anyway.
        //
        // AFTER the untightened test rather than before it, which is a cost
        // decision.  A node the cheap bound already answers wants nothing more
        // computed, and it answers 7-11% of boundaries at 12 and 13 cards.
        //
        // MODE_FAST is unreachable here for the same reason the bound above is
        // -- the gate is the mode.  This is a MODE_FULL item and the flag is
        // documented as one.
        if (ctx.later_tricks && !ctx.multi_nil) {
            const int per_nil =
                ctx.primary_weight + ctx.tertiary_weight + ctx.secondary_weight;
            const int per_partner = ctx.secondary_weight;
            const Hand sp = suit_mask(SUIT_SPADES);
            const int cover = ctx.nil_seat ^ 2;
            const int lho = (ctx.nil_seat + 1) & 3;
            const int rho = (ctx.nil_seat + 3) & 3;
            if (ctx.quick_stats) ++ctx.quick_stats->boundaries;

            // ---- item 44: the forced floor, for all four hands -------------
            //
            // top_spade_run() names one hand and one number, so it constrains
            // one side of the simplex and leaves the other three unremarked.
            // forced_spade_tricks() gives every hand a floor in one walk, and
            // the floors combine rather than compete:
            //
            //     n >= kn                the nil bidder's own
            //     p >= kp                the cover partner's
            //     n + p <= t - ko        both opponents', ADDED, because two
            //                            hands cannot win the same trick
            //
            // Never weaker than the incumbent: for the hand top_spade_run()
            // named, the general count reproduces its run and may exceed it.
            //
            // THE GATE IS WHAT MAKES IT AFFORDABLE, and it is the whole
            // difference between this and the version measured in patch 48. A
            // hand's floor cannot exceed the number of spades it holds, and
            // the triangle only shrinks as the floors grow -- so evaluating it
            // at the spade COUNTS gives the most tightening the walk could
            // ever produce. If that still does not reach the window, the walk
            // is certain to be wasted. Four masked popcounts instead of a walk
            // down every outstanding spade.
            //
            // An empty triangle at the counts means the counts are not
            // simultaneously achievable, so they say nothing and the gate
            // opens rather than closing -- conservative in the only direction
            // that matters.
            if (ctx.spade_matrix) {
                const int cn = count_cards(st.hands[ctx.nil_seat] & sp);
                const int cp = count_cards(st.hands[cover] & sp);
                const int co = count_cards((st.hands[lho] | st.hands[rho]) & sp);
                int ghi = 0, glo = 0;
                const bool bounded =
                    triangle_bounds(per_nil, per_partner, cn, cp, t - co, ghi, glo);
                if (!bounded || ghi <= alpha || glo >= beta) {
                    if (ctx.quick_stats) ++ctx.quick_stats->gate_forced;
                    int forced[4];
                    forced_spade_tricks(st.hands, forced);
                    const int ko = forced[lho] + forced[rho];
                    int hi2 = 0, lo2 = 0;
                    if (triangle_bounds(per_nil, per_partner, forced[ctx.nil_seat],
                                        forced[cover], t - ko, hi2, lo2)) {
                        if (hi2 <= alpha) {
                            if (ctx.quick_stats) ++ctx.quick_stats->fire_forced;
                            best_move = first_legal_move(st);
                            return hi2;
                        }
                        if (lo2 >= beta) {
                            if (ctx.quick_stats) ++ctx.quick_stats->fire_forced;
                            best_move = first_legal_move(st);
                            return lo2;
                        }
                    }
                }
            } else {
                // The incumbent single-hand form, kept reachable so that the
                // change above is a one-flag differential on one binary.
                int k = 0;
                const int owner = top_spade_run(st.hands, k);
                if (owner >= 0 && k > 0 && k <= t) {
                    int kn = 0, kp = 0, room = t;
                    if (owner == ctx.nil_seat) {
                        kn = k;
                    } else if (((owner ^ ctx.nil_seat) & 1) == 0) {
                        kp = k;
                    } else {
                        room = t - k;
                    }
                    int hi2 = 0, lo2 = 0;
                    if (triangle_bounds(per_nil, per_partner, kn, kp, room, hi2, lo2)) {
                        if (hi2 <= alpha) {
                            best_move = first_legal_move(st);
                            return hi2;
                        }
                        if (lo2 >= beta) {
                            best_move = first_legal_move(st);
                            return lo2;
                        }
                    }
                }
            }

            // ---- item 43: DDS section 3, the opponents' can-cash count -----
            //
            // Patch 48 measured this beside item 44 and found the two nearly
            // DISJOINT: 97-98% of the cuts here land at boundaries where the
            // forced floor does not cut. They are complementary rather than
            // competing, which is why they run in sequence -- exactly as the
            // paper runs its own cutoffs.
            //
            // WHY IT IS SPENT ONLY HERE. A can-cash count is a claim about one
            // STRATEGY, so it bounds a node only from the side that owns the
            // strategy. The opponents maximise, so theirs is a lower bound and
            // is spent against beta, and only at a node where they are on
            // lead. The mirror image -- the cover partner's count as an upper
            // bound -- is NOT taken, and bounds.hpp says why: that claim is
            // about a named hand rather than a side, and a nil bidder holding
            // nothing but spades is forced to ruff its own partner's winner.
            //
            // Its gate is one comparison in the common case. The bound is
            // weakest at c = 0 and strongest at the largest c the leader could
            // possibly have, which is bounded by its longest suit; if even
            // that does not reach beta, the per-suit walk is wasted.
            // The strongest this bound can EVER be is c = t, which leaves the
            // nil side nothing to split and floors the value at zero.  So
            // `beta > 0` refutes it outright, in one comparison, before a
            // single popcount is spent -- and that comparison is the whole
            // reason this arm is affordable where item 44's is not.
            if (ctx.quick_tricks && beta <= 0 && ((st.leader ^ ctx.nil_seat) & 1) != 0) {
                int longest = 0;
                for (int suit = 0; suit < 4; ++suit) {
                    const int len = count_cards(st.hands[st.leader] & suit_mask(suit));
                    if (len > longest) longest = len;
                }
                if (longest > 0 && split_floor(per_nil, per_partner, t - longest) >= beta) {
                    if (ctx.quick_stats) ++ctx.quick_stats->gate_cash;
                    const int c = side_cashable_tricks(st.hands, st.leader);
                    if (c > 0) {
                        const int lo3 = split_floor(per_nil, per_partner, t - c);
                        if (lo3 >= beta) {
                            if (ctx.quick_stats) ++ctx.quick_stats->fire_cash;
                            best_move = first_legal_move(st);
                            return lo3;
                        }
                    }
                }
            }
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
                    if constexpr (TRACK)
                        *essential = ranks_read_by_safety_proof(st.hands, ctx.nil_seat);
                    return hi;
                }
                if (lo >= beta) {
                    best_move = first_legal_move(st);
                    if constexpr (TRACK)
                        *essential = ranks_read_by_safety_proof(st.hands, ctx.nil_seat);
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
                    if constexpr (TRACK) *essential = ranks_read_by_set_proof(st.hands, ctx.nil_seat);
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
    // The ranks a partial match pinned, when one supplied the cutoff bound.
    // They are part of why this node's answer is what it is, exactly as a
    // searched move's are, so they are unioned into the essential set at the end
    // -- see the merge below.
    [[maybe_unused]] Hand narrow_essential = 0;
    // The bound this node's fail-soft cutoff test reads, when a partial table
    // match has supplied a tighter one than the window carries.  It is NOT the
    // window: children are searched under `alpha` and `beta` unchanged.
    int cut_bound = 0;
    bool have_cut_bound = false;
    if (ctx.tt && (st.trick_len == 0 || !ctx.tt_boundaries_only)) {
        keyed = encode_state_key(st.hands, st.leader, st.broken, st.trick, st.trick_len, key,
                                 profile, st.nils_broken,
                                     ctx.multi_nil || ctx.opposing);
        if (keyed) {
            hash = mix_key(key);
            // A bound recorded under a wider window is still a fact about the
            // position; what changes with pruning is that not every stored fact
            // is enough to END the node.  The table therefore hands back what it
            // has and says separately whether it settles this window.
            bool answers = false;
            if (const TTEntry* hit = ctx.tt->probe(key, hash, ctx.tt_tag, alpha, beta, answers)) {
                // The essential set of whatever the entry contributes -- the
                // whole answer when it settles the node, the narrowing fact when
                // it does not.  It rides in the bound byte as ONE number applied
                // to all four suits rather than four -- see tt.hpp for why there
                // is no room for the vector, and why one number is the safe way
                // to be short of room.
                [[maybe_unused]] Hand entry_essential = 0;
                if constexpr (TRACK) {
                    KeepVector keep = 0;
                    const int need = bound_need(hit->bound);
                    for (int su = 0; su < 4; ++su) keep = keep_set(keep, su, need);
                    entry_essential = keep_to_essential(keep, profile);
                }
                if (answers) {
                    best_move = from_relative(hit->move, profile);
                    // The answer is the stored subtree's, so the essential set
                    // is its too.
                    if constexpr (TRACK) *essential = entry_essential;
                    return hit->value;
                }
                // PARTIAL (roadmap item 41).  The entry bounds the value on one
                // side without settling the window.  Spend it on the CUTOFF
                // BOUND -- the one this node's own fail-soft test reads -- and
                // nowhere else.  The window the children are searched under is
                // left exactly as the caller gave it.
                //
                // That restriction is the whole item, and it was measured
                // rather than reasoned: narrowing alpha and beta themselves, so
                // that the tightening propagates down the subtree, costs 3.6% of
                // the tree at 13 cards and 5.5% at 11.  See ROADMAP.md item 41
                // for the sweep.  A tighter window makes descendants store
                // one-sided bounds where they would have stored BOUND_EXACT, and
                // an exact entry answers EVERY window where a bound answers
                // almost none; on a table running an 80% hit rate at five probes
                // per store, that trade is heavily negative.
                //
                // Only one of the two bounds can produce a cutoff here -- beta
                // at a maximiser, alpha at a minimiser -- so only the entry kind
                // matching this node's own direction is of any use, and the
                // other is exactly the one whose only effect would have been to
                // propagate.  Hence one test rather than two.
                if (ctx.tt_narrow) {
                    const int value = hit->value;
                    const std::uint8_t kind = bound_kind(hit->bound);
                    const bool max_here = ((st.to_play() ^ ctx.nil_seat) & 1) != 0;
                    if (max_here ? (kind == BOUND_UPPER && value < beta)
                                 : (kind == BOUND_LOWER && value > alpha)) {
                        cut_bound = value;
                        have_cut_bound = true;
                        if constexpr (TRACK) narrow_essential = entry_essential;
                    }
                }
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

    // The threshold the fail-soft cutoff below tests against.  Computed once,
    // because it does not move: loop narrowing raises a maximiser's alpha and
    // lowers a minimiser's beta, and each leaves alone the OTHER bound, which is
    // the one tested here.  A partial table match (item 41) overrides it when it
    // offers a tighter one.
    const int cut_at = have_cut_bound ? cut_bound : (maximizing ? beta : alpha);

    int best = 0;
    bool have_best = false;

    // MERGING (DDS 6.2).  Two rules, and the difference between them is the
    // difference between a value and a bound.  A node that looks at every move
    // is claiming a value and every move it looked at is part of why --
    // MergeAllMovesData unions the lot.  A node that cuts off is claiming only
    // which side of the window the value falls on, and ONE move witnesses that;
    // MergeCutoffMovesData keeps that move's set and discards the siblings.
    [[maybe_unused]] Hand all_moves_essential = 0;
    [[maybe_unused]] Hand cut_move_essential = 0;
    bool cut_off = false;

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
            // 6b.  `maximizing` is the test for an opponent; the covering
            // partner is the branch below.
            promoted = opponent_attack_lead(st, ctx.nil_seat, moves);
        } else if (!maximizing && ctx.cover_duck_short && st.trick_len == 0) {
            // C5.  C1, C2 and C3 were the other seats and cases; all rejected.
            promoted = cover_partner_duck_short(st, ctx.nil_seat, moves);
        }
        if (promoted != NO_CARD) moves &= ~card_bit(promoted);
    }

    // SUIT MIXING (item 35).  DDS section 5 puts the best card of EACH present
    // suit at the head of the list, not one card overall, and says why in as
    // many words: to have a good mixture of moves rather than all cards from
    // one suit first, "in case the heuristic is not good for a particular
    // set-up".  What this loop did instead was promote one card and then
    // enumerate suit-major -- every spade, then every heart -- which is the
    // shape the paper warns against.
    //
    // So: one card from each present suit first, taken in rotation from the
    // promoted card's suit, and then the canonical tail exactly as before.  It
    // is a hedge rather than a bet, which is what separates it from 6c: that
    // one was rejected for spending the cover card unconditionally, and this
    // spends nothing -- the same moves are searched, in a different order.
    //
    // Only where the seat has a free choice of suit, which is the paper's scope
    // too (trick leader, or void in the suit led).  The gate is the move set
    // spanning more than one suit, which a seat following suit never does, and
    // it costs two bit scans on the nodes where ordering runs at all -- BOTH
    // modes, since order_moves is not the fast-mode-only switch the seat rules
    // above make it look like.
    //
    // THE TAIL IS DELIBERATELY NOT MIXED, and that is a measurement rather than
    // a reading of the paper.  Rotating the whole tail saves the same nodes --
    // 77.4M against this version's 77.9M over three 13-card seeds -- and gives
    // them back on throughput, because it charges a four-way suit scan on every
    // move instead of on the first four.  Interleaved wall time came out
    // between 0.96x and 1.03x for the rotated tail against a flat 1.03x for
    // this one; a version that is slower than the incumbent on one seed is not
    // worth 0.7% of nodes.
    // THE INITIAL CURSOR IS LOAD-BEARING, AND IT IS AN ACCIDENT.  Starting at 3
    // means the rotation below tries suit 0 first, and suit 0 is SPADES, so at
    // every node where a seat is void in the led suit and holds a trump the
    // first move searched is a RUFF.  Nothing decided that; it falls out of the
    // bit layout in cards.hpp meeting the initialiser on the next line.
    //
    // It is worth keeping anyway.  Item C2 promoted a discard ahead of that
    // ruff at the cover partner's void nodes -- 4-5% of all nodes at 13 cards --
    // and cost 12.01% of nodes across the three 13-card seeds for it.  Anyone
    // renumbering the suits or changing where this cursor starts should expect
    // to pay that, and tests/test_nil_solver.cpp pins the mechanism so the loss
    // shows up as a failure rather than as a slow benchmark.
    int suit_cursor = 3;
    int mixed = 0;  // moves still to take in rotation
    if (ctx.order_moves && ctx.suit_mix && moves &&
        card_suit(lowest_card(moves)) != card_suit(highest_card(moves))) {
        for (int su = 0; su < 4; ++su)
            if (moves & suit_mask(su)) ++mixed;
        if (promoted != NO_CARD) suit_cursor = card_suit(promoted);
    }

    while (promoted != NO_CARD || moves) {
        CardId card;
        if (promoted != NO_CARD) {
            card = promoted;
            promoted = NO_CARD;
        } else if (mixed > 0) {
            --mixed;
            card = take_next_suit(moves, suit_cursor);
        } else {
            card = take_lowest(moves);
        }

        Hand move_essential = 0;
        const int value =
            value_after_impl<TRACK>(ctx, st, card, alpha, beta, nullptr, &move_essential);
        if constexpr (TRACK) {
            all_moves_essential |= move_essential;
            cut_move_essential = move_essential;
        }

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
        if (maximizing ? best >= cut_at : best <= cut_at) {
            cut_off = true;
            break;
        }
    }
    // The move that reached `cut_at` is the move that just ran: the test runs
    // after every move, so an earlier one that had already reached it would have
    // broken then.  `best` therefore came from this move, and this move's set is
    // the whole witness the bound needs.
    // ...and the ranks behind the cutoff bound, in either case: a cutoff taken
    // against a table entry's bound rests on that entry just as much as on the
    // move that reached it, so dropping its ranks here would leave the witness
    // smaller than the claim it witnesses.
    if constexpr (TRACK)
        *essential = (cut_off ? cut_move_essential : all_moves_essential) | narrow_essential;

    if (keyed) {
        const RelMove rel = best_move == NO_CARD ? REL_NO_MOVE : to_relative(best_move, profile);
        // Which of these three the node earned follows from where `best` landed
        // relative to the window it was GIVEN -- alpha_asked and beta_asked,
        // not the live pair, which narrowing may have moved underneath it.
        // Breaking out of the loop above implies best >= cut_at at a maximiser
        // (narrowing never moves beta there) and best <= cut_at at a minimiser,
        // so a cut node can never be recorded as the bound belonging to the
        // other side: a partial match only ever offers a `cut_at` STRICTLY
        // inside the asked window -- that is what failing to answer means -- so
        // a maximiser's cut lands above alpha_asked and a minimiser's below
        // beta_asked either way.
        //
        // Which leaves the case item 41 introduced: a cut at `best` between
        // alpha_asked and beta_asked, recorded BOUND_EXACT although the node
        // stopped early.  That is the truth rather than an over-claim, and the
        // entry is why.  Take a maximiser whose entry pins V <= y and which
        // stopped at the first best >= y.  The move that produced `best` was
        // searched under the untouched window and came back above alpha, so it
        // came back EXACT and V >= best; with V <= y <= best that forces
        // V = best.  The value is squeezed exact by the same fact that
        // shortened the search.  Symmetrically at a minimiser.
        const std::uint8_t kind = best <= alpha_asked  ? BOUND_UPPER
                                  : best >= beta_asked ? BOUND_LOWER
                                                       : BOUND_EXACT;

        // `need` is the largest number of top slots the backed-up winning ranks
        // require pinned in any one suit -- the truncation level a masked table
        // would have had to give this entry.  It rides in the bound byte's spare
        // bits so a node ANSWERED by this entry can tell its own parent which of
        // its cards were essential; without it a table hit would have to report
        // the whole position, and two boundary probes in three are hits.
        //
        // Boundaries only, because the essential set is a set of cards LIVE at
        // this node, and mid-trick it also carries the winner of the trick in
        // progress, which is in nobody's hand.  A mid-trick entry keeps the
        // "everything pinned" default, which is the safe direction.
        int need = BOUND_NEED_MAX;
        if constexpr (TRACK) {
            if (st.trick_len == 0) {
                const KeepVector keep = essential_to_keep(*essential, profile);
                need = keep_need(keep);
                if (ctx.rank_stats) ctx.rank_stats->record(keep, profile.total);
            }
        }
        ctx.tt->store(key, hash, best, rel, profile.total, pack_bound(kind, need), ctx.tt_tag);
    }
    return best;
}

int search(Ctx& ctx, const State& st, CardId& best_move, int alpha, int beta) {
    if (ctx.track_ranks) {
        Hand essential = 0;
        return search_impl<true>(ctx, st, best_move, alpha, beta, &essential);
    }
    return search_impl<false>(ctx, st, best_move, alpha, beta, nullptr);
}

int value_after(Ctx& ctx, const State& st, CardId card, int alpha, int beta, State* next_out) {
    if (ctx.track_ranks) {
        Hand essential = 0;
        return value_after_impl<true>(ctx, st, card, alpha, beta, next_out, &essential);
    }
    return value_after_impl<false>(ctx, st, card, alpha, beta, next_out, nullptr);
}

// Both modes set up identically: weights into the context, then the shared
// table attached unless the caller turned it off.
//
// The value an entry holds is relative to the weights that produced it, and the
// weights differ between the modes -- a fast-mode 1 and a full-mode 1 are not
// the same number.  new_search() on every solve is what keeps one mode's
// entries out of the other's search, and it is not optional.
// EVERYTHING THAT ASSUMES ONE NIL, SWITCHED OFF FOR A PAIR THAT BOTH BID.
//
// Not caution for its own sake.  Each of these is sound because of a specific
// property of the single-nil objective that the two-nil one does not have:
//
//   static_bounds     bounds.hpp proves things about "the nil bidder" and "the
//                     cover partner" as two hands with opposite jobs.  There is
//                     no cover here, and the side's own tricks are the thing it
//                     is trying not to take.  nil_cannot_be_forced is the sharp
//                     case: the PREDICATE stays true -- that seat really cannot
//                     win another trick -- but its consumer concludes the
//                     primary term vanishes, and the partner's bid can still go
//                     down.  A sound proof wired to a stale consumer returns a
//                     confidently wrong value with nothing to catch it.
//   later_tricks      its three cases are stated as constraints on "the nil
//                     bidder" and "the cover partner", and there is no cover
//                     here.  quick_tricks and spade_matrix ride on it.
//
// target_bounds is NOT in this list any more: patch 66 re-derived it over the
// region the two-nil objective actually reaches, which is a trapezoid rather
// than a simplex.  See the derivation at its call site.
// The transposition table is NOT in this list any more: since patch 59 the key
// carries the broken-nil mask and the shape has its own value tag, so entries
// from the two objectives cannot be confused for each other.  `tt_narrow` still
// is, because a partial match spends its bound through the same triangle
// arithmetic that target_bounds does.
//
// Everything here is a correctness gate, not a tuning choice, and it is also
// the all-off baseline every re-derivation gets A/B'd against.
void disable_single_nil_machinery(Ctx& ctx) {
    ctx.static_bounds = false;
    ctx.full_static_bounds = false;
    ctx.later_tricks = false;
    ctx.quick_tricks = false;
    ctx.spade_matrix = false;
    ctx.tt_narrow = false;
}

void configure(Ctx& ctx, const SeatRoles& roles, const SearchOptions& opts,
               const ObjectiveWeights& weights) {
    // The search still reasons about ONE nil seat and takes the coalitions from
    // its parity, exactly as it did when the caller passed that seat directly.
    // The roles array is the caller's description; this is where phase two will
    // stop collapsing it to a scalar.
    ctx.nil_seat = roles.nil_seat();
    {
        std::string ignored;
        SeatRoles copy = roles;
        ctx.opposing = seat_shape(copy, ignored) == SHAPE_OPPOSING_NILS;
    }
    ctx.multi_nil = !ctx.opposing && nil_count(roles) > 1;
    ctx.settled_tricks = opts.settled_tricks;
    ctx.conjunction = opts.conjunction_seat >= 0;
    if (ctx.conjunction) {
        // THE DEFENDER GOES IN `nil_seat`.  `maximizing` reads that seat's
        // parity, so pointing it at the defending bidder makes the ATTACKING
        // side the maximiser without a second test in search().  It also puts
        // the attacker in `far_nil_seat`, which is what `conjunction_value`
        // reads, so the indicator comes out the right way round for free.
        ctx.far_nil_seat = opts.conjunction_seat & 3;
        for (int seat = 0; seat < 4; ++seat) {
            if (roles.is_nil(seat) && ((seat ^ ctx.far_nil_seat) & 1) != 0) {
                ctx.nil_seat = seat;
            }
        }
        ctx.far_partner_role = roles.role[(ctx.far_nil_seat + 2) & 3];
    }
    if (ctx.opposing) {
        // The value is written from the side that does NOT hold ctx.nil_seat, so
        // that seat's parity keeps driving the existing maximising test: the
        // near side minimises, exactly as the nil side always has.
        for (int seat = 0; seat < 4; ++seat) {
            if (roles.is_nil(seat) && ((seat ^ ctx.nil_seat) & 1) != 0) {
                ctx.far_nil_seat = seat;
                ctx.far_partner_role = roles.role[(seat + 2) & 3];
            }
        }
    }
    // LIVE bids only.  An already-broken one carries no primary weight, and
    // leaving its bit in would charge the pair a second time for a bid it has
    // already lost.
    ctx.nil_mask = static_cast<unsigned char>(live_nil_mask(roles));
    if (ctx.opposing) disable_single_nil_machinery(ctx);
    ctx.primary_weight = weights.primary;
    ctx.secondary_weight = weights.secondary;
    ctx.tertiary_weight = weights.tertiary;
    ctx.collapse = opts.collapse_equivalents;
    // Read off the weights rather than off the mode, because it is a fact about
    // the weights: with any of them negative a later trick could pull the value
    // back down, and what is banked so far would bound nothing.
    // NOT A FACT ABOUT THE WEIGHTS ANY MORE, once a bid sits on each side.
    // Everywhere else the primary weight multiplies a COUNT that only goes up,
    // so a non-negative weight means a non-negative gain.  With one bid per side
    // it multiplies a change in outcome RANK, and that falls when the side the
    // value is written from loses its own bid.  A negative gain from a
    // non-negative weight breaks the static cutoff this flag guards, which is
    // exactly the kind of assumption that survives a rename and dies on a new
    // objective.
    // ...AND NOT UNDER THE CONJUNCTION EITHER, for the same reason spelled a
    // different way: its delta is -1 when the far bid dies after the near one
    // already has, so what is banked bounds nothing.
    ctx.gains_nonnegative = !ctx.opposing && !ctx.conjunction && weights.primary >= 0 &&
                            weights.secondary >= 0 && weights.tertiary >= 0;
    // ...and the per-node form, for the shape the line above refuses outright.
    // The refusal is about the outcome RANK falling, which needs a live bid to
    // fall; with every bid already down the remaining gain is the trick term
    // alone, and that only goes up when its weight does not go down.
    ctx.settled_gains = opts.settled_gains && ctx.opposing && weights.primary >= 0 &&
                        weights.secondary >= 0 && weights.tertiary >= 0;
    // Likewise a fact about the weights.  In MODE_FULL the primary is K*K and
    // the secondary is +/-K, so this is false for every K -- including K = 1,
    // where the primary is 1 but the secondary is not zero.  MODE_FULL
    // therefore never takes a static cutoff, and its node counts stay the fixed
    // point they have been since patch 8.
    // The conjunction's weights are (1, 0, 0) too, and they mean something
    // else: the value is an INDICATOR, not `ctx.nil_seat`'s trick count.  The
    // proofs in bounds.hpp are statements about that count, so reading the
    // weights alone would wire two sound proofs to a value they say nothing
    // about -- the exact failure mode `disable_single_nil_machinery` exists to
    // prevent.  Excluded by name.
    ctx.value_is_nil_tricks =
        !ctx.conjunction && weights.primary == 1 && weights.secondary == 0 &&
        weights.tertiary == 0;
    ctx.static_bounds = opts.use_static_bounds;
    ctx.full_static_bounds = opts.full_static_bounds;
    // Both halves matter.  MODE_FULL never reorders whatever the caller asked
    // for, because it cannot gain from it and would lose the oracle check by
    // doing it; and within MODE_FAST the caller can still switch ordering off
    // to get the control arm.
    // Ordering is answer-neutral in both modes.  What it costs in MODE_FULL is the
    // canonical tie-break on the principal variation, so it runs unless a caller
    // has asked for that line specifically.  See SearchOptions::canonical_pv.
    // The third condition is not about speed.  With ROLE_NIL_SET the primary
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
    ctx.later_tricks = opts.later_tricks;
    ctx.spade_matrix = opts.spade_matrix;
    ctx.quick_tricks = opts.quick_tricks;
    ctx.tt_narrow = opts.tt_narrow_window;
    ctx.suit_mix = opts.suit_mixed_order;
    ctx.cover_duck_short = opts.cover_duck_short;
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
    // Which QUESTION this solve's values answer.  The key says which position an
    // entry is about; without this a two-nil value would be readable by a
    // one-nil search at the same cards, and the two are on different scales.
    ctx.tt_tag = ctx.conjunction
                     ? TAG_CONJUNCTION
                 : ctx.opposing
                     ? TAG_OPPOSING_NILS
                     : (ctx.multi_nil ? TAG_MULTI_NIL
                                      : (opts.mode == MODE_FAST ? TAG_FAST : TAG_FULL));

    // The measurement arm.  Nothing below reads the histogram when it is off,
    // and the `essential` pointer threaded through search() is null in that
    // case, so every merge in the tree is a branch that is never taken.
    ctx.track_ranks = opts.track_rank_masks;
    if (ctx.track_ranks) ctx.rank_stats = &rank_mask_stats_storage();
    if (opts.track_nilset) ctx.nilset_stats = &nil_set_stats_storage();
    if (opts.track_opposed && ctx.opposing) ctx.opposed_stats = &opposed_stats_storage();
    ctx.opposed_reach = opts.opposed_reach && ctx.opposing;
    if (ctx.opposing) {
        // Four states, and for each the range of ranks still reachable from it.
        // A bid never un-breaks, so reachability is containment on the mask.
        const int here[4] = {
            side_rank(true, true, ctx.far_partner_role),    // nothing broken
            side_rank(false, true, ctx.far_partner_role),   // far bid down
            side_rank(true, false, ctx.far_partner_role),   // near bid down
            side_rank(false, false, ctx.far_partner_role),  // both down
        };
        // Which states each can still reach, as indices into `here`.
        static const int reachable[4][4] = {
            {0, 1, 2, 3},  // nothing broken: anything
            {1, 3, -1, -1},  // far down: itself, or both
            {2, 3, -1, -1},  // near down: itself, or both
            {3, -1, -1, -1},  // both down: itself
        };
        for (int i = 0; i < 4; ++i) {
            int lo_rank = here[i];
            int hi_rank = here[i];
            for (int j = 0; j < 4 && reachable[i][j] >= 0; ++j) {
                const int r = here[reachable[i][j]];
                if (r < lo_rank) lo_rank = r;
                if (r > hi_rank) hi_rank = r;
            }
            ctx.reach_lo[i] = weights.primary * (lo_rank - here[i]);
            ctx.reach_hi[i] = weights.primary * (hi_rank - here[i]);
            // A state whose rank cannot move contributes nothing beyond the
            // trick span, and one that spans the whole ladder contributes a
            // range no window excludes.  Neither is worth a per-node test.
            ctx.reach_useful[i] = ctx.reach_lo[i] != 0 || ctx.reach_hi[i] != 0 || i == 3;
        }
        // `here` is indexed 0=none 1=far 2=near 3=both, which is exactly
        // reach_index's (near << 1 | far) once the two middle entries are
        // swapped -- they are written above in reach_index's order already.
    }
    if (opts.track_quick_tricks) ctx.quick_stats = &quick_trick_stats_storage();

    const std::size_t table_mb =
        opts.tt_megabytes == TT_AUTO ? TT_DEFAULT_MEGABYTES : opts.tt_megabytes;
    if (opts.use_memo && table_mb > 0) {
        TranspositionTable& table = shared_table();
        table.resize(table_mb);  // a no-op at the size it already is
        table.new_search();               // this solve may not see the last one's values
        ctx.tt = &table;
    }

    // Last, so that nothing set above can turn a gate back on.  The opposing
    // shape loses target_bounds too: the trapezoid re-derived in patch 66 is a
    // claim about ONE side's bids and its own trick total, and here the value
    // mixes an outcome rank with the far side's tricks.  Correct first.
    if (ctx.multi_nil) disable_single_nil_machinery(ctx);
    if (ctx.opposing) {
        disable_single_nil_machinery(ctx);
        ctx.target_bounds = false;
    }
    if (ctx.conjunction) {
        // Everything the opposing shape gives up, and for the same reasons: the
        // proofs are about one bidder's trick count and the reach bounds are
        // about an outcome rank plus a trick total, and this value is neither.
        // Item 79's mask bound is off too -- the indicator's reachable set is
        // not the rank's, and re-deriving it is a separate item with its own
        // measurement, not a line squeezed into this one.
        disable_single_nil_machinery(ctx);
        ctx.target_bounds = false;
        ctx.opposed_reach = false;
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

// THE WINDOW TRAVELS WITH THE LINE.  Roadmap item 80.
//
// Every step of the walk re-searches a position that is one or more tricks
// DEEPER than the root, and a deeper position's value is measured from itself:
// it does not include what the line has already banked.  So the question the
// root was asked is not the question a step should be asked, and the difference
// is exactly the running gain -- which is what `search_impl` has always handed
// its own children as `alpha - gained, beta - gained` and what this walk, which
// re-enters at a position rather than descending through `value_after`, never
// did.
//
// It did not matter while the windows here were loose.  Patch 77 made one that
// is not: a rank band is `k*k` wide in the primary and a sub-position past a
// broken bid does not carry the `k*k` the root paid for it, so every step past
// the break fell outside an unshifted band, came back a bound rather than a
// value, and `canonical_move_for` matched the bound.  Patch 77 answered that by
// walking under the sentinels, which is correct and blunt: the search ran under
// a band, so nothing the walk probes settles the wider window and it re-searches
// most of what it visits -- 82,348,545 nodes on `opposed13.txt`, 22.6% of the
// search, and invisible because `search_nodes` is snapshotted before the walk.
//
// Shifting is the fix the blunt one stood in for.  Each step is asked precisely
// the question the search answered for it, so the table settles it and the walk
// is lookups again.
//
// WHY A FINITE ALPHA IS SAFE HERE, which is what patch 77 was worried about.
// `canonical_move_for` compares each candidate's value against `v`, the node's
// own value, and its comment says the window has WINDOW_MIN beneath it so no
// child can fail low.  That condition is SUFFICIENT and not necessary. With the
// shifted band, `v` is exact and therefore strictly inside the window; a child
// that fails low returns at or below alpha and a child that fails high returns
// at or above beta, so neither can equal `v`, and the child whose true value IS
// `v` lies strictly inside and comes back exact. No false match is available in
// either direction, so the canonically lowest matching move is still the one
// found.
bool walk_pv(Ctx& ctx, State st, CardId first, std::vector<Play>& pv_out, std::string& err,
             int pv_alpha = WINDOW_MIN, int pv_beta = WINDOW_MAX) {
    CardId move = first;
    // What the line has banked so far.  Bounded by a few hundred against a
    // sentinel of 2^29, so shifting the window by it cannot overflow -- the same
    // headroom argument WINDOW_MIN was halved for.
    int gained = 0;
    while (!st.empty()) {
        if (move == NO_CARD) {
            err = "internal error: no move available at a non-terminal position";
            return false;
        }
        pv_out.push_back(Play{st.to_play(), move});
        State next;
        gained += advance(ctx, st, move, next);
        st = next;
        if (st.empty()) break;
        const int step_alpha = pv_alpha - gained;
        const int step_beta = pv_beta - gained;
        const int v = search(ctx, st, move, step_alpha, step_beta);
        if (ctx.canonicalise) {
            const CardId c = canonical_move_for(ctx, st, v, step_alpha, step_beta);
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
    // Nothing has been won yet from here; a bid the caller declared already
    // broken is handled by its weight, not by this mask.
    st.nils_broken = 0;
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

ObjectiveWeights objective_weights(int tricks_remaining, const SeatRoles& roles,
                                   const SearchOptions& opts) {
    // TWO BIDS ON ONE SIDE.  The primary level counts BIDS DOWN, 0..2, rather
    // than weighting one seat's trick count 0..t.  K*K still separates the
    // levels: the secondary can reach K*t in absolute value, and K*t < K*K, so
    // no run of tricks outweighs one more bid going down.  There is no tertiary
    // -- a pair that both bid has no cover partner whose share could break a
    // tie -- and no MODE_FAST fork, because solve() refuses that combination
    // before this is reached.
    // One bid on each side.  The primary is one step of outcome RANK, which
    // ranges 0..3, so K*K separates it from the secondary exactly as it does for
    // a pair that both bid: the trick term reaches K*tricks_remaining at most,
    // and that is under K*K.  No tertiary -- each side's own tricks are one
    // number, and there is no cover partner whose share could break a tie.
    if (opts.conjunction_seat >= 0) {
        // Item 78's probe.  Nothing is packed above or below the indicator, so
        // the value IS the indicator and the window worth searching is [0, 1] --
        // the same arithmetic that makes MODE_FAST an AND-OR search.
        ObjectiveWeights w;
        w.primary = 1;
        w.secondary = 0;
        w.tertiary = 0;
        return w;
    }
    {
        std::string ignored;
        SeatRoles copy = roles;
        if (seat_shape(copy, ignored) == SHAPE_OPPOSING_NILS) {
            const int k = tricks_remaining + 1;
            ObjectiveWeights w;
            w.primary = k * k;
            w.secondary = opts.minimise_own_tricks ? -k : k;
            w.tertiary = 0;
            return w;
        }
    }

    if (nil_count(roles) > 1) {
        const int k = tricks_remaining + 1;
        ObjectiveWeights w;
        // Zero when every bid is already down, exactly as a single already-set
        // nil zeroes it: there is no primary level left, and what remains is the
        // secondary alone -- each pair taking or shedding as many tricks as it
        // can. Inert either way, since nothing is charged against an empty live
        // mask, but saying it in the weights keeps the objective honest for
        // anything that reads them.
        w.primary = live_nil_mask(roles) ? k * k : 0;
        w.secondary = opts.minimise_own_tricks ? k : -k;
        w.tertiary = 0;
        return w;
    }
    if (opts.mode == MODE_FAST) {
        // Nothing packed above or below the nil bidder's trick count, so the
        // value IS that count and the window is [0, 1].  minimise_own_tricks is
        // inert here by construction -- there is no secondary level for it to
        // point at -- and ROLE_NIL_SET never reaches this function, because
        // solve() answers that combination without searching.
        ObjectiveWeights w;
        w.primary = 1;
        w.secondary = 0;
        w.tertiary = 0;
        return w;
    }

    const int k = tricks_remaining + 1;
    ObjectiveWeights w;
    w.primary = roles.nil_already_set() ? 0 : k * k;
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
// Both gaps are 2k wide and neither depends on the deal.  Undefined under
// ROLE_NIL_SET, where primary is zero and the two halves collapse into each
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

// ---- item 77: the same presolve, for one bid on EACH side -------------------
//
// The shape alone, for the callers that have already validated the roles and do
// not want the message that comes with them.
SeatShape seat_shape_of(const SeatRoles& roles) {
    std::string ignored;
    return seat_shape(roles, ignored);
}

// The rank the opposed objective is written in, without a Ctx to read it off.
// configure() has not run when the presolve does, so this takes the far
// partner's role directly; it is the same call score_trick() charges its
// primary weight against.
int opposed_rank(bool near_survives, bool far_survives, int far_partner_role) {
    return side_rank(far_survives, near_survives, far_partner_role);
}

// The value band a KNOWN outcome rank confines the opposed objective to.
//
// A line's value is primary * (rank(final) - rank(nothing broken)) plus the far
// side's own tricks at `secondary` apiece, and that trick term spans at most
// k*t against a primary of k*k -- so each of the four ranks owns a band of the
// value line and the bands do not touch.  Pinning the rank therefore pins the
// value to within one band, and a window on that band refutes every line
// heading for a different one.
void opposed_rank_band(int rank, int rank0, int tricks_remaining, const ObjectiveWeights& w,
                       int& lo, int& hi) {
    const int span = w.secondary * tricks_remaining;
    const int base = w.primary * (rank - rank0);
    lo = base + (span < 0 ? span : 0);
    hi = base + (span > 0 ? span : 0);
}

bool solve(const Position& pos, const SeatRoles& roles, const SearchOptions& opts,
           Solution& out, std::string& err) {
    if (!validate_seat_roles(roles, err)) return false;
    if (!validate(pos, err)) return false;

    const ObjectiveWeights weights = objective_weights(pos.tricks_remaining(), roles, opts);

    // ---- item 78: the conjunction probe -----------------------------------
    //
    // A THIRD QUESTION ABOUT THE OPPOSED SHAPE, not a third mode.  Item 77's two
    // probes settle two of the four (bid safe / bid breakable) combinations and
    // leave the other two with a one-sided bound; this is what closes them.
    //
    // The value is the indicator and the window is [0, 1], so this is the same
    // AND-OR search MODE_FAST is: the attacking side needs ONE line where its
    // bid survives and the other dies, the defending side needs EVERY such line
    // to fail.  What is new is the defending side's goal, and it is the hazard
    // this item is really about: it wins by EITHER keeping its own bid alive OR
    // breaking the attacker's, a DISJUNCTION, so it may deliberately dump a
    // trick on the attacker's bidder and abandon its own bid to do it.  Nothing
    // here constrains it to protect -- it minimises the indicator, which lets
    // both routes through.  A search that let it only protect would report the
    // attacker succeeding on lines it cannot win, and no corpus would catch it.
    if (opts.conjunction_seat >= 0) {
        std::string shape_err;
        SeatRoles copy = roles;
        if (seat_shape(copy, shape_err) != SHAPE_OPPOSING_NILS) {
            err = "the conjunction probe asks about a deal with a bid on EACH side; " +
                  describe_seat_roles(roles) + " is not one";
            return false;
        }
        const int attacker = opts.conjunction_seat & 3;
        if (!roles.is_nil(attacker)) {
            err = std::string("the conjunction probe names the ATTACKING side's bidder, and ") +
                  SEAT_CHARS[attacker] + " did not bid (" + describe_seat_roles(roles) + ")";
            return false;
        }
        if (opts.mode != MODE_FAST) {
            err = "the conjunction probe answers one boolean and has no trick counts or "
                  "principal variation to report; ask it in fast mode";
            return false;
        }

        out = Solution();
        out.roles = roles;
        out.mode = MODE_FAST;
        out.nil_tricks = TRICKS_NOT_COMPUTED;
        out.nil_side_tricks = TRICKS_NOT_COMPUTED;
        out.opponent_tricks = TRICKS_NOT_COMPUTED;

        Ctx cctx;
        configure(cctx, roles, opts, weights);
        State root = state_of(pos);
        CardId root_move = NO_CARD;
        const int value = search(cctx, root, root_move, 0, 1);
        if (value < 0 || value > 1) {
            std::ostringstream os;
            os << "internal inconsistency: the conjunction probe returned " << value
               << ", which is not an indicator";
            err = os.str();
            return false;
        }
        const TTStats cstats = cctx.tt ? cctx.tt->stats() : TTStats();
        out.conjunction = value > 0;
        out.value = value;
        out.nodes = cctx.nodes;
        out.tt_probes = cstats.probes;
        out.tt_hits = cstats.hits;
        out.tt_partial = cstats.partial;
        out.tt_stores = cstats.stores;
        out.tt_evictions = cstats.evictions;
        return true;
    }

    // FAST MODE IS A SINGLE-NIL QUESTION, and refuses to be asked another.
    //
    // It answers "with perfect play, can the specified player make nil?" over a
    // [0, 1] window.  With two bidders there is no specified player, and asking
    // it of one of them is not merely ambiguous: whether one bid survives is not
    // defined on its own, because it depends on how the pair trades the two off
    // against each other, which is the whole objective.  Refusing here also
    // keeps the [0, 1] window -- and every node count banked on it -- out of
    // reach of the new shape.
    if (opts.mode == MODE_FAST && nil_count(roles) > 1) {
        err = "fast mode answers whether ONE named seat can make nil, and " +
              describe_seat_roles(roles) +
              " has two bidders; ask in full mode, which reports how many are down";
        return false;
    }

    // ---- fast mode: the nil question, and nothing else ---------------------
    if (opts.mode == MODE_FAST) {
        out = Solution();
        out.roles = roles;
        out.mode = MODE_FAST;
        out.nil_tricks = TRICKS_NOT_COMPUTED;
        out.nil_side_tricks = TRICKS_NOT_COMPUTED;
        out.opponent_tricks = TRICKS_NOT_COMPUTED;

        // The caller has asserted the very thing this mode computes.  There is
        // nothing to search: the secondary objective that ROLE_NIL_SET exists
        // to expose has no output to land in here, because fast mode reports no
        // trick counts.  Ask in full mode if you want those numbers.
        if (roles.nil_already_set()) {
            out.nils_set = 1;
            return true;
        }

        Ctx fast_ctx;
        configure(fast_ctx, roles, opts, weights);
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

        out.nils_set = fast_value > 0 ? 1 : 0;
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
    // Skipped under ROLE_NIL_SET, where the primary weight is zero and the two
    // halves of the value range are not separated at all.
    int root_alpha = WINDOW_MIN;
    int root_beta = WINDOW_MAX;
    std::uint64_t presolve_nodes = 0;
    TTStats presolve_stats;
    const bool presolve_eligible = opts.presolve_window && !roles.nil_already_set() &&
                                   pos.tricks_remaining() >= PRESOLVE_MIN_TRICKS;
    if (presolve_eligible && nil_count(roles) == 1) {
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
        if (solve(pos, roles, probe_opts, probe, probe_err) && probe.nils_set == 0) {
            root_beta = max_value_if_nil_safe(pos.tricks_remaining(), weights) + 1;
        }
        presolve_nodes = probe.nodes;
        presolve_stats.probes = probe.tt_probes;
        presolve_stats.hits = probe.tt_hits;
        presolve_stats.partial = probe.tt_partial;
        presolve_stats.stores = probe.tt_stores;
        presolve_stats.evictions = probe.tt_evictions;
    } else if (presolve_eligible && seat_shape_of(roles) == SHAPE_OPPOSING_NILS) {
        // ---- item 77: the presolve, for one bid on EACH side ---------------
        //
        // TWO PROBES, EACH A SINGLE-NIL QUESTION THIS SOLVER ALREADY ANSWERS.
        // Ask of each bidder in turn: can that bid be broken, with BOTH
        // opponents unconstrained and free to throw their own bid away doing
        // it?  That is exactly SHAPE_SINGLE_NIL, so `seat_roles_from_nil` builds
        // the roles and MODE_FAST answers over its [0, 1] window.  Nothing new
        // is searched; what is new is what the pair of answers means.
        //
        // WHY THE ANSWERS COMPOSE.  Each probe is a GUARANTEE -- it holds
        // against every strategy the other side has, including ones that
        // sacrifice the other bid -- so it survives being asked inside a deal
        // where the attacker has a bid of its own to protect.  A guarantee
        // about one bid confines the outcome to two of the four, and two of
        // four is a bound on the rank:
        //
        //   near bid SAFE      the near side (which minimises) can hold the
        //                      outcome to one where the near bid lives, so the
        //                      rank is AT MOST the higher of those two
        //   near bid BREAKABLE the far side (which maximises) can hold it to
        //                      one where the near bid dies, so the rank is AT
        //                      LEAST the lower of those two
        //
        // and the mirror for the far bid, where a safe answer is the MAXIMISER's
        // guarantee and so floors the rank instead of capping it.  Each probe
        // therefore contributes exactly one bound, to whichever end belongs to
        // the side that owns the guarantee.
        //
        // WHEN THE TWO PROBES AGREE the bounds land on opposite ends and close
        // to a single rank: both bids safe means both bids live, both bids
        // breakable means both bids die, and neither needs anything further.
        // WHEN THEY DISAGREE both bounds land on the same end and the band stays
        // open at the other, which is item 23's one-sided shape.  Closing that
        // case needs a probe this mode cannot express -- "can one side break the
        // other's bid WHILE KEEPING ITS OWN" -- and that is a separate item.
        const int near_seat = roles.nil_seat();
        int far_seat = near_seat;
        int far_partner_role = ROLE_OPPONENT;
        for (int seat = 0; seat < 4; ++seat) {
            if (roles.is_nil(seat) && ((seat ^ near_seat) & 1) != 0) {
                far_seat = seat;
                far_partner_role = roles.role[(seat + 2) & 3];
            }
        }

        SearchOptions probe_opts = opts;
        probe_opts.mode = MODE_FAST;

        int rank_lo = 0;
        int rank_hi = 3;
        bool have_lo = false;
        bool have_hi = false;
        // Which probe came back safe, kept because the THIRD probe below is
        // asked by the side whose bid is the one that cannot be broken.
        bool near_safe = false;
        bool far_safe = false;
        bool near_answered = false;
        bool far_answered = false;

        for (int which = 0; which < 2; ++which) {
            const bool probing_near = which == 0;
            Solution probe;
            std::string probe_err;
            // A failed probe is not a failed solve, exactly as above: the
            // search below is complete without either bound.
            const bool ok = solve(pos, seat_roles_from_nil(probing_near ? near_seat : far_seat,
                                                           false),
                                  probe_opts, probe, probe_err);
            presolve_nodes += probe.nodes;
            presolve_stats.probes += probe.tt_probes;
            presolve_stats.hits += probe.tt_hits;
            presolve_stats.partial += probe.tt_partial;
            presolve_stats.stores += probe.tt_stores;
            presolve_stats.evictions += probe.tt_evictions;
            if (!ok) continue;

            const bool safe = probe.nils_set == 0;
            if (probing_near) {
                near_safe = safe;
                near_answered = true;
            } else {
                far_safe = safe;
                far_answered = true;
            }
            // The two outcomes the guarantee leaves standing.  Whichever bid the
            // probe was about is pinned; the other is still free.
            const bool near_lives = probing_near ? safe : true;
            const bool far_lives = probing_near ? true : safe;
            const int a = opposed_rank(probing_near ? near_lives : true,
                                       probing_near ? true : far_lives, far_partner_role);
            const int b = opposed_rank(probing_near ? near_lives : false,
                                       probing_near ? false : far_lives, far_partner_role);
            // Who owns the guarantee decides which end it bounds.  The far side
            // maximises the rank, so its guarantees floor it; the near side
            // minimises, so its guarantees cap it.
            const bool owned_by_far = probing_near ? !safe : safe;
            if (owned_by_far) {
                const int floor_rank = a < b ? a : b;
                if (!have_lo || floor_rank > rank_lo) rank_lo = floor_rank;
                have_lo = true;
            } else {
                const int ceil_rank = a > b ? a : b;
                if (!have_hi || ceil_rank < rank_hi) rank_hi = ceil_rank;
                have_hi = true;
            }
        }

        // ---- item 78c: the third probe, when the first two disagree --------
        //
        // WHEN THEY AGREE THERE IS NOTHING LEFT TO ASK.  Both bids safe pins the
        // rank at "both live"; both breakable pins it at the outcome each side
        // can force on the other.  It is only when exactly ONE bid is safe that
        // the two bounds land on the same end and the band stays open, and that
        // is where deals 3 and 5 of `opposed13.txt` sit -- deal 5 being 72% of
        // what is left of the opposed tree.
        //
        // THE SIDE THAT ASKS IS THE ONE WHOSE BID IS SAFE.  Its bid cannot be
        // taken away, so the only question left is whether it can ALSO take the
        // other's, and that is exactly the conjunction: *can this side force the
        // other's bid down while keeping its own?*  Item 78b's probe, answering
        // one boolean over a [0, 1] window.
        //
        // TWO CANDIDATES REMAIN AND THE PROBE NAMES ONE.  A one-sided bound
        // always leaves exactly two of the four outcome ranks standing, and the
        // conjunction is the extreme one of the pair -- the asking side's best
        // and the other's worst.  So a true answer pins the rank there and a
        // false answer pins it on the other candidate.  Rather than case out the
        // partner leans, the candidates are enumerated and the refuted one is
        // struck off; if that ever leaves other than one standing the band is
        // left as it was, which costs the tightening and nothing else.
        if (opts.conjunction_presolve && near_answered && far_answered &&
            near_safe != far_safe) {
            SearchOptions conj_opts = opts;
            conj_opts.mode = MODE_FAST;
            conj_opts.conjunction_seat = near_safe ? near_seat : far_seat;
            Solution conj;
            std::string conj_err;
            if (solve(pos, roles, conj_opts, conj, conj_err)) {
                presolve_nodes += conj.nodes;
                presolve_stats.probes += conj.tt_probes;
                presolve_stats.hits += conj.tt_hits;
                presolve_stats.partial += conj.tt_partial;
                presolve_stats.stores += conj.tt_stores;
                presolve_stats.evictions += conj.tt_evictions;

                // The outcome the conjunction asserts: the asking side's bid
                // alive and the other's dead.
                const int conj_rank = near_safe
                                          ? opposed_rank(true, false, far_partner_role)
                                          : opposed_rank(false, true, far_partner_role);
                int survivors = 0;
                int only = 0;
                for (int n = 0; n < 2; ++n) {
                    for (int f = 0; f < 2; ++f) {
                        const int r = opposed_rank(n != 0, f != 0, far_partner_role);
                        if (have_lo && r < rank_lo) continue;
                        if (have_hi && r > rank_hi) continue;
                        if (conj.conjunction ? r != conj_rank : r == conj_rank) continue;
                        if (survivors == 0 || r != only) {
                            ++survivors;
                            only = r;
                        }
                    }
                }
                if (survivors == 1) {
                    rank_lo = only;
                    rank_hi = only;
                    have_lo = true;
                    have_hi = true;
                }
            }
        }

        const int rank0 = opposed_rank(true, true, far_partner_role);
        if (have_hi) {
            int lo = 0;
            int hi = 0;
            opposed_rank_band(rank_hi, rank0, pos.tricks_remaining(), weights, lo, hi);
            root_beta = hi + 1;  // strictly inside, so the root still comes back exact
        }
        if (have_lo) {
            int lo = 0;
            int hi = 0;
            opposed_rank_band(rank_lo, rank0, pos.tricks_remaining(), weights, lo, hi);
            root_alpha = lo - 1;
        }
        // THE PV WALK ONCE COULD NOT REUSE THIS WINDOW, and the story is worth
        // keeping because the first fix was the wrong shape.
        //
        // `walk_pv` re-searched each step of the line under the window the ROOT
        // was asked about, UNSHIFTED by what the line had banked.  Sound against
        // a loose window; not against a band.  The root's value carries
        // `primary * (rank - rank0)` -- a whole k*k -- and a sub-position
        // reached AFTER that bid has broken does not, so it sits far above a
        // band centred on a rank the root paid for.  Every such step failed
        // high, `canonical_move_for` compared against a bound rather than a
        // value, and the walk left the line the search chose.  Caught by the
        // replay check on `opposed13.txt` deal 5: search -70, replay -322, and
        // it HID ON 7 OF 8 DEALS because a line where both bids live never moves
        // the rank and so contains every step by accident.
        //
        // Patch 77 answered it by walking under the sentinels.  Correct, and it
        // cost 82,348,545 nodes -- 22.6% of the opposed search, invisible
        // because `search_nodes` is snapshotted before the walk.  Item 80
        // replaced it with the shift the walk never had, which asks each step
        // the question the search answered for it.  Nothing is special-cased
        // here any more.
    }

    Ctx ctx;
    configure(ctx, roles, opts, weights);

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
        ctx.in_pv_walk = true;
        const CardId c = canonical_move_for(ctx, st, value, root_alpha, root_beta);
        ctx.in_pv_walk = false;
        if (c != NO_CARD) move = c;
    }
    const std::uint64_t search_nodes = ctx.nodes + presolve_nodes;
    out.presolve_nodes = presolve_nodes;
    // Snapshot before the PV walk below, which probes the table again and would
    // otherwise inflate the hit count with lookups that did no search work.
    const TTStats table_stats = ctx.tt ? ctx.tt->stats() : TTStats();

    // Walk the chosen moves to recover the principal variation.  With the memo
    // on this is a handful of lookups; with it off each step re-searches a
    // strictly smaller subtree.  Either way the moves are the same ones the
    // search picked, so the PV matches the oracle's play for play.
    out.pv.clear();
    ctx.in_pv_walk = true;
    // THE CONTROL ARM IS PATCH 77'S WALK, NOT AN UNSHIFTED ONE, and the
    // difference is not pedantry.  The first spelling of `--no-pv-shift` left
    // the band in place and stopped shifting it, which is precisely the bug
    // patch 77 found: the replay check fires on `opposed13.txt` deal 5 and the
    // solve fails.  A flag whose OFF position is unsound measures nothing and
    // hands a caller a broken solver, so OFF is what patch 77 actually shipped
    // -- the sentinels, where the walk is correct and slow. Both arms answer;
    // one of them re-searches.
    const int pv_alpha = opts.pv_shift_window ? root_alpha : WINDOW_MIN;
    const int pv_beta = opts.pv_shift_window ? root_beta : WINDOW_MAX;
    if (!walk_pv(ctx, st, move, out.pv, err, pv_alpha, pv_beta)) return false;

    // A solver that lies is worse than no solver.  Replaying recovers the trick
    // counts independently; re-packing them must land back on the search value.
    Tally tally;
    if (!replay_pv(pos, out.pv, roles, tally, err)) {
        err = "internal inconsistency: " + err;
        return false;
    }
    // Re-packed the way the objective in force packs it.  Under two bids the
    // primary weight is charged once per bidder that went down, not once per
    // trick a bidder took -- so this is a different sum, not a special case of
    // the same one, and getting it wrong here would silently stop the check
    // from checking anything.
    const int replayed = ctx.opposing
                             ? weights.primary * (far_side_rank(tally.broken_mask, ctx) -
                                                  far_side_rank(0, ctx)) +
                                   weights.secondary * tally.opponent_tricks
                         : ctx.multi_nil
                             ? weights.primary * tally.live_nils_broken +
                                   weights.secondary * tally.nil_side_tricks
                             : (weights.primary + weights.tertiary) * tally.nil_tricks +
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
    // The replay counts distinct bidders broken, which is the primary level
    // itself under two bids and agrees with (nil_tricks > 0) under one.
    // The replay already folds in bids the caller declared down, so this is
    // one expression for both shapes rather than a special case for each.
    out.nils_set = tally.nils_set;
    out.value = value;
    out.roles = roles;
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

const RankMaskStats& rank_mask_stats() { return rank_mask_stats_storage(); }

void reset_rank_mask_stats() { rank_mask_stats_storage() = RankMaskStats(); }

const NilSetStats& nil_set_stats() { return nil_set_stats_storage(); }

const OpposedStats& opposed_stats() { return opposed_stats_storage(); }
void reset_opposed_stats() { opposed_stats_storage() = OpposedStats(); }

void reset_nil_set_stats() { nil_set_stats_storage() = NilSetStats(); }

const QuickTrickStats& quick_trick_stats() { return quick_trick_stats_storage(); }

void reset_quick_trick_stats() { quick_trick_stats_storage() = QuickTrickStats(); }

bool solve_moves(const Position& pos, const SeatRoles& roles, const SearchOptions& opts,
                 Solution& out, std::vector<MoveScore>& moves_out, std::string& err) {
    moves_out.clear();
    if (!validate_seat_roles(roles, err)) return false;
    const int nil_seat = roles.nil_seat();
    if (opts.mode == MODE_FAST && nil_count(roles) > 1) {
        err = "fast mode answers whether ONE named seat can make nil, and " +
              describe_seat_roles(roles) +
              " has two bidders; ask in full mode, which reports how many are down";
        return false;
    }
    if (!validate(pos, err)) return false;

    out = Solution();
    out.roles = roles;
    out.mode = opts.mode;

    const int tricks_remaining = pos.tricks_remaining();
    const ObjectiveWeights weights = objective_weights(tricks_remaining, roles, opts);
    const bool fast = opts.mode == MODE_FAST;

    State root = state_of(pos);
    if (root.empty()) {
        // Nothing to play, so there is no move list to give and no search to
        // run.  Not an error: an exhausted position is a legal thing to ask
        // about, and the empty list is the honest answer.
        out.nil_tricks = fast ? TRICKS_NOT_COMPUTED : 0;
        out.nil_side_tricks = fast ? TRICKS_NOT_COMPUTED : 0;
        out.opponent_tricks = fast ? TRICKS_NOT_COMPUTED : 0;
        out.nils_set = roles.nil_already_set() ? 1 : 0;
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
    if (fast && roles.nil_already_set()) {
        out.nils_set = 1;
        out.nil_tricks = TRICKS_NOT_COMPUTED;
        out.nil_side_tricks = TRICKS_NOT_COMPUTED;
        out.opponent_tricks = TRICKS_NOT_COMPUTED;
        for (Hand h = reps; h;) {
            const CardId card = take_lowest(h);
            MoveScore ms;
            ms.card = card;
            ms.equals = opts.collapse_equivalents ? equivalent_moves(card, legal, relevant)
                                                  : card_bit(card);
            ms.nils_set = 1;
            ms.is_best = true;
            moves_out.push_back(ms);
        }
        return true;
    }

    Ctx ctx;
    configure(ctx, roles, opts, weights);

    std::uint64_t presolve_nodes = 0;
    const int alpha = fast ? 0 : WINDOW_MIN;
    int beta = fast ? 1 : WINDOW_MAX;

    // Item 23, per-card.  The same presolve bound the single-answer path takes,
    // with one extra obligation: a row owes its own exact value, and a card
    // that loses the nil scores on the far side of the threshold, so the tight
    // window would hand back a bound for exactly the rows a caller most wants
    // a number on.  Those rows -- and only those -- are re-searched wide below.
    //
    // ITEM 77 IS DELIBERATELY NOT WIRED IN HERE.  The opposed shape's band is
    // two-sided, so the re-search below would have to trigger on rows that fall
    // off EITHER end rather than just the low one, and a row whose rank differs
    // from the root's falls off by a whole k*k -- which is most of the
    // interesting rows.  That is a second correctness argument on a path with a
    // different obligation, so it is a separate item rather than a second half
    // of this one.  `--moves` on an opposed deal keeps exactly the behaviour it
    // has today: correct, and without the new window.
    int tight_beta = beta;
    if (!fast && opts.presolve_window && !roles.nil_already_set() && nil_count(roles) == 1 &&
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
        if (solve(pos, roles, probe_opts, probe, probe_err) && probe.nils_set == 0) {
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
            os << "internal inconsistency: the position says nils_set=" << (root_value > 0 ? 1 : 0)
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
            ms.nils_set = ms.value > 0 ? 1 : 0;
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
        if (!replay_pv(pos, line, roles, tally, err)) {
            err = "internal inconsistency replaying " + card_to_string(ms.card) + ": " + err;
            return false;
        }
        // Packed the way the objective in force packs it -- the same fork
        // solve() makes.  Patch 58 forked that one and missed this one, and the
        // check went on reporting a number nothing computed until a test for
        // the shape went looking.  A verifier not forked alongside the thing it
        // verifies does not fail loudly; it stops verifying.
        const int replayed = ctx.opposing
                                 ? weights.primary * (far_side_rank(tally.broken_mask, ctx) -
                                                      far_side_rank(0, ctx)) +
                                       weights.secondary * tally.opponent_tricks
                             : ctx.multi_nil
                                 ? weights.primary * tally.live_nils_broken +
                                       weights.secondary * tally.nil_side_tricks
                                 : (weights.primary + weights.tertiary) * tally.nil_tricks +
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
        ms.nils_set = tally.nils_set;

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
    out.nils_set = fast ? (root_value > 0 ? 1 : 0) : best->nils_set;
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
bool replay_pv(const Position& pos, const std::vector<Play>& pv, const SeatRoles& roles,
               Tally& tally_out, std::string& err) {
    unsigned broken_seats = 0;
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
        broken = spades_broken_after(broken, card_suit(card));
        trick[trick_len++] = card;
        if (trick_len == 4) {
            const int winner = trick_winner(leader, trick, 4);
            if (roles.is_nil(winner)) {
                ++tally_out.nil_tricks;
                // The COUNT of bids down, not the count of tricks: a seat that
                // wins three still only has one bid to lose.  Restricted to LIVE
                // bids, because one already down cannot go down again.
                if (roles.role[winner] == ROLE_NIL) broken_seats |= 1u << winner;
            }
            // BY PARITY, not by role.  `on_nil_side` asks whether a seat has a
            // bid or covers one, which identifies a side only while ONE side
            // has a bid: with a bid on each, it lumps three seats together and
            // leaves one alone.  Parity relative to the reference bidder splits
            // the table the same way for every shape that has one side, and
            // correctly for the shape that has two.
            if (((winner ^ roles.nil_seat()) & 1) == 0) {
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
    tally_out.broken_mask = broken_seats;
    tally_out.live_nils_broken = 0;
    for (int seat = 0; seat < 4; ++seat) {
        if (broken_seats & (1u << seat)) ++tally_out.live_nils_broken;
    }
    tally_out.nils_set = tally_out.live_nils_broken + nil_set_count(roles);
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
        if (winner == sol.nil_seat()) ++running;  // primary counter only

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
           << "   [" << SEAT_CHARS[sol.nil_seat()] << '=' << running << ']';
        if (winner == sol.nil_seat()) os << " <-- nil takes a trick";
        if (winner < 0) break;
        leader = winner;
    }
    return os.str();
}

std::string format_solution(const Position& pos, const Solution& sol,
                            const SearchOptions& opts) {
    const bool nil_is_ns = (sol.nil_seat() & 1) == 0;
    const bool fast = sol.mode == MODE_FAST;
    const char* side = nil_is_ns ? "NS" : "EW";
    const char* other = nil_is_ns ? "EW" : "NS";
    std::ostringstream os;
    os << "PBN            " << deal_to_pbn(pos.hands) << '\n'
       << format_hands(pos) << '\n'
       << "Leader         " << SEAT_CHARS[pos.leader] << '\n'
       << "Seats          " << describe_seat_roles(sol.roles) << "  ("
       << (nil_is_ns ? "N/S minimise, E/W maximise" : "E/W minimise, N/S maximise") << ")\n"
       << "Objective      ";
    if (fast) {
        os << "fast mode: the nil question only, no trick counts and no PV\n";
    } else {
        os << (sol.roles.nil_already_set() ? "nil already set, so secondary only; "
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
        // NAME EVERY BIDDER THE COUNT ACTUALLY COVERS.  `nil_tricks` is
        // incremented whenever ANY bidder wins a trick, which is right -- with a
        // bid on each side the interesting quantity is how many tricks the bids
        // took between them -- and the old label named one seat, so a reader of
        // a two-bid deal saw "Tricks for N 6" and reasonably took it to mean N
        // won six.  N won two; E won four.  Reported by a user comparing two
        // role assignments of the same cards, which is exactly the comparison
        // the label made impossible.
        os << "Tricks for ";
        bool first_bidder = true;
        for (int seat = 0; seat < 4; ++seat) {
            if (!sol.roles.is_nil(seat)) continue;
            if (!first_bidder) os << '+';
            os << SEAT_CHARS[seat];
            first_bidder = false;
        }
        os << "  " << (nil_count(sol.roles) > 1 ? "combined " : "") << sol.nil_tricks
           << " of " << pos.tricks_remaining() << '\n'
           << "Side tricks    " << side << '=' << sol.nil_side_tricks << "  " << other << '='
           << sol.opponent_tricks << '\n';
    }
    if (sol.roles.nil_already_set()) {
        os << "Nil            ALREADY SET (told, not computed)\n";
    } else {
        os << "Nil            "
           << (sol.nils_set ? "FAILS  (can be forced to take a trick)"
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
