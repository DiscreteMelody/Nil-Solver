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
// search.  MODE_FAST zeroes the secondary weight and gives the
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
    // The nil question alone.  Primary weight 1, secondary zero.
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
// 512 MiB since patch 95, raised from patch 32's 256.  The two-nil shape was
// evicting: 2,226,872 evictions against 12,856,780 stores at 256 MiB on the
// hard opposed deal, 17% of stores displacing a live position.  Doubling the
// table is 6.6% of the nodes on that deal (258,394,757 -> 241,340,102) and won
// on wall clock in every one of three interleaved pairs.
//
// DO NOT READ THIS AS "BIGGER IS BETTER".  Throughput falls monotonically as
// the table grows -- 11.3M nodes/sec at 16 MiB down to 7.6M at 2048 -- so the
// node saving and the cache cost run against each other and the curve has a
// floor rather than a slope.  Measured on that deal: 256 MiB 28.8s, 512 27.9s,
// 1024 28.8s, 2048 31.6s.  512 is the bottom, and it is bottom for THIS machine
// -- the optimum tracks L3 size, so a deployment on very different hardware
// should re-measure rather than inherit this number.
//
// Fast mode is happy to take it: more table never costs a fast search nodes --
// 13 cards on seed 11 holds 3,492,640 nodes at 128 MiB and 3,485,739 at 256 --
// and what it used to cost was the allocation, which is now paid once per
// thread rather than per size change.
//
// WHAT THIS COSTS.  A process that solves ONE small position and exits now
// spends 133 ms on a table it barely uses, where the schedule would have spent
// about twelve.  That is the trade: a fixed footprint and no churn for a
// long-lived worker, against a worse one-shot.  A caller on the wrong side of it
// should set the size explicitly, which still overrides this.
constexpr std::size_t TT_DEFAULT_MEGABYTES = 512;
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

    // The ADVERSARIAL proofs in bounds.hpp (Sept 2026): nil_duck_or_cover says
    // the nil side can keep the nil clean against any defence, and
    // forcing_lead_suit / forced_ruff_lead say the opponent on lead can force
    // a trick onto it this trick.  Spent wherever the two every-line proofs
    // are -- as values in MODE_FAST, as fail-soft bounds in MODE_FULL -- and
    // nowhere they are not: not the double-dummy handoff, not doom-charging,
    // which need the outcome pinned on every line.  See bounds.hpp for why the
    // bands are the same under optimal play.
    //
    // Under use_static_bounds, so --no-static still means no proofs at all.
    // Two switches rather than one so each half can be measured alone; the
    // ABI has one bit left and spends it on both (NIL_FLAG_NO_ADVERSARIAL_PROOFS).
    // Answer-neutral: same values, same verdicts, same principal variations.
    bool adversarial_safe = true;
    bool adversarial_doom = true;

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
    // is (primary + secondary) * nil_tricks + secondary * cover, and
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
    //
    // Inert while trick_order is on: the cover partner holds no live bid, so
    // trick_order_moves orders it.  The study measured C5 in front of that
    // order as well -- +9% and +13% of nodes on two 13-card seeds.
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
    // Re-measured Sept 2026 on the per-card call (solve_moves, 155 thirteen-card
    // deals, 512 MiB as the baseline): 256 MiB +3% wall, 128 +13%, 64 +35%, 32
    // +85%; the slowest deal (one bid per side) 39.6 s at 512 and 189 s at 32.
    // The median deal is the same at every size -- it is the tail that
    // overflows the table.  1024 bought nothing further on the ten slowest.
    std::size_t tt_megabytes = TT_AUTO;

    // Ask the operating system for 2 MiB pages for both tables (optimization
    // Q5, Sept 2026; nil/bigalloc.hpp says how and why).  The tables live in
    // demand-zero OS pages either way; this only decides whether huge pages
    // are requested.  Best-effort: where they cannot be had -- Windows without
    // the "Lock pages in memory" privilege, or Linux with transparent huge
    // pages set to "never" -- the request falls back silently.  Changing it
    // between solves reallocates both tables.  Same answers and same nodes
    // either way; `--no-huge-pages` is the control arm.
    //
    // Measured on the per-card call, 155 thirteen-card deals, two long-lived
    // processes interleaved deal by deal (Linux, transparent huge pages in
    // "madvise"): 2 MiB pages -9.9% wall time, 139 deals faster and 6 slower
    // by 2%+; the slowest deal 36.9 s -> 33.6 s.  Demand-zero, page-aligned
    // tables on their own, against the old vector fill: -3.0% steady state,
    // and ~250 ms less per new process or thread (the eager fill is gone).
    bool huge_pages = true;

    // The double-dummy engine's table probe requests every candidate profile's
    // bucket at once after the most recently used one, instead of waiting for
    // each miss in turn (Q9b, Sept 2026; dd::Engine::probe has the numbers).
    // Same answers, same nodes -- only the waiting changes.  Worth 1.3% of
    // per-card wall time on 4 KiB pages, nothing measurable on 2 MiB pages.
    // `--no-dd-prefetch` is the control arm.
    bool dd_prefetch = true;

    // The double-dummy engine's table persists across solves; age its entries
    // so that what earlier solves stored is replaced first (Sept 2026; see
    // dd::Engine::Entry for the measurement).  Same answers -- every stored
    // fact stays true in every deal -- and `--no-dd-age` is the control arm.
    bool dd_age = true;

    // THE OCT 2026 ENGINE PASS (ROADMAP item 99): four changes inside the
    // double-dummy engine, each with its control arm.  The engine answers the
    // same question either way -- can N/S take `target` of the rest -- so every
    // value, verdict, row and line is unchanged; only its node count moves.
    // dd::Engine::search has the argument for each, and item 99 the numbers.
    //
    //   dd_order        DDS's heuristic move ordering (heuristic_sorting.cpp)
    //                   in place of the engine's own short score.  --no-dd-order
    //   dd_lowest_win   DDS's lowest-win rule: a refuted card whose rank is
    //                   below every rank its refutation read stands for the
    //                   mover's lower cards of that suit.  --no-dd-lowest-win
    //   dd_win_by_rank  a trick's winner is a rank the proof relied on only if
    //                   it beat another card of its suit (DDS 6.1).
    //                   --no-dd-by-rank
    //   dd_profiles     k-profiles per table header, 1..26 (was 12).
    //                   --dd-profiles N
    //
    // Each turned off with the rest on, 88 random 13-card deals, values-only
    // rows, tables kept: +33.9% / +12.8% / +2.9% / +3.6% wall on `0 3 2 3` and
    // +146% / +46% / +7.9% / +13.6% on `1 3 2 3`, which is all engine.
    bool dd_order = true;
    bool dd_lowest_win = true;
    bool dd_win_by_rank = true;
    int dd_profiles = 26;

    // Ask the two every-line nil predicates once per trick boundary rather than
    // once per call site (Q7, Sept 2026): the doom check, the double-dummy
    // handoff's pin test and the full-mode static block all asked them of the
    // same state.  A pure cache -- the node count is identical with it off,
    // which is what `--no-boundary-facts` checks.  Worth 0.5% of instructions
    // (callgrind, four 13-card deals); below the wall-time noise.
    bool boundary_facts = true;

    // solve_moves only: recover and replay a line per row (the default), or
    // decode each row's counts from its exact value (false; Q3, Sept 2026).
    // Off, every row keeps its value, nils_set, nils_set_mask (where the
    // objective pins it), nil_side_tricks, opponent_tricks and is_best --
    // identical to the default -- and loses nil_tricks (TRICKS_NOT_COMPUTED)
    // and the position's principal variation.  A row whose mask the value
    // cannot pin (a pair that both bid, one bid down) still walks its line.
    // `--values-only`.
    bool row_lines = true;

    // solve_moves: score the rows best-first by the search's own move order,
    // then report them in canonical order (Q4, Sept 2026).  Same rows, same
    // values, same line; the node count moves.  `--no-row-order`.
    bool row_order = true;

    // M4 (Sept 2026): with a bid on each side or a pair that both bid, spend the
    // adversarial proofs (duck or cover; forcing lead) as band bounds wherever
    // exactly one bid is still live.  Honours adversarial_safe/adversarial_doom
    // (--no-duck-cover, --no-forcing-lead, --no-adversarial-proofs).  Same
    // answers; `--no-multi-live-proofs` is the control arm.  -7.7% nodes and
    // -4.4% wall on the per-card benchmark, all of it on two-bid deals.
    bool multi_live_proofs = true;

    // PAIR PROOFS (Oct 2026): with both of a pair's bids live, spend the
    // single-bid proofs on EITHER bidder as a bound on the pair.  If one of the
    // two can be kept clean (no trick on any line, or duck or cover), at most
    // one bid goes down; if the opponents on lead can force one of them now (a
    // forcing lead or a forced ruff), at least one does.  Honours
    // adversarial_safe/adversarial_doom as M4 does.  Same answers;
    // `--no-pair-proofs` is the control arm.  See search_core.
    bool pair_proofs = true;

    // A pair that both bid, exactly one of them still live: the one-live
    // double-dummy bound (dd_live_bounds below, off by default) from this many
    // tricks up; 0 is off.  Its engine probes pay where the subtree under them
    // is large -- a whole row of `3 1 3 0` or `1 3 0 3`, a twin doomed at the
    // root -- and lose where it is small, so the floor is a depth.  Same
    // answers; `--twin-dd-live 0` is the control arm.  The sweep that chose 11
    // is at dd_one_live_bound in search.cpp.  `--dd-live-bounds`, which turns
    // the bound on for every shape at every depth, takes precedence.
    int twin_dd_live_min_t = 11;

    // ONE LIVE BID, HANDED TO THE SINGLE-NIL SEARCH (Oct 2026, ROADMAP item
    // 100).  With a bid on each side, or three bids, a trick boundary where
    // exactly one bid is still live is a single-nil position in disguise: the
    // dead bids never come back, so the value is the live bid's fate, weighted
    // by one step of outcome rank, plus a trick count -- the single-nil
    // objective up to sign and a constant.  On, such a boundary is searched by
    // a single-nil context, with every piece of single-nil machinery the
    // opposed shape switches off: the proofs spent in full mode, the target
    // and later-tricks bounds, the demoted and broken-band double-dummy
    // bounds, the partial-match cutoff bound.  Its values sit under their own
    // table tag.  Default direction, full mode; the minimise direction keeps
    // the general search.  Same values and lines; `--no-one-live-handoff` is
    // the control arm.  The argument and the measurement are at
    // one_live_handoff in search.cpp.
    bool one_live_handoff = true;

    // The broken-band ceiling (broken_dd_min_t below) inside those handed-off
    // searches, from this many tricks up; 0 is off.  Its own knob because the
    // floor that pays there is lower: measured at 8 (see one_live_handoff in
    // search.cpp), against 10 for a single nil.  Same values and lines;
    // `--one-live-broken-dd N`.
    int one_live_broken_dd_min_t = 8;

    // RE-DERIVE A LINE'S MOVE BY ONE NULL-WINDOW QUESTION PER CANDIDATE (Oct
    // 2026, ROADMAP item 100).  canonical_move_for() looks for the canonically
    // lowest move worth the position's exact value v, and asked each candidate
    // "are you exactly v?" under the window (v - 1, v + 1).  No move of a
    // maximiser is worth more than v and none of a minimiser's less, so "at
    // least v" (or "at most v") is the same question, and a null window
    // answers it with a one-sided proof.  Same moves found; `--no-pv-null` is
    // the control arm.  Read only under tight_pv, which supplies the exact v.
    bool pv_null_window = true;

    // LIVE-SET PROOFS (Oct 2026, ROADMAP item 101).  With a bid on each side or
    // three bids, at a trick boundary where TWO OR MORE bids are still live:
    // the single-bid proofs, each asked of one live bid, restrict which bids can
    // still fall, and the outcome ranks of the masks that remain bound the node.
    // A bid that no line can force (no spades, nil_cannot_be_forced) is out of
    // every outcome; one its own side can keep clean (duck or cover) is out of
    // every outcome that side's strategy allows; one the other side can force
    // now (a forcing lead or a forced ruff) is in every outcome theirs allows.
    // Item 98's pair proofs are this for a pair that both bid, written for
    // one objective; this is the same set of proofs over the rank table, so it
    // reaches three bids, where every bound the twin shape has is switched
    // off.  Honours adversarial_safe/adversarial_doom as M4 does.  Same values
    // and lines; `--no-live-set-proofs` is the control arm.  The argument and
    // the measurement are at live_set_bound in search.cpp.
    bool live_set_proofs = true;

    // THE LIVE-SET DOUBLE-DUMMY BOUND (Oct 2026, ROADMAP item 101).  Same
    // region: two or more bids live, a bid on each side or three bids, full
    // mode in the default direction (the double-dummy engine).  Whatever falls,
    // a side that plays plain double dummy takes at least its double-dummy
    // count D, so the value is at least the worst reachable rank for the far
    // side plus its D tricks, and at most the best one plus what the near
    // side's count leaves -- item 79's reach bound with the trick span
    // narrowed from [0, t] by one engine probe.  Runs at boundaries with at
    // least this many tricks left; 0 is off.  Same values and lines;
    // `--live-set-dd 0` is the control arm.  The sweep that chose the floor
    // is at live_set_bound in search.cpp.
    int live_set_dd_min_t = 6;

    // THE CANONICAL FIRST TRICK OF EVERY ROW (Oct 2026, ROADMAP item 101).
    // solve_moves() scores each row by MTD(f) null windows and then walks the
    // row's line, and each step of the walk asks a node on that line exactly
    // the question one of the row's probes asked it -- "at least v" at a
    // maximiser (the probe that succeeded), "at most v" at a minimiser (the
    // one that failed) -- but of the candidates in CANONICAL order, stopping
    // at the first yes.  The probe asked the same node in the search's own
    // order and stopped at ITS first yes, usually a different card, so the
    // walk proved a second card all over again: 142M of seed-1 #4's 182M walk
    // nodes were the first step alone.  On, the first this-many plies below
    // each row's card try their moves in canonical order in the row's probes,
    // and each walk step hands its candidates' children the plies that remain,
    // so the walk reads back what the probes stored.  Deeper than a trick the
    // order costs the probes' off-line cut nodes more than the walk saves; the
    // sweep that chose 3 is at solve_moves in search.cpp.  Rows that walk no
    // line (values-only) and positions with no bid live keep the search's own
    // order.  Ordering only: same values and lines; `--row-canonical-depth 0`
    // is the control arm.
    int row_canonical_depth = 3;

    // A4 (Sept 2026): with a single live nil and a safe-band window, bound the
    // node by the pair's double-dummy count on the deal with the nil's cards
    // moved to the bottom of every suit (one engine probe).  Runs only at
    // boundaries with at least this many tricks left; 0 is off.  Same answers.
    // 8 measured best (search.cpp, demoted_dd_bound, has the sweep): -3.7%
    // nodes and -3.2% wall on the single-nil deals of the per-card benchmark.
    // `--demoted-dd 0` is the control arm.
    int demoted_dd_min_t = 8;

    // THE BROKEN-BAND CEILING (Oct 2026, ROADMAP item 99): with a single live
    // nil and a window in the BROKEN band -- the question "can the opponents
    // break the nil and hold the pair to x tricks?" -- bound the node by the
    // pair's plain double-dummy count D (one engine probe): a broken outcome
    // leaves the pair at least D, so the value is at most K*K - K*D, and the
    // node fails low whenever D > x.  It is the upper half of
    // dd_one_live_bound, which is off for this shape because its other half
    // (the safe band, now A4's) and its every-depth probes lost on the slowest
    // deals; with the faster engine of item 99 and a floor on the tricks left,
    // this half pays.  Runs at boundaries with at least this many tricks left;
    // 0 is off.  Same answers.  `--broken-dd 0` is the control arm (+18.9%
    // wall on the 88 single-nil deals of item 99, values-only); the sweep is
    // at broken_dd_ceiling in search.cpp.
    int broken_dd_min_t = 10;

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
    // Item 60 step 5: how many nodes solve() will spend computing the trick
    // split for a same-lean deal whose outcome the decision procedure has
    // already settled.  That search does not reach 13 cards (patch 102), so
    // an unbounded default would trade a fast, correct bid mask for a hang on
    // exactly the hands the project cares most about.  On exceeding this the
    // outcome is still reported and the three trick fields stay
    // TRICKS_NOT_COMPUTED -- a smaller answer, never a wrong one.  0 means no
    // limit, for a caller who would rather wait.
    std::uint64_t same_lean_trick_budget = 8u << 20;

    // The same protection for the DECISION PROCEDURE's own probes.  Budgeting
    // only the trick search was a mistake caught by measurement: at 13 cards
    // the bottleneck is upstream, in `solve_cooperative`, whose negative
    // answer has no cutoff (67.6s on a measured random deal) and which used to
    // run unbounded here.  On exhaustion the procedure reports the cell as
    // unresolved -- an honest refusal, never a guess.  0 means no limit.
    std::uint64_t same_lean_probe_budget = 8u << 20;

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
    // worth primary = k*k + 1 and a side trick worth k, and
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

    // Hand every trick-boundary position whose nil question is already settled
    // to the plain double-dummy engine in nil/ddtricks.hpp.  "Settled" means
    // no bid in the objective is still live -- every one is down, or the
    // caller declared it down -- so the packed value from that point is
    // C + W * (far side's tricks) with W > 0, which is ordinary double dummy.
    // Applies to the single already-set nil, a pair that both bid, and one bid
    // per side, in the default direction (each pair takes what it can); the
    // "shed" direction is a different game and stays on the general search.
    // Answer-neutral: the engine returns the same trick counts, and on an
    // exact answer an optimal lead, so values and principal variations are
    // unchanged.  Off with --no-dd-engine / NIL_FLAG_NO_DD_ENGINE.
    bool dd_engine = true;

    // With exactly one bid still live, bound the position by the plain
    // double-dummy count of the live bidder's side (see dd_one_live_bound in
    // search.cpp): surviving caps that side's tricks at the double-dummy
    // count, being broken floors them there.  One or two engine probes per
    // trick boundary, answer-neutral, cuts only.  Gated exactly as dd_engine
    // is.
    //
    // OFF BY DEFAULT since the performance pass of Sept 2026, and the
    // measurement is two-sided, which is why it is parked rather than
    // deleted.  On 50 random and corpus 13-card deals under per-card scoring
    // it wins big on some -- 1.5x to 2.6x on mid-sized opposed and partner
    // deals, where it cuts 15-28% of the boundaries it is asked about -- and
    // loses big on exactly the deals that matter most: the slowest single-nil
    // deal measured goes 94 s -> 215 s with it on, the slowest opposed deal
    // 75 s -> 120 s, the corpus's hard opposed deal 15 s -> 16.5 s.  There its
    // cut rate is 3-10% and the probes cost more than the cuts save.  Neither
    // a per-depth adaptive gate on the observed cut rate, a node budget per
    // probe, nor screening the probe with the static trick floors in
    // bounds.hpp closed that gap; the monsters still lost 9-50%.  Re-measured
    // with the whole pass in place, on its ten hardest deals: 156 s -> 239 s
    // in all with it on -- the slowest single-nil deal 50 s -> 97 s, the
    // slowest opposed deal 64 s -> 96 s -- against wins of 3.2 s -> 1.5 s and
    // 4.5 s -> 3.8 s on two mid-sized opposed deals.  On with
    // --dd-live-bounds for callers whose deals are the kind it wins on; the ABI
    // has no bit to spare for an opt-in, and NIL_FLAG_NO_DD_LIVE_BOUNDS keeps
    // meaning what it says (off), which is now the default.
    bool dd_live_bounds = false;

    // Run the root search before scoring the cards in MODE_FULL's
    // solve_moves(), as it did before the performance pass of Sept 2026.  Off
    // by default: every row owes an exact value, so the per-card loop proves
    // each card from both sides whatever the root search did, and the root's
    // value is the extremum of the rows, which the loop computes anyway (see
    // solve_moves in search.cpp).  MODE_FAST always runs it -- there the root
    // search IS the answer.  On with --moves-root-search; NIL_FLAG_NO_ROW_MTD
    // turns it on together with moves_aspiration off, which restores the old
    // per-card path whole.
    bool moves_root_search = false;

    // A seat with no live bid, following suit while a non-bidding opponent
    // holds the trick, tries the cheapest card that takes it first (see
    // cheap_win_card in search.cpp).  Ordering only, MODE_FULL only; rides on
    // live_order, so NIL_FLAG_NO_LIVE_ORDER turns it off too.  Off with
    // --no-win-order.
    //
    // SUPERSEDED while trick_order is on (the default): every seat this rule
    // orders is ordered by trick_order_moves instead, whose "sure winner
    // first" class contains this card whenever it is sure.  Reached only under
    // --no-trick-order, where it keeps the pre-study tree reproducible.
    bool win_order = true;

    // Order every seat that holds no live bid by a trick-oriented score --
    // the double-dummy engine's own DDS-style rules, with a live bidder's
    // winning card read the right way round for each side -- and put 6b's
    // attacking lead ahead of it for a seat on lead against one live bid (see
    // trick_order_moves in search.cpp).  A live bidder keeps 6a/6d.  Both
    // modes; with it on, win_order and the non-bidder half of live_order are
    // no longer reached.
    //
    // Measured on one binary with this flag the only difference (it takes
    // killer_order below with it), Sept 2026 ordering study (MOVE_ORDERING.md
    // has the tables): per-card full scoring on 155 random, opposed,
    // partner-nil and pathological 13-card deals, nodes -30% and wall time
    // -25% in total, median deal 0.58 s -> 0.37 s, 106 -> 116 of them under a
    // second; MODE_FAST on the 400-deal nil13_verdict corpus, nodes -34% and
    // time -26%.  Not a clean win on every deal -- no ordering is -- and the
    // losses are listed there too.
    //
    // Ordering only: same values, verdicts and principal variations.  Off with
    // --no-trick-order (CLI and nil_bench); NIL_FLAG_NO_LIVE_ORDER turns it off
    // with the rest of the pass's ordering (the ABI has no bit of its own left).
    bool trick_order = true;

    // Try the move that last cut a trick-ordered node at the same depth SECOND
    // at the next such node, behind that node's own first choice -- a killer
    // move, rides on trick_order.  MODE_FULL only.  Measured on top of
    // trick_order over 147 13-card deals under per-card scoring: nodes -6.9%,
    // wall time -5.8%, 55 deals better and 9 worse; first rather than second
    // is 68% WORSE, and in MODE_FAST it is +1.8%.  Ordering only.  Off with
    // --no-killer-order (CLI and nil_bench).
    bool killer_order = true;

    // C1 (Oct 2026 tail study): a live bid holding the trick is left there by
    // the other side unless its partner, still to play, can overtake it by
    // FOLLOWING SUIT.  A partner that can only overtake by ruffing no longer
    // cancels the leave, so the opponents try their losing cards first and
    // make the cover spend a trump to save its nil.  Rides on trick_order;
    // MODE_FULL only.
    // The size-weighted study and the measurements are at trick_order_moves
    // in search.cpp.  Ordering only: same values, verdicts and principal
    // variations.  Off with --no-ruff-overtake-leave (CLI and nil_bench; the
    // ABI has no bit left for it).
    bool ruff_overtake_leave = true;

    // In the MINIMISE direction (minimise_own_tricks), order every seat that
    // holds no live bid by the shed order instead of the trick order: the
    // losing card first, highest first, then the cheapest winner; on a void
    // the highest card that does not win; on lead a score that leads low,
    // away from an opponent's void and into partner's.  See shed_order_moves
    // in search.cpp for the rules and the reasons.  MODE_FULL only -- the nil
    // question has no direction -- and it rides on trick_order.
    //
    // Measured (Oct 2026 study, per-card full scoring, values-only rows,
    // minimise direction, against --no-shed-order): 32 thirteen-card deals of
    // every shape 7,912.7M -> 631.6M nodes (-92.0%) and 1,101.5 -> 63.0 s
    // (-94.3%), all 32 faster, median 9.7 -> 0.8 s; 40 held-out 13-card
    // deals 15,675.3M -> 830.1M (-94.7%) and 1,922.6 -> 70.1 s (-96.4%), all
    // 40 faster, the slowest 1,043.5 -> 14.1 s; 200 positions at 10-12 cards
    // -87.7% nodes; 1,500 at 4-9 cards -71.6%.
    // MTD(f) probes unchanged: the saving is all nodes per probe.
    //
    // Ordering only: same values, verdicts and principal variations.  Off with
    // --no-shed-order (CLI and nil_bench); NIL_FLAG_NO_LIVE_ORDER turns it off
    // with the rest of the ordering, as it does trick_order.
    bool shed_order = true;

    // Under the shed order, an opponent on lead against a SINGLE nil tries the
    // shed order's own first card rather than 6b's attacking lead.  6b stays
    // first against exactly one live bid in the two-bid shapes (opposed and
    // partner nils), and everywhere in the default direction.
    //
    // Where 6b fired in 13-card searches, its card cut first only 58% of the
    // time against 80% for the shed order's own lead.  Taking it off the front
    // against a single nil: -36% of nodes on six 13-card deals, -5.3% on 32
    // (14 better, 4 worse); putting it back on top of the rest, +1.3% over 44
    // (1 better, 7 worse).  In the two-bid shapes taking it off measured -0.6%
    // at 13 cards and -3.7%/+4.2% on two 10-12-card sets, so it stays there.
    //
    // True puts 6b back in front for single-nil shapes too (the control arm,
    // --shed-single-attack).  Ordering only.
    bool shed_single_attack = false;

    // Charge a live bid's primary the moment nil_must_take_a_trick proves it
    // breaks down every line, instead of on the trick where it happens (see
    // charge_for_mask in search.cpp).  The value is unchanged -- the primary is
    // charged once per bid whenever it lands -- but the bid's bit enters the
    // broken mask early, so the one-live-bid and settled machinery apply to a
    // doomed bid's whole subtree.  Full mode only.  Off with --no-doom-charge
    // (CLI and nil_bench; no ABI bit -- see NIL_FLAG_NO_ROW_MTD's note on the
    // flag word).
    bool doom_charge = true;

    // Point the seat-specific ordering rules at the bids that are still LIVE --
    // every live bidder sheds, every seat on lead attacks a live bid opposite --
    // rather than at `nil_seat` alone (see live_bid_promotion in search.cpp).
    // Identical to the old rules while exactly one single-nil bid is live.
    // Ordering only.  Off with --no-live-order / NIL_FLAG_NO_LIVE_ORDER, which
    // also turns off win_order below.
    bool live_order = true;

    // Recover lines by asking each step whether it is worth the value the line
    // already says it is -- a (v - 1, v + 1) window -- instead of re-deriving
    // that value under the caller's window (see canonical_move_for and walk_pv
    // in search.cpp).  The same exact values are compared, so the same
    // canonical line comes back.  Off with --no-tight-pv (CLI and nil_bench; no
    // ABI bit).
    bool tight_pv = true;

    // Score each card in solve_moves() by a sequence of null-window searches
    // (MTD(f)), seeded with the best row so far, instead of one search under the
    // sentinels (see solve_moves in search.cpp).  Every row still gets its exact
    // value.  Full mode only.  Off with --no-moves-aspiration;
    // NIL_FLAG_NO_ROW_MTD turns it off together with moves_root_search on.
    bool moves_aspiration = true;

    // Keep a lower AND an upper bound per transposition-table entry, merging a
    // new store into what the table already holds for the position instead of
    // overwriting it (see TTEntry in tt.hpp).  Memoisation only: same values,
    // same lines.  Off with --no-tt-two-bounds (CLI and nil_bench; no ABI
    // bit), which restores the single-bound table node for node.
    bool tt_two_bounds = true;

    // PER-CALL LIMITS (C0, Oct 2026).  Read by solve_moves() -- the bot's call
    // -- in both modes, by solve() in MODE_FAST (phase 3, Oct 2026: the
    // per-seat fallback, nil_count_set_limited), and by solve_outcome().  Inert
    // in solve()'s MODE_FULL path, which no limited entry point reaches, and
    // stripped from every nested presolve (see without_limits in search.cpp),
    // so a probe a limited call spends is never budgeted twice.  Zero and null
    // are "no limit", which is what every caller that does not set them gets,
    // so the default call is node for node the call it always was.
    //
    // WHY A LIMIT AND NOT ONLY A FASTER SEARCH.  The tail-latency investigation
    // (Oct 2026, §4.4-4.5) found the slow calls are slow because of the whole
    // layout: per-deal times correlate across seatings, static features of the
    // deal predict almost nothing, and re-dealing the 39 unseen cards around a
    // pathological hand never reproduced its time.  So a caller cannot know in
    // advance which determinization will take 80 s, and with calls serialized
    // one such call blocks every call queued behind it.  Nothing inside the
    // search bounds that worst case; a budget does.  What replaces a call that
    // ran out is the caller's decision, which is why the call hands back what
    // it had PROVEN rather than a guess (see MoveScore::known).
    //
    // HOW IT STOPS.  search_core counts nodes as it always has; every
    // LIMIT_CHECK_NODES of them (64K, about 9 ms at 13 cards) it adds what it
    // spent to the call's total and tests the three limits.  When one has run
    // out it throws, and solve_moves catches it.  Unwinding by exception is
    // what makes "stop storing once it fires" free: every frame between the
    // check and solve_moves is abandoned before it reaches its table store, so
    // nothing half-searched is ever written, and the table entries already
    // there are proven bounds like any other.  The double-dummy engine is
    // never interrupted (the check is in search_core, which the engine does
    // not call), so its table stays exactly as trustworthy.  Zero cost when
    // nothing fires: one compare per node against a threshold that is the
    // largest 64-bit number unless a limit is armed.
    //
    //   max_nodes  nodes, counted the way Solution::nodes counts them (engine
    //              nodes included).  Deterministic under a fixed process
    //              history, which makes it the one to test with.
    //   max_ms     wall-clock milliseconds from the start of solve_moves.
    //   cancel     a word another thread may set to non-zero; read at each
    //              check.  The caller owns it and keeps it alive for the call.
    //
    // A limit fires at the first check past it, so a call may overrun by up to
    // one check interval per search context (the main one and up to four
    // handed-off single-nil searches): well under 50 ms at 13 cards.
    std::uint64_t max_nodes = 0;
    std::uint32_t max_ms = 0;
    const volatile std::int32_t* cancel = nullptr;

    // THE PAIR'S PROBES (phase 3, Oct 2026; solve_outcome only).  Before the
    // pair search runs, ask each twin's single-nil question -- that twin as
    // the nil, its partner as the cover -- and spend the two answers as bounds
    // on how many of the pair go down.  See solve_outcome for the argument.
    //
    // PARKED, NOT SHIPPED: OFF by default, on with --outcome-probes (CLI) and
    // NIL_OUTCOME_PROBES (C ABI).  Sound, and measured a loss: on the 26 twin
    // deals of the slow-hands list it cost +49% in total (90.7 s against
    // 61.0 s, the handoff on in both), because both twins are breakable alone
    // on 20 of the 26 and the pair search then runs anyway.  It wins only where
    // one twin is safe alone -- 8.0x on s1-04:3030@1 (0.30 s against 2.39 s) --
    // and nothing cheap tells those deals apart in advance.
    bool outcome_probes = false;

    // THE OUTCOME QUESTION'S ONE-LIVE HANDOFF (phase 3; solve_outcome only).
    // Where one bid alone is still live, the pair or three-bid search hands the
    // position to a single-nil fast search, whose verdict is the exact answer
    // there.  Item 100's idea with the trick term gone; see configure() for the
    // argument.  Off with --no-outcome-handoff (CLI) and NIL_OUTCOME_NO_HANDOFF
    // (C ABI), the control arm.
    bool outcome_handoff = true;
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
    // WHICH bids are down in total, on the same reading as `nils_set` and with
    // the same popcount: `broken_mask` plus the seats the caller declared down.
    // `broken_mask` alone is what the primary level is charged against and is
    // the wrong thing to report, for exactly the reason the two count fields
    // are already kept apart.
    unsigned nils_set_mask = 0;
    int nil_tricks = 0;       // the nil bidder alone
    int nil_side_tricks = 0;  // the nil bidder and its covering partner
    int opponent_tricks = 0;  // the other pair
    // Tricks each seat wins on the replayed line, indexed by absolute seat.
    // Sums to the tricks played.  Within a pair this is ONE optimal line's
    // split, not something the objective pins; see MoveScore::seat_tricks.
    int seat_tricks[4] = {0, 0, 0, 0};
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
    // WHICH bids are broken, as a seat bitmask, where `nils_set` is how many.
    // Bit s is set when the bidder at seat s ends the deal having taken a
    // trick, or was declared down by the caller with ROLE_NIL_SET.  Its popcount
    // is always exactly `nils_set`; a caller that only wants the count can keep
    // reading that field and ignore this one.
    //
    // A count answers "does a nil go down"; a mask answers "does MINE go down",
    // which is the question a player at the table is actually asking and the one
    // a count with two bidders cannot reach.
    //
    // WHETHER THIS IS A PROMISE OR A WITNESS depends on the shape, and
    // `nils_set_mask_determined` is what says which.  See
    // mask_determined_by_objective in nil/seats.hpp.
    unsigned nils_set_mask = 0;
    // Is `nils_set_mask` pinned by the objective, or is it one optimal line's
    // answer among several?  True on every shape where two equally-optimal
    // lines must agree about which bids go down -- one nil, and one bid per
    // side.  False on a pair that both bid, where the primary level counts bids
    // rather than naming them and measurement finds real disagreement.
    //
    // False does NOT mean the mask is wrong: it is a true account of the line
    // this search reported, and its popcount is `nils_set` either way.  It means
    // a differently-ordered search of the same position may name a different
    // bid, so nothing may be pinned to it.
    bool nils_set_mask_determined = true;
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
    // False when solve_moves() stopped on one of SearchOptions' per-call
    // limits (C0) before every row was finished.  The rows then say what each
    // of them had proven (MoveScore::known), the position's own fields are
    // filled only where every row's value was proven, `pv` is empty, and
    // `nodes` is what the call spent before it stopped.  Always true from
    // solve() in MODE_FULL, and from either entry point when no limit was set.
    //
    // ALSO FALSE FROM solve() IN MODE_FAST (phase 3, Oct 2026) when a limit
    // stopped the one boolean search: `nils_set` is then TRICKS_NOT_COMPUTED,
    // the mask holds only the bids the caller declared down, and
    // `nils_set_mask_determined` is false.  A boolean has no partial state, so
    // that is all there is to report.  From solve_moves() in MODE_FAST the
    // rows whose verdict was searched before the stop keep it (ROW_ALL), the
    // rest are ROW_NOTHING, and the position's own verdict is filled when the
    // root search -- which fast mode runs first -- had finished.
    bool complete = true;

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

