// Self-tests for the nil solver.
//
// The rule-unit and small-search cases are deliberately the same ones
// nil_oracle.py checks in its selftest(), so a rule-level disagreement shows up
// here rather than as a mystery one-card divergence deep in a random deal.
//
// The end-to-end agreement testing lives in tools/crosscheck.py, which runs the
// real oracle.
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "nil/position.hpp"
#include "nil/rules.hpp"
#include "nil/search.hpp"
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

Solution must_solve(const Position& pos, const char* nil_seat,
                    const SearchOptions& opts = SearchOptions()) {
    Solution sol;
    std::string err;
    if (!nil::solve(pos, nil::parse_seat(nil_seat), opts, sol, err)) {
        std::cerr << "test bug: solve failed: " << err << "\n";
        std::exit(70);
    }
    return sol;
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

    check("ruff breaks spades",
          nil::spades_broken_after(false, 1, nil::SUIT_HEARTS, nil::SUIT_SPADES, false), true);
    check("discarding a non-spade does not break",
          nil::spades_broken_after(false, 1, nil::SUIT_HEARTS, nil::SUIT_CLUBS, false), false);
    check("spade on a spade lead does not break",
          nil::spades_broken_after(false, 1, nil::SUIT_SPADES, nil::SUIT_SPADES, false), false);
    check("forced spade lead: literal reading does not break",
          nil::spades_broken_after(false, 0, -1, nil::SUIT_SPADES, false), false);
    check("forced spade lead: alternate convention breaks",
          nil::spades_broken_after(false, 0, -1, nil::SUIT_SPADES, true), true);
    check("already broken stays broken",
          nil::spades_broken_after(true, 1, nil::SUIT_HEARTS, nil::SUIT_CLUBS, false), true);

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
        check("single trick, nil E", must_solve(pos, "E").tricks, 1);
        check("single trick, nil N", must_solve(pos, "N").tricks, 0);
        check("single trick, nil S", must_solve(pos, "S").tricks, 0);
        check("single trick, nil E fails", must_solve(pos, "E").nil_fails, true);
        check("single trick, nil N makes", must_solve(pos, "N").nil_fails, false);
        check("single trick PV", nil::format_pv_compact(must_solve(pos, "E")),
              std::string("N:D2 E:DA S:D5 W:D7"));
    }
    {
        // The nil bidder holds the bare ace of the only suit in play: exactly
        // one trick, regardless of anyone's intentions.
        const Position pos = make_position("N:.A2.. .K3.. .54.. .Q6..", "N", true);
        check("bare ace guarantees a trick", must_solve(pos, "N").tricks, 1);
        check("bare ace kills the nil", must_solve(pos, "N").nil_fails, true);
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
        check("squander: E/W duck to hand N a trick", must_solve(pos, "N").tricks, 1);

        // The same layout rotated one seat clockwise, with the nil on E.  The
        // coalitions follow the nil bidder's seat, not a fixed parity, so this
        // must give the same answer.
        const Position rotated = make_position("N:.Q6.. .K2.. .A3.. .54..", "E", true);
        check("squander rotated onto E", must_solve(rotated, "E").tricks, 1);
        check("squander rotated: N/S now maximise", must_solve(rotated, "E").nil_fails, true);
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
        check("void nil must ruff and take the trick", sol.tricks, 1);
        check("void nil fails", sol.nil_fails, true);
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
        check("mid-trick nil survives", sol.tricks, 0);
    }
    {
        // The memo caches a pure function: identical value AND identical PV.
        const Position pos = make_position("N:A2.K.. K3.A.. Q4.Q.. J5.J..", "N", false);
        SearchOptions memo_on;
        SearchOptions memo_off;
        memo_off.use_memo = false;
        const Solution a = must_solve(pos, "S", memo_on);
        const Solution b = must_solve(pos, "S", memo_off);
        check("memo agrees on value", a.tricks, b.tricks);
        check("memo agrees on PV", nil::format_pv_compact(a), nil::format_pv_compact(b));
        check("PV plays every card", static_cast<long long>(a.pv.size()), 12LL);
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
        int tricks = 0;
        std::string err;
        check("good PV replays", nil::replay_pv(pos, sol.pv, 1, false, tricks, err), true);
        check("good PV replays to the same value", tricks, 1);

        std::vector<nil::Play> bad = sol.pv;
        std::swap(bad[0], bad[1]);
        check("out-of-turn PV is rejected", nil::replay_pv(pos, bad, 1, false, tricks, err),
              false);

        std::vector<nil::Play> truncated(sol.pv.begin(), sol.pv.begin() + 2);
        check("short PV is rejected", nil::replay_pv(pos, truncated, 1, false, tricks, err),
              false);
    }

    std::cout << "C ABI\n";
    {
        nil_result r;
        char err[256] = {0};
        const int32_t rc = nil_solve("N:.A2.. .K3.. .54.. .Q6..", NIL_SEAT_NORTH, "",
                                     NIL_SEAT_NORTH, NIL_FLAG_SPADES_BROKEN, &r, err, sizeof(err));
        check("nil_solve returns NIL_OK", static_cast<long long>(rc), 0LL);
        check("nil_solve reports the trick", static_cast<long long>(r.tricks), 1LL);
        check("nil_solve reports failure", static_cast<long long>(r.nil_fails), 1LL);
        check("nil_solve reports tricks remaining", static_cast<long long>(r.tricks_remaining),
              2LL);

        char pv[256] = {0};
        const int32_t rc2 = nil_solve_pv("N:..2. ..A. ..5. ..7.", NIL_SEAT_NORTH, "",
                                         NIL_SEAT_EAST, NIL_FLAG_SPADES_BROKEN, &r, pv, sizeof(pv),
                                         err, sizeof(err));
        check("nil_solve_pv returns NIL_OK", static_cast<long long>(rc2), 0LL);
        check("nil_solve_pv writes the PV", std::string(pv), std::string("N:D2 E:DA S:D5 W:D7"));

        char tiny[4] = {0};
        const int32_t rc3 = nil_solve_pv("N:..2. ..A. ..5. ..7.", NIL_SEAT_NORTH, "",
                                         NIL_SEAT_EAST, NIL_FLAG_SPADES_BROKEN, &r, tiny,
                                         sizeof(tiny), err, sizeof(err));
        check("nil_solve_pv rejects a small buffer", static_cast<long long>(rc3),
              static_cast<long long>(NIL_ERR_BUFFER_TOO_SMALL));

        check("nil_fails convenience wrapper",
              static_cast<long long>(nil_fails("N:.A2.. .K3.. .54.. .Q6..", NIL_SEAT_NORTH, "",
                                               NIL_SEAT_NORTH, NIL_FLAG_SPADES_BROKEN)),
              1LL);

        const int32_t rc4 = nil_solve("garbage", NIL_SEAT_NORTH, "", NIL_SEAT_NORTH, 0, &r, err,
                                      sizeof(err));
        check("nil_solve rejects garbage", static_cast<long long>(rc4),
              static_cast<long long>(NIL_ERR_PARSE));
        check("nil_solve reports an error message", std::strlen(err) > 0, true);

        const int32_t rc5 = nil_solve("N:A... .A.. ..A. ...A", 9, "", NIL_SEAT_NORTH, 0, &r, err,
                                      sizeof(err));
        check("nil_solve rejects a bad leader", static_cast<long long>(rc5),
              static_cast<long long>(NIL_ERR_ILLEGAL_POSITION));

        check("version string", std::string(nil_solver_version()), std::string("0.1.0"));
    }

    std::cout << "\n";
    if (g_failures) {
        std::cout << "FAILURES: " << g_failures << "\n";
        return 1;
    }
    std::cout << "All checks passed.\n";
    return 0;
}
