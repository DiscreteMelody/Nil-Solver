// Property test for nil/ddtricks: the double-dummy engine against an
// independent brute-force minimax, on random positions of up to seven tricks.
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
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <unordered_map>
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

struct KeyHash {
    std::size_t operator()(const std::vector<std::uint64_t>& k) const {
        std::uint64_t x = 1469598103934665603ull;
        for (std::uint64_t v : k) x = (x ^ v) * 1099511628211ull;
        return static_cast<std::size_t>(x);
    }
};

std::unordered_map<std::vector<std::uint64_t>, int, KeyHash> memo;

// N/S tricks from here under optimal play, brute force.
int reference(const RefPos& p) {
    const Hand all = p.h[0] | p.h[1] | p.h[2] | p.h[3];
    if (!all && p.len == 0) return 0;
    std::vector<std::uint64_t> key;
    if (p.len == 0) {
        key = {p.h[0], p.h[1], p.h[2], p.h[3],
               static_cast<std::uint64_t>(p.leader | (p.broken ? 4 : 0))};
        auto it = memo.find(key);
        if (it != memo.end()) return it->second;
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
    if (p.len == 0) memo[key] = best;
    return best;
}

}  // namespace

int main(int argc, char** argv) {
    const int cases = argc > 1 ? std::atoi(argv[1]) : 3000;
    const int max_tricks = argc > 2 ? std::atoi(argv[2]) : 6;
    const unsigned seed = argc > 3 ? static_cast<unsigned>(std::atoi(argv[3])) : 7u;
    std::mt19937 rng(seed);
    dd::engine().resize(16);

    int failures = 0;
    int unbroken_positions = 0;
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
        Hand h[4] = {0, 0, 0, 0};
        for (int seat = 0; seat < 4; ++seat)
            for (int k = 0; k < t; ++k) h[seat] |= card_bit(deck[seat * t + k]);
        const int leader = static_cast<int>(rng() % 4);
        const bool broken = (rng() % 2) == 0;
        if (!broken) ++unbroken_positions;

        RefPos p{};
        for (int s = 0; s < 4; ++s) p.h[s] = h[s];
        p.leader = leader;
        p.len = 0;
        p.broken = broken;
        memo.clear();
        const int want = reference(p);

        CardId best = NO_CARD;
        const int got = dd::engine().ns_exact(h, leader, broken, 0, t, &best);
        bool ok = got == want;
        for (int target = 0; target <= t + 1 && ok; ++target) {
            if (dd::engine().ns_reach(h, leader, broken, target, nullptr) != (want >= target)) {
                ok = false;
            }
        }
        if (ok) {
            const Hand legal = legal_moves(h[leader], 0, -1, broken);
            if (best == NO_CARD || !(legal & card_bit(best))) {
                ok = false;
            } else {
                RefPos q = p;
                q.h[leader] &= ~card_bit(best);
                q.broken = spades_broken_after(broken, card_suit(best));
                q.trick[0] = best;
                q.len = 1;
                if (reference(q) != want) ok = false;
            }
        }
        if (!ok) {
            ++failures;
            if (failures <= 10) {
                std::printf("FAIL case %d: leader %c broken %d engine %d reference %d best %s\n  ",
                            it, SEAT_CHARS[leader], broken ? 1 : 0, got, want,
                            best == NO_CARD ? "--" : card_to_string(best).c_str());
                for (int s = 0; s < 4; ++s)
                    std::printf("%c: %s  ", SEAT_CHARS[s], hand_to_string(h[s]).c_str());
                std::printf("\n");
            }
        }
    }
    std::printf("dd_property: %d positions up to %d tricks (%d with spades unbroken), %d failures\n",
                cases, max_tricks, unbroken_positions, failures);
    return failures == 0 ? 0 : 1;
}