// Weights that pack the objective into one integer:
//
//     value = primary   * nil_tricks
//           + secondary * nil_side_tricks
//
// which the nil side minimises and the opponents maximise.  With
// K = tricks remaining + 1, primary is K*K (or K*K + 1, below) and secondary is
// +/-K, so the level above strictly outranks the trick term and the two compare
// lexicographically.  primary is zero when the roles say the nil is already set.
//
// THERE WAS A THIRD FIELD HERE AND IT WAS NEVER A THIRD LEVEL (Phase B1b).
// `tertiary` was read in exactly seven places and every one of them read it as
// `primary + tertiary` -- never on its own, never against a threshold of its
// own.  It was a redundant decomposition of ONE coefficient: what a trick taken
// by the nil bidder is worth.  Folding it into `primary` is arithmetically the
// identity, which is why this change moves no node count anywhere.
//
// What it used to decompose: under ROLE_NIL_SET with the pair still taking
// tricks, the old level 3 broke the tie over WHICH partner held the pair's
// tricks.  Decision 1 of the rearchitecture makes a nil bidder's tricks count
// toward its team like anyone else's, so that split has no referent -- B1a
// zeroed the level on that shape and this removes the machinery that read it.
// The one place the old decomposition still carried information is the live
// nil in the maximising direction, where the coefficient is K*K + 1 rather
// than K*K; that +1 is now written into `primary` directly, and the live-nil
// value space is unchanged to the bit.
//
// In MODE_FAST the weights are (1, 0) regardless of every other option, so the
// value is the nil bidder's trick count with nothing packed above or below it.
// Weight 1 rather than K*K on purpose: it makes the alpha-beta window literally
// [0, 1] rather than [0, K*K].
struct ObjectiveWeights {
    int primary = 0;
    int secondary = -1;
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
// What one seat's bid comes to after a card, as MoveScore::seat_status reports
// it.  The values are part of the C ABI (NIL_SEAT_STATUS_*) and of nil_cli's
// output, so they are fixed.
enum SeatStatus : int {
    SEAT_NIL_MAKES = 0,  // bid nil, and the nil survives
    SEAT_NIL_SET = 1,    // bid nil, and the nil is broken (or already was)
    SEAT_NO_NIL = 2,     // did not bid nil
    // A live bid whose fate the row has not proven.  Only on a call stopped
    // by a per-call limit (C0); see MoveScore::known.
    SEAT_UNKNOWN = -1,
};

// HOW MUCH OF A ROW IS PROVEN (C0, Oct 2026).  Every row of a call that ran to
// the end is ROW_ALL.  The other three appear only when solve_moves() stopped
// on a per-call limit (SearchOptions::max_nodes and friends), and each says
// which fields can be trusted; the rest read TRICKS_NOT_COMPUTED (or
// SEAT_UNKNOWN, or a mask short of `nils_set` bits), never a guess.
//
//   ROW_ALL      every field, exactly as an unlimited call reports it.
//   ROW_VALUE    the row's value was proven, so every field the value pins is
//                filled -- exactly what values-only rows report (see
//                SearchOptions::row_lines) -- but its line was not walked:
//                nil_tricks and seat_tricks are not known, and on a pair that
//                both bid with one of them down the mask cannot say which.
//   ROW_OUTCOME  the row's MTD(f) bounds had already settled how many bids
//                fall (and the mask, where the bounds pin it), but not the
//                tricks: nil_side_tricks / opponent_tricks are unknown.
//   ROW_NOTHING  nothing proven: nils_set is TRICKS_NOT_COMPUTED.
//
// The order is the order the per-card call proves things in -- each row's
// bounds close in on its value, then the lines are walked -- so a row's state
// only ever moves up this list as a call is given more budget.
enum RowKnown : int {
    ROW_ALL = 0,
    ROW_VALUE = 1,
    ROW_OUTCOME = 2,
    ROW_NOTHING = 3,
};

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

