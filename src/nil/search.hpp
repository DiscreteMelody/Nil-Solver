// Exhaustive double-dummy search for the nil question.
//
// THE QUESTION
// ------------
// Given a layout, a leader, a spades-broken flag and a nil bidder, play the
// hand out under a LEXICOGRAPHIC objective:
//
//   PRIMARY    the nil bidder's trick count.  The nil bidder and its covering
//              partner MINIMISE it; both opponents MAXIMISE it.
//
//   SECONDARY  each pair's own trick count, used only to choose among lines
//              that are already equally good for the primary:
//                  minimise_own_tricks = false   each pair takes what it can
//                  minimise_own_tricks = true    each pair sheds what it can
//
//   TERTIARY   which of the nil side's two partners holds those tricks.  Only
//              the covering partner's tricks count towards the partner's bid,
//              so among lines where the pair takes the same total, the pair
//              prefers the nil bidder to take fewer.  Inert while the primary
//              is on (it has already pinned the nil bidder's count) and off in
//              the "shed" direction (bags accrue to the pair whoever won).
//
// The primary is not ordinary trick maximisation: a side will happily throw
// away a trick of its own if that forces one onto the nil bidder.  The
// secondary only breaks ties.
//
// Both components are strictly opposed, because the two pairs' trick counts
// sum to a constant -- the nil side taking more is identical to the opponents
// taking fewer.  That is why one flag sets a coherent direction for both sides
// at once, and why plain minimax over the packed pair is still well defined.
//
// `nil_already_set` drops the primary objective.  Use it once the nil has
// actually been broken in the real game: there is nothing left to protect or
// to attack, and only the secondary objective matters.
//
// `nil_fails` is (nil_tricks > 0), or forced true when the caller has told us
// the nil is already set.
//
// TWO MODES
// ---------
// Everything above describes the FULL objective, and it is what you want when
// the answer has to say who took which trick.  It is also close to unprunable.
// The packed scalar spans thousands of values, so a window on it excludes
// almost nothing, and every tie on the primary has to be explored to the bottom
// anyway or the secondary comes out wrong.
//
// The nil question itself is boolean, and a boolean question wants a boolean
// search.  MODE_FAST zeroes the secondary and tertiary weights and gives the
// primary weight 1, so the value is literally the nil bidder's trick count and
// the window worth searching is [0, 1].  It answers `nil_fails` and nothing
// else: no trick counts, no principal variation.
//
// That window is what MODE_FAST spends.  A window of width one has no integers
// strictly inside it, so every node in a fast search either reaches beta or
// falls to alpha, and the first move that does it ends the node -- the opponents
// need ONE line that forces a trick onto the nil bidder, the nil side needs
// EVERY opponent line to fail.  The search is an AND-OR search wearing
// alpha-beta's clothes, and it is where the speed comes from.
//
// MODE_FULL does not prune at all.  It is not that it may not: it searches
// between sentinels no value can reach, so there is no window to cut against,
// and its node counts, its move choices and its principal variation are the
// same ones it produced before alpha-beta existed.  That is deliberate.  Full
// mode's answer is checked by replaying its own PV, and tools/crosscheck.py
// checks that PV against nil_oracle.py card for card; a bound-valued search
// has neither.  Full mode stays the reference, and MODE_FAST is the mode that
// goes fast.
//
// WHAT CHECKS FAST MODE
// ---------------------
// Full mode checks itself by replaying its own principal variation and
// re-deriving the value from the replayed trick counts.  Fast mode has no PV to
// replay, so it has no such internal witness; what stands in for it is that the
// two modes must agree on `nil_fails` for every position.  `nil_bench --mode
// both` runs a whole corpus that way and the `corpus_modes` test does it on
// every build.
//
// That agreement has now started doing real work.  The two modes no longer walk
// the same tree -- fast mode cuts and full mode does not -- so agreement is no
// longer a near-tautology about two weightings of one enumeration.  It is a
// pruned answer being held against an unpruned one that the oracle has checked,
// which is exactly the differential test a pruning bug would have to survive.
//
// RELATION TO nil_oracle.py
// -------------------------
// The oracle fixes the coalitions by seat parity (N/S always minimise), so it
// answers this question exactly when its designated player sits N or S.  Here
// the coalitions follow the nil bidder's own parity, which is the same thing up
// to a relabelling of seats; tools/crosscheck.py rotates each deal so the nil
// bidder sits North before asking the oracle.
#ifndef NIL_SEARCH_HPP
#define NIL_SEARCH_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nil/position.hpp"

