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
// WHO IS WHO.  The caller describes the deal with a role per seat -- see
// nil/seats.hpp -- rather than with a nil seat and a flag.  Giving the nil
// bidder ROLE_NIL_SET drops the primary objective: use it once the nil has
// actually been broken in the real game, because there is nothing left to
// protect or to attack and only the secondary objective matters.
//
// `nils_set` is HOW MANY BIDS ARE BROKEN, not whether one is.  With a single
// nil that is 0 or 1 -- numerically what the old boolean `nil_fails` held, so
// `if (nils_set)` still reads "the nil failed" -- and it is the count rather
// than the flag because a pair that both bid has three answers, not two.  A bid
// the caller declared already broken with ROLE_NIL_SET counts toward it, since
// the question is how many are down, not how many the search knocked down.
//
// TWO MODES
// ---------
// Everything above describes the FULL objective, and it is what you want when
// the answer has to say who took which trick.  It used to be close to
// unprunable, and this comment used to explain why with an argument that is
// half true: the packed scalar SPANS thousands of values, so a window on it
// excludes almost nothing, and every tie on the primary has to be explored to
// the bottom anyway or the secondary comes out wrong.
//
// The span is not the support.  A trick is worth per_nil to the bidder,
// per_partner to the cover and nothing to either opponent, so the value is
// per_nil * n + per_partner * c over n + c <= t, and no two (n, c) pairs
// collide -- gcd(k*k + 1, k) = 1.  The reachable set is therefore
// (t + 1)(t + 2) / 2 values: 105 at thirteen cards, not thousands.  Patch 22
// made the window bite anyway and patch 23 seeds it, so full mode does prune
// now, and the paragraph above should be read as history rather than as a
// description of this code.
//
// The correction is recorded rather than quietly applied because it is what
// ROADMAP item 34 was built on, and 34 was refuted -- bisecting 105 values
// converges in six or seven probes exactly as predicted, and still loses.  See
// "Evaluated and rejected".
//
// The nil question itself is boolean, and a boolean question wants a boolean
// search.  MODE_FAST zeroes the secondary and tertiary weights and gives the
// primary weight 1, so the value is literally the nil bidder's trick count and
// the window worth searching is [0, 1].  It answers `nils_set` and nothing
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
// two modes must agree on `nils_set` for every position.  `nil_bench --mode
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
#include "nil/ranks.hpp"
#include "nil/seats.hpp"

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

// Sentinel for SearchOptions::tt_megabytes: let the library choose the size.
constexpr std::size_t TT_AUTO = static_cast<std::size_t>(-1);

// What TT_AUTO resolves to.  ONE SIZE, for every hand size and both modes.
//
// It used to be a schedule -- 32 MiB at a four-card endgame, doubling per trick
// above a floor, capped at 256 in full mode and 128 in fast.  Sizing to the
// question looks obviously right and is not, for a reason that only shows up in
// a process that solves more than one position.
//
// TranspositionTable::resize() reuses the allocation when the size is
// unchanged, and re-zeroes the whole table when it is not.  A hand played out
// asks for a smaller table every trick or two, so a worker following a live
// game walks the schedule downwards and back up again on every deal, and each
// step is a memset of the whole table.  Measured on the schedule this replaced:
//
//     first resize to 256 MiB           133.1 ms   (allocation and page faults)
//     100 resizes to the same size        0.001 ms  (0.00001 ms each)
//     five hands, 13 -> 3 -> 13 cards    256.5 ms   (51.3 ms per hand)
//
// 51 ms per hand, charged against solves that are under a millisecond at the
// small end.  One size makes every resize after the first the free branch.
//
// 256 MiB is the full-mode number from patch 32, and fast mode is happy to take
// it: more table never costs a fast search nodes -- 13 cards on seed 11 holds
// 3,492,640 nodes at 128 MiB and 3,485,739 at 256 -- and what it used to cost
// was the allocation, which is now paid once per thread rather than per size
// change.
//
// WHAT THIS COSTS.  A process that solves ONE small position and exits now
// spends 133 ms on a table it barely uses, where the schedule would have spent
// about twelve.  That is the trade: a fixed footprint and no churn for a
// long-lived worker, against a worse one-shot.  A caller on the wrong side of it
// should set the size explicitly, which still overrides this.
constexpr std::size_t TT_DEFAULT_MEGABYTES = 256;
struct SearchOptions {
    // MODE_FULL by default: the caller who has not thought about it wants the
    // answer that carries its own evidence.
    SearchMode mode = MODE_FULL;

