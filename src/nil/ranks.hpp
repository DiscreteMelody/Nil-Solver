// Which card ranks a subtree's value actually depended on.
//
// WHY THIS EXISTS
// ---------------
// statekey.hpp already throws away absolute ranks and keeps relative ones,
// which is Chang's idea and where most of the collapsing comes from.  What it
// still records is the OWNER of every live card, two bits each.  Two positions
// differing only in which hand holds an irrelevant deuce are two entries.
//
// DDS makes them one, and section 6.1 says how: during the search it records
// which cards won a trick BY RANK -- the heart A that beat three hearts, but
// not the spade A that won a trick nobody could follow -- backs those ranks up
// through the tree, and stores only the ranks at or above the lowest winner in
// each suit.  Everything below is masked out and matches anything.  Ginsberg's
// partition search is the same idea under another name.
//
// THE REPRESENTATION
// ------------------
// A subtree's essential set is a `Hand`: the cards whose ranks its value
// depended on.  Union is `|`, which is what makes the merges in search.cpp one
// instruction each.
//
// What is STORED is coarser, and deliberately so.  Per DDS 6.3, an entry
// records "all ranks above and including the lowest" essential one in each
// suit, so the whole of a suit's requirement collapses to ONE number:
//
//     keep[s] = how many of suit s's live cards, counted from the top, must
//               have their owner pinned.  Zero means the suit's ranks did not
//               matter at all and only the per-hand counts do.
//
// keep is invariant under the relabelling statekey.hpp performs -- it counts
// slots, not ranks -- which is exactly what a value read back out of a
// transposition table needs.  A `Hand` is not: the same slot is a different
// card in a different-but-equivalent position.  So essential sets travel UP the
// tree as Hands and are recorded as keep vectors, and a table hit converts back
// the other way with keep_to_essential().
//
// WHAT IT IS FOR
// --------------
// `need` -- the largest keep over the four suits -- is the population
// measurement ROADMAP item 31 asks for before any table is redesigned.  An
// entry with need <= T is one that a table pinning only the top T slots per
// suit could hold without lying.  The histogram over `need` says how much of
// the table such a design would actually reach, and it costs no table redesign
// to collect.
//
// SOUNDNESS OF THE BACKUP, in one paragraph.  Every objective this solver has
// is a function of the trick winners alone: MODE_FAST's value counts the ones
// that are the nil bidder, MODE_FULL's packs that together with the pair's
// count and the split between the two partners.  So it is enough that a
// position matching an entry's pinned slots reproduces every trick winner.  A
// trick's winner is decided by rank only when a second card of the winner's
// suit was played to it; when it was, that winner is recorded, and the losers
// it beat are all below it and therefore inside the don't-care region, where
// the exact per-hand card counts -- which statekey.hpp keeps -- are the only
// thing the rest of the deal can still ask about them.
#ifndef NIL_RANKS_HPP
#define NIL_RANKS_HPP

#include <cstdint>

#include "nil/cards.hpp"
#include "nil/statekey.hpp"