namespace nil {

struct Play {
    int seat = 0;
    CardId card = NO_CARD;
};

// Which question the search is being asked.  See the header comment.
enum SearchMode {
    // The lexicographic objective: trick counts, a principal variation, and a
    // value the PV replay can be checked against.
    MODE_FULL = 0,
    // The nil question alone.  Primary weight 1, secondary and tertiary zero.
    MODE_FAST = 1,
};

// What the trick counts read in MODE_FAST.  Not zero: zero is a real answer to
// "how many tricks did the nil bidder take", and a caller that mistook one for
// the other would read a failing nil as a made one.
inline constexpr int TRICKS_NOT_COMPUTED = -1;

// Sentinel for SearchOptions::tt_megabytes: choose the size from the position.
constexpr std::size_t TT_AUTO = static_cast<std::size_t>(-1);

// The size TT_AUTO resolves to is chosen by auto_table_megabytes in search.cpp:
// doubling per trick above a floor, which is conservative against measured node
// growth of 2.1x to 3.5x per card, and capped where the measurements stop
// paying -- MODE_FULL at 512 MiB (1024 buys 5% fewer nodes and no wall time,
// 2048 is slower), MODE_FAST at 128 MiB (512 is slower in wall time than 128
// despite fewer nodes).
struct SearchOptions {
    // MODE_FULL by default: the caller who has not thought about it wants the
    // answer that carries its own evidence.
    SearchMode mode = MODE_FULL;

    // See rules.hpp: false is the literal reading of the break rule and matches
    // nil_oracle.py's default.
    bool break_on_forced_spade_lead = false;

    // Direction of the tie-break.  false: each pair takes as many tricks as it
    // can.  true: each pair takes as few as it can (bag avoidance).
    bool minimise_own_tricks = false;

    // Drop the primary objective: the nil has already been broken, so neither
    // side has anything left to protect or attack.
    bool nil_already_set = false;

    // Generate one move per class of rank-equivalent cards instead of all of
    // them -- with the jack gone, SK and SQ are one move played under two
    // names.  See rules.hpp: the classes are exact, and the representative is
    // the canonically lowest card, which is the one the tie-break would have
    // picked anyway.  So this changes neither the value nor the principal
    // variation, only the branching factor.
    //
    // It is on by default and the flag exists to be turned off: a search that
    // enumerates every legal card is the thing to compare against when
    // something disagrees.
    bool collapse_equivalents = true;

    // Answer a position outright when it can be proved without searching: the
    // nil bidder holds nothing that can be forced to win, or holds spades that
    // cannot all be buried.  See nil/bounds.hpp for both proofs.
    //
    // Inert outside MODE_FAST.  Each proof settles the nil bidder's own trick
    // count and says nothing about the pair's total or about which of the two
    // partners holds it, so it settles the whole value only when the value is
    // that count -- which is MODE_FAST's objective and no other.
    //
    // On by default, and like collapse_equivalents the flag exists to be turned
    // off: a fast search with the proofs disabled reaches the same booleans by
    // searching for them, and is what to compare against when one of them is
    // suspected of lying.
    bool use_static_bounds = true;

    // Try promising moves before the canonical enumeration order.  See
    // ROADMAP.md item 6: the orderings that help the nil question are not the
    // usual trick-maximising ones, and they differ by seat -- the nil bidder
    // wants to shed its highest card that can still lose, its covering partner
    // wants to keep high cards to cover with, and the opponents want to attack
    // the suits the nil bidder is short in.
    //
    // Inert outside MODE_FAST, and that is a harder rule than it looks.
    // MODE_FULL searches between sentinels, so it never cuts, so no ordering
    // can save it a single node -- and its move choice is an OUTPUT, checked
    // card for card against nil_oracle.py.  Reordering there would cost the
    // solver its strongest evidence and buy nothing at all.
    //
    // On by default, and like collapse_equivalents and use_static_bounds the
    // flag exists to be turned off: ordering must be answer-neutral, and the
    // way that gets shown here is a differential on one binary with this flag
    // as the only difference between the two columns.
    // Spend the two proofs in bounds.hpp in MODE_FULL as well as MODE_FAST.
    //
    // They prove things about the PLAY -- that the nil bidder takes no further
    // trick, or that it takes at least one.  MODE_FAST's value is exactly that
    // count, so a proof settles the node outright there.  MODE_FULL's value
    // also carries the pair's trick count, so the same proof pins only part of
    // it and yields a fail-soft BOUND, returned only when it clears the window.
    //
    // This spends MODE_FULL's node-count fixed point, which has held since
    // patch 8 and which every measurement before patch 29 was taken under.  It
    // does not spend the differential oracle: values and principal variations
    // are unchanged, because a bound returned only on a cutoff is a claim the
    // caller was already entitled to make do with.
    bool full_static_bounds = true;