    // Direction of the tie-break.  false: each pair takes as many tricks as it
    // can.  true: each pair takes as few as it can (bag avoidance).
    bool minimise_own_tricks = false;

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

    // Tighten that reach bound with the tricks the opponents cannot be denied.
    // DDS section 4's LaterTricks: where one opponent hand holds the top
    // outstanding spades as a run, each of them wins the trick it is played on
    // down every line, so the simplex the bound above ranges over shrinks from
    // n + p <= t to n + p <= t - k.
    //
    // MODE_FULL only, and inert in MODE_FAST twice over -- the reach bound it
    // rides on is gated to full mode, and a count of the OPPONENTS' tricks says
    // nothing about a value that is the nil bidder's own trick count.
    //
    // On by default, off as the control arm.
    bool later_tricks = true;

    // Make that claim about ALL FOUR hands instead of one (roadmap item 44).
    //
    // top_spade_run() answers "which hand holds the top outstanding spades as a
    // run, and how many" -- one constraint on one side of the simplex.
    // forced_spade_tricks() answers "how many tricks is each hand forced to
    // win" for every hand in one walk, using the closed form the paper's rules
    // 2 and 3 are special cases of.  Three constraints instead of one, and the
    // two opponents' floors ADD, because two hands cannot win the same trick.
    //
    // Never weaker than the incumbent, and gated on three masked popcounts so
    // that the walk runs only where it could possibly reach the window.
    //
    // OFF BY DEFAULT, and the measurement is why.  It buys −1.28% of nodes at
    // 13 cards and costs 8.3% of throughput, so it is net slower in wall time:
    // the gate opens on 71% of boundaries, which is too often to pay for a walk
    // down every outstanding spade.  It is carried rather than deleted because
    // the node saving is real, the soundness is established by an exhaustive
    // property test, and it is nearly disjoint from the arm below -- the two
    // together are −2.44% of nodes.  What it needs is a tighter gate or a
    // cheaper walk, and both are open.  See ROADMAP.md item 44.
    //
    // With it off the incumbent single-hand form runs instead, so turning it on
    // is a one-flag differential on one binary.  Rides on later_tricks:
    // MODE_FULL only, trick boundary only.
    bool spade_matrix = false;

    // DDS section 3, spent in the one direction that is sound here (item 43).
    //
    // A can-cash count is a claim about one STRATEGY, so it bounds a node only
    // from the side owning it.  The opponents maximise, so their count is a
    // lower bound, spent against beta at a node where they are on lead.  The
    // cover partner's mirror image is deliberately not taken; see bounds.hpp.
    //
    // Patch 48 measured this against item 44 and found the two nearly
    // disjoint -- 97-98% of these cuts land where the forced floor does not --
    // which is why they ship together and each keeps its own switch.
    //
    // Rides on later_tricks.  On by default, off as the control arm.
    bool quick_tricks = true;

