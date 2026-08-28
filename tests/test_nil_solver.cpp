// Self-tests for the nil solver.
//
// The rule-unit and small-search cases are deliberately the same ones
// nil_oracle.py checks in its selftest(), so a rule-level disagreement shows up
// here rather than as a mystery one-card divergence deep in a random deal.
//
// The end-to-end agreement testing lives in tools/crosscheck.py, which runs the
// real oracle.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "nil/bounds.hpp"
#include "nil/position.hpp"
#include "nil/rules.hpp"
#include "nil/search.hpp"
#include "nil/seats.hpp"
#include "nil/statekey.hpp"
#include "nil/tt.hpp"
#include "nil_solver/nil_solver.h"

namespace {

int g_failures = 0;
bool g_verbose = true;

template <typename T>
std::string to_text(const T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_convertible_v<T, std::string>) {
        return std::string(value);
    } else {
        return std::to_string(value);
    }
}

void check_text(const std::string& name, const std::string& got, const std::string& want) {
    const bool ok = got == want;
    if (!ok) ++g_failures;
    if (g_verbose || !ok) {
        std::cout << "  [" << (ok ? "ok  " : "FAIL") << "] " << name << "\n";
        if (!ok) {
            std::cout << "         got  " << got << "\n";
            std::cout << "         want " << want << "\n";
        }
    }
}

// One entry point for every comparison, so a mixed int/long long call site is
// not an overload puzzle.
template <typename A, typename B>
void check(const std::string& name, const A& got, const B& want) {
    check_text(name, to_text(got), to_text(want));
}

const char* const SEAT_NAMES[4] = {"N", "E", "S", "W"};

// Role lists for the C ABI, which takes them CLOCKWISE FROM THE SEAT THE PBN
// NAMES.  Every deal in this file is N-anchored, so these read N, E, S, W.
const std::int32_t SEATS_NIL_N[4] = {NIL_ROLE_NIL, NIL_ROLE_OPPONENT, NIL_ROLE_COVER,
                                     NIL_ROLE_OPPONENT};
const std::int32_t SEATS_NIL_E[4] = {NIL_ROLE_OPPONENT, NIL_ROLE_NIL, NIL_ROLE_OPPONENT,
                                     NIL_ROLE_COVER};

using nil::CardId;
using nil::Hand;
using nil::Position;
using nil::SearchOptions;
using nil::Solution;

CardId C(const char* text) {
    CardId c = nil::NO_CARD;
    std::string err;
    if (!nil::parse_card(text, c, err)) {
        std::cerr << "test bug: " << err << "\n";
        std::exit(70);
    }
    return c;
}

Hand H(const std::vector<const char*>& names) {
    Hand h = 0;
    for (const char* n : names) h |= nil::card_bit(C(n));
    return h;
}

Position make_position(const char* pbn, const char* leader, bool spades_broken = false,
                       const char* trick = "") {
    Position pos;
    std::string err;
    if (!nil::parse_pbn(pbn, pos.hands, err)) {
        std::cerr << "test bug: " << err << "\n";
        std::exit(70);
    }
    pos.leader = nil::parse_seat(leader);
    pos.spades_broken = spades_broken;
    if (trick && *trick) {
        int count = 0;
        if (!nil::parse_cards(trick, pos.trick, 3, count, err)) {
            std::cerr << "test bug: " << err << "\n";
            std::exit(70);
        }
        pos.trick_len = count;
    }
    if (!nil::validate(pos, err)) {
        std::cerr << "test bug: invalid position: " << err << "\n";
        std::exit(70);
    }
    return pos;
}

// `already_set` used to be SearchOptions::nil_already_set; it is now the nil
// bidder's own role, so it travels with the seat rather than with the options.
Solution must_solve(const Position& pos, const char* nil_seat,
                    const SearchOptions& opts = SearchOptions(), bool already_set = false) {
    Solution sol;
    std::string err;
    const nil::SeatRoles roles =
        nil::seat_roles_from_nil(nil::parse_seat(nil_seat), already_set);
    if (!nil::solve(pos, roles, opts, sol, err)) {
        std::cerr << "test bug: solve failed: " << err << "\n";
        std::exit(70);
    }
    return sol;
}

nil::StateKey key_of(const Position& pos) {
    nil::StateKey key;
    nil::SuitProfile profile;
    nil::encode_state_key(pos.hands, pos.leader, pos.spades_broken, pos.trick, pos.trick_len, key,
                          profile);
    return key;
}

nil::SuitProfile profile_of(const Position& pos) {
    nil::StateKey key;
    nil::SuitProfile profile;
    nil::encode_state_key(pos.hands, pos.leader, pos.spades_broken, pos.trick, pos.trick_len, key,
                          profile);
    return profile;
}

bool key_fits(const Position& pos) {
    nil::StateKey key;
    nil::SuitProfile profile;
    return nil::encode_state_key(pos.hands, pos.leader, pos.spades_broken, pos.trick,
                                 pos.trick_len, key, profile);
}

// xorshift64, so the differential sweep below is a fixed set of deals rather
// than a different one on every run.
struct Rng {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    std::uint64_t next() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
};

// `cards` to each seat off a shuffled deck, plus a leader and a broken flag.
Position random_deal(Rng& rng, int cards) {
    int deck[52];
    for (int i = 0; i < 52; ++i) deck[i] = nil::make_card(i / 13, i % 13 + 2);
    for (int i = 51; i > 0; --i) std::swap(deck[i], deck[rng.below(i + 1)]);
    Position pos;
    for (int seat = 0; seat < 4; ++seat) {
        for (int k = 0; k < cards; ++k) pos.hands[seat] |= nil::card_bit(deck[seat * cards + k]);
    }
    pos.leader = rng.below(4);
    pos.spades_broken = rng.below(2) != 0;
    return pos;
}

