// The smallest description of a position that still determines its value.
//
// WHY THIS EXISTS
// ---------------
// The old memo keyed on the literal search state: four 64-bit hand masks plus a
// packed word of leader/trick/broken, 288 bits per node, hashed into an
// unordered_map.  It is exact, but it is also the most verbose possible key, and
// it treats two positions as different whenever any irrelevant detail differs.
//
// This key is built the other way round: start from the question "what can two
// positions differ in without differing in value?" and throw away everything
// else.  Three things fall out.
//
// 1. ABSOLUTE RANKS DO NOT MATTER, ONLY RELATIVE ONES.
//    Once the SK and SQ have been played, holding the SA and the SJ is the same
//    position as holding the SA and the SK: the same cards beat the same cards.
//    So each suit is compressed to the cards still in the four hands, numbered
//    0..k-1 from the bottom, and only the OWNER of each surviving slot is
//    recorded -- 2 bits per live card.  This is the "relative rank" idea from
//    Chang, "Building a Fast Double-Dummy Bridge Solver" (NYU TR1996-725), and
//    it is where nearly all of the collapsing comes from: it makes positions
//    reached by wildly different play equal, not merely similar.
//
// 2. WHO OWNS WHAT IS ALL THE HANDS ARE FOR.
//    Four 13-bit masks per suit is 52 bits; an owner index per live card is 2.
//    At 5 cards each that is 40 bits of payload against 256 bits of hand masks.
//
// 3. THE CARDS ALREADY ON THE TRICK ARE ALMOST ENTIRELY IRRELEVANT.
//    They are out of every hand.  The only thing the rest of the deal needs from
//    them is: which suit was led, who is currently winning, and how good the
//    winning card is.  "How good" is not a rank -- it is a THRESHOLD, and the
//    only thing that can be compared against it is a card still in someone's
//    hand.  So it is stored as `gap`, the number of live cards of that suit that
//    are below it.  Two tricks where different cards were played but the same
//    number of survivors are trapped underneath are the same position, which the
//    old exact key could never see.
//
// LAYOUT (little-endian bit cursor, header first)
// ----------------------------------------------
//     bits  0.. 1   leader
//     bit      2    spades broken
//     bits  3.. 6   live cards in spades      (0..13)
//     bits  7..10   live cards in hearts
//     bits 11..14   live cards in diamonds
//     bits 15..18   live cards in clubs
//     bits 19..20   cards already on the trick (0..3)
//     -- the next 9 bits are present only when the trick is non-empty --
//     bits 21..22   suit led
//     bits 23..24   winner's offset from the leader
//     bit     25    the winning card is a ruff (a spade on a non-spade lead)
//     bits 26..29   `gap`: live cards of the winning card's suit below it
//     -- payload --
//     then, for spades, hearts, diamonds and clubs in turn, 2 bits per live
//     card from the bottom up: the seat that holds it.
//
// So the key costs 21 + 2n bits at the start of a trick and 30 + 2n in the
// middle of one, where n is the number of cards still in hands.  Five cards
// each is 61 bits: the whole endgame, where all the nodes are, keys into a
// single machine word with the high word left at zero.
//
// The trick-length field is redundant -- validate() guarantees n = 4*T - j, so
// j = 4*ceil(n/4) - n and two bits could be saved.  It is written anyway
// because those two bits are the only thing standing between a future
// loosening of validate() and silently ambiguous keys, and an ambiguous key is
// a wrong answer, not a slow one.
//
// WHAT IS DELIBERATELY *NOT* COLLAPSED, AND WHY
// ---------------------------------------------
// Hearts, diamonds and clubs are interchangeable -- only spades are special --
// so sorting the three side suits into a canonical order is a real symmetry of
// the game and would collapse up to six more positions into one.  It has been
// built and measured TWICE, once before patch 30 and again as patch 42, and it
// is not worth it either time.  The two rejections do not share a reason, and
// the second is the one that closes the item.
//
// FIRST MEASUREMENT, when the key was built at every node:
//
//     6 cards, corpus     44,309 -> 41,942 nodes/position   -5.3%
//     7 cards, random    484,469 -> 447,316 nodes/position   -7.7%
//
// bought at roughly 12% throughput, because the sort ran at every node.  Net
// wall time came out slightly WORSE.  That is a verdict about the PRICE, and it
// used to end with "not worth revisiting unless the key computation gets much
// cheaper".
//
// It then got much cheaper, which is why this was reopened.  Patch 30 confined
// the key to trick boundaries -- 38.6% of nodes -- so the same 12% is charged
// against two nodes in five rather than five in five.  Patch 25's canonical
// re-derivation retires the tie-break hazard below.  And the symmetry AVAILABLE
// grew rather than shrank: a non-spade lead pins its suit and leaves a group of
// order two, and a trick boundary has no lead, so patch 30 deleted exactly the
// nodes where the group was small.  The prediction was -5% to -8% of nodes for
// about 4.6% of throughput.
//
// SECOND MEASUREMENT, patch 42, with all of that true:
//
//     13 cards, full     93,581,425 -> 93,269,285 nodes      -0.33%
//     13 cards, fast     32,963,937 -> 32,839,732 nodes      -0.38%
//
// bought at 3.2% of throughput in full mode and 9.6% in fast, and slower in 7
// of 7 interleaved reps on both.  **The cost model was right and the benefit
// model was wrong by an order of magnitude.**
//
// WHY, and it is a ceiling rather than a fact about the implementation.
// `--tt-stats` counts the DISTINCT POSITIONS STORED, which is exactly what a
// symmetry that identifies positions is supposed to reduce.  Canonicalisation
// reduces it by 0.53-0.66%, on every workload measured, in both modes.  A group
// of order six identifies about six positions in a thousand.
//
// The reason is the first of the two this comment used to give, and patch 30
// did not touch it: every player's holding in a suit is a subset of what they
// were dealt, so one line's residual hearts can only look like another line's
// residual diamonds once both suits are nearly exhausted -- which is exactly
// where the subtrees are cheap.  The first measurement's 5.3% came from a tree
// twenty to three hundred times larger, full of those cheap nearly-exhausted
// positions.  The tree left after patches 22 through 41 is made of hard ones.
//
// So no cheaper sort revives this.  A perfect zero-cost implementation is worth
// under one percent against a measured cost of three to ten, and the number
// that says so is the collapse count rather than the timing.
//
// The correctness cost this comment recorded has, for the record, EXPIRED.  The
// canonical move order is suit-major, so the tie-break between two equally good
// cards in DIFFERENT suits depends on which suit is which, and a move read back
// from an entry stored under a permuted labelling can be the other equally good
// card: still legal, still optimal, still replay-verified, but no longer the
// card nil_oracle.py picks.  Patch 25's canonical re-derivation walks the
// principal variation and re-derives every step, so that move can no longer
// reach the reported PV.  It is left written down because it is one of the
// reasons the item looked reopenable, and because any future symmetry over this
// key inherits the same hazard and the same fix.
//
// Patch 42's implementation was verified before it was timed, and the result is
// worth keeping even though nothing shipped: a harness over 20,000 permuted
// pairs -- random deals of one to six cards, all six side-suit permutations
// applied to the hands -- confirms every permuted pair produces a bit-identical
// key, so the 0.53-0.66% is the whole collapse and not a partial one.
#ifndef NIL_STATEKEY_HPP
#define NIL_STATEKEY_HPP