    // WHICH bids are broken after this card, as a seat bitmask; its popcount is
    // `nils_set`.  This is the field a caller needs to colour a card list by
    // "safe for me" rather than by "somebody's nil dies": with a bid on each
    // side the COUNT is 1 both on the card that kills mine and on the card that
    // kills theirs, and those are opposite advice.
    //
    // Determinacy is a property of the shape rather than of the row, so it is
    // reported once on Solution rather than repeated on all thirteen of these.
    unsigned nils_set_mask = 0;

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

    // Per seat, indexed by absolute seat (N, E, S, W): a SeatStatus read off
    // `nils_set_mask` and the roles.  Filled in every mode, fast included,
    // since the mask is.
    int seat_status[4] = {SEAT_NO_NIL, SEAT_NO_NIL, SEAT_NO_NIL, SEAT_NO_NIL};

    // Per seat, the tricks that seat wins down this card's line, INCLUDING the
    // trick this card completes.  TRICKS_NOT_COMPUTED in MODE_FAST and on
    // values-only rows, which never walk a line.
    //
    // A live nil that makes is pinned at 0.  Every other seat's count is one
    // optimal line's witness: the objective pins each PAIR's total
    // (nil_side_tricks / opponent_tricks) and not how a pair divides it, so a
    // differently-ordered search may move a trick between partners.
    int seat_tricks[4] = {TRICKS_NOT_COMPUTED, TRICKS_NOT_COMPUTED, TRICKS_NOT_COMPUTED,
                          TRICKS_NOT_COMPUTED};

