// Exhaustive property test for duck_depth and cheapest_cover_above
// (MOVE_ORDERING.md item C0).
//
// C0 is a prerequisite rather than a heuristic: three of the four cover-partner
// rules read this one number, so nothing downstream is trustworthy if it is
// wrong, and it ships with no consumer and no A/B.  What it gets instead is
// this -- the COMPLETE input space, not a sample.
//
// One suit has thirteen ranks, and each rank is the nil bidder's, the cover
// partner's, or neither.  That is 3^13 = 1,594,323 holdings, which is every
// question duck_depth can be asked about one suit, and the loop below asks all
// of them in each of the four suits.  There is no sampling and no seed.
//
// Three independent computations must agree on every one of them:
//
//   1. duck_depth itself -- one greedy descent.
//   2. An unconstrained maximum bipartite matching (Kuhn's algorithm), which
//      assumes nothing about the structure of the problem and would find a
//      better pairing if the greedy ever missed one.
//   3. Hall's deficiency formula, m - max_j(j - above_j), which is the same
//      theorem cover_deficit_depth rests on, read for a size instead of for a
//      first failure.
//
// Agreement between 1 and 2 says the greedy is optimal.  Agreement with 3 says
// the answer is the one the rest of bounds.hpp already believes in, so the two
// functions in that file cannot drift apart.
//
// Then the invariants, on every holding: the bound against the two suit
// lengths, monotonicity in each hand separately, and invariance under an
// order-preserving relabelling of the ranks -- that last because the
// transposition table stores RELATIVE ranks, so any measure that consumers will
// eventually key on had better not be able to tell absolute ranks apart.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "nil/bounds.hpp"
#include "nil/cards.hpp"

using nil::CardId;
using nil::card_bit;
using nil::count_cards;
using nil::Hand;
using nil::make_card;

namespace {

long long g_failures = 0;

void fail(const char* what, Hand nil_cards, Hand cover_cards, int suit, int a, int b) {
    if (++g_failures > 20) return;
    std::printf("FAIL %s  suit=%d  nil=%s  cover=%s  got %d want %d\n", what, suit,
                nil::hand_to_string(nil_cards).c_str(),
                nil::hand_to_string(cover_cards).c_str(), a, b);
}

// ---------------------------------------------------------------------------
// Reference 2: maximum bipartite matching, assuming nothing.
// ---------------------------------------------------------------------------
// Nil card i may be paired with cover card j when cover[j] outranks nil[i].
// Kuhn's augmenting-path search; the holdings are at most thirteen cards each,
// so the cost does not matter and the independence does.
bool augment(int i, const std::vector<CardId>& nil_ranks,
             const std::vector<CardId>& cover_ranks, std::vector<char>& seen,
             std::vector<int>& matched_to) {
    for (std::size_t j = 0; j < cover_ranks.size(); ++j) {
        if (seen[j] || cover_ranks[j] <= nil_ranks[static_cast<std::size_t>(i)]) continue;
        seen[j] = 1;
        if (matched_to[j] < 0 ||
            augment(matched_to[j], nil_ranks, cover_ranks, seen, matched_to)) {
            matched_to[j] = i;
            return true;
        }
    }
    return false;
}

int max_matching(const std::vector<CardId>& nil_ranks, const std::vector<CardId>& cover_ranks) {
    std::vector<int> matched_to(cover_ranks.size(), -1);
    int size = 0;
    for (std::size_t i = 0; i < nil_ranks.size(); ++i) {
        std::vector<char> seen(cover_ranks.size(), 0);
        if (augment(static_cast<int>(i), nil_ranks, cover_ranks, seen, matched_to)) ++size;
    }
    return size;
}

// ---------------------------------------------------------------------------
// Reference 3: Hall's deficiency, the form cover_deficit_depth rests on.
// ---------------------------------------------------------------------------
// Number the nil bidder's cards from the top, j = 1..m, and let above_j be how
// many cover cards sit strictly above the j-th.  The largest matching is
// m - max(0, max_j (j - above_j)).
int hall_deficiency(Hand nil_cards, Hand cover_cards, int suit) {
    const int m = count_cards(nil_cards);
    if (!m) return 0;
    const Hand top = card_bit(make_card(suit, 14));
    const Hand bottom = card_bit(make_card(suit, 2));
    int held = 0, above = 0, worst = 0;
    for (Hand bit = top; bit >= bottom; bit >>= 1) {
        if (cover_cards & bit) {
            ++above;
        } else if (nil_cards & bit) {
            ++held;
            if (held - above > worst) worst = held - above;
        }
    }
    return m - worst;
}

// The suit's cards packed downwards so that the ranks actually present occupy
// the bottom of the suit in the same order.  An order-preserving relabelling:
// if duck_depth can tell this apart from the original, it is reading absolute
// ranks, and the transposition table does not preserve those.
void compress(Hand nil_cards, Hand cover_cards, int suit, Hand& nil_out, Hand& cover_out) {
    nil_out = 0;
    cover_out = 0;
    int next = 2;
    const Hand bottom = card_bit(make_card(suit, 2));
    const Hand top = card_bit(make_card(suit, 14));
    // Walk UP so the lowest card present becomes the deuce.
    for (Hand bit = bottom; bit <= top; bit <<= 1) {
        if (nil_cards & bit) {
            nil_out |= card_bit(make_card(suit, next++));
        } else if (cover_cards & bit) {
            cover_out |= card_bit(make_card(suit, next++));
        }
    }
}

}  // namespace