    bool order_moves = true;

    // Evaluate the final trick instead of searching it.
    //
    // At a trick boundary every hand holds the same number of cards, so four
    // cards left means one card each and the trick is settled before it starts.
    // Searching it anyway costs five nodes -- four plies and the terminal --
    // each with a key to encode, a table to probe and an entry to store, for a
    // trick with no decision in it.  Chang's dds opens with the same shortcut
    // (`if (tricks_left == 1) return LastTrick(sp)`); this search did not have
    // it, and the last trick is the widest stratum of the tree.
    //
    // This is an exact value, not a bound, and it does not consult the window:
    // a forced line has one value and every window agrees about it.  So the
    // flag changes node counts and running time and nothing else, which makes
    // it a control arm in the same sense as --no-collapse.
    bool last_trick_eval = true;

    // Narrow the window as a node's own moves come back: a maximiser raises
    // alpha to the best it has seen, a minimiser lowers beta, and the children
    // that follow inherit it.  This is the half of alpha-beta that patch 10
    // deliberately left out, and leaving it out is the whole reason MODE_FULL
    // has been a node-count fixed point since patch 8: without it MODE_FULL's
    // sentinel window is never reachable, so the cutoff below it never fires
    // and the search is exhaustive minimax with a memo bolted on.
    //
    // Turning it on is answer-neutral in both modes, for two different reasons.
    //
    //   MODE_FAST is unchanged NODE FOR NODE, and provably so.  Its window is
    //   null -- beta is alpha + 1 -- so at a maximiser `best > alpha` already
    //   implies `best >= beta`, and the cutoff on the next line fires before
    //   the widened alpha can reach a single child.  Symmetrically at a
    //   minimiser.  Every assignment this flag enables in fast mode is to a
    //   variable the node is about to stop using.
    //
    //   MODE_FULL keeps its exact values and its principal variation.  Values,
    //   because every entry point that needs an exact number asks for it with
    //   the sentinel window -- solve()'s root, walk_pv()'s each step, and the
    //   per-move loop in solve_moves() -- and a node given an unreachable
    //   window cannot fail either way, so what it returns is exact.  The PV,
    //   because a probe under that window can only be answered by a BOUND_EXACT
    //   entry, and an exact entry is by definition one that did not cut: it
    //   enumerated every move in canonical order under strict improvement, so
    //   its stored move is still the canonically lowest of the best.  A move
    //   searched after alpha has risen either fails low -- returning at most
    //   alpha, which cannot strictly improve on it -- or lands inside the
    //   window and is exact.  Neither can displace the incumbent wrongly.
    //
    // On by default, and like the three flags above it exists to be turned off,
    // so that the saving can be measured as a one-flag differential on one
    // binary rather than across two builds.
    bool narrow_window = true;

    // Seed MODE_FULL's root window from a MODE_FAST presolve (roadmap item 23).
    //
    // The two modes answer different questions about the same position, and the
    // cheap one bounds the expensive one.  With k = tricks + 1 the packed value
    // is (primary + tertiary + secondary) * nil_tricks + secondary * cover, and
    // the two halves of that do not overlap: every position where the nil
    // bidder takes no trick scores at most `max_value_if_nil_safe` below, and
    // every position where it takes one scores strictly above.  So a fast
    // search -- which costs on the order of a thousandth of the full one -- buys
    // a bound on the full one for free.
    //
    // Only the nil-safe direction is taken.  The other one was implemented,
    // measured at 1.00x, 0.99x and 1.00x on three sizes, and dropped: it puts
    // alpha under a maximising root, where narrowing was going to raise it
    // anyway on the first child.  See the item for the numbers.
    //
    // Exactness survives because the bound is not a guess.  The true value is
    // on the safe side of the threshold, so a window that ends just above the
    // threshold still contains it, and a node whose window contains its value
    // returns that value rather than a bound.  Per-card scoring in solve_moves
    // is the one place a value can legitimately sit on the far side -- a card
    // that loses the nil -- and that card is re-searched wide.
    //
    // On by default, off as the control arm.
    bool presolve_window = true;

