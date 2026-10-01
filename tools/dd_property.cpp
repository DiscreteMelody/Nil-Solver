// Property test for nil/ddtricks: the double-dummy engine against an
// independent brute-force minimax, on random positions.
//
//   dd_property <cases> <max tricks per hand> <seed> [variants]
//
// What it checks, for every position:
//   * ns_exact returns the brute-force N/S trick count;
//   * ns_reach answers every target 0..t+1 the same way the count implies;
//   * the lead ns_exact reports is optimal: brute force from the position
//     after it gives the same count.
//
// The reference shares nothing with the engine but nil/rules.hpp (legality,
// breaking, trick winner) -- no relative ranks, no bounds, no ordering, no
// collapsing -- and memoises on the EXACT position only.  The engine's table
// is deliberately never cleared between positions: its key is claimed to be
// valid across deals, and this is where that claim gets exercised.
//
// Deals are drawn from a shrunken deck (a random subset of ranks per suit) so
// that small hands still see long suits, voids, ruffs, and spades both broken
// and unbroken.
//
// VARIANTS.  With a fourth argument, each position is followed by that many
// variants that keep every hand's suit lengths and the owners of the top few
// cards of each suit (a random number per suit) and deal the cards below them
// out again among the same hands.  Those are exactly the positions a stored
// fact may be read back on -- the engine's table generalizes over low cards,
// DDS's "winning ranks" -- so the variants are checked the same way, against a
// fresh brute force each.  Independent random positions almost never share
// suit lengths and top cards with something already in the table, which is how
// an unsound generalization (a refuted class of equal cards straddling the
// pinned ranks; see the end of Engine::search) survived this test without the
// variants.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "nil/cards.hpp"
#include "nil/ddtricks.hpp"
#include "nil/rules.hpp"

using namespace nil;

