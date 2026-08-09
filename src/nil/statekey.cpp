#include "nil/statekey.hpp"

#include "nil/rules.hpp"

namespace nil {
namespace {

inline int popcount13(std::uint32_t x) {
    return static_cast<int>(std::bitset<32>(x).count());
}

// Cards of suit `s` from a 64-bit hand mask, as a 13-bit word.
inline std::uint32_t suit_of(Hand h, int s) {
    return static_cast<std::uint32_t>((h >> (s * 16)) & SUIT_BITS);
}

// A little-endian bit cursor over the 128-bit key.  Widths are never more than
// 26, so the only straddle to worry about is one that starts in `lo` and ends
// in `hi`; a width wider than a whole word cannot happen and the shift by 64
// that would be undefined is therefore unreachable.
struct Packer {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
    int pos = 0;

    void put(std::uint64_t value, int width) {
        if (pos < 64) {
            lo |= value << pos;
            const int room = 64 - pos;
            if (width > room) hi |= value >> room;
        } else {
            hi |= value << (pos - 64);
        }
        pos += width;
    }
};

}  // namespace

RelMove to_relative(CardId card, const SuitProfile& profile) {
    const int s = card_suit(card);
    const std::uint32_t below = (1u << (card & 15)) - 1u;
    const int slot = popcount13(profile.present[s] & below);
    return static_cast<RelMove>(s * 16 + slot);
}

CardId from_relative(RelMove move, const SuitProfile& profile) {
    if (move == REL_NO_MOVE) return NO_CARD;
    const int s = (move >> 4) & 3;
    int slot = move & 15;
    std::uint32_t live = profile.present[s];
    while (slot-- > 0) live &= live - 1u;  // drop the slot lowest live cards
    if (!live) return NO_CARD;
    return s * 16 + lowest_card(static_cast<Hand>(live));
}

bool encode_state_key(const Hand hands[4], int leader, bool broken, const CardId trick[3],
                      int trick_len, StateKey& key, SuitProfile& profile) {
    const Hand all = hands[0] | hands[1] | hands[2] | hands[3];

    // Seat indices are two bits; rather than testing four masks per card, split
    // them into the two bit-planes of the index once and test two.
    const Hand plane0 = hands[SEAT_EAST] | hands[SEAT_WEST];    // seats 1 and 3
    const Hand plane1 = hands[SEAT_SOUTH] | hands[SEAT_WEST];   // seats 2 and 3

    profile = SuitProfile();
    for (int s = 0; s < 4; ++s) {
        profile.present[s] = suit_of(all, s);
        profile.length[s] = popcount13(profile.present[s]);
        profile.total += profile.length[s];
    }

    const int header = 21 + (trick_len > 0 ? 9 : 0);
    if (header + 2 * profile.total > 128) {
        key = StateKey();
        return false;
    }

    Packer pk;
    pk.put(static_cast<std::uint64_t>(leader) & 3u, 2);
    pk.put(broken ? 1u : 0u, 1);
    for (int s = 0; s < 4; ++s) pk.put(static_cast<std::uint64_t>(profile.length[s]), 4);
    pk.put(static_cast<std::uint64_t>(trick_len) & 3u, 2);

    if (trick_len > 0) {
        const int led = card_suit(trick[0]);
        int winner = 0;
        for (int i = 1; i < trick_len; ++i) {
            if (beats(trick[i], trick[winner])) winner = i;
        }
        const CardId best = trick[winner];
        const int best_suit = card_suit(best);
        const bool ruffed = best_suit == SUIT_SPADES && led != SUIT_SPADES;
        // How many cards that are still out there sit below the card currently
        // winning.  A live card of `best_suit` beats it exactly when its own
        // slot index is >= this, which is all that survives of its rank.
        const std::uint32_t below = (1u << (best & 15)) - 1u;
        const int gap = popcount13(profile.present[best_suit] & below);

        pk.put(static_cast<std::uint64_t>(led) & 3u, 2);
        pk.put(static_cast<std::uint64_t>(winner) & 3u, 2);
        pk.put(ruffed ? 1u : 0u, 1);
        pk.put(static_cast<std::uint64_t>(gap) & 15u, 4);
    }

    for (int s = 0; s < 4; ++s) {
        std::uint32_t live = profile.present[s];
        const std::uint32_t p0 = suit_of(plane0, s);
        const std::uint32_t p1 = suit_of(plane1, s);
        std::uint64_t owners = 0;
        int slot = 0;
        while (live) {
            const std::uint32_t low = live & (0u - live);
            if (p0 & low) owners |= 1ull << (2 * slot);
            if (p1 & low) owners |= 1ull << (2 * slot + 1);
            live ^= low;
            ++slot;
        }
        pk.put(owners, 2 * profile.length[s]);
    }

    key.lo = pk.lo;
    key.hi = pk.hi;
    return true;
}

std::uint64_t mix_key(const StateKey& key) {
    // splitmix64's finaliser, twice: cheap, and every input bit reaches every
    // output bit, which matters because the low bits of the key are a small
    // dense header that would otherwise cluster in the table.
    const auto mix = [](std::uint64_t z) {
        z += 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    return mix(key.lo ^ mix(key.hi + 0x165667B19E3779F9ull));
}

}  // namespace nil
