#include "nil/ddtricks.hpp"

#include <algorithm>

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

}  // namespace

// ---- table -----------------------------------------------------------------

constexpr int WAYS = 4;
constexpr int HEADER_WAYS = 4;

Engine::Engine() { resize(DD_DEFAULT_MEGABYTES); }

void Engine::resize(std::size_t megabytes) {
    megabytes_ = megabytes;
    if (megabytes == 0) {
        table_.clear();
        table_.shrink_to_fit();
        headers_.clear();
        headers_.shrink_to_fit();
        mask_ = hmask_ = 0;
        return;
    }
    // Seven eighths to facts, one eighth to the profile headers.
    const std::size_t budget = megabytes * 1024u * 1024u;
    std::size_t buckets = 1;
    while (buckets * 2 * WAYS * sizeof(Entry) <= budget / 8 * 7) buckets *= 2;
    table_.assign(buckets * WAYS, Entry{});
    mask_ = buckets - 1;
    std::size_t hbuckets = 1;
    while (hbuckets * 2 * HEADER_WAYS * sizeof(Header) <= budget / 8) hbuckets *= 2;
    headers_.assign(hbuckets * HEADER_WAYS, Header{});
    hmask_ = hbuckets - 1;
}

void Engine::clear() {
    std::fill(table_.begin(), table_.end(), Entry{});
    std::fill(headers_.begin(), headers_.end(), Header{});
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
    if (headers_.empty()) return nullptr;
    const std::uint8_t meta = static_cast<std::uint8_t>(key.meta | 0x80);
    Header* b = &headers_[(mix(key.lengths, key.meta ^ 0x5A) & hmask_) * HEADER_WAYS];
    for (int i = 0; i < HEADER_WAYS; ++i) {
        if (b[i].meta == meta && b[i].lengths == key.lengths) return &b[i];
    }
    if (!create) return nullptr;
    Header* victim = &b[0];
    for (int i = 0; i < HEADER_WAYS; ++i) {
        if (!(b[i].meta & 0x80)) {
            victim = &b[i];
            break;
        }
        if (b[i].count < victim->count) victim = &b[i];
    }
    *victim = Header{};
    victim->lengths = key.lengths;
    victim->meta = meta;
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
            return &e;
        }
    }
    if (!create) return nullptr;
    Entry* victim = &b[0];
    for (int i = 0; i < WAYS; ++i) {
        if (!(b[i].meta & 0x80)) {
            victim = &b[i];
            break;
        }
        if (b[i].depth < victim->depth) victim = &b[i];
    }
    *victim = Entry{};
    victim->lengths = lengths;
    victim->meta = meta;
    for (int s = 0; s < 4; ++s) victim->pat[s] = pat[s];
    victim->lo = 0;
    victim->hi = static_cast<std::int8_t>(depth);
    victim->move = 0xFF;
    victim->depth = static_cast<std::uint8_t>(depth);
    return victim;
}

bool Engine::probe(const Key& key, int target, bool& result, CardId& move, const Hand h[4],
                   std::uint32_t pat_out[4]) {
    if (table_.empty()) return false;
    ++stats_.tt_probes;
    const Header* hd = header(key, false);
    if (!hd) return false;
    for (int i = 0; i < hd->count; ++i) {
        std::uint32_t pat[4];
        truncate(key.code, key.n, hd->prof[i], pat);
        const Entry* e = fact(key.lengths, key.meta, pat, false, 0);
        if (!e) continue;
        if (move == NO_CARD && e->move != 0xFF) move = from_rel(e->move, h);
        if (e->lo >= target || e->hi < target) {
            result = e->lo >= target;
            for (int s = 0; s < 4; ++s) pat_out[s] = e->pat[s];
            return true;
        }
    }
    return false;
}

void Engine::store(const Key& key, const unsigned rel[4], int lo, int hi, CardId move,
                   const Hand h[4], int depth) {
    if (table_.empty()) return;
    std::uint16_t prof = 0;
    for (int s = 0; s < 4; ++s) {
        const unsigned r = rel[s] & key.all[s];
        int k = 0;
        if (r) k = pop32(key.all[s] >> lowest_card(static_cast<Hand>(r)));
        prof = static_cast<std::uint16_t>(prof | (k << (4 * s)));
    }
    Header* hd = header(key, true);
    int i = 0;
    while (i < hd->count && hd->prof[i] != prof) ++i;
    if (i == hd->count) {
        if (hd->count < PROFILES) {
            hd->prof[hd->count++] = prof;
        } else {
            hd->prof[hd->next] = prof;  // the displaced profile's facts go unreachable
            hd->next = static_cast<std::uint8_t>((hd->next + 1) % PROFILES);
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
bool Engine::search(const Pos& p, int target, CardId* witness, unsigned rel[4]) {
    ++stats_.nodes;
    rel[0] = rel[1] = rel[2] = rel[3] = 0;
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

        // Build the key.
        key.lengths = 0;
        for (int s = 0; s < 4; ++s) {
            unsigned hs[4];
            for (int i = 0; i < 4; ++i) {
                hs[i] = sbits(p.h[i], s);
                key.lengths |= static_cast<std::uint64_t>(pop32(hs[i])) << (16 * s + 4 * i);
            }
            unsigned all = hs[0] | hs[1] | hs[2] | hs[3];
            key.all[s] = all;
            std::uint32_t code = 0;
            int n = 0;
            while (all) {
                const int r = top_bit(all);
                all &= ~(1u << r);
                code = (code << 2) | (((hs[1] >> r) & 1u) * 1u + ((hs[2] >> r) & 1u) * 2u +
                                      ((hs[3] >> r) & 1u) * 3u);
                ++n;
            }
            key.code[s] = code;
            key.n[s] = n;
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

    for (Hand m = moves; m;) {
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
    for (int i = 0; i < n; ++i) {
        const CardId c = mv[i];
        Pos child = p;
        child.h[seat] &= ~card_bit(c);
        child.broken = spades_broken_after(p.broken, card_suit(c));
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
        unsigned crel[4];
        const bool r = search(child, child_target, nullptr, crel);
        if (trick_win != NO_CARD) crel[card_suit(trick_win)] |= 1u << (trick_win & 15);
        if (ns_to_move ? r : !r) {
            result = r;
            best = c;
            for (int s = 0; s < 4; ++s) acc[s] = crel[s];
            break;
        }
        for (int s = 0; s < 4; ++s) acc[s] |= crel[s];
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
    if (witness) *witness = NO_CARD;
    unsigned rel[4];
    return search(p, target, witness, rel);
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
