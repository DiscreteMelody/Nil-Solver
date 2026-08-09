#include "nil/search.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#include "nil/rules.hpp"

namespace nil {
namespace {

struct State {
    Hand hands[4];
    int leader;
    CardId trick[3];
    int trick_len;
    bool broken;

    bool empty() const { return (hands[0] | hands[1] | hands[2] | hands[3]) == 0; }
    int to_play() const { return (leader + trick_len) & 3; }
    int led_suit() const { return trick_len ? card_suit(trick[0]) : -1; }
};

struct Key {
    Hand h[4];
    std::uint32_t packed;

    bool operator==(const Key& o) const {
        return h[0] == o.h[0] && h[1] == o.h[1] && h[2] == o.h[2] && h[3] == o.h[3] &&
               packed == o.packed;
    }
};

struct KeyHash {
    std::size_t operator()(const Key& k) const {
        std::uint64_t x = 1469598103934665603ull;
        const auto mix = [&x](std::uint64_t v) {
            x ^= v;
            x *= 1099511628211ull;
            x ^= x >> 29;
        };
        mix(k.h[0]);
        mix(k.h[1]);
        mix(k.h[2]);
        mix(k.h[3]);
        mix(k.packed);
        return static_cast<std::size_t>(x);
    }
};

Key make_key(const State& s) {
    Key k;
    k.h[0] = s.hands[0];
    k.h[1] = s.hands[1];
    k.h[2] = s.hands[2];
    k.h[3] = s.hands[3];
    std::uint32_t p = static_cast<std::uint32_t>(s.leader) |
                      (static_cast<std::uint32_t>(s.trick_len) << 2) |
                      (static_cast<std::uint32_t>(s.broken ? 1 : 0) << 4);
    for (int i = 0; i < s.trick_len; ++i) {
        p |= (static_cast<std::uint32_t>(s.trick[i]) & 0x3Fu) << (5 + 6 * i);
    }
    k.packed = p;
    return k;
}

struct Entry {
    std::int16_t value;
    std::int8_t move;
};

struct Ctx {
    int nil_seat = 0;
    bool break_forced = false;
    bool use_memo = true;
    std::uint64_t nodes = 0;
    std::unordered_map<Key, Entry, KeyHash> memo;
};

// Returns the nil bidder's trick count from `st` onwards, and the canonically
// chosen best move for the seat to play.
//
// No alpha-beta, no move ordering, no rank-equivalence collapsing, no quick
// tricks.  Candidate moves are enumerated in canonical order and replace the
// incumbent only on a STRICT improvement, so among equal-valued moves the
// canonically lowest card wins and the PV is reproducible.  This is the same
// tie-break nil_oracle.py uses.
int search(Ctx& ctx, const State& st, CardId& best_move) {
    ++ctx.nodes;
    best_move = NO_CARD;
    if (st.empty()) return 0;

    Key key;
    if (ctx.use_memo) {
        key = make_key(st);
        const auto it = ctx.memo.find(key);
        if (it != ctx.memo.end()) {
            best_move = it->second.move;
            return it->second.value;
        }
    }

    const int seat = st.to_play();
    // The nil bidder and its partner minimise; the two opponents maximise.
    const bool maximizing = ((seat ^ ctx.nil_seat) & 1) != 0;
    const int led = st.led_suit();

    Hand moves = legal_moves(st.hands[seat], st.trick_len, led, st.broken);
    int best = 0;
    bool have_best = false;

    while (moves) {
        const CardId card = take_lowest(moves);

        State next = st;
        next.hands[seat] &= ~card_bit(card);
        next.broken = spades_broken_after(st.broken, st.trick_len, led, card_suit(card),
                                          ctx.break_forced);

        int value;
        CardId ignored;
        if (st.trick_len == 3) {
            const CardId played[4] = {st.trick[0], st.trick[1], st.trick[2], card};
            const int winner = trick_winner(st.leader, played, 4);
            next.leader = winner;
            next.trick_len = 0;
            value = (winner == ctx.nil_seat ? 1 : 0) + search(ctx, next, ignored);
        } else {
            next.trick[st.trick_len] = card;
            next.trick_len = st.trick_len + 1;
            value = search(ctx, next, ignored);
        }

        if (!have_best || (maximizing ? value > best : value < best)) {
            have_best = true;
            best = value;
            best_move = card;
        }
    }

    if (ctx.use_memo) {
        ctx.memo.emplace(key, Entry{static_cast<std::int16_t>(best),
                                    static_cast<std::int8_t>(best_move)});
    }
    return best;
}

State state_of(const Position& pos) {
    State st;
    for (int s = 0; s < 4; ++s) st.hands[s] = pos.hands[s];
    st.leader = pos.leader;
    st.trick_len = pos.trick_len;
    for (int i = 0; i < 3; ++i) st.trick[i] = pos.trick[i];
    st.broken = pos.spades_broken;
    return st;
}

std::string with_commas(std::uint64_t n) {
    std::string digits = std::to_string(n);
    std::string out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count && count % 3 == 0) out += ',';
        out += *it;
        ++count;
    }
    return std::string(out.rbegin(), out.rend());
}

}  // namespace