    // How much of this row is proven.  ROW_ALL unless the call stopped on a
    // per-call limit; see RowKnown.
    RowKnown known = ROW_ALL;
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

// ---- THE OUTCOME QUESTION (phase 3, Oct 2026) -------------------------------
//
// WHAT IT ANSWERS.  Which bids go down under best play, and nothing about
// tricks: the primary level of the full objective on its own, the way MODE_FAST
// is that level for one nil.  It exists because the bot's fallback after a
// stopped per-card call needs a TRUE answer it can afford, and the one it had --
// each live bid asked as a single nil -- answers a different question wherever
// two bids interact: a pair that both bid can be breakable one at a time and not
// both at once, and the per-seat question cannot see that.
//
// HOW.  The search is the full objective's own search with its trick term
// switched off -- weights (1, 0) on a count of bids down for a pair, on a step
// of outcome rank for three bids -- so its value takes a handful of integers,
// one per reachable outcome, and is found by null-window questions alone
// (bisection over those integers, each question an AND-OR search like
// MODE_FAST's).  Same objective level, so the same answer as the full solve's
// nils_set by construction; the corpus tests check it on every build.
//
// WHICH SHAPES.  One nil (exactly MODE_FAST), a pair that both bid (either may
// be ROLE_NIL_SET), and three bids.  One bid per side is refused: the full
// solve's presolve already answers it from three fast probes (item 77/78c), and
// a caller wanting it per seat has nil_count_set_limited.  Same-lean is refused
// as everywhere else.
//
// BOUNDED.  SearchOptions' limits apply to the whole call, probes included, and
// a stopped call reports the range it had narrowed the answer to.
struct OutcomeSolution {
    // False when a limit stopped the call before the outcome was pinned.
    bool complete = true;
    // How many bids are down, declared ones included: exact when complete,
    // TRICKS_NOT_COMPUTED otherwise.
    int nils_set = TRICKS_NOT_COMPUTED;
    // What the call had PROVEN about that count: it lies in [min, max].  Equal
    // to nils_set at both ends when complete; on a stopped call, the range the
    // questions answered so far left open.
    int nils_set_min = 0;
    int nils_set_max = 0;
    // Bids proven down and proven to make, as seat bitmasks.  A bid in neither
    // is one the answer does not pin: on a stopped call, anything still open;
    // on a finished one, a pair with exactly one of the two down (the objective
    // counts bids, it does not name them) and the same for the twins of a
    // three-bid deal.  Declared bids are in set_mask from the start.
    unsigned set_mask = 0;
    unsigned made_mask = 0;
    // Which objective level was searched, for the diagnostics: the packed value
    // the final question pinned (count, or rank step), or the range so far.
    int value_lo = 0;
    int value_hi = 0;
    std::uint64_t nodes = 0;
    int questions = 0;  // null-window searches run, probes included
    int probes = 0;     // of which single-nil probes
};

bool solve_outcome(const Position& pos, const SeatRoles& roles, const SearchOptions& opts,
                   OutcomeSolution& out, std::string& err);

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
