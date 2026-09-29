// Bit-packing helpers for the two position keys: the main transposition
// table's (statekey.cpp) and the double-dummy engine's (ddtricks.cpp).
//
// WHY THESE EXIST
// ---------------
// Both keys describe a position up to a relabelling of ranks, and both spell
// that the same way: for each suit, the OWNER of every live card, two bits a
// card, lowest live card first.  N is 0, E is 1, S is 2, W is 3, so bit 0 of an
// owner is "East or West" and bit 1 is "South or West".
//
// Both used to build it one card at a time -- find the next live card, test
// which hand holds it, shift, repeat -- which is a loop of up to thirteen
// iterations per suit with a variable shift in each.  Profiled on the hard
// 13-card deals the two loops together were 15-25% of wall time: the main key
// is built at every trick boundary the search expands, and the engine's at
// every trick boundary it visits.
//
// The same answer falls out of two whole-suit operations.  Take the plane of
// cards whose owner has bit 0 set, drop the ranks nobody holds any more
// (compress13), and move bit j of what is left to bit 2j (spread13); do the same
// for bit 1 and shift it up one.  Their OR is the owners string, identical bit
// for bit, because compressing a plane under the live mask is exactly the
// "slot" numbering the loop counted out.  Two table lookups per compress and
// two per spread, against a loop per card.
//
// The suit lengths go the same way: `lane_counts` popcounts the four 16-bit
// suit lanes of a hand in one SWAR pass, where the old code made sixteen
// separate popcount calls per engine key.
#ifndef NIL_BITPACK_HPP
#define NIL_BITPACK_HPP

#include <cstdint>

#include "nil/cards.hpp"

namespace nil {
namespace bitpack {

struct Tables {
    // compress7[m][x]: the bits of x at the positions m selects, packed down
    // to the bottom -- PEXT on seven bits.
    std::uint8_t compress7[128][128];
    // spread7[v]: bit j of v moved to bit 2j.
    std::uint16_t spread7[128];

    Tables() {
        for (unsigned m = 0; m < 128; ++m) {
            for (unsigned x = 0; x < 128; ++x) {
                unsigned out = 0;
                unsigned k = 0;
                for (unsigned b = 0; b < 7; ++b) {
                    if ((m >> b) & 1u) {
                        out |= ((x >> b) & 1u) << k;
                        ++k;
                    }
                }
                compress7[m][x] = static_cast<std::uint8_t>(out);
            }
        }
        for (unsigned v = 0; v < 128; ++v) {
            unsigned out = 0;
            for (unsigned b = 0; b < 7; ++b) out |= ((v >> b) & 1u) << (2 * b);
            spread7[v] = static_cast<std::uint16_t>(out);
        }
    }
};

// Built once, during static initialisation, before anything can search.  An
// inline variable rather than a function-local static so that reading it
// costs no initialisation guard.
inline const Tables TABLES;

// The four 16-bit suit lanes of `h`, each replaced by its own popcount.
inline std::uint64_t lane_counts(Hand h) {
    h = h - ((h >> 1) & 0x5555555555555555ull);
    h = (h & 0x3333333333333333ull) + ((h >> 2) & 0x3333333333333333ull);
    h = (h + (h >> 4)) & 0x0F0F0F0F0F0F0F0Full;
    return (h + (h >> 8)) & 0x001F001F001F001Full;
}

// The bits of `x` (13 wide) at the positions live in `m`, packed to the bottom.
inline unsigned compress13(unsigned x, unsigned m) {
    const unsigned lo = TABLES.compress7[m & 127u][x & 127u];
    const unsigned hi = TABLES.compress7[(m >> 7) & 63u][(x >> 7) & 63u];
    return lo | (hi << count_cards(static_cast<Hand>(m & 127u)));
}

// Bit j of `v` (13 wide) moved to bit 2j.
inline std::uint32_t spread13(unsigned v) {
    return static_cast<std::uint32_t>(TABLES.spread7[v & 127u]) |
           (static_cast<std::uint32_t>(TABLES.spread7[(v >> 7) & 63u]) << 14);
}

// The owners string of one suit: two bits per live card, lowest card first,
// N = 0, E = 1, S = 2, W = 3.  `plane0` holds the suit's cards held by E or W,
// `plane1` those held by S or W, and `live` every card of the suit still held.
inline std::uint32_t owners13(unsigned plane0, unsigned plane1, unsigned live) {
    return spread13(compress13(plane0, live)) | (spread13(compress13(plane1, live)) << 1);
}

}  // namespace bitpack
}  // namespace nil

#endif  // NIL_BITPACK_HPP