namespace nil {

// keep[s] for the four suits, one nibble each, suit-major from spades.  Fits a
// table entry's spare bits and compares in one instruction.
using KeepVector = std::uint16_t;

inline constexpr KeepVector KEEP_ALL = 0xDDDDu;  // 13 pinned slots in every suit

inline int keep_of(KeepVector k, int suit) { return (k >> (suit * 4)) & 15; }

inline KeepVector keep_set(KeepVector k, int suit, int value) {
    return static_cast<KeepVector>((k & ~(15u << (suit * 4))) |
                                   (static_cast<unsigned>(value) << (suit * 4)));
}

// The largest per-suit requirement, which is the truncation level an entry
// needs.  `need == 0` is a position whose value did not depend on any rank at
// all -- only on the suit distribution -- and those are the entries a masked
// table collapses hardest.
inline int keep_need(KeepVector k) {
    int need = 0;
    for (int s = 0; s < 4; ++s) {
        const int v = keep_of(k, s);
        if (v > need) need = v;
    }
    return need;
}

// Turn an essential card set into the keep vector that covers it.
//
// Only the LOWEST essential card of each suit is read, because pinning it pins
// everything above it too -- that is the widening DDS 6.3 describes, and doing
// it here rather than at the comparison keeps the stored form one nibble wide.
//
// `essential` must be a subset of the live cards `profile` describes.  At a
// trick boundary every card played anywhere below is still live here, which is
// why the backup is only ever consumed at boundaries.
inline KeepVector essential_to_keep(Hand essential, const SuitProfile& profile) {
    KeepVector keep = 0;
    for (int s = 0; s < 4; ++s) {
        const Hand in_suit = essential & suit_mask(s);
        if (!in_suit) continue;  // nothing in this suit mattered: keep[s] = 0
        const CardId lowest = lowest_card(in_suit);
        const std::uint32_t below = (1u << (lowest & 15)) - 1u;
        const int slot = count_cards(static_cast<Hand>(profile.present[s] & below));
        keep = keep_set(keep, s, profile.length[s] - slot);
    }
    return keep;
}

// The inverse, against a possibly different but equivalent position: the top
// keep[s] live cards of each suit.  A table hit hands back a keep vector, and
// this is how the hitting node tells its parent which of ITS cards were pinned.
inline Hand keep_to_essential(KeepVector keep, const SuitProfile& profile) {
    Hand essential = 0;
    for (int s = 0; s < 4; ++s) {
        int want = keep_of(keep, s);
        if (want <= 0) continue;
        if (want > profile.length[s]) want = profile.length[s];
        // Peel from the top: the pinned region is the highest `want` slots.
        // highest_card() rather than a bit-scan intrinsic spelled out here --
        // cards.hpp already carries the four-compiler version of this, and an
        // open-coded __builtin_clz compiled fine on GCC and broke the MSVC leg
        // of the build outright.
        Hand live = static_cast<Hand>(profile.present[s]);
        for (int i = 0; i < want && live; ++i) {
            const int top = highest_card(live);
            essential |= card_bit(s * 16 + top);
            live &= ~(1ull << top);
        }
    }
    return essential;
}

// How coarse the entries a search produced turned out to be.
//
// `by_need[n]` counts stores whose keep vector needed n pinned slots in its
// worst suit; `by_need_at[c][n]` splits the same count by how many cards were
// still in the four hands, because an item that only pays in the deep endgame
// and an item that pays at the root are different items.
struct RankMaskStats {
    static constexpr int MAX_NEED = 14;
    static constexpr int MAX_CARDS = 53;

    std::uint64_t stores = 0;
    std::uint64_t by_need[MAX_NEED] = {};
    std::uint64_t by_need_at[MAX_CARDS][MAX_NEED] = {};
    // Sum of keep over the four suits, against the total live cards: the
    // fraction of owner bits a masked entry would still have to pin.
    std::uint64_t pinned_slots = 0;
    std::uint64_t live_slots = 0;

    void record(KeepVector keep, int cards) {
        int need = keep_need(keep);
        if (need >= MAX_NEED) need = MAX_NEED - 1;
        ++stores;
        ++by_need[need];
        if (cards >= 0 && cards < MAX_CARDS) ++by_need_at[cards][need];
        for (int s = 0; s < 4; ++s) pinned_slots += static_cast<unsigned>(keep_of(keep, s));
        live_slots += static_cast<unsigned>(cards);
    }

    void merge(const RankMaskStats& o) {
        stores += o.stores;
        pinned_slots += o.pinned_slots;
        live_slots += o.live_slots;
        for (int n = 0; n < MAX_NEED; ++n) by_need[n] += o.by_need[n];
        for (int c = 0; c < MAX_CARDS; ++c)
            for (int n = 0; n < MAX_NEED; ++n) by_need_at[c][n] += o.by_need_at[c][n];
    }
};

}  // namespace nil

#endif  // NIL_RANKS_HPP
