#include "nil/ddtricks.hpp"

#include <algorithm>

#include "nil/bitpack.hpp"
#include "nil/rules.hpp"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace nil {
namespace dd {

namespace {

constexpr Hand SPADES = suit_mask(SUIT_SPADES);

inline int pop(Hand h) {
#if defined(_MSC_VER) && defined(_M_X64)
    return static_cast<int>(__popcnt64(h));
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(h);
#else
    return count_cards(h);
#endif
}

inline int pop32(unsigned x) { return pop(static_cast<Hand>(x)); }

// The 13 rank bits of one suit, bit 0 = the two.
inline unsigned sbits(Hand h, int suit) {
    return static_cast<unsigned>((h >> (suit * 16)) & SUIT_BITS);
}

inline int top_bit(unsigned x) { return highest_card(static_cast<Hand>(x)); }

// How many of the highest remaining cards of a suit, counted down from the top
// without a gap, `mine` holds.
inline int top_run(unsigned mine, unsigned all) {
    int run = 0;
    while (all) {
        const int r = top_bit(all);
        if (!((mine >> r) & 1u)) break;
        ++run;
        all &= ~(1u << r);
    }
    return run;
}

// The top k rank bits of `all`.
inline unsigned top_k_bits(unsigned all, int k) {
    unsigned out = 0;
    while (k-- > 0 && all) {
        const int r = top_bit(all);
        out |= 1u << r;
        all &= ~(1u << r);
    }
    return out;
}

// ---- the key ---------------------------------------------------------------
//
// Positions are bucketed by suit lengths per hand, leader and breaking state --
// everything a stored fact needs to match exactly.  Within a bucket a fact
// matches when the owners of the top k cards of each suit agree, k being how
// deep the proof actually looked (see Entry).
inline std::uint64_t mix(std::uint64_t a, std::uint64_t b) {
    std::uint64_t x = a * 0x9E3779B97F4A7C15ull ^ (b + 0xC2B2AE3D27D4EB4Full) * 0x165667B19E3779F9ull;
    x ^= x >> 31;
    x *= 0xD6E8FEB86659FD93ull;
    x ^= x >> 32;
    return x;
}

// Stored best lead, relative: suit and position from the top of that suit's
// remaining cards, so it survives the rank relabelling the key ignores.
inline std::uint8_t rel_move(CardId c, const Hand h[4]) {
    const int s = card_suit(c);
    const unsigned all = sbits(h[0] | h[1] | h[2] | h[3], s);
    const int r = c & 15;
    const int above = pop32(all >> (r + 1));
    return static_cast<std::uint8_t>((s << 4) | above);
}

inline CardId from_rel(std::uint8_t m, const Hand h[4]) {
    if (m == 0xFF) return NO_CARD;
    const int s = m >> 4;
    int idx = m & 15;
    unsigned all = sbits(h[0] | h[1] | h[2] | h[3], s);
    while (all) {
        const int r = top_bit(all);
        if (idx-- == 0) return static_cast<CardId>(s * 16 + r);
        all &= ~(1u << r);
    }
    return NO_CARD;
}

// ---- bounds ----------------------------------------------------------------

// Tricks the side on lead can take by force, starting now, without giving up
// the lead: top cards the LEADER holds, cashed from its own hand.
//
// Side suits first.  An opponent holding a spade can only ruff a round it cannot
// follow, so a suit is cashed for at most as many rounds as each spade-holding
// opponent can follow -- and while it follows it discards nothing, so its other
// suits are untouched when their turn comes.  An opponent with no spade can
// never ruff.  Then spades, which nobody can ruff, if they may be led: already
// broken, or the leader holds nothing else once its side-suit winners are gone.
int quick_tricks(const Hand h[4], int leader, bool broken, unsigned rel[4]) {
    const Hand me = h[leader];
    const Hand lho = h[(leader + 1) & 3];
    const Hand rho = h[(leader + 3) & 3];
    const Hand all = h[0] | h[1] | h[2] | h[3];
    const bool lho_sp = (lho & SPADES) != 0;
    const bool rho_sp = (rho & SPADES) != 0;
    int qt = 0;
    int side_cashed = 0;
    for (int s = 1; s < 4; ++s) {
        const unsigned mine = sbits(me, s);
        if (!mine) continue;
        int cap = top_run(mine, sbits(all, s));
        if (!cap) continue;
        if (lho_sp) cap = std::min(cap, pop32(sbits(lho, s)));
        if (rho_sp) cap = std::min(cap, pop32(sbits(rho, s)));
        qt += cap;
        side_cashed += cap;
        rel[s] |= top_k_bits(sbits(all, s), cap);
    }
    const unsigned my_sp = sbits(me, 0);
    if (my_sp) {
        const bool can_lead = broken || side_cashed == pop(me & ~SPADES);
        if (can_lead) {
            const int run = top_run(my_sp, sbits(all, 0));
            qt += run;
            rel[0] |= top_k_bits(sbits(all, 0), run);
        }
    }
    return qt;
}

// Tricks a side is sure of from its spades whoever leads.  The hand holding the
// top spade wins a trick with each spade of its unbroken top run, one trick per
// card since a hand plays one card a trick.  And when the other side holds no
// spade at all, every spade the longer hand plays wins its trick for the side.
void spade_floor(const Hand h[4], int& ns_floor, int& ew_floor) {
    ns_floor = 0;
    ew_floor = 0;
    const unsigned all = sbits(h[0] | h[1] | h[2] | h[3], 0);
    if (!all) return;
    const int top = top_bit(all);
    for (int seat = 0; seat < 4; ++seat) {
        const unsigned mine = sbits(h[seat], 0);
        if ((mine >> top) & 1u) {
            const int run = top_run(mine, all);
            (seat & 1 ? ew_floor : ns_floor) = run;
        }
    }
    const Hand ns_sp = (h[0] | h[2]) & SPADES;
    const Hand ew_sp = (h[1] | h[3]) & SPADES;
    if (!ew_sp) ns_floor = std::max(ns_floor, std::max(pop(h[0] & SPADES), pop(h[2] & SPADES)));
    if (!ns_sp) ew_floor = std::max(ew_floor, std::max(pop(h[1] & SPADES), pop(h[3] & SPADES)));
}

// Can some card of `hand` beat `c` on a trick led in `led`?
inline bool can_beat(Hand hand, CardId c, int led) {
    const int cs = card_suit(c);
    const unsigned follow = sbits(hand, led);
    if (follow) {
        if (cs != led) return false;  // c is a ruff; followers cannot beat it
        return top_bit(follow) > (c & 15);
    }
    const unsigned sp = sbits(hand, 0);
    if (!sp) return false;
    if (cs == 0) return top_bit(sp) > (c & 15);
    return true;
}

// ---- move ordering: DDS's heuristic weights (Oct 2026, ROADMAP item 99) ----
//
// WHY THIS IS HERE.  Profiling the per-card call on 88 random 13-card single-
// nil deals put 48% of its wall time in this engine, and the call the bot makes
// once its nil is down (`1 3 2 3`) is ALL engine.  Against DDS itself (built
// from github.com/dds-bridge/dds with a node counter, bridge rules -- spades
// trump, no breaking rule -- on 30 of those deals) this engine searched 68.1M
// nodes for the root count where DDS searched 4.7M, at about the same cost per
// node.  Turning
// DDS's own pieces off one at a time said where its advantage lives:
//
//     DDS as shipped                      4.7M nodes
//     quick tricks and later tricks off   8.2M    (x1.7)
//     lowest-win skipping off             8.4M    (x1.8)
//     move ordering off (low card first)  255.6M  (x54)
//
// and this engine's short score against no ordering at all was worth x12.6 on
// the same positions.  So the weights below are DDS's, case for case: the
// WeightAlloc functions of heuristic_sorting.cpp (Haglund and Hein; the DDS
// repository is Apache-2.0 licensed), with the bridge-specific trump index
// fixed at spades and nothing else changed.  Ordering only, so the engine's
// answers cannot move; what moves is how soon a node finds its cutoff.
// The same 30 root counts, each change of this pass added in turn (nodes;
// "bridge" is spades broken from the start, as DDS plays, "ours" is spades
// unbroken, as every Spades hand begins):
//
//                                         bridge     ours
//     before this pass                    68.1M     27.4M
//     + DDS's ordering (this section)     17.2M      9.0M
//     + lowest-win skipping               10.5M      5.0M
//     + trick winner by rank only          8.6M      4.4M
//     + 26 profiles per header             7.9M      4.2M
//
// and with everything else on, taking the ordering back out costs x4.8 / x4.1
// (38.2M / 17.1M).  The rest of the gap to DDS is its quick-tricks and later-
// tricks bounds, which are stronger than quick_tricks() below.  See
// SearchOptions::dd_order for what this does to the per-card call.
//
// THE ENCODING.  Ranks are DDS ranks, 2..14, with 0 for "none"; a rank set is
// one suit's 13-bit lane, bit 0 the two, which is exactly DDS's bit_map_rank
// layout -- so DDS's habit of comparing two hands' holdings as integers
// ("rank_in_suit[rho] > rank_in_suit[partner]" means rho's top card is higher)
// carries over unchanged.  `winner` and `second_best` are DDS's: the top two
// cards of the suit AS THE TRICK BEGAN (DDS updates them only when a trick
// completes), while holdings, lengths and `aggr` are the cards still in hand.
// A move is one class of equal cards, as this engine generates them; its rank
// is the class's TOP card, as DDS's groups are named, and it is played as the
// class's lowest card, as this engine always plays it -- the two are the same
// move.  Within a suit the classes are listed from the top down and the suits
// in order, as DDS generates them, because two of the helpers (rank_forces_ace,
// get_top_number) read positions in that list.
// Ranks here are DDS ranks: 2..14, 0 meaning "none".  A rank set is the 13-bit
// lane of one suit, bit 0 = the two, exactly DDS's bit_map_rank layout.

struct OMove {
    CardId card;   // the representative the engine plays (lowest of its class)
    int suit;
    int rank;      // DDS rank of the TOP card of the class
    int seq;       // nonzero when the class has more than one card
    Hand cls;      // the whole class
    int weight;
};

struct OCtx {
    // Current holdings, read on demand: most nodes are followers reading a
    // handful of them, and building all sixteen lanes and their lengths for
    // every node cost more than the weights themselves.
    const Hand* h;
    unsigned ris(int q, int s) const { return sbits(h[q], s); }
    int len(int q, int s) const { return pop32(sbits(h[q], s)); }
    unsigned aggr(int s) const { return sbits(h[0] | h[1] | h[2] | h[3], s); }
    // At trick start: the top two cards of each suit and who holds them, and
    // the ranks gone before this trick began.  Constant through a trick, so a
    // lead computes them and its followers inherit them (Engine::Pos).
    const TrickStart* ts;
    int lead_hand, lead_suit, curr_hand, curr_trick;
    int lead0_rank = 0, move1_rank = 0, move1_suit = 0, high1 = 0;
    int move2_rank = 0, move2_suit = 0, high2 = 0;
    CardId best_move = NO_CARD;     // killer for this depth (leads)
    Hand best_move_tt = 0;          // class of the table's stored move (leads)
};

inline int o_hi(unsigned m) { return m ? top_bit(m) + 2 : 0; }
inline int o_lo(unsigned m) { return m ? lowest_card(static_cast<Hand>(m)) + 2 : 0; }
inline unsigned o_bit(int r) { return r >= 2 ? 1u << (r - 2) : 0u; }
inline int o_rel_rank(unsigned aggr, int r) { return r >= 2 ? pop32(aggr >> (r - 2)) : 0; }
constexpr int O_LHO[4] = {1, 2, 3, 0};
constexpr int O_RHO[4] = {3, 0, 1, 2};
constexpr int O_PART[4] = {2, 3, 0, 1};

// k-th highest card (1-based) of `aggr` in suit s: its holder, or -1.
inline int o_abs_hand(const OCtx& c, int s, unsigned aggr, int k) {
    unsigned a = aggr;
    for (int i = 1; i < k && a; ++i) a &= ~(1u << top_bit(a));
    if (!a) return -1;
    const unsigned b = 1u << top_bit(a);
    for (int h = 0; h < 4; ++h)
        if (c.ris(h, s) & b) return h;
    return -1;
}

// DDS group_data for a rank set: runs of adjacent ranks, g = 0 the lowest.
struct OGroups {
    int last = -1;
    int rank[13];
    unsigned fullseq[13];
    unsigned gap[13];
};
inline void o_groups(unsigned ris, OGroups& g) {
    g.last = -1;
    int r = 0;
    while (r < 13) {
        if (!((ris >> r) & 1u)) { ++r; continue; }
        int lo = r;
        while (r < 13 && ((ris >> r) & 1u)) ++r;
        const int hi = r - 1;  // bit index of the run's top
        ++g.last;
        g.rank[g.last] = hi + 2;
        g.fullseq[g.last] = ((2u << hi) - 1u) & ~((1u << lo) - 1u);
        if (g.last == 0) {
            g.gap[0] = 0;
        } else {
            const int prev_top = g.rank[g.last - 1] - 2;  // bit index
            g.gap[g.last] = ((1u << lo) - 1u) & ~((2u << prev_top) - 1u);
        }
    }
}

int o_rank_forces_ace(const OCtx& c, const OMove* m, int n, unsigned cards4th) {
    OGroups gd;
    o_groups(cards4th, gd);
    int g = gd.last;
    const unsigned removed = c.ts->removed[c.lead_suit];
    while (g >= 1 && (gd.gap[g] & removed) == gd.gap[g]) g--;
    if (g <= 0) return -1;
    const int secondRHO = gd.rank[g - 1];
    if (secondRHO > c.move1_rank) {
        int k = 0;
        while (k < n && m[k].rank > secondRHO) k++;
        if (k) return k - 1;
    } else if (c.high1 == 1) {
        int k = 0;
        while (k < n && m[k].rank > c.move1_rank) k++;
        if (k) return k - 1;
    }
    return -1;
}

void o_get_top_number(const OCtx& c, const OMove* m, int n, unsigned ris, int prank,
                      int& top_number, int& mno) {
    top_number = -10;
    mno = 0;
    while (mno < n - 1 && m[1 + mno].rank > prank) mno++;
    OGroups gd;
    o_groups(ris, gd);
    int g = gd.last;
    const unsigned removed = c.ts->removed[c.lead_suit] | o_bit(prank);
    if (g < 0) {
        top_number = -1;
        return;
    }
    unsigned fullseq = gd.fullseq[g];
    while (g >= 1 && (gd.gap[g] & removed) == gd.gap[g]) fullseq |= gd.fullseq[--g];
    top_number = pop32(fullseq) - 1;
}

constexpr int TRUMP = 0;

void o_trump0(const OCtx& c, OMove* m, int from, int to, int suit) {
    const int lh = c.lead_hand;
    const int L = O_LHO[lh], R = O_RHO[lh], P = O_PART[lh];
    const int suitCount = c.len(lh, suit);
    const int suit_count_lh = c.len(L, suit);
    const int suit_count_rh = c.len(R, suit);
    const unsigned aggr = c.aggr(suit);
    const int countLH = (suit_count_lh == 0 ? c.curr_trick + 1 : suit_count_lh) << 2;
    const int countRH = (suit_count_rh == 0 ? c.curr_trick + 1 : suit_count_rh) << 2;
    const int suit_weight_d = -(((countLH + countRH) << 5) / 13);
    for (int k = from; k < to; k++) {
        int suit_bonus = 0;
        bool win_move = false;
        const int r_rank = o_rel_rank(aggr, m[k].rank);
        if (suit != TRUMP && ((c.ris(L, suit) == 0 && c.ris(L, TRUMP) != 0) ||
                              (c.ris(R, suit) == 0 && c.ris(R, TRUMP) != 0)))
            suit_bonus = -12;
        if (suit != TRUMP && c.len(P, suit) == 0 && c.len(P, TRUMP) > 0 && suit_count_rh > 0)
            suit_bonus += 17;
        if (c.ts->win_hand[suit] == R || c.ts->sec_hand[suit] == R) {
            if (suit_count_rh != 1) suit_bonus += -12;
        } else if (c.ts->win_hand[suit] == L && c.ts->sec_hand[suit] == P) {
            if (c.len(P, suit) != 1) suit_bonus += 27;
        }
        if (suit != TRUMP && suitCount == 1 && c.len(lh, TRUMP) > 0 && c.len(P, suit) > 1 &&
            c.ts->win_hand[suit] == P)
            suit_bonus += 19;
        int suit_weight_delta = suit_bonus + suit_weight_d;
        if (c.ts->win_rank[suit] == m[k].rank) {
            if (suit != TRUMP) {
                if (c.len(P, suit) != 0 || c.len(P, TRUMP) == 0) {
                    if ((c.len(L, suit) != 0 || c.len(L, TRUMP) == 0) &&
                        (c.len(R, suit) != 0 || c.len(R, TRUMP) == 0))
                        win_move = true;
                } else if ((c.len(L, suit) != 0 || c.ris(P, TRUMP) > c.ris(L, TRUMP)) &&
                           (c.len(R, suit) != 0 || c.ris(P, TRUMP) > c.ris(R, TRUMP)))
                    win_move = true;
            } else
                win_move = true;
        } else if (c.ris(P, suit) > (c.ris(L, suit) | c.ris(R, suit))) {
            if (suit != TRUMP) {
                if ((c.len(L, suit) != 0 || c.len(L, TRUMP) == 0) &&
                    (c.len(R, suit) != 0 || c.len(R, TRUMP) == 0))
                    win_move = true;
            } else
                win_move = true;
        } else if (suit != TRUMP) {
            if (c.len(P, suit) == 0 && c.len(P, TRUMP) != 0) {
                if (c.len(L, suit) == 0 && c.len(L, TRUMP) != 0 && c.len(R, suit) == 0 &&
                    c.len(R, TRUMP) != 0) {
                    if (c.ris(P, TRUMP) > (c.ris(L, TRUMP) | c.ris(R, TRUMP))) win_move = true;
                } else if (c.len(L, suit) == 0 && c.len(L, TRUMP) != 0) {
                    if (c.ris(P, TRUMP) > c.ris(L, TRUMP)) win_move = true;
                } else if (c.len(R, suit) == 0 && c.len(R, TRUMP) != 0) {
                    if (c.ris(P, TRUMP) > c.ris(R, TRUMP)) win_move = true;
                } else
                    win_move = true;
            }
        }
        const bool is_best = c.best_move != NO_CARD && (m[k].cls & card_bit(c.best_move));
        const bool is_tt = (m[k].cls & c.best_move_tt) != 0;
        if (win_move) {
            if ((suit_count_lh == 1 && c.ts->win_hand[suit] == L) ||
                (suit_count_rh == 1 && c.ts->win_hand[suit] == R))
                m[k].weight = suit_weight_delta + 35 + r_rank;
            else if (c.ts->win_hand[suit] == lh) {
                if (c.ts->sec_hand[suit] == P)
                    m[k].weight = suit_weight_delta + 48 + r_rank;
                else if (c.ts->win_rank[suit] == m[k].rank)
                    m[k].weight = suit_weight_delta + 31;
                else
                    m[k].weight = suit_weight_delta - 3 + r_rank;
            } else if (c.ts->win_hand[suit] == P) {
                if (c.ts->sec_hand[suit] == lh)
                    m[k].weight = suit_weight_delta + 42 + r_rank;
                else
                    m[k].weight = suit_weight_delta + 28 + r_rank;
            } else if (m[k].seq && m[k].rank == c.ts->sec_rank[suit])
                m[k].weight = suit_weight_delta + 40;
            else if (m[k].seq)
                m[k].weight = suit_weight_delta + 22 + r_rank;
            else
                m[k].weight = suit_weight_delta + 11 + r_rank;
            if (is_best)
                m[k].weight += 55;
            else if (is_tt)
                m[k].weight += 18;
        } else {
            const int thirdBestHand = o_abs_hand(c, suit, aggr, 3);
            if (c.ts->sec_hand[suit] == P && P == thirdBestHand)
                suit_weight_delta += 20;
            else if ((c.ts->sec_hand[suit] == lh && P == thirdBestHand && c.len(P, suit) > 1) ||
                     (c.ts->sec_hand[suit] == P && lh == thirdBestHand && c.len(P, suit) > 1))
                suit_weight_delta += 13;
            if ((suit_count_lh == 1 && c.ts->win_hand[suit] == L) ||
                (suit_count_rh == 1 && c.ts->win_hand[suit] == R))
                m[k].weight = suit_weight_delta + r_rank + 2;
            else if (c.ts->win_hand[suit] == lh) {
                if (c.ts->sec_hand[suit] == P)
                    m[k].weight = suit_weight_delta + 33 + r_rank;
                else if (c.ts->win_rank[suit] == m[k].rank)
                    m[k].weight = suit_weight_delta + 38;
                else
                    m[k].weight = suit_weight_delta - 14 + r_rank;
            } else if (c.ts->win_hand[suit] == P)
                m[k].weight = suit_weight_delta + 34 + r_rank;
            else if (m[k].seq && m[k].rank == c.ts->sec_rank[suit])
                m[k].weight = suit_weight_delta + 35;
            else
                m[k].weight = suit_weight_delta + 17 - m[k].rank;
            if (is_best) m[k].weight += 18;
        }
    }
}

void o_nt0(const OCtx& c, OMove* m, int from, int to, int suit) {
    const int lh = c.lead_hand;
    const int L = O_LHO[lh], R = O_RHO[lh], P = O_PART[lh];
    const unsigned aggr = c.aggr(suit);
    const int suit_count_lh = c.len(L, suit);
    const int suit_count_rh = c.len(R, suit);
    const int countLH = (suit_count_lh == 0 ? c.curr_trick + 1 : suit_count_lh) << 2;
    const int countRH = (suit_count_rh == 0 ? c.curr_trick + 1 : suit_count_rh) << 2;
    int suit_weight_d = -(((countLH + countRH) << 5) / 19);
    if (c.len(P, suit) == 0) suit_weight_d += -9;
    for (int k = from; k < to; k++) {
        int suit_weight_delta = suit_weight_d;
        const int r_rank = o_rel_rank(aggr, m[k].rank);
        const bool is_best = c.best_move != NO_CARD && (m[k].cls & card_bit(c.best_move));
        const bool is_tt = (m[k].cls & c.best_move_tt) != 0;
        if (c.ts->win_rank[suit] == m[k].rank ||
            c.ris(P, suit) > (c.ris(L, suit) | c.ris(R, suit))) {
            if (c.ts->sec_hand[suit] == R) {
                if (suit_count_rh != 1) suit_weight_delta += -1;
            } else if (c.ts->sec_hand[suit] == L) {
                if (suit_count_lh != 1)
                    suit_weight_delta += 22;
                else
                    suit_weight_delta += 16;
            }
            if ((c.ts->sec_hand[suit] != L || suit_count_lh == 1) &&
                (c.ts->sec_hand[suit] != R || suit_count_rh == 1))
                m[k].weight = suit_weight_delta + 45 + r_rank;
            else
                m[k].weight = suit_weight_delta + 18 + r_rank;
            if (is_best)
                m[k].weight += 126;
            else if (is_tt)
                m[k].weight += 32;
        } else {
            if (c.ts->win_hand[suit] == R || c.ts->sec_hand[suit] == R) {
                if (suit_count_rh != 1) suit_weight_delta += -10;
            } else if (c.ts->win_hand[suit] == L && c.ts->sec_hand[suit] == P) {
                if (c.len(P, suit) != 1) suit_weight_delta += 31;
            }
            const int thirdBestHand = o_abs_hand(c, suit, aggr, 3);
            if (c.ts->sec_hand[suit] == P && P == thirdBestHand)
                suit_weight_delta += 35;
            else if ((c.ts->sec_hand[suit] == lh && P == thirdBestHand && c.len(P, suit) > 1) ||
                     (c.ts->sec_hand[suit] == P && lh == thirdBestHand && c.len(P, suit) > 1))
                suit_weight_delta += 25;
            if ((suit_count_lh == 1 && c.ts->win_hand[suit] == L) ||
                (suit_count_rh == 1 && c.ts->win_hand[suit] == R))
                m[k].weight = suit_weight_delta + 28 + r_rank;
            else if (c.ts->win_hand[suit] == lh)
                m[k].weight = suit_weight_delta - 17 + r_rank;
            else if (!m[k].seq)
                m[k].weight = suit_weight_delta + 12 + r_rank;
            else if (m[k].rank == c.ts->sec_rank[suit])
                m[k].weight = suit_weight_delta + 48;
            else
                m[k].weight = suit_weight_delta + 29 - r_rank;
            if (is_best)
                m[k].weight += 47;
            else if (is_tt)
                m[k].weight += 19;
        }
    }
}

void o_trump_notvoid1(const OCtx& c, OMove* m, int n) {
    const int lh = c.lead_hand, ls = c.lead_suit;
    const int P = O_PART[lh], R = O_RHO[lh];
    const int max3rd = o_hi(c.ris(P, ls));
    const int maxpd = o_hi(c.ris(R, ls));
    const int min3rd = o_lo(c.ris(P, ls));
    const int minpd = o_lo(c.ris(R, ls));
    for (int k = 0; k < n; k++) {
        bool win_move = false;
        const int r_rank = o_rel_rank(c.aggr(ls), m[k].rank);
        if (ls == TRUMP) {
            if (maxpd > c.lead0_rank && maxpd > max3rd)
                win_move = true;
            else if (m[k].rank > c.lead0_rank && m[k].rank > max3rd)
                win_move = true;
        } else {
            if (m[k].rank > c.lead0_rank && m[k].rank > max3rd) {
                if (max3rd != 0 || c.len(P, TRUMP) == 0)
                    win_move = true;
                else if (maxpd == 0 && c.len(R, TRUMP) != 0 && c.ris(R, TRUMP) > c.ris(P, TRUMP))
                    win_move = true;
            } else if (maxpd > c.lead0_rank && maxpd > max3rd) {
                if (max3rd != 0 || c.len(P, TRUMP) == 0) win_move = true;
            } else if (c.lead0_rank > maxpd && c.lead0_rank > max3rd && c.lead0_rank > m[k].rank) {
                if (maxpd == 0 && c.len(R, TRUMP) != 0) {
                    if (max3rd != 0 || c.len(P, TRUMP) == 0)
                        win_move = true;
                    else if (c.ris(R, TRUMP) > c.ris(P, TRUMP))
                        win_move = true;
                }
            } else if (maxpd == 0 && c.len(R, TRUMP) != 0)
                win_move = true;
        }
        if (win_move) {
            if (min3rd > m[k].rank)
                m[k].weight = 40 + r_rank;
            else if (maxpd > c.lead0_rank && c.ris(lh, ls) > c.ris(R, ls))
                m[k].weight = 41 + r_rank;
            else if (m[k].rank > c.lead0_rank) {
                if (m[k].rank < maxpd)
                    m[k].weight = 78 - m[k].rank;
                else if (m[k].rank > max3rd)
                    m[k].weight = 73 - m[k].rank;
                else if (m[k].seq)
                    m[k].weight = 62 - m[k].rank;
                else
                    m[k].weight = 49 - m[k].rank;
            } else if (maxpd > 0)
                m[k].weight = 47 - m[k].rank;
            else
                m[k].weight = 40 - m[k].rank;
        } else if (m[k].rank < min3rd || m[k].rank < minpd)
            m[k].weight = -9 + r_rank;
        else if (m[k].rank < c.lead0_rank)
            m[k].weight = -16 + r_rank;
        else if (m[k].seq)
            m[k].weight = 22 - m[k].rank;
        else
            m[k].weight = 10 - m[k].rank;
    }
}

void o_nt_notvoid1(const OCtx& c, OMove* m, int n) {
    const int lh = c.lead_hand, ls = c.lead_suit;
    const int P = O_PART[lh], R = O_RHO[lh];
    const int max3rd = o_hi(c.ris(P, ls));
    const int maxpd = o_hi(c.ris(R, ls));
    if (maxpd > c.lead0_rank && maxpd > max3rd) {
        for (int k = 0; k < n; k++) m[k].weight = -m[k].rank;
    } else {
        const int min3rd = o_lo(c.ris(P, ls));
        const int minpd = o_lo(c.ris(R, ls));
        for (int k = 0; k < n; k++) {
            const int r_rank = o_rel_rank(c.aggr(ls), m[k].rank);
            if (m[k].rank > c.lead0_rank && m[k].rank > max3rd)
                m[k].weight = 81 - m[k].rank;
            else if (min3rd > m[k].rank || minpd > m[k].rank)
                m[k].weight = -3 + r_rank;
            else if (m[k].rank < c.lead0_rank)
                m[k].weight = -11 + r_rank;
            else if (m[k].seq)
                m[k].weight = 10 + r_rank;
            else
                m[k].weight = 13 - m[k].rank;
        }
    }
}

void o_trump_void1(const OCtx& c, OMove* m, int from, int to, int suit) {
    const int lh = c.lead_hand, ls = c.lead_suit, ch = c.curr_hand;
    const int P = O_PART[lh], R = O_RHO[lh];
    const int suitCount = c.len(ch, suit);
    int suitAdd;
    if (ls == TRUMP) {
        if (c.ris(R, ls) > (c.ris(P, ls) | o_bit(c.lead0_rank)))
            suitAdd = (suitCount << 6) / 44;
        else {
            suitAdd = (suitCount << 6) / 36;
            if (suitCount == 2 && c.ts->sec_hand[suit] == ch) suitAdd += -4;
        }
        for (int k = from; k < to; k++) m[k].weight = -m[k].rank + suitAdd;
    } else if (suit != TRUMP) {
        if (c.len(P, ls) != 0) {
            if (c.ris(R, ls) > (c.ris(P, ls) | o_bit(c.lead0_rank)))
                suitAdd = 60 + (suitCount << 6) / 44;
            else if (c.len(R, ls) == 0 && c.len(R, TRUMP) != 0)
                suitAdd = 60 + (suitCount << 6) / 44;
            else {
                suitAdd = -2 + (suitCount << 6) / 36;
                if (suitCount == 2 && c.ts->sec_hand[suit] == ch) suitAdd += -4;
            }
        } else if (c.len(R, ls) == 0 && c.ris(R, TRUMP) > c.ris(P, TRUMP))
            suitAdd = 60 + (suitCount << 6) / 44;
        else if (c.len(P, TRUMP) == 0 && c.ris(R, ls) > o_bit(c.lead0_rank))
            suitAdd = 60 + (suitCount << 6) / 44;
        else {
            suitAdd = -2 + (suitCount << 6) / 36;
            if (suitCount == 2 && c.ts->sec_hand[suit] == ch) suitAdd += -4;
        }
        for (int k = from; k < to; k++) m[k].weight = -m[k].rank + suitAdd;
    } else if (c.len(P, ls) != 0) {
        suitAdd = (suitCount << 6) / 44;
        for (int k = from; k < to; k++) m[k].weight = 24 - m[k].rank + suitAdd;
    } else if (c.len(R, ls) == 0 && c.len(R, TRUMP) != 0 && c.ris(R, TRUMP) > c.ris(P, TRUMP)) {
        suitAdd = (suitCount << 6) / 44;
        for (int k = from; k < to; k++) m[k].weight = 24 - m[k].rank + suitAdd;
    } else {
        for (int k = from; k < to; k++) {
            if (o_bit(m[k].rank) > c.ris(P, TRUMP)) {
                suitAdd = (suitCount << 6) / 44;
                m[k].weight = 24 - m[k].rank + suitAdd;
            } else {
                suitAdd = (suitCount << 6) / 36;
                if (suitCount == 2 && c.ts->sec_hand[suit] == ch) suitAdd += -4;
                m[k].weight = 15 - m[k].rank + suitAdd;
            }
        }
    }
}

void o_nt_void1(const OCtx& c, OMove* m, int from, int to, int suit) {
    const int lh = c.lead_hand, ls = c.lead_suit, ch = c.curr_hand;
    const int P = O_PART[lh], R = O_RHO[lh];
    const int suitCount = c.len(ch, suit);
    if (c.ris(R, ls) > (c.ris(P, ls) | o_bit(c.lead0_rank))) {
        int suitAdd = (suitCount << 6) / 23;
        if (suitCount == 2 && c.ts->sec_hand[suit] == ch)
            suitAdd += -2;
        else if (suitCount == 1 && c.ts->win_hand[suit] == ch)
            suitAdd += -3;
        for (int k = from; k < to; k++) m[k].weight = -m[k].rank + suitAdd;
    } else {
        int suitAdd = (suitCount << 6) / 33;
        if (suitCount == 2 && c.ts->sec_hand[suit] == ch)
            suitAdd += -6;
        else if (suitCount == 1 && c.ts->win_hand[suit] == ch)
            suitAdd += -8;
        for (int k = from; k < to; k++) m[k].weight = -m[k].rank + suitAdd;
    }
}

void o_trump_notvoid2(const OCtx& c, OMove* m, int n) {
    const int lh = c.lead_hand, ls = c.lead_suit;
    const int R = O_RHO[lh];
    const unsigned cards4th = c.ris(R, ls);
    const int max4th = o_hi(cards4th);
    const int min4th = o_lo(cards4th);
    const int max3rd = m[0].rank;
    if (ls == TRUMP) {
        if (c.high1 == 0 && c.lead0_rank > max4th) {
            for (int k = 0; k < n; k++) m[k].weight = -m[k].rank;
            return;
        } else if (max3rd < min4th || max3rd < c.move1_rank) {
            for (int k = 0; k < n; k++) m[k].weight = -m[k].rank;
            return;
        } else if (max3rd > max4th) {
            for (int k = 0; k < n; k++) {
                if (m[k].rank > max4th && m[k].rank > c.move1_rank)
                    m[k].weight = 58 - m[k].rank;
                else
                    m[k].weight = -m[k].rank;
            }
        } else {
            const int kBonus = o_rank_forces_ace(c, m, n, cards4th);
            for (int k = 0; k < n; k++) m[k].weight = -m[k].rank;
            if (kBonus != -1) m[kBonus].weight += 20;
            return;
        }
    } else if (c.move1_suit == TRUMP) {
        for (int k = 0; k < n; k++) m[k].weight = -m[k].rank;
        return;
    } else if (c.high1 == 0) {
        if (max4th == 0) {
            for (int k = 0; k < n; k++) m[k].weight = -m[k].rank;
            return;
        } else if (c.lead0_rank > max4th) {
            for (int k = 0; k < n; k++) m[k].weight = -m[k].rank;
            return;
        } else if (max3rd < min4th || max3rd < c.move1_rank) {
            for (int k = 0; k < n; k++) m[k].weight = -m[k].rank;
            return;
        } else if (max3rd > max4th) {
            for (int k = 0; k < n; k++) {
                if (m[k].rank > max4th)
                    m[k].weight = 58 - m[k].rank;
                else
                    m[k].weight = -m[k].rank;
            }
        } else {
            const int kBonus = o_rank_forces_ace(c, m, n, cards4th);
            for (int k = 0; k < n; k++) {
                if (m[k].rank > c.move1_rank && m[k].rank > max4th)
                    m[k].weight = 60 - m[k].rank;
                else
                    m[k].weight = -m[k].rank;
            }
            if (kBonus != -1) m[kBonus].weight += 20;
        }
    } else {
        if (max4th == 0) {
            for (int k = 0; k < n; k++) {
                if (m[k].rank > c.move1_rank)
                    m[k].weight = 20 - m[k].rank;
                else
                    m[k].weight = -m[k].rank;
            }
            return;
        } else if (max3rd < min4th || max3rd < c.move1_rank) {
            for (int k = 0; k < n; k++) m[k].weight = -m[k].rank;
            return;
        } else if (max3rd > max4th) {
            for (int k = 0; k < n; k++) {
                if (m[k].rank > c.move1_rank && m[k].rank > max4th)
                    m[k].weight = 58 - m[k].rank;
                else
                    m[k].weight = -m[k].rank;
            }
            return;
        }
        const int kBonus = o_rank_forces_ace(c, m, n, cards4th);
        for (int k = 0; k < n; k++) {
            if (m[k].rank > c.move1_rank && m[k].rank > max4th)
                m[k].weight = 60 - m[k].rank;
            else
                m[k].weight = -m[k].rank;
        }
        if (kBonus != -1) m[kBonus].weight += 20;
    }
}

void o_nt_notvoid2(const OCtx& c, OMove* m, int n) {
    const int lh = c.lead_hand, ls = c.lead_suit, ch = c.curr_hand;
    const int R = O_RHO[lh], L = O_LHO[lh], P = O_PART[lh];
    const unsigned cards4th = c.ris(R, ls);
    const int max4th = o_hi(cards4th);
    const int min4th = o_lo(cards4th);
    const int max3rd = m[0].rank;
    if (c.high1 == 0 && c.lead0_rank > max4th) {
        for (int k = 0; k < n; k++) m[k].weight = -m[k].rank;
        if (c.len(lh, ls) == 0 && c.ts->win_hand[ls] == ch) {
            int oppLen = c.len(R, ls) - 1;
            const int lhoLen = c.len(L, ls);
            if (lhoLen > oppLen) oppLen = lhoLen;
            int top_number, mno;
            o_get_top_number(c, m, n, c.ris(P, ls), c.lead0_rank, top_number, mno);
            if (oppLen <= top_number) m[mno].weight += 20;
        }
        return;
    } else if (max3rd < min4th || max3rd < c.move1_rank) {
        for (int k = 0; k < n; k++) m[k].weight = -m[k].rank;
        return;
    }
    int kBonus = -1;
    if (max4th > max3rd && max4th > c.move1_rank) kBonus = o_rank_forces_ace(c, m, n, cards4th);
    for (int k = 0; k < n; k++) {
        if (m[k].rank > c.move1_rank && m[k].rank > max4th)
            m[k].weight = 60 - m[k].rank;
        else
            m[k].weight = -m[k].rank;
    }
    if (kBonus != -1) m[kBonus].weight += 20;
}

void o_trump_void2(const OCtx& c, OMove* m, int from, int to, int suit) {
    const int lh = c.lead_hand, ls = c.lead_suit, ch = c.curr_hand;
    const int R = O_RHO[lh];
    int suitAdd;
    const int suitCount = c.len(ch, suit);
    const int max4th = o_hi(c.ris(R, ls));
    if (ls == TRUMP || suit != TRUMP) {
        suitAdd = (suitCount << 6) / 40;
        for (int k = from; k < to; k++) m[k].weight = -m[k].rank + suitAdd;
        return;
    } else if (c.high1 == 0 && c.lead0_rank > max4th && (max4th != 0 || c.len(R, TRUMP) == 0)) {
        for (int k = from; k < to; k++) m[k].weight = -m[k].rank - 50;
        return;
    }
    for (int k = from; k < to; k++) {
        if (c.move1_suit == TRUMP && m[k].rank < c.move1_rank) {
            const int r_rank = o_rel_rank(c.aggr(suit), m[k].rank);
            suitAdd = (suitCount << 6) / 40;
            m[k].weight = -32 + r_rank + suitAdd;
        } else if (c.high1 == 0) {
            if (max4th != 0) {
                suitAdd = (suitCount << 6) / 50;
                if (c.ts->sec_hand[ls] == lh)
                    m[k].weight = 36 - m[k].rank + suitAdd;
                else
                    m[k].weight = 48 - m[k].rank + suitAdd;
            } else if (o_bit(m[k].rank) > c.ris(R, TRUMP)) {
                suitAdd = (suitCount << 6) / 50;
                m[k].weight = 48 - m[k].rank + suitAdd;
            } else {
                suitAdd = (suitCount << 6) / 50;
                m[k].weight = -12 - m[k].rank + suitAdd;
            }
        } else if (max4th != 0) {
            suitAdd = (suitCount << 6) / 50;
            m[k].weight = 72 - m[k].rank + suitAdd;
        } else if (o_bit(m[k].rank) > c.ris(R, TRUMP)) {
            suitAdd = (suitCount << 6) / 50;
            m[k].weight = 48 - m[k].rank + suitAdd;
        } else {
            suitAdd = (suitCount << 6) / 50;
            m[k].weight = 36 - m[k].rank + suitAdd;
        }
    }
}

void o_nt_void2(const OCtx& c, OMove* m, int from, int to, int suit) {
    const int ch = c.curr_hand;
    const int suitCount = c.len(ch, suit);
    int suitAdd = (suitCount << 6) / 24;
    if (suitCount == 2 && c.ts->sec_hand[suit] == ch) suitAdd -= 4;
    if (suitCount == 1 && c.ts->win_hand[suit] == ch) suitAdd -= 4;
    for (int k = from; k < to; k++) m[k].weight = -m[k].rank + suitAdd;
}

void o_combined_notvoid3(const OCtx& c, OMove* m, int n) {
    if (c.high2 == 1 || (c.lead_suit != TRUMP && c.move2_suit == TRUMP)) {
        for (int k = 0; k < n; k++) m[k].weight = -m[k].rank;
    } else {
        for (int k = 0; k < n; k++) {
            if (m[k].rank > c.move2_rank)
                m[k].weight = 30 - m[k].rank;
            else
                m[k].weight = -m[k].rank;
        }
    }
}

void o_trump_void3(const OCtx& c, OMove* m, int from, int to, int suit) {
    const int ch = c.curr_hand, ls = c.lead_suit;
    const int mylen = c.len(ch, suit);
    int val = (mylen << 6) / 24;
    if (mylen == 2 && c.ts->sec_hand[suit] == ch) val -= 2;
    if (ls == TRUMP) {
        for (int k = from; k < to; k++) m[k].weight = -m[k].rank + val;
    } else if (c.high2 == 1) {
        if (suit == TRUMP)
            for (int k = from; k < to; k++) m[k].weight = 2 - m[k].rank + val;
        else
            for (int k = from; k < to; k++) m[k].weight = 25 - m[k].rank + val;
    } else if (c.move2_suit == TRUMP) {
        if (suit == TRUMP) {
            for (int k = from; k < to; k++) {
                const int r_rank = o_rel_rank(c.aggr(suit), m[k].rank);
                if (m[k].rank > c.move2_rank)
                    m[k].weight = 33 + r_rank;
                else
                    m[k].weight = -13 + r_rank;
            }
        } else
            for (int k = from; k < to; k++) m[k].weight = 14 - m[k].rank + val;
    } else if (suit == TRUMP) {
        for (int k = from; k < to; k++) {
            const int r_rank = o_rel_rank(c.aggr(suit), m[k].rank);
            m[k].weight = 33 + r_rank;
        }
    } else {
        for (int k = from; k < to; k++) m[k].weight = 14 - m[k].rank + val;
    }
}

void o_nt_void3(const OCtx& c, OMove* m, int from, int to, int suit) {
    const int ch = c.curr_hand;
    const int mylen = c.len(ch, suit);
    int val = (mylen << 6) / 27;
    if (mylen == 2 && c.ts->sec_hand[suit] == ch)
        val -= 6;
    else if (mylen == 1 && c.ts->win_hand[suit] == ch)
        val -= 8;
    for (int k = from; k < to; k++) m[k].weight = -m[k].rank + val;
}

// The driver: what DDS's MoveGen0 (leads) and MoveGen123 (followers) do around
// the weight functions above -- build the move list in DDS's order, pick the
// weight case (4 * position + trump-left + void, as moves.cpp encodes it), and
// sort by weight, highest first.  The sort is stable where DDS's sorting
// networks are not, so ties can come out in a different order than DDS's; that
// is ordering only.  `killer` is DDS's best_move for this depth and `tt_class`
// the class of the table's stored lead (best_move_tt); both are read at leads
// only, as DDS reads them.
void trick_start_of(const Hand h[4], TrickStart& ts) {
    for (int s = 0; s < 4; ++s) {
        ts.win_rank[s] = ts.sec_rank[s] = 0;
        ts.win_hand[s] = ts.sec_hand[s] = -1;
        const unsigned a0 = sbits(h[0] | h[1] | h[2] | h[3], s);
        ts.removed[s] = static_cast<std::uint16_t>(~a0 & 0x1FFFu);
        if (!a0) continue;
        const int b1 = top_bit(a0);
        ts.win_rank[s] = static_cast<std::int8_t>(b1 + 2);
        for (int q = 0; q < 4; ++q)
            if ((sbits(h[q], s) >> b1) & 1u) ts.win_hand[s] = static_cast<std::int8_t>(q);
        const unsigned a1 = a0 & ~(1u << b1);
        if (a1) {
            const int b2 = top_bit(a1);
            ts.sec_rank[s] = static_cast<std::int8_t>(b2 + 2);
            for (int q = 0; q < 4; ++q)
                if ((sbits(h[q], s) >> b2) & 1u) ts.sec_hand[s] = static_cast<std::int8_t>(q);
        }
    }
}

int dds_order_moves(const Hand h[4], const CardId trick[4], int leader, int len, int seat,
                    const TrickStart& ts, Hand legal, Hand moves, Hand relevant, CardId killer,
                    Hand tt_class, CardId out[13]) {
    OCtx oc;
    oc.h = h;
    oc.ts = &ts;
    oc.lead_hand = leader;
    oc.lead_suit = len ? card_suit(trick[0]) : 0;
    oc.curr_hand = seat;
    oc.curr_trick = pop(h[seat]) - 1;  // DDS's `tricks`: tricks left after this one
    // The trick so far, as DDS's track records it: the card winning after the
    // second and third plays, and which play it was.
    if (len >= 1) oc.lead0_rank = (trick[0] & 15) + 2;
    if (len >= 2) {
        CardId w = trick[0];
        int hi = 0;
        for (int i = 1; i < len; ++i) {
            const CardId c = trick[i];
            if (card_suit(c) == card_suit(w)) {
                if ((c & 15) > (w & 15)) {
                    w = c;
                    hi = i;
                }
            } else if (card_suit(c) == TRUMP) {
                w = c;
                hi = i;
            }
            if (i == 1) {
                oc.move1_rank = (w & 15) + 2;
                oc.move1_suit = card_suit(w);
                oc.high1 = hi;
            } else {
                oc.move2_rank = (w & 15) + 2;
                oc.move2_suit = card_suit(w);
                oc.high2 = hi;
            }
        }
    }
    if (len == 0) {
        oc.best_move = killer;
        oc.best_move_tt = tt_class;
    }

    // Each class's TOP card, all at once: a legal card is the top of its class
    // when the next relevant card above it is not legal.  The mirror image of
    // distinct_moves' upward flood (rules.hpp), filling downward through ranks
    // no relevant card occupies.
    Hand tops;
    {
        Hand gap = ~(relevant | SUIT_PADDING);
        Hand flood = legal;
        flood |= gap & (flood >> 1);
        gap &= gap >> 1;
        flood |= gap & (flood >> 2);
        gap &= gap >> 2;
        flood |= gap & (flood >> 4);
        gap &= gap >> 4;
        flood |= gap & (flood >> 8);
        tops = legal & ~(flood >> 1);
    }

    OMove om[13];
    int n = 0;
    int from[4], to[4];
    for (int s = 0; s < 4; ++s) {
        from[s] = n;
        Hand rs = moves & suit_mask(s);
        const int first = n;
        while (rs) {
            const CardId r = take_lowest(rs);
            const Hand rbit = card_bit(r);
            // The first top at or above the representative is its class's.
            const CardId top = lowest_card(tops & ~(rbit - 1));
            const Hand cls = legal & ((card_bit(top) << 1) - rbit);
            om[n++] = OMove{r, s, (top & 15) + 2, top != r ? 1 : 0, cls, 0};
        }
        std::reverse(om + first, om + n);  // top class first, as DDS lists them
        to[s] = n;
    }

    const bool trump_left = ts.win_rank[TRUMP] != 0;
    if (len == 0) {
        for (int s = 0; s < 4; ++s) {
            if (to[s] == from[s]) continue;
            if (trump_left)
                o_trump0(oc, om, from[s], to[s], s);
            else
                o_nt0(oc, om, from[s], to[s], s);
        }
    } else if (oc.ris(seat, oc.lead_suit) != 0) {
        OMove* m = om + from[oc.lead_suit];
        const int k = to[oc.lead_suit] - from[oc.lead_suit];
        if (len == 1) {
            if (trump_left)
                o_trump_notvoid1(oc, m, k);
            else
                o_nt_notvoid1(oc, m, k);
        } else if (len == 2) {
            if (trump_left)
                o_trump_notvoid2(oc, m, k);
            else
                o_nt_notvoid2(oc, m, k);
        } else {
            o_combined_notvoid3(oc, m, k);
        }
    } else {
        for (int s = 0; s < 4; ++s) {
            if (to[s] == from[s]) continue;
            if (len == 1) {
                if (trump_left)
                    o_trump_void1(oc, om, from[s], to[s], s);
                else
                    o_nt_void1(oc, om, from[s], to[s], s);
            } else if (len == 2) {
                if (trump_left)
                    o_trump_void2(oc, om, from[s], to[s], s);
                else
                    o_nt_void2(oc, om, from[s], to[s], s);
            } else {
                if (trump_left)
                    o_trump_void3(oc, om, from[s], to[s], s);
                else
                    o_nt_void3(oc, om, from[s], to[s], s);
            }
        }
    }
    for (int i = 1; i < n; ++i) {
        const OMove x = om[i];
        int j = i;
        while (j > 0 && om[j - 1].weight < x.weight) {
            om[j] = om[j - 1];
            --j;
        }
        om[j] = x;
    }
    for (int i = 0; i < n; ++i) out[i] = om[i].card;
    return n;
}

}  // namespace

// ---- table -----------------------------------------------------------------

constexpr int WAYS = 4;
constexpr int HEADER_WAYS = 4;

Engine::Engine() { resize(DD_DEFAULT_MEGABYTES); }

void Engine::resize(std::size_t megabytes, bool huge_pages) {
    megabytes_ = megabytes;
    facts_.release();
    heads_.release();
    table_ = nullptr;
    headers_ = nullptr;
    mask_ = hmask_ = 0;
    if (megabytes == 0) return;
    // Seven eighths to facts, one eighth to the profile headers.
    const std::size_t budget = megabytes * 1024u * 1024u;
    std::size_t buckets = 1;
    while (buckets * 2 * WAYS * sizeof(Entry) <= budget / 8 * 7) buckets *= 2;
    std::size_t hbuckets = 1;
    while (hbuckets * 2 * HEADER_WAYS * sizeof(Header) <= budget / 8) hbuckets *= 2;
    if (!facts_.allocate(buckets * WAYS * sizeof(Entry), huge_pages) ||
        !heads_.allocate(hbuckets * HEADER_WAYS * sizeof(Header), huge_pages)) {
        facts_.release();
        heads_.release();
        return;  // no table: correct, slow
    }
    table_ = static_cast<Entry*>(facts_.data());
    mask_ = buckets - 1;
    headers_ = static_cast<Header*>(heads_.data());
    hmask_ = hbuckets - 1;
}

void Engine::clear() {
    if (table_) std::fill(table_, table_ + (mask_ + 1) * WAYS, Entry{});
    if (headers_) std::fill(headers_, headers_ + (hmask_ + 1) * HEADER_WAYS, Header{});
    for (CardId& k : lead_killer_) k = NO_CARD;
}

namespace {

inline std::uint64_t fact_hash(std::uint64_t lengths, std::uint8_t meta, const std::uint32_t pat[4]) {
    return mix(lengths ^ (static_cast<std::uint64_t>(meta) << 56),
               (static_cast<std::uint64_t>(pat[0]) << 32 | pat[1]) ^
                   ((static_cast<std::uint64_t>(pat[2]) << 32 | pat[3]) * 0x9E3779B97F4A7C15ull));
}

inline void truncate(const std::uint32_t code[4], const int n[4], std::uint16_t prof,
                     std::uint32_t pat[4]) {
    for (int s = 0; s < 4; ++s) {
        const int k = (prof >> (4 * s)) & 15;
        const std::uint32_t top = k ? code[s] >> (2 * (n[s] - k)) : 0;
        pat[s] = (static_cast<std::uint32_t>(k) << 26) | top;
    }
}

}  // namespace

Engine::Header* Engine::header(const Key& key, bool create) {
    if (!headers_) return nullptr;
    const std::uint8_t meta = static_cast<std::uint8_t>(key.meta | 0x80);
    Header* b = &headers_[(mix(key.lengths, key.meta ^ 0x5A) & hmask_) * HEADER_WAYS];
    for (int i = 0; i < HEADER_WAYS; ++i) {
        if (b[i].meta == meta && b[i].lengths == key.lengths) {
            if (create) b[i].pad = epoch_;
            return &b[i];
        }
    }
    if (!create) return nullptr;
    Header* victim = &b[0];
    for (int i = 0; i < HEADER_WAYS; ++i) {
        if (!(b[i].meta & 0x80) || stale(b[i].pad)) {
            victim = &b[i];
            break;
        }
        if (b[i].count < victim->count) victim = &b[i];
    }
    *victim = Header{};
    victim->lengths = key.lengths;
    victim->meta = meta;
    victim->pad = epoch_;
    return victim;
}

Engine::Entry* Engine::fact(std::uint64_t lengths, std::uint8_t meta, const std::uint32_t pat[4],
                            bool create, int depth) {
    meta = static_cast<std::uint8_t>(meta | 0x80);
    Entry* b = &table_[(fact_hash(lengths, meta, pat) & mask_) * WAYS];
    for (int i = 0; i < WAYS; ++i) {
        Entry& e = b[i];
        if (e.meta == meta && e.lengths == lengths && e.pat[0] == pat[0] && e.pat[1] == pat[1] &&
            e.pat[2] == pat[2] && e.pat[3] == pat[3]) {
            if (create) e.pad[0] = epoch_;
            return &e;
        }
    }
    if (!create) return nullptr;
    Entry* victim = &b[0];
    for (int i = 0; i < WAYS; ++i) {
        if (!(b[i].meta & 0x80) || stale(b[i].pad[0])) {
            victim = &b[i];
            break;
        }
        if (b[i].depth < victim->depth) victim = &b[i];
    }
    *victim = Entry{};
    victim->lengths = lengths;
    victim->meta = meta;
    victim->pad[0] = epoch_;
    for (int s = 0; s < 4; ++s) victim->pat[s] = pat[s];
    victim->lo = 0;
    victim->hi = static_cast<std::int8_t>(depth);
    victim->move = 0xFF;
    victim->depth = static_cast<std::uint8_t>(depth);
    return victim;
}

bool Engine::probe(const Key& key, int target, bool& result, CardId& move, const Hand h[4],
                   std::uint32_t pat_out[4]) {
    if (!table_) return false;
    ++stats_.tt_probes;
    Header* hd = header(key, false);
    if (!hd) return false;
    // THE PROFILE SCAN IS A CHAIN OF CACHE MISSES, AND IT NEED NOT BE (Q9b).
    //
    // Each profile is one exact-key lookup at a random bucket of a 56 MiB
    // array, so a probe that tries n profiles waits for n misses one after the
    // other.  Measured over the 155-deal per-card benchmark: 189M probes, 485M
    // fact lookups, 1.86 profiles per hit and 8.67 per miss (a miss reads every
    // profile the header holds, 10.8M of them all twelve), and the lookup
    // itself was the largest single line of the profile at 16% of wall time.
    //
    // The addresses do not depend on each other -- truncating the key to a
    // profile needs only the key -- so after the MRU profile (which answers
    // 65% of the hits on its own) every remaining bucket is requested at once
    // and then read in the same order as before.  The scan, its order, what it
    // returns and the MRU update are unchanged; only the waiting overlaps.
    //
    // MEASURED, per-card call, 155 thirteen-card deals, leave-one-out against
    // everything else in the Sept 2026 pass: on ordinary 4 KiB pages -- what a
    // Windows service gets without the large-page privilege -- switching this
    // off costs 1.3% of wall time (68 deals slower, 33 faster by 2%+).  On
    // 2 MiB pages it is within noise (+0.2%): with the table's page walks gone
    // the serial misses were already cheap.  Kept for the 4 KiB case.
    const int count = std::min<int>(hd->count, profile_cap_);
    std::uint32_t pats[PROFILES][4];
    const Entry* buckets[PROFILES];
    const std::uint8_t fmeta = static_cast<std::uint8_t>(key.meta | 0x80);
    constexpr int ahead = 1;  // the MRU profile is read before the rest are requested
    for (int i = 0; i < count; ++i) {
        if (prefetch_ && i == ahead) {
            for (int j = ahead; j < count; ++j) {
                truncate(key.code, key.n, hd->prof[j], pats[j]);
                buckets[j] = &table_[(fact_hash(key.lengths, fmeta, pats[j]) & mask_) * WAYS];
                prefetch_line(buckets[j]);
                prefetch_line(buckets[j] + WAYS - 1);
            }
        }
        if (!prefetch_ || i < ahead) {
            truncate(key.code, key.n, hd->prof[i], pats[i]);
            buckets[i] = &table_[(fact_hash(key.lengths, fmeta, pats[i]) & mask_) * WAYS];
        }
        const Entry* e = nullptr;
        for (int w = 0; w < WAYS; ++w) {
            const Entry& c = buckets[i][w];
            if (c.meta == fmeta && c.lengths == key.lengths && c.pat[0] == pats[i][0] &&
                c.pat[1] == pats[i][1] && c.pat[2] == pats[i][2] && c.pat[3] == pats[i][3]) {
                e = &c;
                break;
            }
        }
        if (!e) continue;
        if (move == NO_CARD && e->move != 0xFF) move = from_rel(e->move, h);
        if (e->lo >= target || e->hi < target) {
            result = e->lo >= target;
            for (int s = 0; s < 4; ++s) pat_out[s] = e->pat[s];
            if (mru_ && i > 0) {
                const std::uint16_t hit = hd->prof[i];
                for (int j = i; j > 0; --j) hd->prof[j] = hd->prof[j - 1];
                hd->prof[0] = hit;
            }
            return true;
        }
    }
    return false;
}

void Engine::store(const Key& key, const unsigned rel[4], int lo, int hi, CardId move,
                   const Hand h[4], int depth) {
    if (!table_) return;
    std::uint16_t prof = 0;
    for (int s = 0; s < 4; ++s) {
        const unsigned r = rel[s] & key.all[s];
        int k = 0;
        if (r) k = pop32(key.all[s] >> lowest_card(static_cast<Hand>(r)));
        prof = static_cast<std::uint16_t>(prof | (k << (4 * s)));
    }
    Header* hd = header(key, true);
    // Only the first `profile_cap_` profiles are ever read (probe), so only
    // they count as present; a header filled under a larger cap keeps its
    // extra slots, unread, until they are pushed out.
    const int live = std::min<int>(hd->count, profile_cap_);
    int i = 0;
    while (i < live && hd->prof[i] != prof) ++i;
    if (i == live) {
        if (mru_) {
            if (hd->count < profile_cap_) ++hd->count;
            for (int j = hd->count - 1; j > 0; --j) hd->prof[j] = hd->prof[j - 1];
            hd->prof[0] = prof;
        } else if (hd->count < profile_cap_) {
            hd->prof[hd->count++] = prof;
        } else {
            hd->prof[hd->next] = prof;  // the displaced profile's facts go unreachable
            hd->next = static_cast<std::uint8_t>((hd->next + 1) % profile_cap_);
        }
    }
    std::uint32_t pat[4];
    truncate(key.code, key.n, prof, pat);
    Entry* e = fact(key.lengths, key.meta, pat, true, depth);
    if (lo > e->lo) e->lo = static_cast<std::int8_t>(lo);
    if (hi < e->hi) e->hi = static_cast<std::int8_t>(hi);
    if (move != NO_CARD) e->move = rel_move(move, h);
    e->depth = static_cast<std::uint8_t>(depth);
}

// ---- search ----------------------------------------------------------------
//
// Returns whether N/S can take >= target of the remaining tricks, and in `rel`
// the ranks (per suit, absolute bits) of the cards whose rank the answer
// depended on: every card that won a trick somewhere in the proof, plus the
// cards a static bound relied on.  Only positions at a trick boundary are
// stored; `rel` is what lets a stored fact cover every position that agrees on
// those cards.
bool Engine::search(const Pos& p, int target, CardId* witness, unsigned rel[4],
                    unsigned forb[4]) {
    ++stats_.nodes;
    rel[0] = rel[1] = rel[2] = rel[3] = 0;
    forb[0] = forb[1] = forb[2] = forb[3] = 0;
    const int seat = (p.leader + p.len) & 3;
    const bool ns_to_move = (seat & 1) == 0;
    const bool root = witness != nullptr;

    Key key;
    CardId tt_card = NO_CARD;
    int t = 0;

    if (p.len == 0) {
        t = pop(p.h[p.leader]);
        if (target <= 0) return true;
        if (target > t) return false;

        if (t == 1) {
            CardId played[4];
            for (int i = 0; i < 4; ++i) played[i] = lowest_card(p.h[(p.leader + i) & 3]);
            const int winner = trick_winner(p.leader, played, 4);
            const CardId w = played[(winner - p.leader) & 3];
            rel[card_suit(w)] |= 1u << (w & 15);
            if (root) *witness = played[0];
            return (winner & 1) == 0;  // target is 1 here
        }

        // Build the key.  The owners of each suit's live cards, two bits a
        // card with the lowest card in the lowest bits -- the same string the
        // old top-down loop shifted together, built a whole suit at a time
        // (nil/bitpack.hpp).  The lengths nibble for suit s and hand i sits at
        // bit 16s + 4i, which is where each hand's four suit-lane counts land
        // once shifted by 4i.
        key.lengths = bitpack::lane_counts(p.h[0]) | (bitpack::lane_counts(p.h[1]) << 4) |
                      (bitpack::lane_counts(p.h[2]) << 8) | (bitpack::lane_counts(p.h[3]) << 12);
        {
            const Hand all_h = p.h[0] | p.h[1] | p.h[2] | p.h[3];
            const Hand plane0 = p.h[1] | p.h[3];  // owner bit 0: East or West
            const Hand plane1 = p.h[2] | p.h[3];  // owner bit 1: South or West
            const std::uint64_t counts = bitpack::lane_counts(all_h);
            for (int s = 0; s < 4; ++s) {
                const unsigned all = sbits(all_h, s);
                key.all[s] = all;
                key.code[s] = bitpack::owners13(sbits(plane0, s), sbits(plane1, s), all);
                key.n[s] = static_cast<int>((counts >> (16 * s)) & 31u);
            }
        }
        const bool broken = p.broken || key.all[0] == 0;
        key.meta = static_cast<std::uint8_t>(p.leader | (broken ? 4 : 0));

        bool stored = false;
        std::uint32_t pat[4];
        if (probe(key, target, stored, tt_card, p.h, pat) && !root) {
            ++stats_.tt_cuts;
            for (int s = 0; s < 4; ++s) rel[s] = top_k_bits(key.all[s], static_cast<int>(pat[s] >> 26));
            return stored;
        }

        if (!root) {
            unsigned qrel[4] = {0, 0, 0, 0};
            const int qt = quick_tricks(p.h, p.leader, p.broken, qrel);
            if ((p.leader & 1) == 0 ? qt >= target : t - qt < target) {
                ++stats_.quick_cuts;
                const bool ns = (p.leader & 1) == 0;
                store(key, qrel, ns ? qt : 0, ns ? t : t - qt, NO_CARD, p.h, t);
                for (int s = 0; s < 4; ++s) rel[s] = qrel[s];
                return ns;
            }
            int ns_floor = 0, ew_floor = 0;
            spade_floor(p.h, ns_floor, ew_floor);
            if (ns_floor >= target || t - ew_floor < target) {
                ++stats_.later_cuts;
                unsigned frel[4] = {0, 0, 0, 0};
                // The top run of spades is the only rank the bound can have read.
                int run = 0;
                const unsigned all_sp = key.all[0];
                if (all_sp) {
                    for (int i = 0; i < 4; ++i) {
                        const unsigned mine = sbits(p.h[i], 0);
                        if ((mine >> top_bit(all_sp)) & 1u) run = top_run(mine, all_sp);
                    }
                }
                frel[0] = top_k_bits(all_sp, run);
                const bool yes = ns_floor >= target;
                store(key, frel, yes ? ns_floor : 0, yes ? t : t - ew_floor, NO_CARD, p.h, t);
                for (int s = 0; s < 4; ++s) rel[s] = frel[s];
                return yes;
            }
        }
    }

    // ---- generate and order moves ------------------------------------------
    const int led = p.len ? card_suit(p.trick[0]) : -1;
    const Hand hand = p.h[seat];
    const Hand legal = legal_moves(hand, p.len, led, p.broken);
    const CardId winning = p.len ? trick_best_card(p.trick, p.len) : NO_CARD;
    const Hand relevant = relevant_cards(p.h, winning);
    const Hand moves = (legal & (legal - 1)) ? distinct_moves(legal, relevant) : legal;

    CardId mv[13];
    int sc[13];
    int n = 0;
    const Hand all = p.h[0] | p.h[1] | p.h[2] | p.h[3];
    const int partner = seat ^ 2;

    bool partner_safe = false;
    if (p.len) {
        int win_seat = -1;
        for (int i = 0; i < p.len; ++i) {
            if (p.trick[i] == winning) win_seat = (p.leader + i) & 3;
        }
        if (win_seat == partner) {
            partner_safe = true;
            for (int q = p.len + 1; q < 4; ++q) {
                if (can_beat(p.h[(p.leader + q) & 3], winning, led)) partner_safe = false;
            }
        }
    }
    const Hand tt_class = tt_card != NO_CARD && (legal & card_bit(tt_card))
                              ? equivalent_moves(tt_card, legal, relevant)
                              : 0;

    // DDS's weights (item 99) where there is a choice to order; the score
    // below is the engine's original order and the control arm (--no-dd-order).
    // A lead computes what the weights read about the trick's start once, for
    // itself and its three followers.
    TrickStart lead_ts;
    const TrickStart* ts = p.ts;
    if (dds_order_ && p.len == 0) {
        trick_start_of(p.h, lead_ts);
        ts = &lead_ts;
    }
    if (dds_order_ && (moves & (moves - 1))) {
        n = dds_order_moves(p.h, p.trick, p.leader, p.len, seat, *ts, legal, moves, relevant,
                            p.len == 0 ? lead_killer_[t] : NO_CARD, tt_class, mv);
    }
    for (Hand m = n ? Hand{0} : moves; m;) {
        const CardId c = take_lowest(m);
        const int s = card_suit(c);
        const int rk = c & 15;
        int score;
        if (p.len == 0) {
            const unsigned all_s = sbits(all, s);
            const unsigned mine_s = sbits(hand, s);
            const int top = top_bit(all_s);
            bool ruff_risk = false;
            if (s != 0) {
                for (int d = 1; d <= 3; d += 2) {
                    const Hand o = p.h[(seat + d) & 3];
                    if (!sbits(o, s) && (o & SPADES)) ruff_risk = true;
                }
            }
            const bool lowest_of_mine = (mine_s & ((1u << rk) - 1u)) == 0;
            if (rk == top) {
                score = ruff_risk ? 20 : 80;  // cash a winner
            } else if ((sbits(p.h[partner], s) >> top) & 1u) {
                score = lowest_of_mine ? (ruff_risk ? 15 : 60) : 10;  // toward partner's winner
            } else {
                score = lowest_of_mine ? 30 : 5;
                if (s == 0) score -= 10;  // a low trump into their tops
            }
            if (tt_class & card_bit(c)) score += 1000;
        } else {
            const bool wins = beats(c, winning);
            const bool following = s == led;
            if (partner_safe) {
                score = wins ? 5 : 60;  // don't overtake a partner who has it
            } else if (wins) {
                bool sure = true;
                for (int q = p.len + 1; q < 4; ++q) {
                    if (can_beat(p.h[(p.leader + q) & 3], c, led)) sure = false;
                }
                score = sure ? 90 : (p.len == 1 ? 25 : 45);
            } else {
                score = 50;
            }
            if (!following) {
                if (s == 0 && !wins) score -= 20;                               // wasted trump
                if (s != 0 && rk == top_bit(sbits(all, s))) score -= 15;        // a winner thrown
            }
        }
        int i = n++;
        const int key_score = score * 16 + (15 - rk);
        while (i > 0 && sc[i - 1] < key_score) {
            sc[i] = sc[i - 1];
            mv[i] = mv[i - 1];
            --i;
        }
        sc[i] = key_score;
        mv[i] = c;
    }

    // ---- expand --------------------------------------------------------------
    bool result = !ns_to_move;
    CardId best = NO_CARD;
    unsigned acc[4] = {0, 0, 0, 0};
    unsigned facc[4] = {0, 0, 0, 0};
    // LOWEST WIN (DDS's Moves::MakeNext; item 99, lowest_win).  Per suit, the
    // rank below which the mover's remaining cards of that suit are already
    // known to fail: 0 until a refuted move sets it.
    //
    // THE RULE.  A move that came back without a cut has a refutation, and the
    // ranks that refutation relied on are its `crel`: every card that won a
    // trick by rank somewhere in it, and every rank a stored fact or a static
    // bound read.  If the refuted card's own rank is below the lowest such rank
    // in its suit -- below `low`, taken over everything refuted at this node so
    // far, which can only be lower -- then nothing in the refutation compared
    // that card with anything, and the same refutation answers any other card
    // of the mover's in that suit that is also below `low`: swap the two cards'
    // names and every trick along every line of it has the same winner.  So
    // those cards are not searched.  That is DDS's lowest_win, and it costs
    // DDS 1.8x in nodes to turn off.
    //
    // WHY IT NEEDS NO STRADDLE REPAIR, when the static classes below do.  The
    // static collapse calls two cards equal because no LIVE card sits between
    // them -- a fact about this position, which a stored fact read back on a
    // different position need not share.  This equality is a fact about the
    // refutation: it holds in every position that agrees on the ranks the
    // refutation read, and the fact this node stores pins at least those
    // (`acc` contains the refuted move's `crel`, so the stored cut is at or
    // below `low`).  In any position the fact is read back on, the mover's
    // cards of that suit below `low` are refuted by the same refutation, pinned
    // or not.
    unsigned lowest_win[4] = {0, 0, 0, 0};
    for (int i = 0; i < n; ++i) {
        const CardId c = mv[i];
        if (lowest_win_ && (c & 15) < static_cast<int>(lowest_win[card_suit(c)])) {
            ++stats_.lowest_win_skips;
            continue;
        }
        Pos child = p;
        child.h[seat] &= ~card_bit(c);
        child.broken = spades_broken_after(p.broken, card_suit(c));
        child.ts = ts;
        int child_target = target;
        CardId trick_win = NO_CARD;
        if (p.len < 3) {
            child.trick[p.len] = c;
            child.len = p.len + 1;
        } else {
            const CardId played[4] = {p.trick[0], p.trick[1], p.trick[2], c};
            const int winner = trick_winner(p.leader, played, 4);
            trick_win = played[(winner - p.leader) & 3];
            child.leader = winner;
            child.len = 0;
            if ((winner & 1) == 0) --child_target;
        }
        unsigned crel[4], cforb[4];
        const bool r = search(child, child_target, nullptr, crel, cforb);
        // The trick's winner is a rank the proof relied on -- but only when it
        // won BY RANK, beating another card of its own suit (DDS 6.1, the rule
        // the main search's TRACK backup already follows; item 99,
        // win_by_rank).  A ruff with no other spade in the trick wins whatever
        // the spade's rank, and so does a card nobody else could follow or
        // ruff; pinning such a card pinned its whole suit from the top down to
        // it for nothing, which cost the stored fact most of its reach.
        if (trick_win != NO_CARD) {
            const int ws = card_suit(trick_win);
            bool by_rank = true;
            if (win_by_rank_) {
                const int same = (card_suit(p.trick[0]) == ws) + (card_suit(p.trick[1]) == ws) +
                                 (card_suit(p.trick[2]) == ws) + (card_suit(c) == ws);
                by_rank = same >= 2;
            }
            if (by_rank) crel[ws] |= 1u << (trick_win & 15);
        }
        if (ns_to_move ? r : !r) {
            result = r;
            best = c;
            for (int s = 0; s < 4; ++s) {
                acc[s] = crel[s];
                facc[s] = cforb[s];
            }
            if (p.len == 0 && t < 14) lead_killer_[t] = c;
            break;
        }
        for (int s = 0; s < 4; ++s) {
            acc[s] |= crel[s];
            facc[s] |= cforb[s];
        }
        if (lowest_win_) {
            const int cs = card_suit(c);
            if (lowest_win[cs] == 0) {
                // The lowest rank the refutations so far relied on in this
                // suit, AFTER the straddle repair below would lower it: a
                // refuted class's range under the cut takes the cut down to its
                // representative, exactly as it will when the boundary above
                // stores the fact.  16 when the suit was never read at all --
                // then every card of it the mover holds is refuted alike.
                int low = 16;
                if (acc[cs]) {
                    unsigned cut = acc[cs] & (0u - acc[cs]);
                    if (facc[cs] & cut) {
                        while (facc[cs] & (cut >> 1)) cut >>= 1;
                        cut >>= 1;
                    }
                    low = cut ? lowest_card(static_cast<Hand>(cut)) : 0;
                }
                if ((c & 15) < low) lowest_win[cs] = static_cast<unsigned>(low);
            }
        }
    }
    // A REFUTED CLASS MUST NOT STRADDLE THE CUT.
    //
    // A stored fact pins the owners of the top k cards of each suit, down to
    // the lowest card the proof relied on, and lets everything below vary.  A
    // node that looked at ALL of its moves (none worked) claims more than one
    // that cut: every move of the mover fails, and it only tried one
    // representative -- the lowest card -- per class of rank-equivalent cards.
    // If a class runs across that cut, with its upper members pinned and its
    // representative below, the proof does not transfer.  In a position the
    // fact is read back on, the pinned upper members are still the mover's, but
    // the card under them can now belong to someone else, so playing the upper
    // member is a DIFFERENT move from playing a low card -- and it is the one
    // the proof never tried.
    //
    // Measured, not hypothetical: from 8 tricks, East holds ST S9 S8 over
    // North's S7, one spade class represented by the eight.  The proof pinned
    // the ten and the nine only; the fact was then read back on East ST S9 S5
    // under the S7, where the nine and the five are two moves, and North-South
    // lose a trick to the one the proof skipped.  The engine said 2 tricks where
    // there is 1, and a 9-trick settled position 4 where there are 2, which the
    // general search disagrees with.
    //
    // The repair is to lower the cut to the representative whenever it would
    // land strictly inside a refuted class, which makes the whole class match
    // exactly wherever the fact applies.  Where the cut lands is only known at
    // the trick boundary that stores the fact -- the proof below adds cards to
    // it -- so an all-node reports the rank ranges its classes cover, (lowest,
    // highest], as `forb`; they ride up to the boundary with the proof they
    // belong to (the cutting child's at a cut node, every child's at an all-node)
    // and are applied there.  Cards the range skips (ranks no hand holds any
    // more) are harmless to include.  A class wholly above the cut is pinned
    // already, and one wholly below it is low cards on both sides: any of the
    // mover's low cards stands in for any other, as the generalization assumes.
    //
    // An earlier repair pinned EVERY representative at every all-node.  That is
    // also sound, but it pinned each suit the mover could play down to its
    // lowest card, and it cost up to a factor 6 in wall time on the 13-card
    // benchmark; this rule pins only where the proof needs it.
    //
    // A node that CUT needs none of this: the working move is either pinned or
    // a low card, and the mover has a card like it in every position the fact
    // covers.
    if (best == NO_CARD) {  // the loop ran out without a cut
        const Hand members = legal & ~moves;  // every class member but its representative
        if (members) {
            // Fill downward from each member through ranks nobody holds and
            // through other members; it stops just above the representative.
            Hand pass = (members | ~relevant) & ~SUIT_PADDING;
            Hand f = members;
            f |= pass & (f >> 1);
            pass &= pass >> 1;
            f |= pass & (f >> 2);
            pass &= pass >> 2;
            f |= pass & (f >> 4);
            pass &= pass >> 4;
            f |= pass & (f >> 8);
            for (int s = 0; s < 4; ++s) facc[s] |= sbits(f, s);
        }
    }
    if (p.len == 0) {
        // Where each suit's cut lands, and whether it lands inside a class.
        for (int s = 0; s < 4; ++s) {
            if (!(acc[s] & facc[s])) continue;  // nothing pinned in the ranges
            unsigned cut = acc[s] & (0u - acc[s]);  // lowest pinned rank
            if (!(facc[s] & cut)) continue;
            while (facc[s] & (cut >> 1)) cut >>= 1;
            acc[s] |= cut >> 1;  // the representative under the range
        }
    } else {
        for (int s = 0; s < 4; ++s) forb[s] = facc[s];
    }
    for (int s = 0; s < 4; ++s) rel[s] = acc[s];
    if (root) *witness = best != NO_CARD ? best : (n ? mv[0] : NO_CARD);

    if (p.len == 0) {
        store(key, rel, result ? target : 0, result ? t : target - 1, best, p.h, t);
    }
    return result;
}

bool Engine::ns_reach(const Hand hands[4], int leader, bool broken, int target,
                      CardId* witness) {
    ++stats_.probes;
    Pos p;
    for (int i = 0; i < 4; ++i) p.h[i] = hands[i];
    p.leader = leader & 3;
    p.len = 0;
    p.broken = broken;
    p.trick[0] = p.trick[1] = p.trick[2] = p.trick[3] = NO_CARD;
    p.ts = nullptr;
    if (witness) *witness = NO_CARD;
    unsigned rel[4], forb[4];
    return search(p, target, witness, rel, forb);
}

int Engine::ns_exact(const Hand hands[4], int leader, bool broken, int lo, int hi,
                     CardId* best) {
    const int t = pop(hands[leader & 3]);
    lo = std::max(lo, 0);
    hi = std::min(hi, t);
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (ns_reach(hands, leader, broken, mid, nullptr)) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    if (best) {
        *best = NO_CARD;
        const bool ns_leads = (leader & 1) == 0;
        if (ns_leads && lo > 0) {
            ns_reach(hands, leader, broken, lo, best);
        } else if (!ns_leads && lo < t) {
            ns_reach(hands, leader, broken, lo + 1, best);
        }
        if (*best == NO_CARD) {
            // Every move is optimal here; any legal lead will do.
            const Hand legal = legal_moves(hands[leader & 3], 0, -1, broken);
            if (legal) *best = lowest_card(legal);
        }
    }
    return lo;
}

Engine& engine() {
    static thread_local Engine e;
    return e;
}

}  // namespace dd
}  // namespace nil