    // Spend a transposition-table entry that matches the position but does not
    // settle it on this node's CUTOFF BOUND.
    //
    // tt.hpp calls such a match PARTIAL and used to report it as a miss.  It is
    // not a miss: BOUND_LOWER at x says the value is at least x and BOUND_UPPER
    // at x says it is at most x, and both are true of the position whatever
    // window happens to be asking.  This solver has never used them, because
    // probe() threw the entry away.
    //
    // WHAT IS *NOT* DONE, AND WHY.  The textbook move here is the tighten-the-
    // window half of alpha-beta-with-memory (Plaat et al.): raise alpha onto a
    // lower bound, lower beta onto an upper one, and let the tighter window
    // carry down the whole subtree.  That was built first and it is a LOSS --
    // 5.5% more nodes at 11 cards, 3.6% at 13.  The mechanism is worth carrying
    // forward past this item: a tighter window makes descendants record
    // one-sided bounds where they would have recorded BOUND_EXACT, and an exact
    // entry answers every window while a bound answers almost none.  This
    // table runs an 80% hit rate at roughly five probes per store, so entry
    // QUALITY is worth more than window tightness, and by a wide margin.  The
    // full sweep -- both directions, one direction, and depth-gated -- is in
    // ROADMAP.md item 41, and it is monotone: every increment of propagation
    // costs.
    //
    // WHAT IS DONE.  Only one of the two bounds can end this node -- beta at a
    // maximiser, alpha at a minimiser -- so the entry is compared against that
    // bound alone and, if it is tighter, it becomes the cutoff threshold.
    // `alpha` and `beta` are untouched, so children are searched under exactly
    // the window the caller gave and their entries stay as exact as they were.
    // The benefit is kept and the cost is not paid.
    //
    // WHY THE EARLIER CUTOFF IS SOUND.  Take a maximiser whose entry pins
    // V <= y and which stops at the first `best >= y`.  The move that produced
    // `best` was searched under the untouched window, so if `best` is above
    // alpha it came back exact and V >= best; with V <= y <= best that forces
    // V = best, and an exact value is entitled to end a node under any window.
    // If instead `best` is at or below alpha the node has failed low against the
    // caller's own window, which it is entitled to report as it always was.
    // Symmetrically at a minimiser.  The value is squeezed exact by the very
    // fact that shortened the search.
    //
    // So MODE_FULL's principal variation survives: walk_pv() and
    // canonical_move_for() identify the canonical move by comparing child values
    // for equality, and every child value they compare is still the true one.
    //
    // MODE_FULL only, by arithmetic rather than by a gate: MODE_FAST asks every
    // node about [0, 1] and every value it stores is BOUND_UPPER at 0 or
    // BOUND_LOWER at 1, so every match settles its window and `partial` is
    // identically zero.  Patch 12 measured that and it is still true.
    //
    // On by default, off as the control arm.
    bool tt_narrow_window = true;

    // Put one card from each present suit at the head of the move list, in
    // rotation, before the canonical tail.  DDS section 5, whose stated aim is
    // "good mixture of moves (i.e. not all cards from the same suit first) in
    // case the heuristic is not good for a particular set-up".
    //
    // A hedge, not a bet: the same moves are searched, in a different order,
    // and nothing is spent to decide the order.  That is what separates it from
    // the rejected 6c, which spent the cover card unconditionally.
    //
    // Follows order_moves, which is BOTH modes -- not MODE_FAST only, however
    // much the seat-specific heuristics above read that way.  Inert on a seat
    // following suit: its moves are all one suit and the rotation has nothing
    // to rotate.
    //
    // Worth 5.4% to 9.0% of nodes at 11 and 13 cards in fast mode and 7.9% in
    // full mode at 11.  It costs nodes on easy deals -- 11.5% at 9 cards, 2.3%
    // at 12 -- which is what a hedge does: it pays where the primary heuristic
    // misfires, and deep trees are where that happens.
    //
    // Answer-neutral: reordering a full enumeration under fail-soft cutoffs
    // cannot change the value.  It can change which of several equally good
    // moves comes back, exactly as 6a, 6b and 6d can.  In MODE_FULL the
    // canonical re-derivation of patch 25 pins the principal variation
    // regardless, so the test arm carries --check-pv there and checks values
    // only in fast mode -- the same split corpus_ordering makes.
    //
    // On by default, off as the control arm.
    bool suit_mixed_order = true;

    // With a bid on each side, take the static end-of-trick cutoff in positions
    // where every bid is ALREADY DOWN.  Roadmap item 76.
    //
    // The cutoff is guarded by `gains_nonnegative`, which that shape refuses
    // outright: its primary weight multiplies outcome RANK, and rank falls when
    // a side loses its own bid, so a non-negative weight does not mean a
    // non-negative gain.  True as far as it goes, and it goes no further than
    // the first position where no nil is left to break -- there the rank is
    // fixed, what remains is the trick term, and that only rises.
    //
    // Measured on the patch-69 deals, 43% of an opposed tree sits in exactly
    // that state, because breaking a nil ENDS the subtree in the single-nil
    // solver and does not end it here.  Same answer either way; the flag exists
    // to be the control arm.
    bool settled_gains = true;