bool solve(const Position& pos, int nil_seat, const SearchOptions& opts, Solution& out,
           std::string& err) {
    if (nil_seat < 0 || nil_seat > 3) {
        err = "nil seat out of range";
        return false;
    }
    if (!validate(pos, err)) return false;

    Ctx ctx;
    ctx.nil_seat = nil_seat;
    ctx.break_forced = opts.break_on_forced_spade_lead;
    ctx.use_memo = opts.use_memo;

    State st = state_of(pos);
    CardId move = NO_CARD;
    const int value = search(ctx, st, move);
    const std::uint64_t search_nodes = ctx.nodes;

    // Walk the chosen moves to recover the principal variation.  With the memo
    // on this is a handful of lookups; with it off each step re-searches a
    // strictly smaller subtree.  Either way the moves are the same ones the
    // search picked, so the PV matches the oracle's play for play.
    out.pv.clear();
    while (!st.empty()) {
        const int seat = st.to_play();
        out.pv.push_back(Play{seat, move});

        const int led = st.led_suit();
        State next = st;
        next.hands[seat] &= ~card_bit(move);
        next.broken = spades_broken_after(st.broken, st.trick_len, led, card_suit(move),
                                          ctx.break_forced);
        if (st.trick_len == 3) {
            const CardId played[4] = {st.trick[0], st.trick[1], st.trick[2], move};
            next.leader = trick_winner(st.leader, played, 4);
            next.trick_len = 0;
        } else {
            next.trick[st.trick_len] = move;
            next.trick_len = st.trick_len + 1;
        }
        st = next;
        if (st.empty()) break;
        search(ctx, st, move);
        if (move == NO_CARD) {
            err = "internal error: no move available at a non-terminal position";
            return false;
        }
    }

    out.tricks = value;
    out.nil_fails = value > 0;
    out.nil_seat = nil_seat;
    out.nodes = search_nodes;

    // A solver that lies is worse than no solver.
    int replayed = 0;
    if (!replay_pv(pos, out.pv, nil_seat, opts.break_on_forced_spade_lead, replayed, err)) {
        err = "internal inconsistency: " + err;
        return false;
    }
    if (replayed != value) {
        std::ostringstream os;
        os << "internal inconsistency: search says " << value << ", replaying the PV gives "
           << replayed;
        err = os.str();
        return false;
    }
    return true;
}

bool replay_pv(const Position& pos, const std::vector<Play>& pv, int nil_seat,
               bool break_on_forced_spade_lead, int& tricks_out, std::string& err) {
    Hand hands[4];
    for (int s = 0; s < 4; ++s) hands[s] = pos.hands[s];
    int leader = pos.leader;
    bool broken = pos.spades_broken;
    CardId trick[4];
    int trick_len = pos.trick_len;
    for (int i = 0; i < pos.trick_len; ++i) trick[i] = pos.trick[i];
    tricks_out = 0;

    for (std::size_t ply = 0; ply < pv.size(); ++ply) {
        const int seat = pv[ply].seat;
        const CardId card = pv[ply].card;
        const int expected = (leader + trick_len) & 3;
        std::ostringstream os;
        if (seat < 0 || seat > 3) {
            os << "ply " << ply << ": seat out of range";
            err = os.str();
            return false;
        }
        if (seat != expected) {
            os << "ply " << ply << ": " << SEAT_CHARS[seat] << " played out of turn (expected "
               << SEAT_CHARS[expected] << ")";
            err = os.str();
            return false;
        }
        if (card < 0 || card > 63 || !(hands[seat] & card_bit(card))) {
            os << "ply " << ply << ": " << SEAT_CHARS[seat] << " does not hold "
               << (card >= 0 && card <= 63 ? card_to_string(card) : std::string("?"));
            err = os.str();
            return false;
        }
        const int led = trick_len ? card_suit(trick[0]) : -1;
        const Hand allowed = legal_moves(hands[seat], trick_len, led, broken);
        if (!(allowed & card_bit(card))) {
            os << "ply " << ply << ": " << SEAT_CHARS[seat] << " played " << card_to_string(card)
               << ", which is illegal; legal: " << hand_to_string(allowed);
            err = os.str();
            return false;
        }
        hands[seat] &= ~card_bit(card);
        broken = spades_broken_after(broken, trick_len, led, card_suit(card),
                                     break_on_forced_spade_lead);
        trick[trick_len++] = card;
        if (trick_len == 4) {
            const int winner = trick_winner(leader, trick, 4);
            if (winner == nil_seat) ++tricks_out;
            leader = winner;
            trick_len = 0;
        }
    }

    if (trick_len) {
        err = "PV ends mid-trick";
        return false;
    }
    if (hands[0] | hands[1] | hands[2] | hands[3]) {
        err = "PV does not play out every card";
        return false;
    }
    return true;
}