int main() {
    // The documented examples, first and by name.  These are the four worked
    // through in MOVE_ORDERING.md; if the definition ever drifts from what the
    // cover rules were specified against, it drifts here.
    struct {
        const char* label;
        const char* cover;
        const char* nil_hand;
        int want;
    } const examples[] = {
        {"C0 example: cover JT87, nil 954", "JT87", "954", 3},
        {"C0 example: cover KQ4, nil J96", "KQ4", "J96", 2},
        {"C2 example, clubs: cover KQ, nil A2", "KQ", "A2", 1},
        {"C2 example, hearts: cover 54, nil Q32", "54", "Q32", 2},
        {"C3 tier 4, clubs: cover KQ4, nil J96", "KQ4", "J96", 2},
        {"C3 tier 4, diamonds: cover JT87, nil 954", "JT87", "954", 3},
    };
    const char* const ranks = "23456789TJQKA";
    for (const auto& ex : examples) {
        Hand cover = 0, nil_cards = 0;
        for (const char* p = ex.cover; *p; ++p)
            for (int r = 0; r < 13; ++r)
                if (ranks[r] == *p) cover |= card_bit(make_card(nil::SUIT_HEARTS, r + 2));
        for (const char* p = ex.nil_hand; *p; ++p)
            for (int r = 0; r < 13; ++r)
                if (ranks[r] == *p) nil_cards |= card_bit(make_card(nil::SUIT_HEARTS, r + 2));
        const int got = nil::duck_depth(nil_cards, cover, nil::SUIT_HEARTS);
        if (got != ex.want) {
            std::printf("FAIL %s: got %d want %d\n", ex.label, got, ex.want);
            ++g_failures;
        }
    }

    long long cases = 0;
    for (int suit = 0; suit < 4; ++suit) {
        // Every assignment of the thirteen ranks to {nil, cover, neither}.
        long long total = 1;
        for (int i = 0; i < 13; ++i) total *= 3;

        for (long long code = 0; code < total; ++code) {
            Hand nil_cards = 0, cover_cards = 0;
            long long rest = code;
            for (int r = 0; r < 13; ++r) {
                const int which = static_cast<int>(rest % 3);
                rest /= 3;
                const Hand bit = card_bit(make_card(suit, r + 2));
                if (which == 1) nil_cards |= bit;
                else if (which == 2) cover_cards |= bit;
            }
            ++cases;

            const int got = nil::duck_depth(nil_cards, cover_cards, suit);

            // 2. Unconstrained maximum matching.
            std::vector<CardId> nil_ranks, cover_ranks;
            for (int r = 0; r < 13; ++r) {
                const Hand bit = card_bit(make_card(suit, r + 2));
                if (nil_cards & bit) nil_ranks.push_back(make_card(suit, r + 2));
                if (cover_cards & bit) cover_ranks.push_back(make_card(suit, r + 2));
            }
            const int best = max_matching(nil_ranks, cover_ranks);
            if (got != best) fail("greedy is not a maximum matching", nil_cards, cover_cards,
                                  suit, got, best);

            // 3. Hall's deficiency.
            const int hall = hall_deficiency(nil_cards, cover_cards, suit);
            if (got != hall) fail("disagrees with Hall deficiency", nil_cards, cover_cards,
                                  suit, got, hall);

            // Bounded by both suit lengths.
            const int n = count_cards(nil_cards), c = count_cards(cover_cards);
            const int cap = n < c ? n : c;
            if (got < 0 || got > cap)
                fail("outside [0, min(lengths)]", nil_cards, cover_cards, suit, got, cap);

            // Zero exactly when no cover card outranks the nil bidder's lowest.
            if (nil_cards) {
                const Hand lowest = nil_cards & (~nil_cards + 1);
                const bool any_above = (cover_cards & ~((lowest << 1) - 1)) != 0;
                if ((got > 0) != any_above)
                    fail("zero-duck condition", nil_cards, cover_cards, suit, got,
                         any_above ? 1 : 0);
            }

            // cheapest_cover_above, checked against a plain scan.  Every rank
            // is tried as the target, including ones nobody holds, because a
            // caller may pass any card of the suit.
            for (int r = 2; r <= 14; ++r) {
                const CardId target = make_card(suit, r);
                CardId want = nil::NO_CARD;
                for (int q = r + 1; q <= 14; ++q) {
                    if (cover_cards & card_bit(make_card(suit, q))) {
                        want = make_card(suit, q);
                        break;
                    }
                }
                if (nil::cheapest_cover_above(cover_cards, target) != want)
                    fail("cheapest_cover_above", nil_cards, cover_cards, suit, r, want);
            }

            // Order-preserving relabelling of the ranks present.
            Hand cn = 0, cc = 0;
            compress(nil_cards, cover_cards, suit, cn, cc);
            const int compressed = nil::duck_depth(cn, cc, suit);
            if (compressed != got)
                fail("not invariant under relabelling", cn, cc, suit, compressed, got);

            // Monotone in each hand separately: a card added to either holding
            // can only add pairings, never remove one.
            for (int r = 0; r < 13; ++r) {
                const Hand bit = card_bit(make_card(suit, r + 2));
                if ((nil_cards | cover_cards) & bit) continue;
                if (nil::duck_depth(nil_cards | bit, cover_cards, suit) < got)
                    fail("not monotone in the nil holding", nil_cards | bit, cover_cards,
                         suit, nil::duck_depth(nil_cards | bit, cover_cards, suit), got);
                if (nil::duck_depth(nil_cards, cover_cards | bit, suit) < got)
                    fail("not monotone in the cover holding", nil_cards, cover_cards | bit,
                         suit, nil::duck_depth(nil_cards, cover_cards | bit, suit), got);
            }
        }
    }

    std::printf("duck_depth: %lld holdings checked exhaustively across four suits\n", cases);
    if (g_failures) {
        std::printf("FAILURES: %lld\n", g_failures);
        return 1;
    }
    std::printf("All properties hold.\n");
    return 0;
}