namespace {

struct RefPos {
    Hand h[4];
    CardId trick[4];
    int leader;
    int len;
    bool broken;
};

// The reference's memo: an open-addressed table on the EXACT position, stamped
// with a generation so starting a new position costs nothing.  (A
// std::unordered_map keyed on a heap-allocated vector was the first version;
// its per-lookup allocation and O(buckets) clear() made this test take ten
// minutes under MSVC.)  Leader and breaking state ride in padding bits of the
// first hand, which a Hand never uses.
struct MemoSlot {
    Hand h[4];
    std::uint32_t gen;
    std::int8_t value;
};
constexpr std::size_t MEMO_SLOTS = std::size_t{1} << 20;
std::vector<MemoSlot> memo(MEMO_SLOTS);
std::uint32_t memo_gen = 1;
std::size_t memo_used = 0;

void memo_new_position() {
    ++memo_gen;
    memo_used = 0;
}

std::size_t memo_index(const Hand k[4]) {
    std::uint64_t x = k[0] * 0x9E3779B97F4A7C15ull;
    x ^= (k[1] + 0x632BE59BD9B4E019ull) * 0xC2B2AE3D27D4EB4Full;
    x ^= (k[2] + 0x85EBCA77C2B2AE63ull) * 0x165667B19E3779F9ull;
    x ^= (k[3] + 0x27D4EB2F165667C5ull) * 0xD6E8FEB86659FD93ull;
    x ^= x >> 29;
    return static_cast<std::size_t>(x) & (MEMO_SLOTS - 1);
}

// Returns the slot holding `k`, or the empty slot where it belongs, or null
// when the table is too full to be worth probing (the search then simply
// recomputes -- slower, never wrong).
MemoSlot* memo_find(const Hand k[4]) {
    std::size_t i = memo_index(k);
    for (int probes = 0; probes < 64; ++probes, i = (i + 1) & (MEMO_SLOTS - 1)) {
        MemoSlot& m = memo[i];
        if (m.gen != memo_gen) return &m;
        if (m.h[0] == k[0] && m.h[1] == k[1] && m.h[2] == k[2] && m.h[3] == k[3]) return &m;
    }
    return nullptr;
}

// N/S tricks from here under optimal play, brute force.
int reference(const RefPos& p) {
    const Hand all = p.h[0] | p.h[1] | p.h[2] | p.h[3];
    if (!all && p.len == 0) return 0;
    Hand key[4] = {p.h[0] | (static_cast<Hand>(p.leader | (p.broken ? 4 : 0)) << 61), p.h[1],
                   p.h[2], p.h[3]};
    if (p.len == 0) {
        if (MemoSlot* m = memo_find(key)) {
            if (m->gen == memo_gen) return m->value;
        }
    }
    const int seat = (p.leader + p.len) & 3;
    const bool ns = (seat & 1) == 0;
    const int led = p.len ? card_suit(p.trick[0]) : -1;
    Hand moves = legal_moves(p.h[seat], p.len, led, p.broken);
    int best = ns ? -1 : 1000;
    while (moves) {
        const CardId c = take_lowest(moves);
        RefPos q = p;
        q.h[seat] &= ~card_bit(c);
        q.broken = spades_broken_after(p.broken, card_suit(c));
        int gained = 0;
        if (p.len < 3) {
            q.trick[p.len] = c;
            q.len = p.len + 1;
        } else {
            const CardId played[4] = {p.trick[0], p.trick[1], p.trick[2], c};
            const int w = trick_winner(p.leader, played, 4);
            q.leader = w;
            q.len = 0;
            gained = (w & 1) == 0 ? 1 : 0;
        }
        const int v = gained + reference(q);
        if (ns ? v > best : v < best) best = v;
    }
    if (p.len == 0 && memo_used < MEMO_SLOTS / 2) {
        if (MemoSlot* m = memo_find(key)) {
            if (m->gen != memo_gen) ++memo_used;
            for (int i = 0; i < 4; ++i) m->h[i] = key[i];
            m->gen = memo_gen;
            m->value = static_cast<std::int8_t>(best);
        }
    }
    return best;
}

// One position against the reference: the exact count, every zero-window
// target, and the reported lead.  `got`, `want` and `best` are for the report.
bool check_position(const Hand h[4], int leader, bool broken, int t, int& got, int& want,
                    CardId& best) {
    RefPos p{};
    for (int s = 0; s < 4; ++s) p.h[s] = h[s];
    p.leader = leader;
    p.len = 0;
    p.broken = broken;
    memo_new_position();
    want = reference(p);

    best = NO_CARD;
    got = dd::engine().ns_exact(h, leader, broken, 0, t, &best);
    if (got != want) return false;
    for (int target = 0; target <= t + 1; ++target) {
        if (dd::engine().ns_reach(h, leader, broken, target, nullptr) != (want >= target)) {
            return false;
        }
    }
    const Hand legal = legal_moves(h[leader], 0, -1, broken);
    if (best == NO_CARD || !(legal & card_bit(best))) return false;
    RefPos q = p;
    q.h[leader] &= ~card_bit(best);
    q.broken = spades_broken_after(broken, card_suit(best));
    q.trick[0] = best;
    q.len = 1;
    return reference(q) == want;
}

// A variant of `h`: the top `keep` cards of each suit (a random number per
// suit) stay with their owners, and the cards below them are dealt out again
// among the same hands, each hand keeping its count of them.
void deal_low_cards_again(const Hand h[4], Hand out[4], std::mt19937& rng) {
    for (int s = 0; s < 4; ++s) out[s] = h[s];
    const Hand all = h[0] | h[1] | h[2] | h[3];
    for (int suit = 0; suit < 4; ++suit) {
        std::vector<CardId> live;  // highest first
        for (int r = 14; r >= 2; --r) {
            if (all & card_bit(make_card(suit, r))) live.push_back(make_card(suit, r));
        }
        const std::size_t keep = rng() % (live.size() + 1);
        std::vector<int> owners;
        for (std::size_t i = keep; i < live.size(); ++i) {
            for (int seat = 0; seat < 4; ++seat) {
                if (out[seat] & card_bit(live[i])) {
                    owners.push_back(seat);
                    out[seat] &= ~card_bit(live[i]);
                }
            }
        }
        std::shuffle(owners.begin(), owners.end(), rng);
        for (std::size_t i = keep; i < live.size(); ++i) {
            out[owners[i - keep]] |= card_bit(live[i]);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const int cases = argc > 1 ? std::atoi(argv[1]) : 2000;
    const int max_tricks = argc > 2 ? std::atoi(argv[2]) : 5;
    const unsigned seed = argc > 3 ? static_cast<unsigned>(std::atoi(argv[3])) : 7u;
    const int variants = argc > 4 ? std::atoi(argv[4]) : 0;
    std::mt19937 rng(seed);
    dd::engine().resize(16);

    int failures = 0;
    int unbroken_positions = 0;
    int variant_positions = 0;
    for (int it = 0; it < cases; ++it) {
        const int t = 1 + static_cast<int>(rng() % max_tricks);
        // A shrunken deck: pick 4t distinct cards, biased toward few suits.
        std::vector<CardId> deck;
        const int suits_used = 1 + static_cast<int>(rng() % 4);
        int suit_pick[4] = {0, 1, 2, 3};
        std::shuffle(suit_pick, suit_pick + 4, rng);
        for (int k = 0; k < suits_used; ++k) {
            for (int r = 2; r <= 14; ++r) deck.push_back(make_card(suit_pick[k], r));
        }
        if (static_cast<int>(deck.size()) < 4 * t) {
            for (int s = 0; s < 4; ++s) {
                bool used = false;
                for (int k = 0; k < suits_used; ++k) used |= suit_pick[k] == s;
                if (!used)
                    for (int r = 2; r <= 14; ++r) deck.push_back(make_card(s, r));
            }
        }
        std::shuffle(deck.begin(), deck.end(), rng);
        Hand dealt[4] = {0, 0, 0, 0};
        for (int seat = 0; seat < 4; ++seat)
            for (int k = 0; k < t; ++k) dealt[seat] |= card_bit(deck[seat * t + k]);
        const int leader = static_cast<int>(rng() % 4);
        const bool broken = (rng() % 2) == 0;
        if (!broken) ++unbroken_positions;

        for (int v = 0; v <= variants; ++v) {
            Hand h[4];
            if (v == 0) {
                for (int s = 0; s < 4; ++s) h[s] = dealt[s];
            } else {
                deal_low_cards_again(dealt, h, rng);
                ++variant_positions;
            }
            int got = 0, want = 0;
            CardId best = NO_CARD;
            // Every position is a new solve as far as the engine's aging is
            // concerned (dd::Engine::new_solve): facts from earlier positions
            // stay readable but are replaced first -- and must still be right
            // when they are read back here.
            dd::engine().new_solve(true);
            if (check_position(h, leader, broken, t, got, want, best)) continue;
            ++failures;
            if (failures <= 10) {
                std::printf("FAIL case %d variant %d: leader %c broken %d engine %d reference %d "
                            "best %s\n  ",
                            it, v, SEAT_CHARS[leader], broken ? 1 : 0, got, want,
                            best == NO_CARD ? "--" : card_to_string(best).c_str());
                for (int s = 0; s < 4; ++s)
                    std::printf("%c: %s  ", SEAT_CHARS[s], hand_to_string(h[s]).c_str());
                std::printf("\n");
            }
        }
    }
    std::printf("dd_property: %d positions up to %d tricks (%d with spades unbroken) and %d "
                "variants, %d failures\n",
                cases, max_tricks, unbroken_positions, variant_positions, failures);
    return failures == 0 ? 0 : 1;
}