int trick_winner_of(const char* leader, const std::vector<const char*>& cards) {
    CardId ids[4];
    for (std::size_t i = 0; i < cards.size(); ++i) ids[i] = C(cards[i]);
    return nil::trick_winner(nil::parse_seat(leader), ids, static_cast<int>(cards.size()));
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quiet") == 0) g_verbose = false;
    }

    std::cout << "Rule units\n";
    check("unbroken lead excludes spades",
          nil::legal_moves(H({"SA", "H3", "C9"}), 0, -1, false), H({"H3", "C9"}));
    check("unbroken lead forced when hand is all spades",
          nil::legal_moves(H({"SA", "S3"}), 0, -1, false), H({"SA", "S3"}));
    check("broken lead allows spades", nil::legal_moves(H({"SA", "H3"}), 0, -1, true),
          H({"SA", "H3"}));
    check("must follow suit",
          nil::legal_moves(H({"SA", "H3", "H9"}), 1, nil::SUIT_HEARTS, true), H({"H3", "H9"}));
    check("void may play anything",
          nil::legal_moves(H({"SA", "C3"}), 1, nil::SUIT_HEARTS, true), H({"SA", "C3"}));

    // Playing a spade breaks spades, with no cases and no convention to pick.
    check("ruff breaks spades", nil::spades_broken_after(false, nil::SUIT_SPADES), true);
    check("discarding a non-spade does not break",
          nil::spades_broken_after(false, nil::SUIT_CLUBS), false);
    check("a spade played on a spade lead breaks too",
          nil::spades_broken_after(false, nil::SUIT_SPADES), true);
    check("a forced spade lead breaks spades",
          nil::spades_broken_after(false, nil::SUIT_SPADES), true);
    check("already broken stays broken",
          nil::spades_broken_after(true, nil::SUIT_CLUBS), true);

    check("highest of led suit wins", trick_winner_of("N", {"D2", "DA", "D5", "D7"}), 1);
    check("any spade beats any non-spade", trick_winner_of("N", {"DA", "S2", "DK", "DQ"}), 1);
    check("highest spade wins", trick_winner_of("S", {"S2", "SA", "DK", "S9"}), 3);
    check("third-suit discard cannot win", trick_winner_of("N", {"D2", "CA", "D3", "HA"}), 2);

    std::cout << "Canonical order\n";
    {
        // Iterating a hand mask must give (suit index, rank) order, which is
        // what makes the PV tie-break match the oracle's.
        Hand h = H({"CA", "S2", "HT", "SA", "D3"});
        std::string order = nil::hand_to_string(h);
        check("mask iterates in canonical order", order, std::string("S2 SA HT D3 CA"));
    }

    std::cout << "Search\n";
    {
        // One card each, all diamonds: E's ace wins, full stop.
        const Position pos = make_position("N:..2. ..A. ..5. ..7.", "N", true);
        check("single trick, nil E", must_solve(pos, "E").nil_tricks, 1);
        check("single trick, nil N", must_solve(pos, "N").nil_tricks, 0);
        check("single trick, nil S", must_solve(pos, "S").nil_tricks, 0);
        check("single trick, nil E fails", must_solve(pos, "E").nils_set, 1);
        check("single trick, nil N makes", must_solve(pos, "N").nils_set, 0);
        check("single trick PV", nil::format_pv_compact(must_solve(pos, "E")),
              std::string("N:D2 E:DA S:D5 W:D7"));
    }
    {
        // The nil bidder holds the bare ace of the only suit in play: exactly
        // one trick, regardless of anyone's intentions.
        const Position pos = make_position("N:.A2.. .K3.. .54.. .Q6..", "N", true);
        check("bare ace guarantees a trick", must_solve(pos, "N").nil_tricks, 1);
        check("bare ace kills the nil", must_solve(pos, "N").nils_set, 1);
    }
    {
        // Squander test.  All hearts, two cards each:
        //   N: HK H2   E: HA H3   S: H5 H4   W: HQ H6     nil N, N leads.
        // N holds the lowest heart in play, so N can win at most with HK => <= 1.
        // E and W each hold exactly one card that beats HK.  They have two
        // tricks and can dump both high cards on the trick where N plays H2,
        // leaving HK to win the other one => >= 1.  Exact answer: 1.
        // A solver whose defenders maximise their OWN tricks would grab the king
        // with the ace and report 0 here.
        const Position pos = make_position("N:.K2.. .A3.. .54.. .Q6..", "N", true);
        check("squander: E/W duck to hand N a trick", must_solve(pos, "N").nil_tricks, 1);

        // The same layout rotated one seat clockwise, with the nil on E.  The
        // coalitions follow the nil bidder's seat, not a fixed parity, so this
        // must give the same answer.
        const Position rotated = make_position("N:.Q6.. .K2.. .A3.. .54..", "E", true);
        check("squander rotated onto E", must_solve(rotated, "E").nil_tricks, 1);
        check("squander rotated: N/S now maximise", must_solve(rotated, "E").nils_set, 1);
    }
    {
        // Spade-break restriction actually constrains the opening lead.
        // N holds SA and one club; spades unbroken, so N must lead the club.
        const Position unbroken = make_position("N:A...2 K...3 Q...4 J...5", "N", false);
        check("unbroken opening lead is the club",
              nil::card_to_string(must_solve(unbroken, "N").pv[0].card), std::string("C2"));
        const Position broken = make_position("N:A...2 K...3 Q...4 J...5", "N", true);
        check("broken opening lead may be the spade",
              nil::card_to_string(must_solve(broken, "N").pv[0].card), std::string("SA"));
    }
    {
        // Nil holds a singleton spade and the defenders can force it in.
        // W leads a heart; N is void and must play its lone spade, winning.
        const Position pos = make_position("N:2... .3.. .5.. .7..", "W", false);
        const Solution sol = must_solve(pos, "N");
        check("void nil must ruff and take the trick", sol.nil_tricks, 1);
        check("void nil fails", sol.nils_set, 1);
    }
    {
        // Position resumed mid-trick: W led H7, N followed with H2, E played
        // H4.  S is on play and holds HQ and H3; whichever it chooses, N's
        // remaining HK never wins.
        const Position pos = make_position("N:.K.. .A.. .Q3.. ...2", "W", true, "H7 H2 H4");
        const Solution sol = must_solve(pos, "N");
        check("mid-trick first play is S's", sol.pv[0].seat, 2);
        check("mid-trick PV covers the rest of the deal",
              static_cast<long long>(sol.pv.size()), 5LL);
        check("mid-trick nil survives", sol.nil_tricks, 0);
    }
    {
        // The memo caches a pure function: identical value AND identical PV.
        const Position pos = make_position("N:A2.K.. K3.A.. Q4.Q.. J5.J..", "N", false);
        SearchOptions memo_on;
        SearchOptions memo_off;
        memo_off.use_memo = false;
        const Solution a = must_solve(pos, "S", memo_on);
        const Solution b = must_solve(pos, "S", memo_off);
        check("memo agrees on value", a.nil_tricks, b.nil_tricks);
        check("memo agrees on PV", nil::format_pv_compact(a), nil::format_pv_compact(b));
        check("PV plays every card", static_cast<long long>(a.pv.size()), 12LL);
    }

    std::cout << "Duck depth under the cover hand (item C0)\n";
    {
        // The examples MOVE_ORDERING.md specifies the cover rules against.  The
        // exhaustive check lives in tools/duck_depth_property.cpp; these are
        // here so that a definition drifting away from what C1-C3 were written
        // against fails in the fast test binary rather than only in the slow one.
        auto ducks = [](std::initializer_list<const char*> nil_cards,
                        std::initializer_list<const char*> cover_cards) {
            return nil::duck_depth(H(nil_cards), H(cover_cards), nil::SUIT_HEARTS);
        };
        check("cover JT87 over nil 954 shelters three",
              ducks({"H9", "H5", "H4"}, {"HJ", "HT", "H8", "H7"}), 3);
        check("cover KQ4 over nil J96 shelters two",
              ducks({"HJ", "H9", "H6"}, {"HK", "HQ", "H4"}), 2);
        check("cover KQ over nil A2 shelters one",
              ducks({"HA", "H2"}, {"HK", "HQ"}), 1);
        check("cover 54 over nil Q32 shelters two",
              ducks({"HQ", "H3", "H2"}, {"H5", "H4"}), 2);

        // A high card shelters ONE card, not every card under it.  This is the
        // whole reason the measure is a matching rather than a count.
        check("one high cover shelters one card", ducks({"H9", "H8", "H7"}, {"HA"}), 1);
        check("three high covers shelter three", ducks({"H9", "H8", "H7"}, {"HA", "HK", "HQ"}),
              3);

        // The two ways to be worth nothing, and they are different.
        check("a void nil ducks nothing", ducks({}, {"HA", "HK"}), 0);
        check("a void cover shelters nothing", ducks({"H2", "H3"}, {}), 0);
        check("a cover entirely below the nil shelters nothing",
              ducks({"HA", "HK"}, {"H3", "H2"}), 0);

        // Bounded by the shorter holding, whatever the ranks say.
        check("one cover card caps the count at one", ducks({"H2", "H3", "H4"}, {"HA"}), 1);

        // Suits are independent: the same shape in clubs reads the same, and a
        // holding in another suit is not visible to the walk.
        check("the same shape in clubs",
              nil::duck_depth(H({"C9", "C5", "C4"}), H({"CJ", "CT", "C8", "C7"}),
                              nil::SUIT_CLUBS),
              3);
        check("another suit's cards are not counted",
              nil::duck_depth(H({"C9", "SA", "SK"}), H({"CJ", "S2"}), nil::SUIT_CLUBS), 1);
    }

    std::cout << "Equivalent-card reduction\n";
    {
        // With the queen gone, holding SK and SJ is holding one card twice: the
        // representative is the canonically lower of the two, which is the one
        // the tie-break would have chosen anyway.
        const Hand held = H({"SK", "SJ"});
        check("a dead gap collapses the pair", nil::distinct_moves(held, held), H({"SJ"}));
        check("a live card between them keeps both",
              nil::distinct_moves(held, held | H({"SQ"})), held);

        // The gap may be any width, and a run of any length is still one move.
        check("the widest gap collapses", nil::distinct_moves(H({"SA", "S2"}), H({"SA", "S2"})),
              H({"S2"}));
        check("a run of three is one move",
              nil::distinct_moves(H({"SA", "SK", "SQ"}), H({"SA", "SK", "SQ"})), H({"SQ"}));

        // Two runs split by a card somebody else holds are two moves.
        check("a run either side of a live card gives two",
              nil::distinct_moves(H({"SA", "SK", "S3", "S2"}),
                                  H({"SA", "SK", "SQ", "S3", "S2"})),
              H({"SK", "S2"}));

        // SA and H2 are adjacent BITS, and never the same move.  This is what
        // SUIT_PADDING is for; without it the fill walks out of the spades.
        check("the fill stops at a suit boundary",
              nil::distinct_moves(H({"SA", "H2"}), H({"SA", "H2"})), H({"SA", "H2"}));
        check("equal ranks in different suits never collapse",
              nil::distinct_moves(H({"SK", "HK", "DK", "CK"}), H({"SK", "HK", "DK", "CK"})),
              H({"SK", "HK", "DK", "CK"}));

        // A single legal card is its own class, which is what lets the search
        // skip the reduction outright when there is only one move.
        check("a lone move survives", nil::distinct_moves(H({"SK"}), H({"SK", "SQ", "SJ"})),
              H({"SK"}));
        check("an empty move set stays empty",
              nil::distinct_moves(0, H({"SK", "SQ"})), static_cast<Hand>(0));
    }
    {
        // Only the card currently WINNING the trick can separate two ranks.
        CardId trick[3] = {C("HQ"), C("HA"), C("H3")};
        check("the best card is the running winner",
              nil::card_to_string(nil::trick_best_card(trick, 3)), std::string("HA"));
        check("an empty trick has no best card", nil::trick_best_card(trick, 0), nil::NO_CARD);
        CardId ruffed[3] = {C("HA"), C("S2"), nil::NO_CARD};
        check("a ruff is the best card",
              nil::card_to_string(nil::trick_best_card(ruffed, 2)), std::string("S2"));

        const Hand hands[4] = {H({"HK", "HJ"}), H({"H4"}), H({"H5"}), H({"H6"})};
        // HQ led and is winning: the king takes the trick and the jack does not,
        // so the queen holds them apart.
        check("the winning card separates the ranks it sits between",
              nil::distinct_moves(H({"HK", "HJ"}), nil::relevant_cards(hands, C("HQ"))),
              H({"HK", "HJ"}));
        // Overtake it with the ace and the queen is as dead as any card from a
        // finished trick: nothing will ever be compared against it again.
        CardId beaten[3] = {C("HQ"), C("HA"), nil::NO_CARD};
        check("a losing trick card separates nothing",
              nil::distinct_moves(H({"HK", "HJ"}),
                                  nil::relevant_cards(hands, nil::trick_best_card(beaten, 2))),
              H({"HJ"}));
    }
    {
        // End to end on layouts built out of runs, where the reduction has the
        // most to remove: same answer, same principal variation, fewer nodes.
        SearchOptions on;
        SearchOptions off;
        off.collapse_equivalents = false;
        const char* deals[] = {
            "N:AKQ.2.. JT9.3.. 876.4.. 543.5..",
            "N:AK.AK.. QJ.QJ.. T9.T9.. 87.87..",
            "N:432.A.. 765.K.. T98.Q.. AKQ.J..",
        };
        const char* seats[] = {"N", "E", "S", "W"};
        for (const char* pbn : deals) {
            for (const char* seat : seats) {
                const Position pos = make_position(pbn, "N", true);
                const Solution a = must_solve(pos, seat, on);
                const Solution b = must_solve(pos, seat, off);
                const std::string tag = std::string(pbn).substr(2, 8) + " nil " + seat;
                check("reduction keeps the value, " + tag, a.value, b.value);
                check("reduction keeps the PV, " + tag, nil::format_pv_compact(a),
                      nil::format_pv_compact(b));
                check("reduction removes work, " + tag, a.nodes < b.nodes, true);
            }
        }
    }
    {
        // The same claim over deals nobody chose: every objective variant, every
        // nil seat, value and PV compared card for card against the search that
        // enumerates all of them.  A fixed seed, so a failure is reproducible.
        Rng rng;
        long long with = 0;
        long long without = 0;
        for (int deal = 0; deal < 30; ++deal) {
            const Position pos = random_deal(rng, 3 + (deal % 2));
            std::string err;
            if (!nil::validate(pos, err)) {
                check("random deal is valid", err, std::string(""));
                continue;
            }
            for (int variant = 0; variant < 4; ++variant) {
                SearchOptions on;
                on.minimise_own_tricks = (variant & 1) != 0;
                const bool already_set = (variant & 2) != 0;
                SearchOptions off = on;
                off.collapse_equivalents = false;
                for (int seat = 0; seat < 4; ++seat) {
                    const Solution a = must_solve(pos, SEAT_NAMES[seat], on, already_set);
                    const Solution b = must_solve(pos, SEAT_NAMES[seat], off, already_set);
                    with += static_cast<long long>(a.nodes);
                    without += static_cast<long long>(b.nodes);
                    if (a.value == b.value &&
                        nil::format_pv_compact(a) == nil::format_pv_compact(b))
                        continue;
                    const std::string tag = nil::deal_to_pbn(pos.hands) + " v" +
                                            std::to_string(variant) + " nil " + SEAT_NAMES[seat];
                    check("sweep: same value, " + tag, a.value, b.value);
                    check("sweep: same PV, " + tag, nil::format_pv_compact(a),
                          nil::format_pv_compact(b));
                }
            }
        }
        check("sweep: the reduction never costs nodes", with <= without, true);
        check("sweep: and it saves some", with < without, true);
    }

    std::cout << "Lexicographic secondary objective\n";
    {
        SearchOptions take;                       // default: each pair takes what it can
        SearchOptions shed;
        shed.minimise_own_tricks = true;
        const nil::SeatRoles live = nil::seat_roles_from_nil(nil::SEAT_NORTH, false);
        const nil::SeatRoles already = nil::seat_roles_from_nil(nil::SEAT_NORTH, true);

        check("weights: primary dominates", nil::objective_weights(4, live, take).primary, 25);
        check("weights: take", nil::objective_weights(4, live, take).secondary, -5);
        check("weights: shed", nil::objective_weights(4, live, shed).secondary, 5);
        check("weights: the cover level is on when taking",
              nil::objective_weights(4, live, take).tertiary, 1);
        check("weights: and off when shedding",
              nil::objective_weights(4, live, shed).tertiary, 0);
        check("weights: already set drops the primary",
              nil::objective_weights(4, already, take).primary, 0);
        {
            // Each level must strictly outrank everything below it, or the
            // tie-break starts overruling the nil.
            const nil::ObjectiveWeights w = nil::objective_weights(4, live, take);
            check("weights: levels do not overlap",
                  w.primary > std::abs(w.secondary) * 4 + w.tertiary * 4, true);
        }

        // N/S take two tricks here whatever they do; the question is who holds
        // them.  Optimising the pair total alone leaves one with the nil
        // bidder, where it is worth nothing to the partner's bid.  The tertiary
        // level moves both onto the partner without costing the pair anything.
        const Position split = make_position("N:9.42.J. 5.Q.9.A A6.6..6 ..AT.Q2", "E", true);
        const Solution settled = must_solve(split, "N", take, /*already_set=*/true);
        check("nil set: the pair still takes everything it can",
              settled.nil_side_tricks, 2);
        check("nil set: and its partner ends up holding it", settled.nil_tricks, 0);

        // Two cards each, N is nil and safe either way, so the primary is a tie
        // and the secondary decides.  S holds HA H3: cashing the ace wins tricks
        // for N/S, ducking with the three sheds them.
        //   N: H2 C2   E: H5 C5   S: HA H3   W: H6 C6      leader E
        const Position cover = make_position("N:.2..2 .5..5 .A3.. .6..6", "E", true);
        const Solution grab = must_solve(cover, "N", take);
        const Solution duck = must_solve(cover, "N", shed);
        check("secondary does not disturb the primary (take)", grab.nil_tricks, 0);
        check("secondary does not disturb the primary (shed)", duck.nil_tricks, 0);
        check("take: N/S keep what they can", grab.nil_side_tricks, 1);
        check("shed: N/S give away what they can", duck.nil_side_tricks, 0);
        check("the two directions really do differ",
              nil::format_pv_compact(grab) != nil::format_pv_compact(duck), true);

        // Protecting the nil is not free.  N/S can hold N to zero here, but only
        // by giving up a trick they could otherwise win: with the nil already
        // set there is nothing to protect, and the same layout yields them all
        // three tricks, one of which N itself takes.
        const Position costly = make_position("N:7..6.3 6.J.2. J3.7.. 9..3.9", "N", true);
        const Solution protect = must_solve(costly, "N", take);
        const Solution ignore = must_solve(costly, "N", take, /*already_set=*/true);
        check("nil is protected", protect.nil_tricks, 0);
        check("protecting it costs a trick", protect.nil_side_tricks, 2);
        check("already set: primary is off", ignore.nil_tricks, 1);
        check("already set: N/S now take everything", ignore.nil_side_tricks, 3);
        check("already set: nil_fails is asserted, not computed", ignore.nils_set, 1);

        // Tallies stay consistent whatever the knobs say.
        for (int variant = 0; variant < 4; ++variant) {
            SearchOptions o;
            o.minimise_own_tricks = (variant & 1) != 0;
            const bool already_set = (variant & 2) != 0;
            const Solution sol = must_solve(costly, "N", o, already_set);
            const std::string label = std::string(o.minimise_own_tricks ? "shed" : "take") +
                                      (already_set ? "/set" : "/live");
            check(label + ": sides sum to the tricks played",
                  sol.nil_side_tricks + sol.opponent_tricks, costly.tricks_remaining());
            check(label + ": nil is part of its own side",
                  sol.nil_tricks <= sol.nil_side_tricks, true);
        }

        // THE lexicographic property: a tie-break can never move the primary.
        // If this fails the packing has overflowed and the secondary has started
        // outranking the nil.
        const char* layouts[] = {
            "N:A2.K.. K3.A.. Q4.Q.. J5.J..",
            "N:7..6.3 6.J.2. J3.7.. 9..3.9",
            "N:.A2.. .K3.. .54.. .Q6..",
        };
        for (const char* layout : layouts) {
            const Position pos = make_position(layout, "N", true);
            check(std::string("primary is stable under the tie-break: ") + layout,
                  must_solve(pos, "N", take).nil_tricks, must_solve(pos, "N", shed).nil_tricks);
        }
    }

    std::cout << "Boolean / lexicographic mode split\n";
    {
        SearchOptions full;
        SearchOptions fast;
        fast.mode = nil::MODE_FAST;

        // The whole point of the mode: nothing packed above or below the nil
        // bidder's trick count, so the value IS that count and the window
        // alpha-beta wants is [0, 1] rather than [0, K*K].
        const nil::SeatRoles north_nil = nil::seat_roles_from_nil(nil::SEAT_NORTH, false);
        check("fast weights: primary is 1",
              nil::objective_weights(4, north_nil, fast).primary, 1);
        check("fast weights: no secondary",
              nil::objective_weights(4, north_nil, fast).secondary, 0);
        check("fast weights: no tertiary",
              nil::objective_weights(4, north_nil, fast).tertiary, 0);
        {
            SearchOptions fast_shed = fast;
            fast_shed.minimise_own_tricks = true;
            check("fast weights: the tie-break direction has nothing to point at",
                  nil::objective_weights(4, north_nil, fast_shed).secondary, 0);
        }

        // E can be forced to take a trick here; this is the same layout the C
        // ABI section uses, from N's side.
        const Position forced = make_position("N:.A2.. .K3.. .54.. .Q6..", "N", true);
        const Solution fast_sol = must_solve(forced, "N", fast);
        const Solution full_sol = must_solve(forced, "N", full);
        check("fast mode answers the question", fast_sol.nils_set, 1);
        check("and agrees with full mode", fast_sol.nils_set, full_sol.nils_set);
        check("fast mode records its mode", fast_sol.mode == nil::MODE_FAST, true);
        check("full mode records its mode", full_sol.mode == nil::MODE_FULL, true);

        // Not zero, and not the number that happens to be right today: items 4
        // and 3 turn the fast value into a bound, and a caller who had come to
        // read these would not find out.
        check("fast mode does not report nil tricks", fast_sol.nil_tricks,
              nil::TRICKS_NOT_COMPUTED);
        check("fast mode does not report side tricks", fast_sol.nil_side_tricks,
              nil::TRICKS_NOT_COMPUTED);
        check("fast mode does not report opponent tricks", fast_sol.opponent_tricks,
              nil::TRICKS_NOT_COMPUTED);
        check("fast mode has no principal variation", fast_sol.pv.empty(), true);
        check("full mode still has one", full_sol.pv.size(), forced.tricks_remaining() * 4);

        // The caller asserted the only thing this mode computes, so there is
        // nothing to search.
        {
            const Solution told = must_solve(forced, "N", fast, /*already_set=*/true);
            check("fast + already set: answered by assertion", told.nils_set, 1);
            check("fast + already set: without searching", told.nodes, 0ull);
        }

        // minimise_own_tricks is inert in fast mode -- same answer, and the
        // same work done to reach it, because the weights it feeds are zero.
        {
            SearchOptions fast_shed = fast;
            fast_shed.minimise_own_tricks = true;
            const Solution shed_sol = must_solve(forced, "N", fast_shed);
            check("fast mode ignores the tie-break: answer", shed_sol.nils_set,
                  fast_sol.nils_set);
            check("fast mode ignores the tie-break: nodes", shed_sol.nodes, fast_sol.nodes);
        }

        // THE property this whole item rests on.  Fast mode has no PV to
        // replay, so this agreement is its only self-check; when it stops
        // holding, the boolean has started lying and nothing else would say so.
        {
            Rng rng;
            int checked = 0;
            int disagreed = 0;
            for (int deal = 0; deal < 30; ++deal) {
                const Position pos = random_deal(rng, 4);
                for (const char* seat : SEAT_NAMES) {
                    for (int variant = 0; variant < 2; ++variant) {
                        SearchOptions a;
                        a.minimise_own_tricks = variant != 0;
                        SearchOptions b = a;
                        b.mode = nil::MODE_FAST;
                        ++checked;
                        if (must_solve(pos, seat, a).nils_set !=
                            must_solve(pos, seat, b).nils_set) {
                            ++disagreed;
                        }
                    }
                }
            }
            check("modes agree on nil_fails across a random sweep", disagreed, 0);
            check("and the sweep actually ran", checked, 240);
        }
    }

    std::cout << "Nil-specialised alpha-beta\n";
    {
        // The claim of the whole item, stated as a test so it cannot quietly
        // stop being true: the boolean search visits strictly fewer nodes than
        // the exhaustive one on a position with room to cut.
        const Position pos = make_position("N:A2.K3.. .A4.K5. Q6..J8. T9.T9..", "N", true);
        SearchOptions full;
        SearchOptions fast;
        fast.mode = nil::MODE_FAST;
        const Solution slow = must_solve(pos, "N", full);
        const Solution quick = must_solve(pos, "N", fast);
        check("alpha-beta agrees with the exhaustive search", quick.nils_set, slow.nils_set);
        check("alpha-beta visits fewer nodes", quick.nodes < slow.nodes, true);

        // MODE_FULL is still asked between sentinels no value can reach, so the
        // value it returns is exact.  What changed in patch 22 is that its own
        // window narrows underneath it, so the cutoff is reachable and the
        // table now holds bounds as well as exact values.
        SearchOptions full_again = full;
        full_again.tt_megabytes = 4;  // a different table, so nothing is inherited
        check("full mode is unchanged by the presence of a window",
              must_solve(pos, "N", full_again).value, slow.value);

        // The control arm, and the reason it exists.  Turning narrowing off
        // restores the exhaustive search exactly: same value, same principal
        // variation, and strictly more nodes.
        SearchOptions wide = full;
        wide.narrow_window = false;
        const Solution exhaustive = must_solve(pos, "N", wide);
        check("narrowing does not change the value", slow.value, exhaustive.value);
        bool same_pv = slow.pv.size() == exhaustive.pv.size();
        for (std::size_t i = 0; same_pv && i < slow.pv.size(); ++i) {
            same_pv = slow.pv[i].seat == exhaustive.pv[i].seat &&
                      slow.pv[i].card == exhaustive.pv[i].card;
        }
        check("narrowing does not change the principal variation", same_pv, true);
        check("narrowing visits fewer nodes", slow.nodes < exhaustive.nodes, true);
        check("and without it full mode stores nothing but exact values",
              exhaustive.tt_partial, 0ull);

        // ---- item 23: the presolve-seeded root window ----------------------
        //
        // The bound is derived from the objective's weights rather than
        // estimated, so it may not move the value or the line -- only the work.
        SearchOptions nopre = full;
        nopre.presolve_window = false;
        const Solution unbounded = must_solve(pos, "N", nopre);
        check("the presolve does not change the value", slow.value, unbounded.value);
        bool pre_pv = slow.pv.size() == unbounded.pv.size();
        for (std::size_t i = 0; pre_pv && i < slow.pv.size(); ++i) {
            pre_pv = slow.pv[i].seat == unbounded.pv[i].seat &&
                     slow.pv[i].card == unbounded.pv[i].card;
        }
        check("the presolve does not change the principal variation", pre_pv, true);

        // And it may not hide its cost.  A presolved solve reports the fast
        // search's nodes as its own, so the count can only be honest if it is
        // above the fast solve's on its own.
        SearchOptions probe = full;
        probe.mode = nil::MODE_FAST;
        const Solution fast_only = must_solve(pos, "N", probe);
        check("a presolved solve counts the presolve's nodes",
              slow.nodes > fast_only.nodes, true);

        // The threshold itself, which is the whole argument.  Re-derived here
        // from the documented weight formula rather than read back out of the
        // solver, so that this is an independent statement of the invariant and
        // not a restatement of the code under test.
        bool separated = true;
        for (int tricks = 1; tricks <= 13 && separated; ++tricks) {
            for (int variant = 0; variant < 2 && separated; ++variant) {
                const bool min_own = variant != 0;
                const int k = tricks + 1;
                const int primary = k * k;
                const int secondary = min_own ? k : -k;
                const int tertiary = min_own ? 0 : 1;
                // Highest a position can score with the nil bidder taking none.
                const int safe_hi = secondary > 0 ? secondary * tricks : 0;
                // Lowest it can score with the nil bidder taking one: that
                // trick, plus the cover tricks arranged to drag it down.
                const int per_nil = primary + tertiary + secondary;
                const int fail_lo =
                    per_nil + (secondary < 0 ? secondary * (tricks - 1) : 0);
                separated = fail_lo > safe_hi;
            }
        }
        check("a failing nil always outscores every safe one, at every size",
              separated, true);

        // Alpha-beta cost the table nothing, and this is what says so.  Every
        // node of a fast search is asked about the same window: the only gain
        // that could shift one is the nil bidder winning a trick, and that is
        // exactly the case the "already past beta" cutoff answers without
        // recursing.  So every stored bound is on the same window as every
        // probe, and a bound that matches the position always settles it.
        // Should a later item vary the window, this stops being zero, which is
        // the honest signal that the table has started losing hits.
        check("no bound is ever recorded against a window it cannot answer",
              quick.tt_partial, 0ull);
    }
    {
        // The same claim, swept rather than observed once, because patch 12
        // made something depend on it.  Roadmap item 5 -- try the table's
        // stored move first at a node with a table hit -- is closed on the
        // argument that no such node exists: a probe that matches the position
        // always settles the window, so a node either returns from the table
        // without searching or searches without having found anything.  The
        // set of nodes item 5 wanted is exactly the set counted by tt_partial.
        //
        // So this is not a curiosity about table bookkeeping any more.  It is
        // the premise of a closed roadmap item, and if a later item varies the
        // window -- aspiration, a non-null window, a second goal -- this is
        // what says so, and item 5 becomes live again in the same breath.
        Rng rng;
        int checked = 0;
        std::uint64_t fast_partial = 0;
        std::uint64_t full_partial = 0;
        std::uint64_t searched_nodes = 0;
        for (int deal = 0; deal < 20; ++deal) {
            for (int cards = 4; cards <= 6; ++cards) {
                const Position pos = random_deal(rng, cards);
                for (const char* seat : SEAT_NAMES) {
                    for (int variant = 0; variant < 2; ++variant) {
                        SearchOptions opts;
                        opts.mode = variant == 0 ? nil::MODE_FAST : nil::MODE_FULL;
                        const Solution sol = must_solve(pos, seat, opts);
                        (variant == 0 ? fast_partial : full_partial) += sol.tt_partial;
                        searched_nodes += sol.nodes;
                        ++checked;
                    }
                }
            }
        }
        // MODE_FAST: the theorem of item 5, unchanged.  Its window is null, so
        // every entry it stores settles every window it will ever be probed
        // against, and no node can hold a stored move with work still to do.
        check("no fast node holds a stored move and still has moves to search", fast_partial,
              0ull);
        // MODE_FULL: no longer true, and deliberately so.  Patch 22 gave full
        // mode a window that narrows, which is precisely the condition the
        // comment above named as the one that would revive item 5.  This side
        // of the sweep is what says the population is now non-empty, so that
        // the revival is a measured fact rather than an inference.
        check("full nodes now do, which is item 5 live again", full_partial > 0ull, true);
        check("and the sweep did real work", searched_nodes > 100000ull, true);
        check("and it ran the whole cross product", checked, 480);
    }
    {
        // A bound is only worth what the window it was recorded against is
        // worth.  Storing "the value is at least 5" must answer a search asking
        // about anything up to 5 and nothing beyond it; symmetrically for an
        // upper bound.  This is the one piece of new table logic, so it gets
        // tested directly rather than only through the search.
        //
        // probe() reports two things since item 41 and they are not the same
        // question: whether the table HOLDS this position, and whether what it
        // holds SETTLES the window.  A match that does not settle is a partial,
        // and it comes back rather than being thrown away, because a one-sided
        // bound is still a fact the caller can spend.  Both halves are pinned
        // below -- the second column is the one that used to be the whole test.
        nil::TranspositionTable table;
        table.resize(1);
        nil::StateKey key;
        key.lo = 0x0123456789ABCDEFull;
        key.hi = 0x00000000000000FFull;
        const std::uint64_t hash = nil::mix_key(key);
        bool answers = false;
        const auto matched = [&](int alpha, int beta, std::uint8_t tag = nil::TAG_FAST) {
            return table.probe(key, hash, tag, alpha, beta, answers) != nullptr;
        };

        table.store(key, hash, 5, 3, 8, nil::BOUND_LOWER, nil::TAG_FAST);
        check("a lower bound answers a window it sits above", matched(4, 5), true);
        check("and says so", answers, true);
        check("a lower bound still MATCHES a window above it", matched(5, 6), true);
        check("but does not answer it", answers, false);

        table.store(key, hash, 5, 3, 8, nil::BOUND_UPPER, nil::TAG_FAST);
        check("an upper bound answers a window it sits below", matched(5, 6), true);
        check("and says so", answers, true);
        check("an upper bound still MATCHES a window below it", matched(3, 4), true);
        check("but does not answer it", answers, false);

        table.store(key, hash, 5, 3, 8, nil::BOUND_EXACT, nil::TAG_FAST);
        check("an exact value answers any window", matched(0, 1), true);
        check("and says so", answers, true);

        // The second lock on the door between the two objectives.  A fast-mode
        // 1 and a full-mode 1 are different numbers; what normally keeps them
        // apart is that every solve bumps the generation, and this is what
        // catches a future change that lets two objectives share one.
        //
        // A tag mismatch is not a partial: it is not this position's entry at
        // all, so the pointer must be null AND `answers` false.  Item 41 makes
        // that distinction worth stating -- a caller that reached for the value
        // behind a non-null pointer without checking the tag would be reading
        // the other objective's scale.
        check("an entry is invisible to the other objective", matched(0, 1, nil::TAG_FULL),
              false);
        check("and offers it nothing to narrow with", answers, false);
    }
    {
        // The boolean search now has a self-check that does not go through full
        // mode at all: pruning is what the table's bounds are for, so a search
        // with no table must reach the same answer as one with a table it is
        // constantly evicting from.
        Rng rng;
        int checked = 0;
        int disagreed = 0;
        for (int deal = 0; deal < 12; ++deal) {
            const Position pos = random_deal(rng, 5);
            for (const char* seat : SEAT_NAMES) {
                SearchOptions memo;
                memo.mode = nil::MODE_FAST;
                SearchOptions none = memo;
                none.use_memo = false;
                SearchOptions tiny = memo;
                tiny.tt_megabytes = 1;
                const bool a = must_solve(pos, seat, memo).nils_set;
                ++checked;
                if (must_solve(pos, seat, none).nils_set != a) ++disagreed;
                if (must_solve(pos, seat, tiny).nils_set != a) ++disagreed;
            }
        }
        check("bounded entries do not change the boolean", disagreed, 0);
        check("and that sweep actually ran", checked, 48);
    }
    {
        // Both modes on one position, in both orders, on one shared table.
        // Alternating them is the case where a bound written for one objective
        // could be read as a value for the other -- which would be a wrong
        // answer rather than a slow one, and would show up nowhere else.
        Rng rng;
        int disagreed = 0;
        for (int deal = 0; deal < 10; ++deal) {
            const Position pos = random_deal(rng, 5);
            for (const char* seat : SEAT_NAMES) {
                SearchOptions full;
                SearchOptions fast;
                fast.mode = nil::MODE_FAST;
                const bool fast_first = must_solve(pos, seat, fast).nils_set;
                const Solution then_full = must_solve(pos, seat, full);
                const bool fast_after = must_solve(pos, seat, fast).nils_set;
                if (fast_first != then_full.nils_set) ++disagreed;
                if (fast_after != then_full.nils_set) ++disagreed;
                // The interleaved full solve must also still be internally
                // consistent, which solve() checks by replaying its own PV --
                // must_solve would have exited if it were not.
                if (then_full.nil_tricks < 0) ++disagreed;
            }
        }
        check("interleaving the two modes on one table changes neither", disagreed, 0);
    }
    {
        // A deeper differential sweep than the corpus reaches.  Six cards is
        // 40 positions in the corpus and every one of them is oracle-checked;
        // this adds deals the corpus has never seen, at a size where the
        // pruning is doing real work.
        Rng rng;
        int checked = 0;
        int disagreed = 0;
        for (int deal = 0; deal < 8; ++deal) {
            const Position pos = random_deal(rng, 6);
            for (const char* seat : SEAT_NAMES) {
                for (int variant = 0; variant < 2; ++variant) {
                    SearchOptions a;
                    a.minimise_own_tricks = variant != 0;
                    SearchOptions b = a;
                    b.mode = nil::MODE_FAST;
                    ++checked;
                    if (must_solve(pos, seat, a).nils_set !=
                        must_solve(pos, seat, b).nils_set) {
                        ++disagreed;
                    }
                }
            }
        }
        check("modes agree at six cards", disagreed, 0);
        check("and that sweep actually ran", checked, 64);
    }

    std::cout << "Nil-safe and nil-set static bounds\n";
    {
        // THE SAFE PROOF, condition by condition.  `hands` here is always a
        // legal trick-boundary layout, since that is the only shape either
        // proof is defined on.

        // The worked example: the nil bidder holds C3 C2, H5 with H2 H3 H4
        // already gone, and DJ as the last diamond in the deal.  Nothing it
        // holds can be forced to win, and the jack is safe for the third
        // reason rather than the second -- nobody else has a diamond, so
        // nobody can lead one and it can only ever fall as a discard.
        {
            const Hand hands[4] = {H({"C3", "C2", "H5", "DJ"}),    // N, the nil bidder
                                   H({"C7", "C6", "H8", "HK"}),
                                   H({"CA", "CT", "H9", "HQ"}),
                                   H({"CK", "CJ", "H7", "HT"})};
            check("worked example: the nil is safe", nil::nil_cannot_be_forced(hands, 0, false),
                  true);
            // ...but not while it is the one on lead: three discards on a
            // diamond lead and the jack wins.
            check("worked example: on lead the lone diamond is a trick",
                  nil::nil_cannot_be_forced(hands, 0, true), false);
        }
        {
            // Same layout with one club moved so the nil bidder holds C8 over
            // somebody's C6.  Condition 2 fails in clubs and the proof stops.
            const Hand hands[4] = {H({"C8", "C2", "H5", "DJ"}),
                                   H({"C7", "C6", "H8", "HK"}),
                                   H({"CA", "CT", "H9", "HQ"}),
                                   H({"CK", "CJ", "H7", "HT"})};
            check("a card above an outstanding one breaks the proof",
                  nil::nil_cannot_be_forced(hands, 0, false), false);
        }
        {
            // The deuce of spades is still a spade.  Every other suit is as
            // low as it gets, and the proof must still refuse: hearts run out,
            // the nil bidder ruffs, and the lowest trump in the deck wins.
            const Hand hands[4] = {H({"S2", "H2", "H3"}), H({"SA", "H8", "HK"}),
                                   H({"SK", "H9", "HQ"}), H({"SQ", "H7", "HT"})};
            check("any spade at all defeats the safe proof",
                  nil::nil_cannot_be_forced(hands, 0, false), false);
        }
        {
            // Nobody else holds the suit AND the nil bidder is not on lead:
            // condition 2's second form, which is what makes the worked
            // example's diamond safe.  Checked on its own so the two forms
            // cannot both regress behind one test.
            const Hand hands[4] = {H({"D5", "D4"}), H({"H8", "HK"}), H({"H9", "HQ"}),
                                   H({"H7", "HT"})};
            check("a suit nobody else holds is safe off lead",
                  nil::nil_cannot_be_forced(hands, 0, false), true);
            check("and is a trick on lead", nil::nil_cannot_be_forced(hands, 0, true), false);
        }

        // THE SET PROOF.  The pattern from the roadmap, one row at a time, each
        // in a deal where the ranks named are the ranks outstanding.
        {
            // 1st highest.
            const Hand a[4] = {H({"SA"}), H({"SK"}), H({"SQ"}), H({"SJ"})};
            check("holding the top spade is a trick", nil::nil_must_take_a_trick(a, 0), true);
            // 2nd and 3rd.
            const Hand b[4] = {H({"SK", "SQ"}), H({"SA", "S2"}), H({"SJ", "S3"}),
                               H({"ST", "S4"})};
            check("2nd and 3rd highest is a trick", nil::nil_must_take_a_trick(b, 0), true);
            // 3rd, 4th and 5th.
            const Hand c[4] = {H({"SQ", "SJ", "ST"}), H({"SA", "S2", "S3"}),
                               H({"SK", "S4", "S5"}), H({"S9", "S6", "S7"})};
            check("3rd through 5th highest is a trick", nil::nil_must_take_a_trick(c, 0), true);
            // 4th, 5th, 6th and 7th.
            const Hand d[4] = {H({"SJ", "ST", "S9", "S8"}), H({"SA", "SK", "S2", "S3"}),
                               H({"SQ", "S4", "S5", "S6"}), H({"S7", "H2", "H3", "H4"})};
            check("4th through 7th highest is a trick", nil::nil_must_take_a_trick(d, 0), true);
        }
        {
            // The gaps in the pattern.  One below the block and the covers are
            // enough: the ace buries the king, the queen buries the jack.
            const Hand a[4] = {H({"SK", "SJ"}), H({"SA", "S2"}), H({"SQ", "S3"}),
                               H({"ST", "S4"})};
            check("2nd and 4th highest is escapable", nil::nil_must_take_a_trick(a, 0), false);
            const Hand b[4] = {H({"SK"}), H({"SA"}), H({"SQ"}), H({"SJ"})};
            check("the 2nd highest alone is escapable", nil::nil_must_take_a_trick(b, 0), false);
            const Hand c[4] = {H({"H2"}), H({"SA"}), H({"SQ"}), H({"SJ"})};
            check("no spades at all is not a forced trick",
                  nil::nil_must_take_a_trick(c, 0), false);
        }
        {
            // The general form catches slack blocks too, not just the tight
            // r(j) = 2j - 1 rows above: 2nd, 3rd and 9th is set on the 3rd,
            // and the 9th never enters into it.
            const Hand a[4] = {H({"SK", "SQ", "S5"}), H({"SA", "S2", "S3"}),
                               H({"SJ", "ST", "S4"}), H({"S9", "S8", "S7"})};
            check("a slack block is caught on its tight prefix",
                  nil::nil_must_take_a_trick(a, 0), true);
        }
        {
            // The two proofs are complementary on the spade test, which is the
            // property the search relies on to ask it once.
            const Hand hands[4] = {H({"S2", "H2"}), H({"SA", "H8"}), H({"SK", "H9"}),
                                   H({"SQ", "H7"})};
            check("no position satisfies both proofs",
                  nil::nil_cannot_be_forced(hands, 0, false) &&
                      nil::nil_must_take_a_trick(hands, 0),
                  false);
        }

        // END TO END.  A proof that fires must reach the answer the search
        // reaches without it, and must not reach it by searching.
        {
            SearchOptions on;
            on.mode = nil::MODE_FAST;
            SearchOptions off = on;
            off.use_static_bounds = false;

            // Safe: the nil bidder holds the two lowest clubs and nothing else,
            // and E is on lead.
            const Position safe = make_position("N:...32 ...AK ...QJ ...T9", "E", true);
            check("safe proof: same answer", must_solve(safe, "N", on).nils_set,
                  must_solve(safe, "N", off).nils_set);
            check("safe proof: and it is 'makes'", must_solve(safe, "N", on).nils_set, 0);
            check("safe proof: settled at the root", must_solve(safe, "N", on).nodes, 1ull);
            check("safe proof: which the search had to work for",
                  must_solve(safe, "N", off).nodes > 1ull, true);

            // Set: the nil bidder holds SK SQ with the ace outstanding.
            const Position set = make_position("N:KQ... A2... J3... T4...", "E", true);
            check("set proof: same answer", must_solve(set, "N", on).nils_set,
                  must_solve(set, "N", off).nils_set);
            check("set proof: and it is 'fails'", must_solve(set, "N", on).nils_set, 1);
            check("set proof: settled at the root", must_solve(set, "N", on).nodes, 1ull);
            check("set proof: which the search had to work for",
                  must_solve(set, "N", off).nodes > 1ull, true);
        }
        {
            // MODE_FULL must not see the bounds at all.  It owes its caller the
            // pair's trick total and the split between the two partners, and
            // neither proof says anything about either -- so the flag that
            // gates them is read off the weights and comes out false for every
            // full-mode weighting.  Node counts identical is the strongest way
            // to say it.
            const char* layouts[] = {
                "N:...32 ...AK ...QJ ...T9",
                "N:KQ... A2... J3... T4...",
                "N:A2.K3.. .A4.K5. Q6..J8. T9.T9..",
            };
            for (const char* pbn : layouts) {
                const Position pos = make_position(pbn, "E", true);
                for (int seat = 0; seat < 4; ++seat) {
                    for (int variant = 0; variant < 4; ++variant) {
                        SearchOptions on;
                        on.minimise_own_tricks = (variant & 1) != 0;
                        const bool already_set = (variant & 2) != 0;
                        // The claim is about MODE_FULL's OWN search, which
                        // still cannot read a proof stated in nil tricks.  As
                        // of patch 23 a full solve may also run a MODE_FAST
                        // presolve, and that one does see the bounds and does
                        // count its nodes here, so the presolve is turned off
                        // to keep the comparison about the thing being claimed.
                        on.presolve_window = false;
                        SearchOptions off = on;
                        off.use_static_bounds = false;
                        const Solution a = must_solve(pos, SEAT_NAMES[seat], on, already_set);
                        const Solution b = must_solve(pos, SEAT_NAMES[seat], off, already_set);
                        if (a.value == b.value &&
                            nil::format_pv_compact(a) == nil::format_pv_compact(b))
                            continue;
                        const std::string tag = std::string(pbn).substr(2, 8) + " nil " +
                                                SEAT_NAMES[seat] + " v" + std::to_string(variant);
                        // WHAT THIS USED TO CLAIM, and why it does not any more.
                        // Until patch 29 the assertion was `a.nodes == b.nodes`:
                        // MODE_FULL could not read a proof stated in nil tricks,
                        // so switching the proofs on changed nothing there and
                        // its node count was a fixed point.  Patch 29 spends
                        // that deliberately -- the proofs now bound a full-mode
                        // node instead of settling it -- so the count may fall
                        // and the claim narrows to what is actually owed.
                        //
                        // The value and the principal variation are still owed
                        // exactly, and they are the whole of the contract: a
                        // fail-soft bound is returned only when it already
                        // clears the window, which is a claim the caller was
                        // entitled to make do with.
                        //
                        // NODES ARE NOT ASSERTED IN EITHER DIRECTION, and the
                        // first draft of this patch wrongly claimed they could
                        // only fall.  They usually do -- 15.9% at 13 cards on a
                        // hard seed -- but a node answered by a bound stores a
                        // BOUND where it would otherwise have stored an exact
                        // value, and a later probe that would have been settled
                        // by the exact one is not settled by the bound.  Pruning
                        // here can therefore cost work elsewhere, and on an easy
                        // 13-card seed it measured 0.21% worse.  A one-sided
                        // proof cannot change an answer; it can very much change
                        // what the table is worth.
                        check("full mode value unmoved, " + tag, a.value, b.value);
                        check("full mode PV unmoved, " + tag, nil::format_pv_compact(a),
                              nil::format_pv_compact(b));
                    }
                }
            }
        }
        {
            // THE DIFFERENTIAL.  The proofs are one-sided, so switching them on
            // may only remove work -- never change a boolean, and never add a
            // node.  Deals nobody chose, at a size where they fire often, with
            // a fixed seed so a failure is reproducible.  This is what
            // corpus_static runs on the oracle-verified corpus; here it runs
            // deeper than the corpus reaches.
            Rng rng;
            int checked = 0;
            int disagreed = 0;
            long long with = 0;
            long long without = 0;
            for (int deal = 0; deal < 12; ++deal) {
                const Position pos = random_deal(rng, 6);
                for (const char* seat : SEAT_NAMES) {
                    SearchOptions on;
                    on.mode = nil::MODE_FAST;
                    SearchOptions off = on;
                    off.use_static_bounds = false;
                    const Solution a = must_solve(pos, seat, on);
                    const Solution b = must_solve(pos, seat, off);
                    ++checked;
                    if (a.nils_set != b.nils_set) ++disagreed;
                    with += static_cast<long long>(a.nodes);
                    without += static_cast<long long>(b.nodes);
                }
            }
            check("static bounds never change the boolean", disagreed, 0);
            check("and that sweep actually ran", checked, 48);
            check("static bounds never cost nodes", with <= without, true);
            check("and they save some", with < without, true);
        }
        {
            // The C ABI carries the off switch too, and the boolean is the same
            // through it.
            char err[256] = {0};
            nil_result on;
            nil_result off;
            const uint32_t base = NIL_FLAG_SPADES_BROKEN | NIL_FLAG_FAST_MODE;
            check("ABI: static bounds on",
                  static_cast<long long>(nil_solve("N:KQ... A2... J3... T4...", NIL_SEAT_EAST, "",
                                                   SEATS_NIL_N, base, &on, err, sizeof(err))),
                  0LL);
            check("ABI: static bounds off",
                  static_cast<long long>(nil_solve("N:KQ... A2... J3... T4...", NIL_SEAT_EAST, "",
                                                   SEATS_NIL_N,
                                                   base | NIL_FLAG_NO_STATIC_BOUNDS, &off, err,
                                                   sizeof(err))),
                  0LL);
            check("ABI: the switch does not move the answer",
                  static_cast<long long>(on.nils_set), static_cast<long long>(off.nils_set));
            check("ABI: and the answer is 'fails'", static_cast<long long>(on.nils_set), 1LL);
        }
    }

    std::cout << "Later tricks control arm\n";
    {
        // top_spade_run() is a soundness lever, so the units below pin the two
        // properties the proof leans on rather than only the speed-up: the run
        // must be TOP-ANCHORED and confined to ONE hand.  Both failures are
        // silent -- they widen the claim without widening the bound, so the
        // search stays correct until the day a deal exercises the gap.
        {
            const Position pos = make_position("N:AKQ2... 3.234.. 4..234. 5...234", "N");
            int run = -1;
            check("the top run is found", nil::top_spade_run(pos.hands, run),
                  static_cast<int>(nil::parse_seat("N")));
            check("and counted to its end", run, 3);
        }
        {
            // AK split across two hands is NOT two tricks: both may be void in
            // the led suit and both may ruff the same trick.  Only the hand
            // holding the top spade gets counted, and only for its own run.
            const Position pos = make_position("N:A2.23.. K3..23. 45...23 67.45..", "N");
            int run = -1;
            check("a split top pair names the ace's hand", nil::top_spade_run(pos.hands, run),
                  static_cast<int>(nil::parse_seat("N")));
            check("and stops at the king it does not hold", run, 1);
        }
        {
            // Un-anchored is worth nothing: KQ under an outstanding ace may be
            // played over, so the hand holding them is promised no trick here.
            const Position pos = make_position("N:KQ.23.. A3..23. 45...23 67.45..", "N");
            int run = -1;
            check("an un-anchored holding names the ace's hand instead",
                  nil::top_spade_run(pos.hands, run), static_cast<int>(nil::parse_seat("E")));
            check("and counts only the ace", run, 1);
        }
        {
            const Position pos = make_position("N:.A32.. .K45.. .Q67.. .J89..", "N");
            int run = -1;
            check("no spades left, no claim", nil::top_spade_run(pos.hands, run), -1);
            check("and the run is zero", run, 0);
        }

        // The differential.  MODE_FULL, because that is the only mode the bound
        // is wired into, and the value rather than the boolean, because the
        // whole point of full mode is the split between the two nil-side hands.
        Rng rng;
        int checked = 0;
        int disagreed = 0;
        long long with = 0;
        long long without = 0;
        for (int i = 0; i < 60; ++i) {
            const Position pos = random_deal(rng, 5);
            int which = 0;
            for (const char* seat : SEAT_NAMES) {
                SearchOptions on;
                on.mode = nil::MODE_FULL;
                on.minimise_own_tricks = (which++ % 2 != 0);
                SearchOptions off = on;
                off.later_tricks = false;
                const Solution a = must_solve(pos, seat, on);
                const Solution b = must_solve(pos, seat, off);
                ++checked;
                if (a.nil_tricks != b.nil_tricks || a.nil_side_tricks != b.nil_side_tricks) {
                    ++disagreed;
                }
                with += static_cast<long long>(a.nodes);
                without += static_cast<long long>(b.nodes);
            }
        }
        check("later tricks never move the trick split", disagreed, 0);
        check("and that sweep actually ran", checked, 240);
        check("later tricks never cost nodes in aggregate", with <= without, true);
        check("and they save some", with < without, true);
    }

    std::cout << "Cutoff bound from a partial table match (item 41)\n";
    {
        // A partial match is an entry that describes this position but does not
        // settle the window.  It still bounds the value on one side, and this
        // arm checks that spending it on the cutoff threshold moves nothing
        // except the node count.
        //
        // The PRINCIPAL VARIATION is checked here as well as the value, which
        // the ordering arms above deliberately do not do.  The reason is the
        // soundness argument this item rests on: an earlier cutoff is allowed
        // only because the value it stops at is squeezed exact between the
        // entry's bound and fail-soft's.  If that ever stopped holding, a node
        // would return a bound where the PV walk expects a value, and the line
        // -- not the value -- is where it would show first.
        Rng rng;
        int checked = 0;
        int value_moved = 0;
        int line_moved = 0;
        int counts_differ = 0;
        long long with = 0;
        long long without = 0;
        for (int deal = 0; deal < 40; ++deal) {
            const Position pos = random_deal(rng, 5);
            int which = 0;
            for (const char* seat : SEAT_NAMES) {
                SearchOptions on;
                on.mode = nil::MODE_FULL;
                on.minimise_own_tricks = (which++ % 2 != 0);
                SearchOptions off = on;
                off.tt_narrow_window = false;
                const Solution a = must_solve(pos, seat, on);
                const Solution b = must_solve(pos, seat, off);
                ++checked;
                if (a.value != b.value || a.nil_tricks != b.nil_tricks ||
                    a.nil_side_tricks != b.nil_side_tricks) {
                    ++value_moved;
                }
                if (a.pv.size() != b.pv.size()) {
                    ++line_moved;
                } else {
                    for (std::size_t i = 0; i < a.pv.size(); ++i) {
                        if (a.pv[i].seat != b.pv[i].seat || a.pv[i].card != b.pv[i].card) {
                            ++line_moved;
                            break;
                        }
                    }
                }
                if (a.nodes != b.nodes) ++counts_differ;
                with += static_cast<long long>(a.nodes);
                without += static_cast<long long>(b.nodes);
            }
        }
        check("the cutoff bound never moves the value or the split", value_moved, 0);
        check("nor the principal variation", line_moved, 0);
        check("and that sweep actually ran", checked, 160);
        check("it never costs nodes in aggregate", with <= without, true);
        check("and it saves some", with < without, true);
        // The arm has to be doing something, or the three checks above pass on
        // a flag that is not plumbed through -- which is how patch 15's inert
        // control arm would have looked if it had claimed not to be inert.
        check("and it is reached on real positions", counts_differ > 0, true);

        // MODE_FAST is untouched, and by arithmetic rather than by a gate: its
        // window is [0, 1] at every node and every value it stores is a bound at
        // one end or the other, so every entry that matches settles the window
        // and there are no partials to spend.  Node counts must therefore be
        // IDENTICAL, not merely no worse.
        long long fast_on = 0;
        long long fast_off = 0;
        for (int deal = 0; deal < 12; ++deal) {
            const Position pos = random_deal(rng, 6);
            for (const char* seat : SEAT_NAMES) {
                SearchOptions on;
                on.mode = nil::MODE_FAST;
                SearchOptions off = on;
                off.tt_narrow_window = false;
                fast_on += static_cast<long long>(must_solve(pos, seat, on).nodes);
                fast_off += static_cast<long long>(must_solve(pos, seat, off).nodes);
            }
        }
        check("MODE_FAST has no partial entries to spend", fast_on, fast_off);
    }

    std::cout << "Move ordering control arm\n";
    {
        // Patch 15 ships the switch, not the heuristics.  Everything below
        // therefore pins the shape rather than a speed-up, which is the same
        // thing patch 9 did for the mode split one patch ahead of alpha-beta:
        // settle the flag, the ABI bit and the differential while they are
        // still cheap to settle, so that item 6a has nowhere to hide.
        //
        // When 6a lands, the two "identical" checks below become "the boolean
        // is identical and the node count is not", and THAT is the differential
        // this section exists for.  Until then they say the flag is plumbed all
        // the way through and changes nothing, which is exactly true.
        Rng rng;
        int checked = 0;
        int disagreed = 0;
        long long ordered_nodes = 0;
        long long canonical_nodes = 0;
        for (int deal = 0; deal < 12; ++deal) {
            const Position pos = random_deal(rng, 6);
            for (const char* seat : SEAT_NAMES) {
                SearchOptions on;
                on.mode = nil::MODE_FAST;
                SearchOptions off = on;
                off.order_moves = false;
                const Solution a = must_solve(pos, seat, on);
                const Solution b = must_solve(pos, seat, off);
                ++checked;
                if (a.nils_set != b.nils_set) ++disagreed;
                ordered_nodes += static_cast<long long>(a.nodes);
                canonical_nodes += static_cast<long long>(b.nodes);
            }
        }
        check("move ordering never changes the boolean", disagreed, 0);
        check("and that sweep actually ran", checked, 48);
        // Ordering may only ever remove work.  It reorders the moves a node
        // looks at; it never removes one, so a cutoff it fails to find sooner
        // is a cutoff the canonical order would not have found either.
        check("move ordering never costs nodes", ordered_nodes <= canonical_nodes, true);
        // Patch 15 shipped the arm inert and this line read "and today it saves
        // none, because 6a has not landed".  6a landed; this is what it became.
        check("and 6a saves some", ordered_nodes < canonical_nodes, true);

        {
            // MODE_FULL used to ignore the request entirely, because ordering
            // it would have moved a principal variation the oracle checks card
            // for card.  Since patch 25 it orders like any other mode and
            // re-derives the reported move canonically afterwards, so what is
            // pinned here is what was always the point: the value and the LINE
            // do not move, while the node count now may and should.
            const Position pos = make_position("N:A2.K3.. .A4.K5. Q6..J8. T9.T9..", "N", true);
            SearchOptions on;
            SearchOptions off = on;
            off.order_moves = false;
            const Solution a = must_solve(pos, "N", on);
            const Solution b = must_solve(pos, "N", off);
            check("ordering does not move full mode's value", a.value, b.value);
            // Deliberately no node-count assertion.  At the two-card position
            // above, ordering has nothing to save and the canonical re-derivation
            // still costs a few lookups, so the ordered run is legitimately the
            // dearer one.  The saving is a property of deep positions and is
            // measured in the benchmark, where it is 1.8x at thirteen cards;
            // pinning a direction here would pin the wrong one.
            check("full mode ignores the ordering switch: PV",
                  nil::format_pv_compact(a) == nil::format_pv_compact(b), true);
        }
        {
            // The C ABI carries the off switch too, on the NO_COLLAPSE and
            // NO_STATIC_BOUNDS model, and the boolean is the same through it.
            char err[256] = {0};
            nil_result on;
            nil_result off;
            const uint32_t base = NIL_FLAG_SPADES_BROKEN | NIL_FLAG_FAST_MODE;
            check("ABI: ordering on",
                  static_cast<long long>(nil_solve("N:KQ... A2... J3... T4...", NIL_SEAT_EAST, "",
                                                   SEATS_NIL_N, base, &on, err, sizeof(err))),
                  0LL);
            check("ABI: ordering off",
                  static_cast<long long>(nil_solve("N:KQ... A2... J3... T4...", NIL_SEAT_EAST, "",
                                                   SEATS_NIL_N, base | NIL_FLAG_NO_ORDERING,
                                                   &off, err, sizeof(err))),
                  0LL);
            check("ABI: the switch does not move the answer",
                  static_cast<long long>(on.nils_set), static_cast<long long>(off.nils_set));
        }
    }

    std::cout << "Compact state key\n";
    {
        // Absolute ranks do not matter, only the order the four hands hold
        // them in.  These two positions are the same game.
        const Position low = make_position("N:2... 3... 4... 5...", "N");
        const Position high = make_position("N:3... 5... 9... T...", "N");
        check("rank relabelling keys the same", key_of(low) == key_of(high), true);

        // ...but who holds what still does.
        const Position swapped = make_position("N:2... 4... 3... 5...", "N");
        check("owner order keys differently", key_of(low) == key_of(swapped), false);

        // Both of these have the owner stream N,E,S,W and would collide if the
        // suit lengths were not in the header: one is four spades, the other is
        // two spades and two hearts.
        const Position split = make_position("N:2... 3... .2.. .3..", "N");
        check("suit lengths disambiguate", key_of(low) == key_of(split), false);

        // The leader and the broken flag are part of the position.
        Position other_leader = low;
        other_leader.leader = nil::parse_seat("E");
        check("leader is in the key", key_of(low) == key_of(other_leader), false);
        Position broken = low;
        broken.spades_broken = true;
        check("broken flag is in the key", key_of(low) == key_of(broken), false);
    }
    {
        // What survives of a card already on the trick is not its rank but how
        // many live cards it beats.  HK and HA both sit above all three
        // survivors, so the rest of the deal cannot tell them apart.
        const Position king = make_position("N:- .3.. .4.. .5..", "N", false, "HK");
        const Position ace = make_position("N:- .3.. .4.. .5..", "N", false, "HA");
        check("equally dominant trick cards key the same", key_of(king) == key_of(ace), true);

        // H2 beats none of them, which is a different position.
        const Position deuce = make_position("N:- .3.. .4.. .5..", "N", false, "H2");
        check("a beatable trick card keys differently", key_of(king) == key_of(deuce), false);
    }
    {
        // A relative move is a slot number, so it only means anything read
        // against the live cards of the position it came from.
        const Position pos = make_position("N:AK... Q9... J8... T7...", "N");
        const nil::SuitProfile profile = profile_of(pos);
        check("live spades counted", profile.length[nil::SUIT_SPADES], 8);
        check("live hearts counted", profile.length[nil::SUIT_HEARTS], 0);
        check("total counted", profile.total, 8);

        const CardId queen = C("SQ");
        check("relative move round trips",
              nil::from_relative(nil::to_relative(queen, profile), profile), queen);
        check("bottom slot is the lowest live card",
              nil::from_relative(static_cast<nil::RelMove>(nil::SUIT_SPADES * 16), profile),
              C("S7"));
        check("no-move sentinel round trips",
              nil::from_relative(nil::REL_NO_MOVE, profile), nil::NO_CARD);
    }
    {
        // 21 + 2n bits at the start of a trick, 30 + 2n inside one.  A whole
        // deal fits; a whole deal with one card already led does not -- and
        // that is the only shape that does not, because it can only arise in
        // the first trick, where nothing has transposed yet.
        const char* full = "N:8.K5.KT2.KQT9762 AQT643.T.QJ864.8 J9.A943.73.AJ543 K752.QJ8762.A95.";
        check("52 cards at a trick boundary fit", key_fits(make_position(full, "N")), true);
        const char* led = "N:.K5.KT2.KQT9762 AQT643.T.QJ864.8 J9.A943.73.AJ543 K752.QJ8762.A95.";
        check("51 cards mid-trick do not fit",
              key_fits(make_position(led, "N", false, "S8")), false);

        const Position five = make_position("N:AK2.4.5. Q93.6.7. J84.8.9. T75.T.J.", "N");
        nil::StateKey key;
        nil::SuitProfile profile;
        nil::encode_state_key(five.hands, five.leader, five.spades_broken, five.trick,
                              five.trick_len, key, profile);
        check("a five-card ending keys into one word", key.hi == 0ull, true);
    }

    std::cout << "Transposition table\n";
    {
        // The table caches a pure function.  Whatever it does to the node
        // count, it must not move the value or the principal variation --
        // including when it is far too small and evicting constantly.
        const char* deals[] = {
            "N:A2.K3.. .A4.K5. Q6..J8. T9.T9..",
            "N:AK.Q.J. 23.4.5. 67.8.9. TQ.T.A.",
        };
        const char* seats[] = {"N", "E", "S", "W"};
        for (const char* pbn : deals) {
            for (const char* seat : seats) {
                for (int mode = 0; mode < 2; ++mode) {
                    SearchOptions big;
                    big.minimise_own_tricks = mode == 1;
                    SearchOptions none = big;
                    none.use_memo = false;
                    SearchOptions tiny = big;
                    tiny.tt_megabytes = 1;

                    const Position pos = make_position(pbn, "N", true);
                    const Solution with = must_solve(pos, seat, big);
                    const Solution without = must_solve(pos, seat, none);
                    const Solution cramped = must_solve(pos, seat, tiny);

                    const std::string tag = std::string(pbn).substr(2, 8) + " nil " + seat +
                                            (mode ? " min" : " max");
                    check("no table: same value, " + tag, without.value, with.value);
                    check("no table: same PV, " + tag, nil::format_pv_compact(without),
                          nil::format_pv_compact(with));
                    check("1 MiB table: same value, " + tag, cramped.value, with.value);
                    check("1 MiB table: same PV, " + tag, nil::format_pv_compact(cramped),
                          nil::format_pv_compact(with));
                }
            }
        }
    }
    {
        // Consecutive solves share one table.  A different nil seat gives the
        // same positions different values, so the generation counter is what
        // stops the previous solve's answers leaking into this one; four seats
        // on one deal, twice, is the case that would expose it.
        const Position pos = make_position("N:A2.K3.. .A4.K5. Q6..J8. T9.T9..", "N", true);
        const char* seats[] = {"N", "E", "S", "W"};
        int first[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) first[i] = must_solve(pos, seats[i], SearchOptions()).value;
        for (int i = 3; i >= 0; --i) {
            check(std::string("value is seat-stable across table reuse, ") + seats[i],
                  must_solve(pos, seats[i], SearchOptions()).value, first[i]);
        }
        nil::release_transposition_table();
        check("value survives releasing the table",
              must_solve(pos, "N", SearchOptions()).value, first[0]);
    }

    std::cout << "Parsing\n";
    {
        const std::string full =
            "N:8.K5.KT2.KQT9762 AQT643.T.QJ864.8 J9.A943.73.AJ543 K752.QJ8762.A95.";
        Hand hands[4];
        std::string err;
        check("full deal parses", nil::parse_pbn(full, hands, err), true);
        check("full deal is 13 cards each",
              std::to_string(nil::count_cards(hands[0])) + "," +
                  std::to_string(nil::count_cards(hands[1])) + "," +
                  std::to_string(nil::count_cards(hands[2])) + "," +
                  std::to_string(nil::count_cards(hands[3])),
              std::string("13,13,13,13"));
        check("full deal round-trips", nil::deal_to_pbn(hands, 0), full);

        Hand rotated[4];
        check("rotating deal parses", nil::parse_pbn("E:..2. ..3. ..4. ..5.", rotated, err), true);
        check("hands rotate clockwise from the named seat", nil::deal_to_pbn(rotated, 0),
              std::string("N:..5. ..2. ..3. ..4."));

        Hand h = 0xFFFF;
        check("lone dash is an empty hand", nil::parse_pbn_hand("-", h, err), true);
        check("lone dash yields no cards", static_cast<long long>(nil::count_cards(h)), 0LL);
        check("'10' is accepted for the ten", nil::parse_pbn_hand("10...", h, err), true);
        check("'10' is the ten of spades", nil::hand_to_string(h), std::string("ST"));

        check("bad rank is rejected", nil::parse_pbn_hand("X...", h, err), false);
        check("wrong group count is rejected", nil::parse_pbn_hand("A.2.3", h, err), false);
        check("missing colon is rejected", nil::parse_pbn("A... .... .... ....", hands, err),
              false);
    }

    std::cout << "Validation\n";
    {
        // Spades cannot be broken while every spade is still unplayed.  A full
        // deal has had no card played at all, so the flag is claiming something
        // that cannot have happened -- and the position it describes is not
        // merely unreachable but expensive, since unbroken spades are what
        // forbid a voluntary spade lead near the root.
        std::string err;
        Position full = make_position(
            "N:KJ94.QJ6.9.QJT32 QT83.T432.AQ52.6 762.AK8.J873.K85 A5.975.KT64.A974", "W",
            false);
        check("a full deal with spades unbroken is valid", nil::validate(full, err), true);
        full.spades_broken = true;
        check("a full deal cannot have spades already broken", nil::validate(full, err), false);

        // Below thirteen the count proves nothing: the absent spades were never
        // dealt rather than played, so a constructed ending may start broken.
        Position ending = make_position("N:..2. ..A. ..5. ..7.", "N", true);
        check("a constructed ending may start broken", nil::validate(ending, err), true);
    }
    {
        Position pos;
        std::string err;
        nil::parse_pbn("N:A... A... .... ....", pos.hands, err);
        check("duplicate card is rejected", nil::validate(pos, err), false);

        Position sizes;
        nil::parse_pbn("N:A2... K... .... ....", sizes.hands, err);
        check("inconsistent hand sizes are rejected", nil::validate(sizes, err), false);

        Position revoke = make_position("N:.2.. .3.. .4.. .5..", "N");
        // W is on the trick with a club while still holding a heart.
        revoke.leader = nil::parse_seat("N");
        revoke.trick[0] = C("H2");
        revoke.trick_len = 1;
        check("trick card still in a hand is rejected", nil::validate(revoke, err), false);
    }

    std::cout << "Replay verifier\n";
    {
        const Position pos = make_position("N:..2. ..A. ..5. ..7.", "N", true);
        const Solution sol = must_solve(pos, "E");
        const nil::SeatRoles east_nil = nil::seat_roles_from_nil(nil::SEAT_EAST, false);
        nil::Tally tally;
        std::string err;
        check("good PV replays", nil::replay_pv(pos, sol.pv, east_nil, tally, err), true);
        check("good PV replays to the same value", tally.nil_tricks, 1);
        check("replay tallies the nil's side", tally.nil_side_tricks, 1);
        check("replay tallies the opponents", tally.opponent_tricks, 0);

        std::vector<nil::Play> bad = sol.pv;
        std::swap(bad[0], bad[1]);
        check("out-of-turn PV is rejected", nil::replay_pv(pos, bad, east_nil, tally, err),
              false);

        std::vector<nil::Play> truncated(sol.pv.begin(), sol.pv.begin() + 2);
        check("short PV is rejected", nil::replay_pv(pos, truncated, east_nil, tally, err),
              false);
    }

    std::cout << "Seat roles\n";
    {
        std::string err;
        nil::SeatRoles roles;

        // The text form runs clockwise from the anchor, so the SAME four values
        // describe a different deal against a different anchor.  This is the
        // one thing about the format that can be got wrong quietly, so it is
        // pinned from both ends.
        check("roles parse against North",
              nil::parse_seat_roles("0 3 2 3", nil::SEAT_NORTH, roles, err), true);
        check("north anchor puts the nil on North", nil::describe_seat_roles(roles),
              std::string("N=nil E=opponent S=cover W=opponent"));
        check("roles parse against West",
              nil::parse_seat_roles("0 3 2 3", nil::SEAT_WEST, roles, err), true);
        check("west anchor puts the nil on West", nil::describe_seat_roles(roles),
              std::string("N=opponent E=cover S=opponent W=nil"));

        // Separators are the caller's business, not the format's.
        nil::SeatRoles spaced;
        nil::SeatRoles commas;
        nil::SeatRoles joined;
        check("space separated", nil::parse_seat_roles("1 3 2 3", nil::SEAT_EAST, spaced, err),
              true);
        check("comma separated", nil::parse_seat_roles("1,3,2,3", nil::SEAT_EAST, commas, err),
              true);
        check("run together", nil::parse_seat_roles("1323", nil::SEAT_EAST, joined, err), true);
        check("all three read the same", spaced == commas && commas == joined, true);
        check("and a nil-set seat is still the nil seat", spaced.nil_seat(),
              static_cast<int>(nil::SEAT_EAST));
        check("which reports itself as already set", spaced.nil_already_set(), true);

        // Round trip through the anchor it was written against.
        check("formatting inverts parsing", nil::seat_roles_to_string(spaced, nil::SEAT_EAST),
              std::string("1 3 2 3"));
        check("and re-anchoring rewrites the order",
              nil::seat_roles_to_string(spaced, nil::SEAT_NORTH), std::string("3 1 3 2"));

        // seat_roles_from_nil is what the old two scalars meant.
        check("from_nil matches the text form",
              nil::seat_roles_from_nil(nil::SEAT_WEST, false) ==
                  (nil::parse_seat_roles("0 3 2 3", nil::SEAT_WEST, roles, err), roles),
              true);
        check("a live nil is not already set",
              nil::seat_roles_from_nil(nil::SEAT_WEST, false).nil_already_set(), false);
        check("the cover sits across from the nil",
              nil::seat_roles_from_nil(nil::SEAT_WEST, false).cover_seat(),
              static_cast<int>(nil::SEAT_EAST));
        check("the nil bidder is on its own side",
              nil::seat_roles_from_nil(nil::SEAT_WEST, false).on_nil_side(nil::SEAT_WEST), true);
        check("and an opponent is not",
              nil::seat_roles_from_nil(nil::SEAT_WEST, false).on_nil_side(nil::SEAT_NORTH), false);

        // Malformed text.
        check("four values are required",
              nil::parse_seat_roles("0 3 2", nil::SEAT_NORTH, roles, err), false);
        check("and no more than four",
              nil::parse_seat_roles("0 3 2 3 3", nil::SEAT_NORTH, roles, err), false);
        check("values stay in range",
              nil::parse_seat_roles("0 3 2 9", nil::SEAT_NORTH, roles, err), false);
        check("letters are not roles",
              nil::parse_seat_roles("N 3 2 3", nil::SEAT_NORTH, roles, err), false);

        // Shapes phase one does not accept.  The two-nil case is refused as a
        // FEATURE that has not landed, and the message says so, because that is
        // the distinction a caller has to be able to act on.
        nil::SeatRoles two_nils;
        check("opposing nils parse",
              nil::parse_seat_roles("0 0 2 2", nil::SEAT_NORTH, two_nils, err), true);
        check("but do not validate", nil::validate_seat_roles(two_nils, err), false);
        check("and are refused as unsupported rather than malformed",
              err.find("not supported yet") != std::string::npos, true);

        nil::SeatRoles no_nil;
        check("a deal with no nil parses",
              nil::parse_seat_roles("3 3 2 3", nil::SEAT_NORTH, no_nil, err), true);
        check("but does not validate", nil::validate_seat_roles(no_nil, err), false);

        nil::SeatRoles bad_cover;
        check("a cover beside the nil parses",
              nil::parse_seat_roles("0 2 3 3", nil::SEAT_NORTH, bad_cover, err), true);
        check("but does not validate", nil::validate_seat_roles(bad_cover, err), false);

        // The whole point: the same deal written against two anchors, with the
        // roles rotated to match, is one question and gets one answer.
        const Position from_north =
            make_position("N:.A2.. .K3.. .54.. .Q6..", "N", true);
        nil::SeatRoles north_written;
        nil::SeatRoles west_written;
        check("north-anchored roles read",
              nil::parse_seat_roles("3 0 3 2", nil::SEAT_NORTH, north_written, err), true);
        check("west-anchored roles read",
              nil::parse_seat_roles("2 3 0 3", nil::SEAT_WEST, west_written, err), true);
        check("the two spellings agree", north_written == west_written, true);
        nil::Solution a;
        nil::Solution b;
        nil::SearchOptions plain;
        check("north spelling solves", nil::solve(from_north, north_written, plain, a, err), true);
        check("west spelling solves", nil::solve(from_north, west_written, plain, b, err), true);
        check("and they are the same answer", a.value, b.value);
        check("down to the line", nil::format_pv_compact(a), nil::format_pv_compact(b));

        // solve() refuses an unsupported shape rather than answering something
        // adjacent to what was asked.
        nil::Solution refused;
        check("solve refuses opposing nils",
              nil::solve(from_north, two_nils, plain, refused, err), false);
        std::vector<nil::MoveScore> no_moves;
        check("and so does solve_moves",
              nil::solve_moves(from_north, two_nils, plain, refused, no_moves, err), false);
    }

    std::cout << "Two nils on one side\n";
    {
        std::string err;
        nil::SearchOptions plain;
        nil::SeatRoles pair_bids;
        check("a pair that both bid parses",
              nil::parse_seat_roles("0 3 0 3", nil::SEAT_NORTH, pair_bids, err), true);
        check("and validates", nil::validate_seat_roles(pair_bids, err), true);
        check("it is the partner-nils shape",
              static_cast<int>(nil::seat_shape(pair_bids, err)),
              static_cast<int>(nil::SHAPE_PARTNER_NILS));
        check("with two bidders", nil::nil_count(pair_bids), 2);

        // The shapes next door, each refused for its own reason.
        struct Refusal {
            const char* text;
            const char* label;
        };
        const Refusal refusals[] = {
            {"0 0 3 3", "nils on opposing sides"},
            {"0 3 0 2", "a cover with nobody left to cover"},
            {"0 0 0 3", "three nils"},
        };
        for (const Refusal& r : refusals) {
            nil::SeatRoles bad;
            std::string why;
            check(std::string("parses: ") + r.label,
                  nil::parse_seat_roles(r.text, nil::SEAT_NORTH, bad, why), true);
            check(std::string("refuses ") + r.label, nil::validate_seat_roles(bad, why), false);
        }

        // WEIGHTS.  The primary is charged per BID that goes down, 0..2, so it
        // has to outrank anything the secondary can reach: |secondary| * t must
        // stay under it, or a run of tricks could overturn a nil.
        for (int t : {2, 3, 5, 9, 13}) {
            const nil::ObjectiveWeights w = nil::objective_weights(t, pair_bids, plain);
            check("weights separate at " + std::to_string(t) + " tricks",
                  std::abs(w.secondary) * t < w.primary, true);
            check("no cover level at " + std::to_string(t) + " tricks", w.tertiary, 0);
        }
        {
            nil::SearchOptions shed;
            shed.minimise_own_tricks = true;
            check("max wants the pair's tricks",
                  nil::objective_weights(4, pair_bids, plain).secondary < 0, true);
            check("min wants rid of them",
                  nil::objective_weights(4, pair_bids, shed).secondary > 0, true);
        }

        // CONCENTRATION, and the reason the two levels are not interchangeable.
        // Two tricks the pair cannot avoid taking.  Splitting them kills both
        // bids; funnelling both through one seat kills one.  The trick COUNT is
        // identical either way, so only the primary level tells them apart.
        const Position funnel = make_position("N:.A.A. .2.2. .3.3. .4.4.", "N", true);
        nil::Solution fsol;
        check("the funnel deal solves", nil::solve(funnel, pair_bids, plain, fsol, err), true);
        check("both tricks land on the pair", fsol.nil_side_tricks, 2);
        check("but only one bid goes down", fsol.nils_set, 1);
        check("nils_set counts BIDDERS, not tricks", fsol.nil_tricks, 2);

        // The mirror: one trick, and a bidder cannot avoid winning it.
        const Position forced = make_position("N:.5.. .3.. .4.. .2..", "W", true);
        nil::Solution osol;
        check("the forced deal solves", nil::solve(forced, pair_bids, plain, osol, err), true);
        check("exactly one bid goes down", osol.nils_set, 1);

        // And one where nobody on the pair can be made to win.
        const Position safe = make_position("N:.2.. .A.. .3.. .K..", "N", true);
        nil::Solution ssol;
        check("the safe deal solves", nil::solve(safe, pair_bids, plain, ssol, err), true);
        check("no bid goes down", ssol.nils_set, 0);
        check("and the pair takes nothing", ssol.nil_side_tricks, 0);

        // THE TABLE IS NOT ALLOWED TO CHANGE THE ANSWER.  Patch 59 put the
        // broken-nil mask into the key; if it were missing, a hit from a
        // position that only LOOKS the same would return a value from a line
        // where different bids were already down.  That failure is silent, so
        // this is what catches it.
        const char* const deals[] = {
            "N:.A.A. .2.2. .3.3. .4.4.",
            "N:A2.K.. 43.A.. 65.Q.. 87.J..",
            "N:K.2.Q. 3.A.4. 5.J.6. 7.T.8.",
        };
        for (const char* pbn : deals) {
            const Position pos = make_position(pbn, "N", true);
            nil::SearchOptions with_tt;
            nil::SearchOptions without_tt;
            without_tt.use_memo = false;
            nil::Solution a;
            nil::Solution b;
            check("table on solves", nil::solve(pos, pair_bids, with_tt, a, err), true);
            check("table off solves", nil::solve(pos, pair_bids, without_tt, b, err), true);
            check("the table does not move the value", a.value, b.value);
            check("nor the count of bids down", a.nils_set, b.nils_set);
            check("nor the pair's trick total", a.nil_side_tricks, b.nil_side_tricks);
        }

        // ONE BID ALREADY DOWN, ONE STILL ALIVE.  The state a real hand reaches
        // the moment one of two nils breaks, and the reason re-solving from it
        // matters: the dead seat has nothing left to protect, so the pair
        // funnels every trick it cannot avoid through THAT seat and keeps the
        // live bid standing.  The same funnel deal, with N's bid already gone.
        nil::SeatRoles one_down;
        check("a broken bid beside a live one parses",
              nil::parse_seat_roles("1 3 0 3", nil::SEAT_NORTH, one_down, err), true);
        check("and validates", nil::validate_seat_roles(one_down, err), true);
        nil::Solution dsol;
        check("it solves", nil::solve(funnel, one_down, plain, dsol, err), true);
        check("both tricks still land on the pair", dsol.nil_side_tricks, 2);
        check("the live bid survives", dsol.nils_set, 1);
        check("because the dead seat took them all", dsol.nil_tricks, 2);

        // The declared bid counts toward the reported total even when the seat
        // never wins a trick -- it is down because the caller said so.
        nil::Solution safe_down;
        check("the safe deal solves with one bid declared down",
              nil::solve(safe, one_down, plain, safe_down, err), true);
        check("and reports that one as down", safe_down.nils_set, 1);
        check("with no tricks to the pair", safe_down.nil_side_tricks, 0);

        // BOTH BIDS ALREADY DOWN.  Accepted, and it degenerates rather than
        // failing: with no live bid there is no primary level, so what is left
        // is the secondary alone -- each pair taking or shedding as many tricks
        // as it can.  That is an ordinary double-dummy question, the hand is
        // still being played, and the tricks are still worth points.
        //
        // It is also exactly what a SINGLE nil already set has always done, so
        // the two must agree: a dead bidder and a cover partner both play
        // freely, having nothing left to protect.  That equivalence is the test.
        nil::SeatRoles all_down;
        nil::SeatRoles down_and_cover;
        check("both bids down parses",
              nil::parse_seat_roles("1 3 1 3", nil::SEAT_NORTH, all_down, err), true);
        check("and validates", nil::validate_seat_roles(all_down, err), true);
        check("no primary level is left",
              nil::objective_weights(6, all_down, plain).primary, 0);
        check("one down plus a cover parses",
              nil::parse_seat_roles("1 3 2 3", nil::SEAT_NORTH, down_and_cover, err), true);

        for (const char* pbn : deals) {
            const Position pos = make_position(pbn, "N", true);
            for (bool shed : {false, true}) {
                nil::SearchOptions o;
                o.minimise_own_tricks = shed;
                nil::Solution both;
                nil::Solution cover;
                const std::string tag = shed ? " (shedding)" : " (taking)";
                check("both-down solves" + tag, nil::solve(pos, all_down, o, both, err), true);
                check("down-plus-cover solves" + tag,
                      nil::solve(pos, down_and_cover, o, cover, err), true);
                check("the two agree on the pair's tricks" + tag, both.nil_side_tricks,
                      cover.nil_side_tricks);
                check("and on the opponents'" + tag, both.opponent_tricks,
                      cover.opponent_tricks);
            }
            nil::Solution both;
            check("both bids are reported down", nil::solve(pos, all_down, plain, both, err),
                  true);
            check("as two", both.nils_set, 2);
        }

        // FAST MODE REFUSES THE SHAPE.  It asks whether ONE named seat can make
        // nil, and there is no named seat here -- nor is one bid's survival
        // defined on its own, since it turns on how the pair trades the two off.
        nil::SearchOptions fast_opts;
        fast_opts.mode = nil::MODE_FAST;
        nil::Solution unused;
        check("fast mode refuses two bidders",
              nil::solve(funnel, pair_bids, fast_opts, unused, err), false);
        check("and says why", err.find("two bidders") != std::string::npos, true);
        std::vector<nil::MoveScore> no_moves;
        check("so does solve_moves",
              nil::solve_moves(funnel, pair_bids, fast_opts, unused, no_moves, err), false);

        // Every card scored, under the shape.
        nil::Solution msol;
        std::vector<nil::MoveScore> scored;
        check("solve_moves handles two bidders",
              nil::solve_moves(funnel, pair_bids, plain, msol, scored, err), true);
        check("and scores every legal card", static_cast<int>(scored.size()), 2);
        check("agreeing with solve on the count", msol.nils_set, fsol.nils_set);
    }

    std::cout << "One nil on each side\n";
    {
        std::string err;
        nil::SearchOptions plain;

        // THE RANKING, from the specification.  Both sides agree on the ends;
        // the partner's role decides only the middle.
        check("mine surviving alone is best", nil::side_rank(true, false, nil::ROLE_COVER), 3);
        check("and best either way", nil::side_rank(true, false, nil::ROLE_OPPONENT), 3);
        check("theirs surviving alone is worst", nil::side_rank(false, true, nil::ROLE_COVER), 0);
        check("save-ours prefers both surviving",
              nil::side_rank(true, true, nil::ROLE_COVER) >
                  nil::side_rank(false, false, nil::ROLE_COVER), true);
        check("set-theirs prefers both dying",
              nil::side_rank(false, false, nil::ROLE_OPPONENT) >
                  nil::side_rank(true, true, nil::ROLE_OPPONENT), true);

        // WHICH ARRANGEMENTS ARE ORDINARY TWO-TEAM GAMES.  Partners leaning
        // opposite ways gives rankings that are exact reverses; leaning the same
        // way gives both sides a shared interest and no scalar to fight over.
        nil::SeatRoles opposed;
        nil::SeatRoles mirrored;
        nil::SeatRoles protective;
        nil::SeatRoles aggressive;
        check("mixed parses",
              nil::parse_seat_roles("0 0 3 2", nil::SEAT_NORTH, opposed, err), true);
        check("mirrored mixed parses",
              nil::parse_seat_roles("0 0 2 3", nil::SEAT_NORTH, mirrored, err), true);
        check("both protective parses",
              nil::parse_seat_roles("0 0 2 2", nil::SEAT_NORTH, protective, err), true);
        check("both aggressive parses",
              nil::parse_seat_roles("0 0 3 3", nil::SEAT_NORTH, aggressive, err), true);
        check("mixed is strictly opposed", nil::strictly_opposed(opposed), true);
        check("so is its mirror", nil::strictly_opposed(mirrored), true);
        check("both protective is not", nil::strictly_opposed(protective), false);
        check("both aggressive is not", nil::strictly_opposed(aggressive), false);

        check("the opposed shape validates", nil::validate_seat_roles(opposed, err), true);
        check("it is the opposing shape", static_cast<int>(nil::seat_shape(opposed, err)),
              static_cast<int>(nil::SHAPE_OPPOSING_NILS));
        check("the unopposed pair is refused",
              nil::validate_seat_roles(protective, err), false);
        check("and says the sides share an interest",
              err.find("share an interest") != std::string::npos, true);

        // East leads the only high card and East holds a bid, so exactly one
        // bid dies and it is East's.
        const Position one_trick = make_position("N:.2.. .A.. .3.. .4..", "E", true);
        nil::Solution sol;
        check("the opposed shape solves", nil::solve(one_trick, opposed, plain, sol, err),
              true);
        check("one bid goes down", sol.nils_set, 1);
        check("and the far side took the trick", sol.opponent_tricks, 1);

        // The table must not move the answer: the key carries the broken-bid
        // mask and the shape has its own value tag.
        nil::SearchOptions no_tt;
        no_tt.use_memo = false;
        nil::Solution untabled;
        check("table off solves", nil::solve(one_trick, opposed, no_tt, untabled, err), true);
        check("and agrees on the value", untabled.value, sol.value);

        // Fast mode asks about ONE named seat and there are two bidders.
        nil::SearchOptions fast_opts;
        fast_opts.mode = nil::MODE_FAST;
        nil::Solution unused;
        check("fast mode refuses two bidders",
              nil::solve(one_trick, opposed, fast_opts, unused, err), false);
    }

    std::cout << "C ABI\n";
    {
        nil_result r;
        char err[256] = {0};
        const int32_t rc = nil_solve("N:.A2.. .K3.. .54.. .Q6..", NIL_SEAT_NORTH, "",
                                     SEATS_NIL_N, NIL_FLAG_SPADES_BROKEN, &r, err, sizeof(err));
        check("nil_solve returns NIL_OK", static_cast<long long>(rc), 0LL);
        check("nil_solve reports the trick", static_cast<long long>(r.nil_tricks), 1LL);
        check("nil_solve reports the side total",
              static_cast<long long>(r.nil_side_tricks), 1LL);
        check("nil_solve reports the opponents",
              static_cast<long long>(r.opponent_tricks), 1LL);
        check("nil_solve reports failure", static_cast<long long>(r.nils_set), 1LL);
        check("nil_solve reports tricks remaining", static_cast<long long>(r.tricks_remaining),
              2LL);

        char pv[256] = {0};
        const int32_t rc2 = nil_solve_pv("N:..2. ..A. ..5. ..7.", NIL_SEAT_NORTH, "",
                                         SEATS_NIL_E, NIL_FLAG_SPADES_BROKEN, &r, pv, sizeof(pv),
                                         err, sizeof(err));
        check("nil_solve_pv returns NIL_OK", static_cast<long long>(rc2), 0LL);
        check("nil_solve_pv writes the PV", std::string(pv), std::string("N:D2 E:DA S:D5 W:D7"));

        char tiny[4] = {0};
        const int32_t rc3 = nil_solve_pv("N:..2. ..A. ..5. ..7.", NIL_SEAT_NORTH, "",
                                         SEATS_NIL_E, NIL_FLAG_SPADES_BROKEN, &r, tiny,
                                         sizeof(tiny), err, sizeof(err));
        check("nil_solve_pv rejects a small buffer", static_cast<long long>(rc3),
              static_cast<long long>(NIL_ERR_BUFFER_TOO_SMALL));

        check("nil_fails convenience wrapper",
              static_cast<long long>(nil_count_set("N:.A2.. .K3.. .54.. .Q6..", NIL_SEAT_NORTH, "",
                                               SEATS_NIL_N, NIL_FLAG_SPADES_BROKEN)),
              1LL);

        // Fast mode across the ABI: same boolean, no counts, and asking for a
        // principal variation is refused rather than quietly answered with an
        // empty string or with a slower search the caller did not ask for.
        nil_result fr;
        const int32_t rc_fast =
            nil_solve("N:.A2.. .K3.. .54.. .Q6..", NIL_SEAT_NORTH, "", SEATS_NIL_N,
                      NIL_FLAG_SPADES_BROKEN | NIL_FLAG_FAST_MODE, &fr, err, sizeof(err));
        check("fast mode returns NIL_OK", static_cast<long long>(rc_fast), 0LL);
        check("fast mode agrees on the boolean", static_cast<long long>(fr.nils_set),
              static_cast<long long>(r.nils_set));
        check("fast mode reports unknown nil tricks", static_cast<long long>(fr.nil_tricks),
              static_cast<long long>(NIL_TRICKS_UNKNOWN));
        check("fast mode reports unknown side tricks",
              static_cast<long long>(fr.nil_side_tricks),
              static_cast<long long>(NIL_TRICKS_UNKNOWN));
        check("fast mode reports unknown opponent tricks",
              static_cast<long long>(fr.opponent_tricks),
              static_cast<long long>(NIL_TRICKS_UNKNOWN));
        check("fast mode still reports tricks remaining",
              static_cast<long long>(fr.tricks_remaining), 2LL);

        char fast_pv[256] = {0};
        const int32_t rc_pv_fast =
            nil_solve_pv("N:..2. ..A. ..5. ..7.", NIL_SEAT_NORTH, "", SEATS_NIL_E,
                         NIL_FLAG_SPADES_BROKEN | NIL_FLAG_FAST_MODE, &fr, fast_pv,
                         sizeof(fast_pv), err, sizeof(err));
        check("nil_solve_pv refuses fast mode", static_cast<long long>(rc_pv_fast),
              static_cast<long long>(NIL_ERR_UNSUPPORTED));
        check("and says why", std::strlen(err) > 0, true);

        const int32_t rc4 = nil_solve("garbage", NIL_SEAT_NORTH, "", SEATS_NIL_N, 0, &r, err,
                                      sizeof(err));
        check("nil_solve rejects garbage", static_cast<long long>(rc4),
              static_cast<long long>(NIL_ERR_PARSE));
        check("nil_solve reports an error message", std::strlen(err) > 0, true);

        const int32_t rc5 = nil_solve("N:A... .A.. ..A. ...A", 9, "", SEATS_NIL_N, 0, &r, err,
                                      sizeof(err));
        check("nil_solve rejects a bad leader", static_cast<long long>(rc5),
              static_cast<long long>(NIL_ERR_ILLEGAL_POSITION));

        // A seats array is required, and the two ways of getting it wrong get
        // different codes.  NIL_SEAT_NORTH is literally 0, so an un-migrated
        // call site compiles as a null pointer rather than failing to build;
        // this is what catches it.
        const int32_t rc_null = nil_solve("N:A... .A.. ..A. ...A", NIL_SEAT_NORTH, "", nullptr, 0,
                                          &r, err, sizeof(err));
        check("nil_solve rejects a null seats array", static_cast<long long>(rc_null),
              static_cast<long long>(NIL_ERR_NULL_ARG));

        const std::int32_t out_of_range[4] = {9, NIL_ROLE_OPPONENT, NIL_ROLE_COVER,
                                              NIL_ROLE_OPPONENT};
        const int32_t rc_range = nil_solve("N:A... .A.. ..A. ...A", NIL_SEAT_NORTH, "",
                                           out_of_range, 0, &r, err, sizeof(err));
        check("nil_solve rejects an out-of-range role", static_cast<long long>(rc_range),
              static_cast<long long>(NIL_ERR_ILLEGAL_POSITION));

        const std::int32_t two_nils[4] = {NIL_ROLE_NIL, NIL_ROLE_NIL, NIL_ROLE_COVER,
                                          NIL_ROLE_COVER};
        const int32_t rc_two = nil_solve("N:A... .A.. ..A. ...A", NIL_SEAT_NORTH, "", two_nils, 0,
                                         &r, err, sizeof(err));
        check("nil_solve reports two nils as unsupported, not malformed",
              static_cast<long long>(rc_two), static_cast<long long>(NIL_ERR_UNSUPPORTED));

        // The roles are read against the PBN's anchor, so the same deal spelled
        // two ways is one question across the ABI too.
        nil_result from_n;
        nil_result from_w;
        const std::int32_t nil_on_east_n[4] = {NIL_ROLE_OPPONENT, NIL_ROLE_NIL, NIL_ROLE_OPPONENT,
                                               NIL_ROLE_COVER};
        const std::int32_t nil_on_east_w[4] = {NIL_ROLE_COVER, NIL_ROLE_OPPONENT, NIL_ROLE_NIL,
                                               NIL_ROLE_OPPONENT};
        check("N-anchored spelling returns NIL_OK",
              static_cast<long long>(nil_solve("N:..2. ..A. ..5. ..7.", NIL_SEAT_NORTH, "",
                                               nil_on_east_n, NIL_FLAG_SPADES_BROKEN, &from_n, err,
                                               sizeof(err))),
              0LL);
        check("W-anchored spelling returns NIL_OK",
              static_cast<long long>(nil_solve("W:..7. ..2. ..A. ..5.", NIL_SEAT_NORTH, "",
                                               nil_on_east_w, NIL_FLAG_SPADES_BROKEN, &from_w, err,
                                               sizeof(err))),
              0LL);
        check("and the two agree on the nil count",
              static_cast<long long>(from_n.nil_tricks),
              static_cast<long long>(from_w.nil_tricks));

        // TWO BIDS ACROSS THE ABI.  Accepted now, where patch 54 refused it, and
        // fast mode refused with the code that means "this build cannot do
        // that" rather than the one that means "something broke inside".
        const std::int32_t pair_bids_wire[4] = {NIL_ROLE_NIL, NIL_ROLE_OPPONENT, NIL_ROLE_NIL,
                                                NIL_ROLE_OPPONENT};
        nil_result pair_res;
        check("nil_solve accepts a pair that both bid",
              static_cast<long long>(nil_solve("N:.A.A. .2.2. .3.3. .4.4.", NIL_SEAT_NORTH, "",
                                               pair_bids_wire, NIL_FLAG_SPADES_BROKEN, &pair_res,
                                               err, sizeof(err))),
              0LL);
        check("and reports one bid down", static_cast<long long>(pair_res.nils_set), 1LL);
        check("with both tricks on the pair",
              static_cast<long long>(pair_res.nil_side_tricks), 2LL);

        check("fast mode is unsupported for two bidders, not an internal error",
              static_cast<long long>(nil_solve("N:.A.A. .2.2. .3.3. .4.4.", NIL_SEAT_NORTH, "",
                                               pair_bids_wire,
                                               NIL_FLAG_SPADES_BROKEN | NIL_FLAG_FAST_MODE,
                                               &pair_res, err, sizeof(err))),
              static_cast<long long>(NIL_ERR_UNSUPPORTED));

        check("version string", std::string(nil_solver_version()), std::string("0.1.0"));
    }

    // ---- the move list -----------------------------------------------------
    //
    // The property that matters is that scoring every card cannot disagree with
    // scoring the position, so most of these are differentials rather than
    // pinned numbers: solve_moves against solve, and each row against an
    // independent solve of the position that row leads to.
    {
        std::cout << "\nMove list\n";

        // Rank equivalence with a witness a person can check.  North holds
        // SK SQ S2 and the jack is West's, so the king and the queen are one
        // move under two names and the deuce is not -- East's 543 sit between.
        nil::Position pos;
        std::string err;
        check("equivalence deal parses",
              nil::parse_pbn("N:KQ2.A.. 543.2.. A76.3.. J98.4..", pos.hands, err), true);
        pos.leader = nil::SEAT_NORTH;
        pos.spades_broken = true;

        nil::SearchOptions opts;
        const nil::SeatRoles north_nil = nil::seat_roles_from_nil(nil::SEAT_NORTH, false);
        nil::Solution sol;
        std::vector<nil::MoveScore> moves;
        check("solve_moves succeeds", nil::solve_moves(pos, north_nil, opts, sol, moves, err),
              true);
        check("one row per class", moves.size(), std::size_t{3});
        check("rows are in canonical order",
              nil::card_to_string(moves[0].card) + " " + nil::card_to_string(moves[1].card) + " " +
                  nil::card_to_string(moves[2].card),
              std::string("S2 SQ HA"));
        check("the queen stands for the king",
              nil::hand_to_string(moves[1].equals), std::string("SQ SK"));
        check("the deuce stands for itself", nil::hand_to_string(moves[0].equals),
              std::string("S2"));

        // Every legal card is named exactly once across the rows: the classes
        // partition the legal moves, so a caller expanding them gets the whole
        // move set and no card twice.
        nil::Hand covered = 0;
        int overlaps = 0;
        for (const nil::MoveScore& m : moves) {
            if (covered & m.equals) ++overlaps;
            covered |= m.equals;
        }
        const nil::Hand legal = nil::legal_moves(pos.hands[nil::SEAT_NORTH], 0, -1, true);
        check("the classes cover every legal card", nil::hand_to_string(covered),
              nil::hand_to_string(legal));
        check("and never name one twice", overlaps, 0);

        // The move list must not move the position's own answer.
        nil::Solution plain;
        check("solve succeeds", nil::solve(pos, north_nil, opts, plain, err), true);
        check("move list agrees on the value", sol.value, plain.value);
        check("move list agrees on nil_fails", sol.nils_set, plain.nils_set);
        check("move list agrees on the nil count", sol.nil_tricks, plain.nil_tricks);
        check("move list reproduces the principal variation", nil::format_pv_compact(sol),
              nil::format_pv_compact(plain));

        // The differential.  Each row says what happens after its card; solving
        // the position that card actually reaches has to say the same thing, by
        // a path that shares nothing with solve_moves but the rules.
        int row_mismatches = 0;
        for (const nil::MoveScore& m : moves) {
            nil::Position child = pos;
            const int seat = (pos.leader + pos.trick_len) & 3;
            child.hands[seat] &= ~nil::card_bit(m.card);
            child.spades_broken =
                nil::spades_broken_after(pos.spades_broken, nil::card_suit(m.card));
            if (pos.trick_len == 3) {
                const nil::CardId played[4] = {pos.trick[0], pos.trick[1], pos.trick[2], m.card};
                child.leader = nil::trick_winner(pos.leader, played, 4);
                child.trick_len = 0;
            } else {
                child.trick[pos.trick_len] = m.card;
                child.trick_len = pos.trick_len + 1;
            }

            nil::Solution after;
            if (!nil::solve(child, north_nil, opts, after, err)) {
                ++row_mismatches;
                continue;
            }
            // The child reports the REST of the hand; the row also carries the
            // trick this card completed, which mid-trick is none.
            if (after.nil_tricks != m.nil_tricks || after.nils_set != m.nils_set) {
                ++row_mismatches;
            }
        }
        check("every row matches an independent solve of the position it reaches",
              row_mismatches, 0);

        // Fast mode: same booleans, no counts.
        nil::SearchOptions fast = opts;
        fast.mode = nil::MODE_FAST;
        nil::Solution fsol;
        std::vector<nil::MoveScore> fmoves;
        check("fast move list succeeds",
              nil::solve_moves(pos, north_nil, fast, fsol, fmoves, err), true);
        check("fast mode lists the same cards", fmoves.size(), moves.size());
        int boolean_mismatches = 0;
        int counted = 0;
        for (std::size_t i = 0; i < fmoves.size() && i < moves.size(); ++i) {
            if (fmoves[i].card != moves[i].card) ++boolean_mismatches;
            if (fmoves[i].nils_set != moves[i].nils_set) ++boolean_mismatches;
            if (fmoves[i].nil_tricks != nil::TRICKS_NOT_COMPUTED) ++counted;
        }
        check("fast mode agrees card for card on the boolean", boolean_mismatches, 0);
        check("fast mode withholds the per-card counts", counted, 0);

        // An exhausted position is a legal thing to ask about, and the answer
        // is an empty list rather than an error.
        nil::Position done;
        for (int s = 0; s < 4; ++s) done.hands[s] = 0;
        done.leader = nil::SEAT_NORTH;
        nil::Solution dsol;
        std::vector<nil::MoveScore> dmoves;
        check("an empty position is not an error",
              nil::solve_moves(done, north_nil, opts, dsol, dmoves, err), true);
        check("and lists no moves", dmoves.size(), std::size_t{0});
    }

    // ---- the move list across the C ABI ------------------------------------
    {
        std::cout << "\nC ABI: nil_solve_moves\n";
        nil_result r;
        nil_move rows[NIL_MAX_MOVES];
        std::int32_t count = -1;
        char err[256] = {0};

        const std::int32_t rc =
            nil_solve_moves("N:2...  A... K... Q...", NIL_SEAT_NORTH, "", SEATS_NIL_N,
                            NIL_FLAG_NONE, &r, rows, NIL_MAX_MOVES, &count, err, sizeof err);
        check("nil_solve_moves returns NIL_OK", static_cast<long long>(rc), 0LL);
        check("one legal card", static_cast<long long>(count), 1LL);
        check("and it is the spade deuce",
              std::to_string(rows[0].suit) + ":" + std::to_string(rows[0].rank),
              std::string("0:2"));
        check("which is forced, so it is best", static_cast<long long>(rows[0].is_best), 1LL);
        check("it stands for no other card", static_cast<long long>(rows[0].equal_ranks), 0LL);

        // A buffer too small reports the size it needed rather than leaving the
        // caller to guess.
        std::int32_t needed = -1;
        const std::int32_t rc_small = nil_solve_moves(
            "N:KQ2.A.. 543.2.. A76.3.. J98.4..", NIL_SEAT_NORTH, "", SEATS_NIL_N,
            NIL_FLAG_SPADES_BROKEN, &r, rows, 1, &needed, err, sizeof err);
        check("a small buffer is rejected", static_cast<long long>(rc_small),
              static_cast<long long>(NIL_ERR_BUFFER_TOO_SMALL));
        check("and still reports the count needed", static_cast<long long>(needed), 3LL);

        // The equals bitfield uses DDS's encoding: bit r for rank r, and the
        // card's own rank left clear.
        std::int32_t n2 = 0;
        const std::int32_t rc2 = nil_solve_moves(
            "N:KQ2.A.. 543.2.. A76.3.. J98.4..", NIL_SEAT_NORTH, "", SEATS_NIL_N,
            NIL_FLAG_SPADES_BROKEN, &r, rows, NIL_MAX_MOVES, &n2, err, sizeof err);
        check("the equivalence deal returns NIL_OK", static_cast<long long>(rc2), 0LL);
        check("three classes", static_cast<long long>(n2), 3LL);
        check("the queen's row names the king",
              static_cast<long long>(rows[1].equal_ranks), static_cast<long long>(1 << 13));
        check("and does not name itself",
              static_cast<long long>(rows[1].equal_ranks & (1 << 12)), 0LL);

        // Fast mode withholds the counts here too.
        std::int32_t n3 = 0;
        const std::int32_t rc3 = nil_solve_moves(
            "N:KQ2.A.. 543.2.. A76.3.. J98.4..", NIL_SEAT_NORTH, "", SEATS_NIL_N,
            NIL_FLAG_SPADES_BROKEN | NIL_FLAG_FAST_MODE, &r, rows, NIL_MAX_MOVES, &n3, err,
            sizeof err);
        check("fast mode returns NIL_OK", static_cast<long long>(rc3), 0LL);
        check("fast mode lists the same three", static_cast<long long>(n3), 3LL);
        check("fast mode withholds a row's count", static_cast<long long>(rows[0].nil_tricks),
              static_cast<long long>(NIL_TRICKS_UNKNOWN));
    }

    std::cout << "\n";
    if (g_failures) {
        std::cout << "FAILURES: " << g_failures << "\n";
        return 1;
    }
    std::cout << "All checks passed.\n";
    return 0;
}