    // Answer a MODE_FULL node from arithmetic alone when the best or worst the
    // remaining tricks could possibly be worth already falls outside the
    // window.  DDS section 2's TargetReached, whose second direction -- "tricks
    // won plus tricks left to play cannot reach the target" -- this search
    // never had.
    //
    // A trick is worth per_nil to the nil bidder, per_partner to the cover
    // partner and nothing to either opponent, so a subtree with t tricks left
    // is worth per_nil * n + per_partner * p over n + p <= t.  That is linear
    // over a simplex and its extremes are at its vertices, so the whole bound
    // is a popcount and two multiplications, and it reads no cards at all.
    //
    // Inert in MODE_FAST by construction, not by measurement: there the value
    // is the nil bidder's trick count, so the reachable range is [0, t] against
    // a window that is [0, 1] at every node, and neither test can fire above
    // the empty position.
    //
    // On by default, off as the control arm.
    bool target_bounds = true;

    // Consult the transposition table only at a trick boundary, never in the
    // middle of a trick.
    //
    // This is what DDS does -- "positions stored in the Transposition Table
    // always consist of completed tricks" (Haglund and Hein, section 6) -- and
    // this solver did not, because the key in statekey.hpp was built to
    // describe a mid-trick position too and there was no reason not to use it.
    // The reason is throughput, and it is large.
    //
    // WHAT A MID-TRICK LOOKUP COSTS.  encode_state_key walks every live card of
    // every suit, so it is O(cards remaining) -- up to 52 iterations of a bit
    // loop -- and mix_key, probe and store follow it.  That is the dominant
    // per-node cost in this search, and three nodes in four are mid-trick.
    //
    // WHAT IT BUYS, MEASURED BY PLY.  Hit rates at 13 cards, fast mode, over
    // three seeds: 62-70% at a trick boundary, 5-11% at ply 1, 19-27% at ply 2,
    // 25-32% at ply 3.  Ply 1 is the worst and the reason is structural: the
    // only route to a ply-1 node is its boundary parent, and a boundary parent
    // reached a second time is answered by the table before it regenerates any
    // child.  So a ply-1 entry is stored on a path that, by construction,
    // nothing walks twice -- it is a memo of a position visited once.  It is
    // also 27% of all stores, so what it mostly does is evict boundary entries
    // that would have been hit.
    //
    // Plies 2 and 3 do transpose, because the `gap` encoding makes different
    // played cards equal when they trap the same number of survivors.  They
    // still lose: the hits they earn are worth less than the key construction
    // they charge on every mid-trick node, and than the boundary entries they
    // displace.  A ply sweep on one binary at 13 cards, seed 11:
    //
    //     plies using the table    nodes/position    ms/position
    //     0,1,2,3 (the old default)   4,606,934         1,075
    //     0,2,3                       4,192,289           846
    //     0,2                         3,679,073           499
    //     0 only                      3,556,097           297
    //
    // Node counts move both ways -- boundary-only costs 31% more nodes at 13
    // cards on seed 3 and saves 23% on seed 11, because relieving the table of
    // three quarters of its stores is worth more than the mid-trick hits on
    // deals whose boundary set does not fit.  Wall time does not move both
    // ways: it is 2.3x to 3.6x better at 11 to 13 cards and better at every
    // size measured, including the 4-6 card corpus.
    //
    // Answer-neutral by construction.  The table is a memo, so declining to
    // write or read part of it can change how long a search takes and nothing
    // else.  On by default, off as the control arm.
    bool tt_boundaries_only = true;

    // Look positions up in a transposition table.  The search is a pure
    // function of the position, so the table changes neither the value nor the
    // principal variation -- it is memoisation, not alpha-beta or any other
    // search enhancement.  There is still no pruning of any kind.
    //
    // What it keys on is nil/statekey.hpp: not the literal state, but the
    // smallest description of it that still determines the value.  Two
    // positions that differ only in absolute ranks, or only in which card is
    // currently winning a trick as opposed to how many live cards it beats, are
    // one entry.
    bool use_memo = true;