    // On lead, let the cover partner play the cheapest card the nil bidder can
    // duck beneath, in the nil bidder's shortest suit: the nil bidder is safe
    // on the trick by construction, and the suit shortens toward the void that
    // makes later leads free discards.  Roadmap item C5, tier three of the old
    // four-tier lead rule.  Ordering only: same answer.
    //
    // OFF BY DEFAULT AND OPT-IN, which is unusual here and deliberate.  Measured
    // it is the only item in the move-ordering block with two-sided signal: it
    // won three of six workloads on every rep, by 4.1%, 4.3% and 12.0% of nodes,
    // and lost two of six on every rep.  Nothing came out neutral.  That fails
    // the bar -- every rep a win -- so it does not ship on, and it is kept
    // rather than deleted because the next experiment is a tie-break rather
    // than a rewrite.  See MOVE_ORDERING.md.
    bool cover_duck_short = false;

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
    // TT_AUTO means "let the library choose", which is what a caller who has
    // not thought about it should get, and it resolves to TT_DEFAULT_MEGABYTES
    // -- one size for every hand size and both modes.  See the constant for why
    // it stopped being a schedule.
    //
    // Sizing this by hand is still the setting most likely to be wrong by an
    // order of magnitude in either direction: a 13-card MODE_FULL solve at a
    // flat 32 MiB spends 688 million nodes where 256 spends 46 million.  An
    // explicit number wins over TT_AUTO, and 0 is the same as use_memo = false.
    std::size_t tt_megabytes = TT_AUTO;

    // Back up which card ranks a subtree's value actually depended on, and
    // record how coarse the resulting table entries would have been.
    //
    // DDS sections 6.1-6.3 and Ginsberg's partition search: a card that won a
    // trick BY RANK matters, one that won because nobody could follow does not,
    // and an entry need only pin the ranks at or above the lowest winner in
    // each suit.  ROADMAP item 31 wants that machinery measured before any
    // table is redesigned around it, because the redesign trades an exact hash
    // for a masked scan and the question is not the hit rate but the throughput.
    //
    // This is the measurement and nothing else.  It changes no key, no probe
    // and no store; it computes an extra `Hand` per node and a keep vector per
    // store, and hands the histogram back through rank_mask_stats().  OFF by
    // default, because it costs throughput and buys the search nothing.
    bool track_rank_masks = false;

    // Count the three-way outcome of the forced-trick proof at trick
    // boundaries: fires today, would fire only under an adversarial reading, or
    // neither.  Roadmap item 32 asks for this population BEFORE the proof is
    // written, and the number it produces is a ceiling -- see
    // nil_forced_ceiling in bounds.hpp for why it over-fires on purpose.
    // Off by default and free when off.
    bool track_nilset = false;
    // Roadmap item 79's population and ceiling sweep.  Measurement only; free
    // when off, because the counting site is behind a null pointer.
    bool track_opposed = false;
    // Roadmap item 79: answer an opposed node from the ranks its broken-bid mask
    // can still reach, when that range already falls outside the window.  The
    // `target_bounds` this shape lost at patch 68, re-derived over the thing the
    // opposed value is actually written in.  Off is the control arm.
    bool opposed_reach = true;
    // Roadmap item 80: shift the principal-variation walk's window by what the
    // line has banked, so each step is asked the question the search answered
    // for it rather than the question the ROOT was asked.  Off is the control
    // arm and is what patch 77 shipped.  Answer-neutral, PV-neutral and
    // node-neutral in everything reported -- the walk's own nodes are
    // snapshotted out of the count -- so it moves wall time and nothing else.
    bool pv_shift_window = true;
    // ITEM 78's PROBE.  The seat of the ATTACKING side's bidder, or -1 for a
    // normal solve.  With it set the search answers one boolean and nothing
    // else: can that side force ITS bid to survive while the other's dies?
    //
    // It is not a mode, it is a question about a shape -- SHAPE_OPPOSING_NILS
    // and nothing else -- so it rides on `seats` rather than replacing it, and
    // solve() refuses it anywhere the shape does not apply.
    int conjunction_seat = -1;

