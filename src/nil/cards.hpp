// Cards, hands, and the canonical ordering used for tie-breaking.
//
// A card is a bit index into a 64-bit hand mask:
//
//     bit index = suit * 16 + (rank - 2)
//
// with suits ordered S < H < D < C (PBN order) and ranks 2 < ... < A.  Only 13
// of every 16 bits are used, which keeps suit extraction to a shift and lets a
// suit be masked off with a single constant.
//
// Iterating a mask from the least significant bit therefore walks cards in
// (suit index, rank) order -- exactly the canonical order nil_oracle.py uses
// for its principal-variation tie-break.  Keeping the two enumerations
// identical is what lets us diff PVs and not just values.
#ifndef NIL_CARDS_HPP
#define NIL_CARDS_HPP

#include <bitset>
#include <cstdint>
#include <string>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace nil {

enum Suit : int {
    SUIT_SPADES = 0,
    SUIT_HEARTS = 1,
    SUIT_DIAMONDS = 2,
    SUIT_CLUBS = 3,
};

enum Seat : int {
    SEAT_NORTH = 0,
    SEAT_EAST = 1,
    SEAT_SOUTH = 2,
    SEAT_WEST = 3,
};

inline const char* const SUIT_CHARS = "SHDC";
inline const char* const RANK_CHARS = "23456789TJQKA";
inline const char* const SEAT_CHARS = "NESW";

using CardId = int;            // 0..63, see the note above
using Hand = std::uint64_t;    // set of CardIds

inline constexpr Hand SUIT_BITS = 0x1FFFull;  // 13 ranks
inline constexpr CardId NO_CARD = -1;

inline constexpr Hand suit_mask(int suit) { return SUIT_BITS << (suit * 16); }
inline constexpr int card_suit(CardId c) { return c >> 4; }
inline constexpr int card_rank(CardId c) { return (c & 15) + 2; }
inline constexpr CardId make_card(int suit, int rank) { return suit * 16 + (rank - 2); }
inline constexpr Hand card_bit(CardId c) { return 1ull << c; }

inline int count_cards(Hand h) { return static_cast<int>(std::bitset<64>(h).count()); }

// Index of the lowest set bit; h must be non-zero.
inline int lowest_card(Hand h) {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long idx;
    _BitScanForward64(&idx, h);
    return static_cast<int>(idx);
#elif defined(_MSC_VER)
    unsigned long idx;
    if (_BitScanForward(&idx, static_cast<unsigned long>(h))) return static_cast<int>(idx);
    _BitScanForward(&idx, static_cast<unsigned long>(h >> 32));
    return static_cast<int>(idx) + 32;
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(h);
#else
    int i = 0;
    while (!((h >> i) & 1ull)) ++i;
    return i;
#endif
}

// Pop and return the canonically lowest card of `h`.
inline CardId take_lowest(Hand& h) {
    const int b = lowest_card(h);
    h &= h - 1;
    return b;
}

std::string card_to_string(CardId c);
std::string hand_to_string(Hand h);  // "SA SK H3" in canonical order

// "SA", "hT", "D10" (suit letter first, as in nil_oracle.py's card_from_str).
bool parse_card(const std::string& text, CardId& out, std::string& err);

// Whitespace/comma separated list of cards, e.g. "H4 HK".
bool parse_cards(const std::string& text, CardId* out, int max_out, int& count,
                 std::string& err);

// 'N'/'E'/'S'/'W' (case insensitive); returns -1 if unrecognised.
int parse_seat(const std::string& text);

}  // namespace nil

#endif  // NIL_CARDS_HPP
