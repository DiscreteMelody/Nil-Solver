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
int search(Ctx& ctx, const State& st, CardId& best_move, int alpha, int beta) {
    ++ctx.nodes;
    best_move = NO_CARD;
    if (st.empty()) return 0;

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

    // The key describes the position up to a relabelling of ranks, so the move
    // that comes back out of the table is a slot number rather than a card and
    // has to be read against THIS position's live cards.  That relabelling is
    // order preserving -- it never moves a card across a suit and never
    // reorders two cards within one -- so the canonically lowest of several
    // equally good moves is still the canonically lowest one after it, and the
    // principal variation is the same one an uncached search would produce.
    StateKey key;
    SuitProfile profile;
    std::uint64_t hash = 0;
    bool keyed = false;
    if (ctx.tt) {
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
    int best = 0;
    bool have_best = false;

    // ORDERING (item 6a).  One card lifted out of the mask and searched first;
    // everything else keeps the canonical ascending order it always had.  That
    // is the whole mechanism, and it is deliberately not a sort: the loop below
    // costs a `take_lowest` per move today, and both optimisations this project
    // has rejected lost on throughput rather than on nodes.
    CardId promoted = NO_CARD;
    if (ctx.order_moves && seat == ctx.nil_seat && st.trick_len > 0) {
        promoted = nil_bidder_shed(st, moves);
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

        State next = st;
        next.hands[seat] &= ~card_bit(card);
        next.broken = spades_broken_after(st.broken, st.trick_len, led, card_suit(card),
                                          ctx.break_forced);

        int value;
        CardId ignored;
        if (st.trick_len == 3) {
            const CardId played[4] = {st.trick[0], st.trick[1], st.trick[2], card};
            const int winner = trick_winner(st.leader, played, 4);
            next.leader = winner;
            next.trick_len = 0;
            int gained = 0;
            if (winner == ctx.nil_seat) gained += ctx.primary_weight + ctx.tertiary_weight;
            if (((winner ^ ctx.nil_seat) & 1) == 0) gained += ctx.secondary_weight;

            if (ctx.gains_nonnegative && gained >= beta) {
                // This trick alone has already carried the line to beta, and no
                // later trick can take it back, so the rest of the hand cannot
                // change which side of the window the value falls on.  `gained`
                // is a lower bound on it, which is all a fail-high owes the
                // caller.  Chang's `if (goal <= 0) return 1`, arrived at from
                // the window rather than from the rules.
                //
                // It also has a second effect worth knowing about.  In
                // MODE_FAST the only non-zero gain is the nil bidder taking a
                // trick, which is worth exactly 1, and beta is exactly 1 -- so
                // this branch intercepts every gain that could have shifted the
                // window, and the shifted call below is never reached.  Every
                // node of a fast search therefore sees the same window [0, 1],
                // every entry it stores is a bound on that window, and no probe
                // that finds an entry ever fails to be answered by it.  The
                // pruning is free of the usual cost of bounded entries, and
                // Solution::tt_partial staying at zero is what checks it.
                value = gained;
            } else {
                // The child is asked about the value of the REST of the hand,
                // so the window it has to beat is this one less what the trick
                // just banked.
                value = gained + search(ctx, next, ignored, alpha - gained, beta - gained);
            }
        } else {
            next.trick[st.trick_len] = card;
            next.trick_len = st.trick_len + 1;
            value = search(ctx, next, ignored, alpha, beta);
        }

        if (!have_best || (maximizing ? value > best : value < best)) {
            have_best = true;
            best = value;
            best_move = card;
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
        // relative to the window it was given, and the window did not move.
        // Breaking out of the loop above implies best >= beta > alpha, so a cut
        // node can never be recorded as an upper bound.
        const std::uint8_t bound = best <= alpha   ? BOUND_UPPER
                                   : best >= beta  ? BOUND_LOWER
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
    // Both halves matter.  MODE_FULL never reorders whatever the caller asked
    // for, because it cannot gain from it and would lose the oracle check by
    // doing it; and within MODE_FAST the caller can still switch ordering off
    // to get the control arm.
    ctx.order_moves = opts.order_moves && opts.mode == MODE_FAST;
    ctx.tt_tag = opts.mode == MODE_FAST ? TAG_FAST : TAG_FULL;

    if (opts.use_memo && opts.tt_megabytes > 0) {
        TranspositionTable& table = shared_table();
        table.resize(opts.tt_megabytes);  // a no-op at the size it already is
        table.new_search();               // this solve may not see the last one's values
        ctx.tt = &table;
    }
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

    Ctx ctx;
    configure(ctx, nil_seat, opts, weights);

    State st = state_of(pos);
    CardId move = NO_CARD;
    // No window.  Full mode owes its caller trick counts, a principal variation
    // and a value the replay can be checked against, and all three want exact
    // numbers at every node rather than bounds.  Alpha-beta is MODE_FAST's, and
    // MODE_FULL declines it here rather than by a flag deeper in.
    const int value = search(ctx, st, move, WINDOW_MIN, WINDOW_MAX);
    const std::uint64_t search_nodes = ctx.nodes;
    // Snapshot before the PV walk below, which probes the table again and would
    // otherwise inflate the hit count with lookups that did no search work.
    const TTStats table_stats = ctx.tt ? ctx.tt->stats() : TTStats();

    // Walk the chosen moves to recover the principal variation.  With the memo
    // on this is a handful of lookups; with it off each step re-searches a
    // strictly smaller subtree.  Either way the moves are the same ones the
    // search picked, so the PV matches the oracle's play for play.
    out.pv.clear();
    while (!st.empty()) {
        const int seat = st.to_play();
        out.pv.push_back(Play{seat, move});

        const int led = st.led_suit();
        State next = st;
        next.hands[seat] &= ~card_bit(move);
        next.broken = spades_broken_after(st.broken, st.trick_len, led, card_suit(move),
                                          ctx.break_forced);
        if (st.trick_len == 3) {
            const CardId played[4] = {st.trick[0], st.trick[1], st.trick[2], move};
            next.leader = trick_winner(st.leader, played, 4);
            next.trick_len = 0;
        } else {
            next.trick[st.trick_len] = move;
            next.trick_len = st.trick_len + 1;
        }
        st = next;
        if (st.empty()) break;
        search(ctx, st, move, WINDOW_MIN, WINDOW_MAX);
        if (move == NO_CARD) {
            err = "internal error: no move available at a non-terminal position";
            return false;
        }
    }

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
    out.nodes = search_nodes;
    out.tt_probes = table_stats.probes;
    out.tt_hits = table_stats.hits;
    out.tt_partial = table_stats.partial;
    out.tt_stores = table_stats.stores;
    out.tt_evictions = table_stats.evictions;
    return true;
}

void release_transposition_table() { shared_table().resize(0); }

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