    // Table size in mebibytes, rounded DOWN to a power-of-two bucket count.
    // Zero has the same effect as use_memo = false.
    //
    // The table is bounded, so once a search overflows it the node count
    // depends on this number: a bigger table finds more of its own earlier work
    // and visits fewer nodes.  Benchmarks are only comparable at equal size,
    // which is why nil_bench records it in the history file.
    // TT_AUTO means "pick a size from the position", which is what a caller who
    // has not thought about it should get.  Sizing this by hand is the setting
    // most likely to be wrong by an order of magnitude in either direction: a
    // 13-card MODE_FULL solve at the old flat 32 MiB spends 688 million nodes
    // where 512 MiB spends 111 million, and a 4-card one at 512 MiB has bought
    // half a gigabyte to hold a few thousand entries.
    //
    // Resolved once per solve against tricks_remaining and mode; see
    // auto_table_megabytes.  An explicit number still wins.
    std::size_t tt_megabytes = TT_AUTO;

    // Return the canonically lowest of the equally-best lines, rather than
    // whichever one the move ordering happened to reach first.
    //
    // Only the LINE is at stake, never a value and never a trick count.  Two
    // optimal lines score the same by definition, and the trick counts are
    // recovered from the score rather than from the walk: with a nil trick
    // worth primary + tertiary = k*k + 1 and a side trick worth k, and
    // gcd(k*k + 1, k) = 1, no two (nil_tricks, side_tricks) pairs in range
    // share a value.  So a differently-ordered search yields the same numbers
    // off a different-but-equally-optimal line.
    //
    // It matters for exactly one thing: the corpus compares principal
    // variations against nil_oracle.py card for card, and that comparison is
    // the project's strongest correctness evidence.  So the entry point that
    // hands a caller a line asks for the canonical one and pays for it by not
    // reordering; the entry points that do not expose a line -- nil_solve and
    // nil_solve_moves -- turn this off and take the ordering, which is worth
    // 2.3x on a hard thirteen.
    bool canonical_pv = true;
};

// Who took what along a line.
struct Tally {
    int nil_tricks = 0;       // the nil bidder alone
    int nil_side_tricks = 0;  // the nil bidder and its covering partner
    int opponent_tricks = 0;  // the other pair
};

struct Solution {
    // In MODE_FAST these three are TRICKS_NOT_COMPUTED: the search never
    // tracked them, and reporting the number that happens to fall out of the
    // primary today would make callers depend on something items 3 and 4 take
    // away.
    int nil_tricks = 0;
    int nil_side_tricks = 0;
    int opponent_tricks = 0;
    // The one field both modes fill in, and the one they must agree on.
    bool nil_fails = false;
    // The scalar the search minimised: the packed lexicographic value in
    // MODE_FULL, the nil bidder's trick count in MODE_FAST.
    int value = 0;
    int nil_seat = 0;
    // Principal variation, one entry per remaining card.  Empty in MODE_FAST.
    std::vector<Play> pv;
    std::uint64_t nodes = 0;
    // Which mode produced this, so a caller holding a Solution can tell what is
    // in it without having kept the SearchOptions around.
    SearchMode mode = MODE_FULL;

    // Transposition table behaviour for this solve.  `tt_hits` is the number of
    // nodes answered from the table; `tt_evictions` counts stores that threw
    // away a different live position, which is the signal that the table is too
    // small for the depth being attempted.
    //
    // `tt_partial` counts probes that found the position but held only a bound,
    // and a bound too weak to settle the window being asked about.  It is zero
    // in MODE_FAST, whose null window every entry settles; MODE_FULL has stored
    // bounds and produced partial hits since patch 22.  It is the price
    // of pruning in MODE_FAST: work the table remembered doing and could not
    // hand back.
    std::uint64_t tt_probes = 0;
    std::uint64_t tt_hits = 0;
    std::uint64_t tt_partial = 0;
    std::uint64_t tt_stores = 0;
    std::uint64_t tt_evictions = 0;
};

// Weights that pack the three levels into one integer:
//
//     value = primary   * nil_tricks
//           + secondary * nil_side_tricks
//           + tertiary  * nil_tricks
//
// which the nil side minimises and the opponents maximise.  With
// K = tricks remaining + 1, primary is K*K and secondary is +/-K, so each level
// strictly outranks everything below it and the three compare
// lexicographically.  primary is zero when the nil is already set.
//
// A caveat about the one case the tertiary bites -- a nil already set while the
// pair is still taking tricks.  There, "the pair maximises its partner's
// tricks" and "the opponents maximise their own" are not strictly opposed:
// both sides would rather the nil bidder took nothing, so the split between the
// two partners is slack that only one side cares about, not a tug of war.  The
// tertiary sits BELOW the pair's total on purpose, so the opponents' objective
// stays exactly "take as many as we can" and the split resolves against the
// pair.  The partner count reported is therefore the one the pair can
// guarantee, not the one it might get if the opponents were helping.
//
// In MODE_FAST the weights are (1, 0, 0) regardless of every other option, so
// the value is the nil bidder's trick count with nothing packed above or below
// it.  Weight 1 rather than K*K on purpose: it makes the alpha-beta window the
// next roadmap item wants literally [0, 1] rather than [0, K*K].
struct ObjectiveWeights {
    int primary = 0;
    int secondary = -1;
    int tertiary = 0;
};

ObjectiveWeights objective_weights(int tricks_remaining, const SearchOptions& opts);

// Validates, searches, then independently replays the PV as a self-check.
// Returns false and sets `err` on an invalid position or an internal
// inconsistency.
bool solve(const Position& pos, int nil_seat, const SearchOptions& opts,
           Solution& out, std::string& err);

// One legal card at the root, and what playing it leads to.
//
// This is the DDS-shaped answer: not "what should I play" but "here is every
// card you may play, and here is what each one costs".  A game client scores
// its own move list against it; a teaching tool shows the player which cards
// were safe and which threw the nil away.
struct MoveScore {
    // The card, and the other legal cards that are the same move under a
    // different name.
    //
    // `equals` ALWAYS includes `card` itself, so it is never zero and a caller
    // that wants every legal card can iterate `equals` and ignore `card`.  With
    // the jack gone, playing the king and playing the queen reach positions
    // that differ only by swapping two labels, so the search looks at one of
    // them and this records the other -- the same reduction rules.hpp already
    // performs, read backwards.  Under collapse_equivalents = false every class
    // is a singleton and `equals` is just `card`.
    CardId card = NO_CARD;
    Hand equals = 0;