    // ITEM 78c: spend a THIRD probe when the first two disagree, to close the
    // rank band they can only bound from one end.  Inert unless the presolve
    // runs at all, and inert on the two-thirds of deals the first two settle
    // outright.  Off is the control arm.
    bool conjunction_presolve = true;

    // Item 82: in a position where every bid is already down, answer the node
    // from a floor on one side's remaining tricks.  Off is the control arm.
    bool settled_tricks = true;

    // Report how often each arm's gate opens and each arm fires (items 43 and
    // 44).  Measurement only and free when off: every counter is guarded on a
    // null pointer that is set only when this is true.
    bool track_quick_tricks = false;


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
    // Distinct LIVE bidders that took at least one trick.  This is what the
    // search charges its primary weight against, so it is what a re-packing
    // self-check has to compare with.
    int live_nils_broken = 0;
    // Which bidder seats took a trick, as a seat bitmask.  The outcome rank of
    // an opposing-nils deal is a function of exactly this.
    unsigned broken_mask = 0;
    // How many bids are down in total: the above plus any the caller declared
    // already broken.  This is what gets reported, because the question is how
    // many are down and not how many the search knocked down.
    int nils_set = 0;
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
    // How many of the bids are broken.  0 or 1 with a single nil.
    int nils_set = 0;
    // The scalar the search minimised: the packed lexicographic value in
    // MODE_FULL, the nil bidder's trick count in MODE_FAST.
    int value = 0;
    // What each seat was doing, as the caller described it.  `nil_seat()` is
    // the field this used to be.
    SeatRoles roles;
    int nil_seat() const { return roles.nil_seat(); }
    // Principal variation, one entry per remaining card.  Empty in MODE_FAST.
    std::vector<Play> pv;
    std::uint64_t nodes = 0;
    // How many of `nodes` the root-window presolve spent, so the arm's cost can
    // be read off the same run that measures its benefit rather than inferred
    // from a second one.  Included in `nodes` rather than beside it -- the
    // presolve is work this solve did -- and zero whenever none ran.
    // Item 78's probe, when `SearchOptions::conjunction_seat` asked for it:
    // can the named side force ITS bid to survive while the other's dies?
    // False on every solve that did not ask.
    bool conjunction = false;
    std::uint64_t presolve_nodes = 0;
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
// lexicographically.  primary is zero when the roles say the nil is already
// set.
//
// A caveat about the one case the tertiary bites -- ROLE_NIL_SET while the
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

ObjectiveWeights objective_weights(int tricks_remaining, const SeatRoles& roles,
                                   const SearchOptions& opts);

// Validates, searches, then independently replays the PV as a self-check.
// Returns false and sets `err` on an invalid position or an internal
// inconsistency.
bool solve(const Position& pos, const SeatRoles& roles, const SearchOptions& opts,
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
    // How many of the bids are broken.  0 or 1 with a single nil.
    int nils_set = 0;

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
bool solve_moves(const Position& pos, const SeatRoles& roles, const SearchOptions& opts,
                 Solution& out, std::vector<MoveScore>& moves_out, std::string& err);

// Replays a PV, checking every play for legality and turn order, and reports
// who took what.  Also useful for checking a PV produced elsewhere.
bool replay_pv(const Position& pos, const std::vector<Play>& pv, const SeatRoles& roles,
               Tally& tally_out, std::string& err);

// The transposition table is kept between calls, because a corpus run solves
// hundreds of positions and reallocating tens of megabytes for each one costs
// more than the search does.  Call this to hand the memory back; the next
// solve() will simply allocate again.
void release_transposition_table();

// The winning-rank histogram accumulated since the last reset, across every
// solve on this thread that ran with SearchOptions::track_rank_masks.  Empty
// when nothing did.  See nil/ranks.hpp for what the numbers mean.
const RankMaskStats& rank_mask_stats();
void reset_rank_mask_stats();

// Where the forced-trick proof stands at trick boundaries where the nil bidder
// still holds a spade.  `both` fires today; `ceiling_only` is what an
// adversarial proof could add AT MOST.
// Roadmap item 79's population count, and its CEILING.  Measurement only --
// nothing in the search consumes it.
//
// With a bid on each side the outcome RANK is a step function of which bids have
// broken, and a bid never un-breaks, so the set of ranks a node can still reach
// is a function of `st.nils_broken` alone: four masks, computable without
// reading a card.  That set, plus the range of the trick term, bounds the
// subtree -- which is the `target_bounds` this shape lost and has never had
// back.
//
// Two questions, and patch 76 is the reason both are asked.  `state_*` is the
// POPULATION: where the opposed tree actually spends its nodes, re-measured
// after patch 77 because the band moved it.  `would_answer` is the FIRING RATE:
// how many of those nodes the bound would have settled against the window the
// node was actually asked about.  A cutoff being legal in 43% of the tree said
// nothing about how often it fires, and that item was worth 1.29% where the
// population predicted much more.
struct OpposedStats {
    std::uint64_t nodes = 0;          // opposed nodes reaching the count
    std::uint64_t state_intact = 0;   // no bid broken yet
    std::uint64_t state_near_down = 0;    // the bid on ctx.nil_seat's side is gone
    std::uint64_t state_far_down = 0;     // the other one is
    std::uint64_t state_both_down = 0;
    // Of the above, how many the reachable-rank bound would have answered, and
    // which way.  Split by state so the ceiling is attributable rather than one
    // aggregate that hides which region pays.
    std::uint64_t would_answer = 0;
    std::uint64_t would_answer_near_down = 0;
    std::uint64_t would_answer_far_down = 0;
    std::uint64_t would_answer_both_down = 0;
    std::uint64_t would_answer_intact = 0;
    // Nodes spent AFTER the value was found, recovering the principal variation
    // and re-deriving the canonical move.  Counted apart because they are not
    // part of the search's population and because patch 77 made them expensive:
    // it walks the line under the sentinels while the search ran under a band,
    // so the table entries do not settle the wider window and the walk
    // re-searches.  Not included in `nodes` or in any state row.
    // ---- item 81's population and ceiling ---------------------------------
    //
    // WITH EXACTLY ONE BID STILL LIVE THE POSITION IS A SINGLE-NIL POSITION.
    // The dead bid can never come back, so the only thing left that can move the
    // outcome rank is whether the survivor survives -- which is precisely the
    // question `bounds.hpp` answers, and precisely the machinery
    // `disable_single_nil_machinery` switches off for this shape.  Item 76 made
    // that argument for the BOTH-down region; the one-down region is unclaimed
    // and is about half the tree.
    //
    // A proof firing pins the rank, which collapses item 79's reachable set from
    // two values to one and so tightens its bound by a whole `k*k`.  These count
    // how often each step of that is available: how many one-down nodes sit at a
    // trick boundary where the proofs can be asked, how often either fires, and
    // -- the only number that matters -- how many nodes the PINNED bound would
    // answer that the two-valued one does not.
    std::uint64_t one_down_boundary = 0;
    std::uint64_t one_down_proof_doomed = 0;
    std::uint64_t one_down_proof_safe = 0;
    std::uint64_t one_down_answered_now = 0;
    std::uint64_t one_down_answered_pinned = 0;

    // ---- item 82: is there ROOM for a trick bound where the rank is settled?
    //
    // With every bid down the rank cannot move again, so the subtree is worth
    // the far side's remaining tricks and nothing else -- an ordinary
    // double-dummy trick count, which is what DDS section 3's QuickTricks and
    // section 4's LaterTricks bound and what `disable_single_nil_machinery`
    // switches off shape-wide.
    //
    // BEFORE RE-EXPRESSING EITHER BOUND FOR THIS SHAPE, which is most of the
    // work of shipping them, ask the cheaper question: how STRONG would a bound
    // have to be to decide anything?  A node's window admits a range of trick
    // counts; a bound decides it only by proving one side takes at least so
    // many.  That number is computable from the window and the tricks left with
    // no bound written at all, and its distribution is the ceiling.
    //
    // `settled_need[i]` counts nodes where the weakest sufficient claim is "some
    // side takes at least i more tricks", with 6 meaning six or more.  Index 0
    // is a node already decided by arithmetic; a large index is a node no cheap
    // bound will reach.
    std::uint64_t settled_boundary = 0;
    std::uint64_t settled_hopeless = 0;   // window admits the whole range
    std::uint64_t settled_need[7] = {0, 0, 0, 0, 0, 0, 0};

    // ...and how often the WEAKEST proof available actually delivers it.
    //
    // Item 81 died in the gap between "a claim this weak would suffice" and "a
    // claim this weak is provable", so the gap is measured here before anything
    // is spent.  The proof used is the cheapest sound one in the file:
    // `top_spade_run`, which gives the holder of the top outstanding spades and
    // how many it holds consecutively.  Spades are trump, so nothing beats them
    // and each wins the trick it is played on -- a floor on that SEAT's tricks,
    // hence on its side's, needing one mask test and a short loop.
    //
    // It is deliberately the weak version.  DDS section 3 counts side-suit
    // winners and ruffs as well, and section 4 bounds the other side from the
    // other end.  If even the spade run alone fires often, the full bound is
    // worth writing; if it does not, that is the number to know before writing
    // it rather than after.
    std::uint64_t settled_spade_proved = 0;
    std::uint64_t settled_spade_short = 0;   // right side, not enough tricks
    std::uint64_t settled_spade_wrong_side = 0;

    // ...and the same question asked of the STRONGER proofs, patch 87.
    //
    // `settled_forced` is `forced_spade_tricks` summed over the proving side's
    // two hands -- strictly stronger than `top_spade_run`, which reads one hand
    // and only its top run, and sound to add because each floor is proved on its
    // own hand and two hands cannot win the same trick.
    //
    // `settled_cash` adds DDS section 3's can-cash count for the side ON LEAD.
    // A can-cash count is a statement about one strategy, so it bounds the node
    // from one side only -- but at a settled node that is enough both ways: the
    // far side maximises its own tricks, so cashing floors the value, and the
    // near side minimises them, so its cashing caps the value.  `best` is used
    // rather than `sum`, because `sum` is optimistic and a measurement that
    // overstates its own ceiling is worse than none.
    std::uint64_t settled_forced_proved = 0;
    std::uint64_t settled_cash_proved = 0;
    std::uint64_t settled_either_proved = 0;



    std::uint64_t pv_walk_nodes = 0;
};

struct NilSetStats {
    std::uint64_t boundaries = 0;     // trick boundaries with a spade in the nil hand
    std::uint64_t proof_fires = 0;    // nil_must_take_a_trick says forced
    std::uint64_t ceiling_only = 0;   // silent today, the permissive test says forced
    std::uint64_t neither = 0;
};

// Roadmap item 43's population count: what a DDS section 3 quick-trick count
// would have bought, measured at the boundaries where the bounds that exist
// today stay silent.  Measurement only -- nothing in the search consumes it.
//
// `boundaries` counts full-mode trick boundaries that reached the end of the
// reach-bound block WITHOUT the untightened simplex or the incumbent
// later-tricks tightening answering them.  Every other counter is a subset of
// it, so each reads directly as a fraction of the population still open.
struct QuickTrickStats {
    std::uint64_t boundaries = 0;    // trick boundaries the untightened bound left open
    std::uint64_t gate_forced = 0;   // the spade-count gate let the forced walk run
    std::uint64_t fire_forced = 0;   // the forced-floor triangle cut
    std::uint64_t gate_cash = 0;     // the longest-suit gate let the cash walk run
    std::uint64_t fire_cash = 0;     // the opponents' can-cash floor cut
};


const QuickTrickStats& quick_trick_stats();
void reset_quick_trick_stats();

const OpposedStats& opposed_stats();
void reset_opposed_stats();
const NilSetStats& nil_set_stats();
void reset_nil_set_stats();

std::string format_pv_compact(const Solution& sol);       // "N:D2 E:DA S:D5 W:D7"
std::string format_pv(const Position& pos, const Solution& sol);  // one line per trick
std::string format_solution(const Position& pos, const Solution& sol,
                            const SearchOptions& opts);

}  // namespace nil

#endif  // NIL_SEARCH_HPP