#include <cstdint>

#include "nil/cards.hpp"

namespace nil {

// The fingerprint itself.  Compared whole, never decoded.
struct StateKey {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;

    bool operator==(const StateKey& o) const { return lo == o.lo && hi == o.hi; }
    bool operator!=(const StateKey& o) const { return !(*this == o); }
};

// The live cards of each suit, in absolute rank space (bit r is rank r+2).
// Encoding produces this anyway, and it is the dictionary needed to translate a
// relative move back into a real card, so it comes back out rather than being
// recomputed.
struct SuitProfile {
    std::uint32_t present[4] = {0, 0, 0, 0};
    int length[4] = {0, 0, 0, 0};
    int total = 0;  // cards still in the four hands
};

// A card named by its position among the live cards of its suit rather than by
// its rank: suit * 16 + relative rank.  This is what a transposition table can
// store, because it survives the relabelling that makes two positions equal.
using RelMove = std::uint8_t;
inline constexpr RelMove REL_NO_MOVE = 0xFFu;

// `card` must be live in `profile` (every legal move is).
RelMove to_relative(CardId card, const SuitProfile& profile);

// Inverse of to_relative against a possibly different but equivalent position.
// Returns NO_CARD for REL_NO_MOVE or an out-of-range slot.
CardId from_relative(RelMove move, const SuitProfile& profile);

// Builds the key and the profile.  Returns false when the position needs more
// than 128 bits, which happens only within the first trick of a full 13-card
// deal -- where no two lines have yet converged, so there is nothing to look up
// anyway.  `profile` is filled in either way.
//
// `trick` holds the cards already played to the current trick in play order
// from `leader`; those cards must already be absent from `hands`.
// `nils_broken` is the set of nil bidders that have already taken a trick, and
// `carry_nils_broken` says whether it belongs in the key at all.
//
// IT IS OPTIONAL BECAUSE THE SINGLE-NIL KEY MUST NOT MOVE.  Under one bid the
// objective is additive and a position's value does not depend on how it was
// reached, so the mask is not part of the position and packing it would waste
// four bits of a 128-bit budget -- and, worse, shift every field after it and
// change which positions fit, which would move node counts that a dozen
// measurements are banked against.  Under two bids the value DOES depend on
// which bids are already down, so two layouts identical in cards are different
// positions and the key has to say so.
//
// The four bits are spent only when carried, so the budget check below is
// unchanged for one bid and tightened by four bits for two.  At thirteen cards
// that costs the opening position, which was already unkeyable and has nothing
// to transpose with anyway.
bool encode_state_key(const Hand hands[4], int leader, bool broken, const CardId trick[3],
                      int trick_len, StateKey& key, SuitProfile& profile,
                      unsigned nils_broken = 0, bool carry_nils_broken = false);

// Avalanche both words into one hash.  Table index and verification are
// separate: the index comes from this, equality is checked against the full
// 128-bit key, so a hash collision costs a probe and never an answer.
std::uint64_t mix_key(const StateKey& key);

}  // namespace nil

#endif  // NIL_STATEKEY_HPP