    // Does the nil fail AFTER this card is played, against best play by
    // everyone from there on?
    //
    // Read it from whichever side you are on: for the nil bidder or its
    // covering partner, false means this card holds the nil together; for an
    // opponent, true means this card breaks it.  It is one fact rather than
    // two, because a double-dummy answer does not depend on who asked.
    bool nil_fails = false;

    // As Solution's, but for the position after this card, and INCLUDING the
    // trick this card completes if it completes one.  TRICKS_NOT_COMPUTED in
    // MODE_FAST, for the reason Solution gives.
    int nil_tricks = TRICKS_NOT_COMPUTED;
    int nil_side_tricks = TRICKS_NOT_COMPUTED;
    int opponent_tricks = TRICKS_NOT_COMPUTED;

    // The scalar this move scored on the position's own objective, comparable
    // with Solution::value and with the other entries in the list.
    int value = 0;

    // True when this card achieves the position's value -- i.e. it is one of
    // the moves the search would have been content to pick.  There is usually
    // more than one.
    bool is_best = false;
};

// Validates, then scores EVERY legal card at the root rather than just the best
// one.  `out` is filled in as solve() would fill it, and `moves_out` gets one
// entry per equivalence class (or per legal card under
// collapse_equivalents = false), in canonical order: suit-major, ascending
// rank.
//
// The cost is not the same as solve()'s, and in MODE_FAST it is not close.  The
// whole point of a boolean search is that it stops at the first card that
// settles the question; asking about all of them forbids exactly that, so
// expect several times the work of the plain call.  One transposition table is
// shared across the root moves, which is what keeps the multiple from being the
// branching factor.  MODE_FULL is nearer to free: it never cut in the first
// place, so the extra work is the bookkeeping rather than the search.
bool solve_moves(const Position& pos, int nil_seat, const SearchOptions& opts, Solution& out,
                 std::vector<MoveScore>& moves_out, std::string& err);

// Replays a PV, checking every play for legality and turn order, and reports
// who took what.  Also useful for checking a PV produced elsewhere.
bool replay_pv(const Position& pos, const std::vector<Play>& pv, int nil_seat,
               bool break_on_forced_spade_lead, Tally& tally_out, std::string& err);

// The transposition table is kept between calls, because a corpus run solves
// hundreds of positions and reallocating tens of megabytes for each one costs
// more than the search does.  Call this to hand the memory back; the next
// solve() will simply allocate again.
void release_transposition_table();

std::string format_pv_compact(const Solution& sol);       // "N:D2 E:DA S:D5 W:D7"
std::string format_pv(const Position& pos, const Solution& sol);  // one line per trick
std::string format_solution(const Position& pos, const Solution& sol,
                            const SearchOptions& opts);

}  // namespace nil

#endif  // NIL_SEARCH_HPP