std::string format_pv_compact(const Solution& sol) {
    std::ostringstream os;
    for (std::size_t i = 0; i < sol.pv.size(); ++i) {
        if (i) os << ' ';
        os << SEAT_CHARS[sol.pv[i].seat] << ':' << card_to_string(sol.pv[i].card);
    }
    return os.str();
}

std::string format_pv(const Position& pos, const Solution& sol) {
    struct Item {
        int seat;
        CardId card;
        bool from_pv;
    };
    std::vector<Item> plays;
    for (int i = 0; i < pos.trick_len; ++i)
        plays.push_back(Item{(pos.leader + i) & 3, pos.trick[i], false});
    for (const Play& p : sol.pv) plays.push_back(Item{p.seat, p.card, true});

    std::ostringstream os;
    int leader = pos.leader;
    int running = 0;
    bool first_line = true;
    for (std::size_t i = 0; i < plays.size(); i += 4) {
        const std::size_t n = std::min<std::size_t>(4, plays.size() - i);
        CardId cards[4];
        for (std::size_t j = 0; j < n; ++j) cards[j] = plays[i + j].card;
        const int winner = (n == 4) ? trick_winner(leader, cards, 4) : -1;
        if (winner == sol.nil_seat) ++running;

        if (!first_line) os << '\n';
        first_line = false;
        os << "  T" << (i / 4 + 1) << " ";
        for (std::size_t j = 0; j < n; ++j) {
            std::string cell = std::string(1, SEAT_CHARS[plays[i + j].seat]) + ":" +
                               card_to_string(plays[i + j].card);
            while (cell.size() < 5) cell += ' ';
            os << ' ' << cell << (plays[i + j].from_pv ? ' ' : '*');
        }
        os << "  won by " << (winner >= 0 ? std::string(1, SEAT_CHARS[winner]) : std::string("?"))
           << "   [" << SEAT_CHARS[sol.nil_seat] << '=' << running << ']';
        if (winner == sol.nil_seat) os << " <-- nil takes a trick";
        if (winner < 0) break;
        leader = winner;
    }
    return os.str();
}

std::string format_solution(const Position& pos, const Solution& sol) {
    const bool nil_is_ns = (sol.nil_seat & 1) == 0;
    std::ostringstream os;
    os << "PBN            " << deal_to_pbn(pos.hands) << '\n'
       << format_hands(pos) << '\n'
       << "Leader         " << SEAT_CHARS[pos.leader] << '\n'
       << "Nil bidder     " << SEAT_CHARS[sol.nil_seat] << "  ("
       << (nil_is_ns ? "N/S minimise, E/W maximise" : "E/W minimise, N/S maximise") << ")\n"
       << "Spades broken  " << (pos.spades_broken ? "yes" : "no") << '\n';
    if (pos.trick_len) {
        os << "On the trick   ";
        for (int i = 0; i < pos.trick_len; ++i) {
            if (i) os << ' ';
            os << card_to_string(pos.trick[i]);
        }
        os << "  (marked * below)\n";
    }
    os << "Tricks for " << SEAT_CHARS[sol.nil_seat] << "   " << sol.tricks << " of "
       << pos.tricks_remaining() << '\n'
       << "Nil            " << (sol.nil_fails ? "FAILS  (can be forced to take a trick)"
                                              : "MAKES  (cannot be forced to take a trick)")
       << '\n'
       << "Nodes          " << with_commas(sol.nodes) << '\n'
       << "Principal variation:\n"
       << format_pv(pos, sol) << '\n'
       << "Compact PV:\n"
       << "  " << format_pv_compact(sol);
    return os.str();
}

}  // namespace nil
