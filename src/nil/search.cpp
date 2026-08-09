#include "nil/search.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "nil/rules.hpp"
#include "nil/statekey.hpp"
#include "nil/tt.hpp"

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

// One table per thread, reused across solves and invalidated by generation
// rather than by clearing.  See release_transposition_table().
TranspositionTable& shared_table() {
    static thread_local TranspositionTable table;
    return table;
}

struct Ctx {
    int nil_seat = 0;
    int primary_weight = 0;    // K*K, or 0 when the nil is already set
    int secondary_weight = -1; // -K the nil side wants tricks, +K it wants rid of them
    int tertiary_weight = 0;   // 1 when the cover partner's share is what counts
    bool break_forced = false;
    std::uint64_t nodes = 0;
    TranspositionTable* tt = nullptr;  // null when the caller turned it off
};

// Returns the packed objective value from `st` onwards (see objective_weights),
// and the canonically chosen best move for the seat to play.  The nil side
// minimises the value; the opponents maximise it.
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

    // The key describes the position up to a relabelling of ranks, so the move
    // that comes back out of the table is a slot number rather than a card and
    // has to be read against THIS position's live cards.  That relabelling is
    // order preserving -- it never moves a card across a suit and never
    // reorders two cards within one -- so the canonically lowest of several
    // equally good moves is still the canonically lowest one after it, and the
    // principal variation is the same one an uncached search would produce.
    StateKey key;
    SuitProfile profile;
    std::uint64_t hash = 0;
    bool keyed = false;
    if (ctx.tt) {
        keyed = encode_state_key(st.hands, st.leader, st.broken, st.trick, st.trick_len, key,
                                 profile);
        if (keyed) {
            hash = mix_key(key);
            if (const TTEntry* hit = ctx.tt->probe(key, hash)) {
                best_move = from_relative(hit->move, profile);
                return hit->value;
            }
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
            int gained = 0;
            if (winner == ctx.nil_seat) gained += ctx.primary_weight + ctx.tertiary_weight;
            if (((winner ^ ctx.nil_seat) & 1) == 0) gained += ctx.secondary_weight;
            value = gained + search(ctx, next, ignored);
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

    if (keyed) {
        const RelMove rel = best_move == NO_CARD ? REL_NO_MOVE : to_relative(best_move, profile);
        ctx.tt->store(key, hash, best, rel, profile.total, BOUND_EXACT);
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

ObjectiveWeights objective_weights(int tricks_remaining, const SearchOptions& opts) {
    const int k = tricks_remaining + 1;
    ObjectiveWeights w;
    w.primary = opts.nil_already_set ? 0 : k * k;
    w.secondary = opts.minimise_own_tricks ? k : -k;
    w.tertiary = opts.minimise_own_tricks ? 0 : 1;
    return w;
}

bool solve(const Position& pos, int nil_seat, const SearchOptions& opts, Solution& out,
           std::string& err) {
    if (nil_seat < 0 || nil_seat > 3) {
        err = "nil seat out of range";
        return false;
    }
    if (!validate(pos, err)) return false;

    const ObjectiveWeights weights = objective_weights(pos.tricks_remaining(), opts);

    Ctx ctx;
    ctx.nil_seat = nil_seat;
    ctx.primary_weight = weights.primary;
    ctx.secondary_weight = weights.secondary;
    ctx.tertiary_weight = weights.tertiary;
    ctx.break_forced = opts.break_on_forced_spade_lead;

    TranspositionTable& table = shared_table();
    if (opts.use_memo && opts.tt_megabytes > 0) {
        table.resize(opts.tt_megabytes);  // a no-op at the size it already is
        table.new_search();               // this solve may not see the last one's values
        ctx.tt = &table;
    }

    State st = state_of(pos);
    CardId move = NO_CARD;
    const int value = search(ctx, st, move);
    const std::uint64_t search_nodes = ctx.nodes;
    // Snapshot before the PV walk below, which probes the table again and would
    // otherwise inflate the hit count with lookups that did no search work.
    const TTStats table_stats = ctx.tt ? ctx.tt->stats() : TTStats();

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

    // A solver that lies is worse than no solver.  Replaying recovers the trick
    // counts independently; re-packing them must land back on the search value.
    Tally tally;
    if (!replay_pv(pos, out.pv, nil_seat, opts.break_on_forced_spade_lead, tally, err)) {
        err = "internal inconsistency: " + err;
        return false;
    }
    const int replayed = (weights.primary + weights.tertiary) * tally.nil_tricks +
                         weights.secondary * tally.nil_side_tricks;
    if (replayed != value) {
        std::ostringstream os;
        os << "internal inconsistency: search says " << value << ", replaying the PV gives "
           << replayed << " (nil=" << tally.nil_tricks << ", side=" << tally.nil_side_tricks
           << ")";
        err = os.str();
        return false;
    }

    out.nil_tricks = tally.nil_tricks;
    out.nil_side_tricks = tally.nil_side_tricks;
    out.opponent_tricks = tally.opponent_tricks;
    // When the caller has told us the nil is already broken, that is a fact
    // about the game, not something for the search to rediscover.
    out.nil_fails = opts.nil_already_set || tally.nil_tricks > 0;
    out.value = value;
    out.nil_seat = nil_seat;
    out.nodes = search_nodes;
    out.tt_probes = table_stats.probes;
    out.tt_hits = table_stats.hits;
    out.tt_stores = table_stats.stores;
    out.tt_evictions = table_stats.evictions;
    return true;
}

void release_transposition_table() { shared_table().resize(0); }

bool replay_pv(const Position& pos, const std::vector<Play>& pv, int nil_seat,
               bool break_on_forced_spade_lead, Tally& tally_out, std::string& err) {
    Hand hands[4];
    for (int s = 0; s < 4; ++s) hands[s] = pos.hands[s];
    int leader = pos.leader;
    bool broken = pos.spades_broken;
    CardId trick[4];
    int trick_len = pos.trick_len;
    for (int i = 0; i < pos.trick_len; ++i) trick[i] = pos.trick[i];
    tally_out = Tally{};

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
            if (winner == nil_seat) ++tally_out.nil_tricks;
            if (((winner ^ nil_seat) & 1) == 0) {
                ++tally_out.nil_side_tricks;
            } else {
                ++tally_out.opponent_tricks;
            }
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
        if (winner == sol.nil_seat) ++running;  // primary counter only

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

std::string format_solution(const Position& pos, const Solution& sol,
                            const SearchOptions& opts) {
    const bool nil_is_ns = (sol.nil_seat & 1) == 0;
    const char* side = nil_is_ns ? "NS" : "EW";
    const char* other = nil_is_ns ? "EW" : "NS";
    std::ostringstream os;
    os << "PBN            " << deal_to_pbn(pos.hands) << '\n'
       << format_hands(pos) << '\n'
       << "Leader         " << SEAT_CHARS[pos.leader] << '\n'
       << "Nil bidder     " << SEAT_CHARS[sol.nil_seat] << "  ("
       << (nil_is_ns ? "N/S minimise, E/W maximise" : "E/W minimise, N/S maximise") << ")\n"
       << "Objective      "
       << (opts.nil_already_set ? "nil already set, so secondary only; "
                                : "nil tricks first, then ")
       << (opts.minimise_own_tricks ? "each pair sheds what it can"
                                    : "each pair takes what it can")
       << '\n'
       << "Spades broken  " << (pos.spades_broken ? "yes" : "no") << '\n';
    if (pos.trick_len) {
        os << "On the trick   ";
        for (int i = 0; i < pos.trick_len; ++i) {
            if (i) os << ' ';
            os << card_to_string(pos.trick[i]);
        }
        os << "  (marked * below)\n";
    }
    os << "Tricks for " << SEAT_CHARS[sol.nil_seat] << "   " << sol.nil_tricks << " of "
       << pos.tricks_remaining() << '\n'
       << "Side tricks    " << side << '=' << sol.nil_side_tricks << "  " << other << '='
       << sol.opponent_tricks << '\n';
    if (opts.nil_already_set) {
        os << "Nil            ALREADY SET (told, not computed)\n";
    } else {
        os << "Nil            "
           << (sol.nil_fails ? "FAILS  (can be forced to take a trick)"
                             : "MAKES  (cannot be forced to take a trick)")
           << '\n';
    }
    os << "Nodes          " << with_commas(sol.nodes) << '\n'
       << "Principal variation:\n"
       << format_pv(pos, sol) << '\n'
       << "Compact PV:\n"
       << "  " << format_pv_compact(sol);
    return os.str();
}

}  // namespace nil
