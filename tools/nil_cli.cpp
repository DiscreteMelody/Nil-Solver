// nil_cli -- command line front end for the nil solver.
//
// Deliberately line-oriented so that `diff` localises a divergence, and with a
// --compact mode that tools/crosscheck.py parses.
//
//   nil_cli --pbn 'N:A...2 K...3 Q...4 J...5' --leader N --nil N
//   nil_cli --pbn '...' --leader W --nil S --trick 'H4 HK' --spades-broken
//   nil_cli --pbn '...' --nil S --compact
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "nil/position.hpp"
#include "nil/search.hpp"

namespace {

// Above this the search is still not finishing in a sitting.  It is a guard
// rail, not a statement about what the solver can represent -- pass --force to
// go past it.
constexpr int NIL_CLI_CARD_LIMIT = 9;

void usage(const char* argv0) {
    std::cout
        << "usage: " << argv0 << " --pbn <deal> [options]\n"
        << "\n"
        << "  --pbn <deal>            PBN deal, e.g. 'N:A...2 K...3 Q...4 J...5'\n"
        << "                          (spades.hearts.diamonds.clubs, clockwise from\n"
        << "                          the named seat; cards already on the current\n"
        << "                          trick must not appear)\n"
        << "  --leader <N|E|S|W>      seat that led the current trick   [N]\n"
        << "  --nil <N|E|S|W>         seat that bid nil                 [N]\n"
        << "  --trick '<cards>'       cards already played to the current trick, in\n"
        << "                          play order from the leader, e.g. 'H4 HK'\n"
        << "  --spades-broken         start with spades already broken\n"
        << "  --mode full|fast        full (default) answers everything: trick\n"
        << "                          counts, a principal variation, and a value\n"
        << "                          the replay checks. fast answers only whether\n"
        << "                          the nil can be broken\n"
        << "  --secondary max|min     tie-break: each pair takes as many tricks as\n"
        << "                          it can (max, default) or as few (min); no\n"
        << "                          effect in fast mode, which has no tie-break\n"
        << "  --nil-already-set       the nil has already been broken; drop the\n"
        << "                          primary objective and optimise only the\n"
        << "                          secondary one\n"
        << "  --break-on-forced-lead  a forced spade lead breaks spades (see README)\n"
        << "  --no-memo               disable the transposition table (same answer,\n"
        << "                          much slower)\n"
        << "  --no-collapse           generate every legal card rather than one per\n"
        << "                          class of rank-equivalent ones (same answer and\n"
        << "                          same PV, many more nodes)\n"
        << "  --no-static             do not settle a position by proof; search for\n"
        << "                          the answer instead (same answer, more nodes;\n"
        << "                          fast mode only)\n"
        << "  --no-narrow             do not narrow the window as moves come back\n"
        << "                          (same answer and same PV, many more nodes;\n"
        << "                          full mode only)\n"
        << "  --no-presolve           do not spend a fast search to bound full\n"
        << "                          mode's root window (same answer, more nodes;\n"
        << "                          full mode only)\n"
        << "  --no-ordering           try moves in canonical order rather than a\n"
        << "                          promising-first one (same answer, more nodes;\n"
        << "                          fast mode only)\n"
        << "  --no-last-trick         search the forced final trick instead of\n"
        << "                          evaluating it (same answer, more nodes)\n"
        << "  --no-target-bounds      do not answer a node from the reachable\n"
        << "                          range of the tricks left (same answer,\n"
        << "                          more nodes; full mode only)\n"
        << "  --tt-all-plies          consult the transposition table at every\n"
        << "                          ply, not only at a trick boundary (same\n"
        << "                          answer, and much slower)\n"
        << "  --tt-mb <n>             transposition table size in MiB         [256]\n"
        << "  --tt-stats              also report transposition table behaviour\n"
        << "  --moves                 score every legal card, not just the best:\n"
        << "                          one line per card with whether the nil\n"
        << "                          survives it and what it costs\n"
        << "  --compact               print only the machine-readable result\n"
        << "  --force                 allow more than 9 cards per hand (very slow)\n"
        << "  --help                  this message\n";
}

bool need_value(int argc, char** argv, int& i, const char* name, std::string& out) {
    if (i + 1 >= argc) {
        std::cerr << "error: " << name << " needs a value\n";
        return false;
    }
    out = argv[++i];
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string pbn;
    std::string trick_text;
    std::string leader_text = "N";
    std::string nil_text = "N";
    bool spades_broken = false;
    bool compact = false;
    bool list_moves = false;
    bool force = false;
    bool tt_stats = false;
    nil::SearchOptions opts;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        std::string value;
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (arg == "--pbn") {
            if (!need_value(argc, argv, i, "--pbn", pbn)) return 2;
        } else if (arg == "--leader") {
            if (!need_value(argc, argv, i, "--leader", leader_text)) return 2;
        } else if (arg == "--nil" || arg == "--designated") {
            if (!need_value(argc, argv, i, "--nil", nil_text)) return 2;
        } else if (arg == "--trick") {
            if (!need_value(argc, argv, i, "--trick", trick_text)) return 2;
        } else if (arg == "--spades-broken") {
            spades_broken = true;
        } else if (arg == "--mode") {
            std::string mode;
            if (!need_value(argc, argv, i, "--mode", mode)) return 2;
            if (mode == "fast") {
                opts.mode = nil::MODE_FAST;
            } else if (mode == "full") {
                opts.mode = nil::MODE_FULL;
            } else {
                std::cerr << "error: --mode takes 'full' or 'fast'\n";
                return 2;
            }
        } else if (arg == "--secondary") {
            std::string mode;
            if (!need_value(argc, argv, i, "--secondary", mode)) return 2;
            if (mode == "min") {
                opts.minimise_own_tricks = true;
            } else if (mode == "max") {
                opts.minimise_own_tricks = false;
            } else {
                std::cerr << "error: --secondary takes 'max' or 'min'\n";
                return 2;
            }
        } else if (arg == "--nil-already-set") {
            opts.nil_already_set = true;
        } else if (arg == "--break-on-forced-lead") {
            opts.break_on_forced_spade_lead = true;
        } else if (arg == "--no-memo") {
            opts.use_memo = false;
        } else if (arg == "--no-collapse") {
            opts.collapse_equivalents = false;
        } else if (arg == "--no-static") {
            opts.use_static_bounds = false;
        } else if (arg == "--no-full-static") {
            opts.full_static_bounds = false;
        } else if (arg == "--no-ordering") {
            opts.order_moves = false;
        } else if (arg == "--no-last-trick") {
            opts.last_trick_eval = false;
        } else if (arg == "--tt-all-plies") {
            opts.tt_boundaries_only = false;
        } else if (arg == "--no-target-bounds") {
            opts.target_bounds = false;
        } else if (arg == "--no-narrow") {
            opts.narrow_window = false;
        } else if (arg == "--no-presolve") {
            opts.presolve_window = false;
        } else if (arg == "--no-canonical-pv") {
            opts.canonical_pv = false;
        } else if (arg == "--tt-mb") {
            std::string mb;
            if (!need_value(argc, argv, i, "--tt-mb", mb)) return 2;
            const long long value = std::atoll(mb.c_str());
            if (value < 0) {
                std::cerr << "error: --tt-mb cannot be negative\n";
                return 2;
            }
            opts.tt_megabytes = static_cast<std::size_t>(value);
        } else if (arg == "--tt-stats") {
            tt_stats = true;
        } else if (arg == "--moves") {
            list_moves = true;
        } else if (arg == "--compact") {
            compact = true;
        } else if (arg == "--force") {
            force = true;
        } else {
            std::cerr << "error: unknown argument '" << arg << "'\n";
            usage(argv[0]);
            return 2;
        }
    }

    if (pbn.empty()) {
        std::cerr << "error: --pbn is required\n";
        usage(argv[0]);
        return 2;
    }

    const int leader = nil::parse_seat(leader_text);
    const int nil_seat = nil::parse_seat(nil_text);
    if (leader < 0) {
        std::cerr << "error: bad --leader '" << leader_text << "'\n";
        return 2;
    }
    if (nil_seat < 0) {
        std::cerr << "error: bad --nil '" << nil_text << "'\n";
        return 2;
    }

    std::string err;
    nil::Position pos;
    if (!nil::parse_pbn(pbn, pos.hands, err)) {
        std::cerr << "error: " << err << "\n";
        return 2;
    }
    pos.leader = leader;
    pos.spades_broken = spades_broken;
    if (!trick_text.empty()) {
        int count = 0;
        if (!nil::parse_cards(trick_text, pos.trick, 3, count, err)) {
            std::cerr << "error: " << err << "\n";
            return 2;
        }
        pos.trick_len = count;
    }
    if (!nil::validate(pos, err)) {
        std::cerr << "error: " << err << "\n";
        return 2;
    }
    if (pos.cards_per_hand() > NIL_CLI_CARD_LIMIT && !force) {
        std::cerr << "error: " << pos.cards_per_hand() << " cards per hand; ";
        if (opts.mode == nil::MODE_FAST) {
            std::cerr << "the boolean search does prune, and a full thirteen is usually "
                         "well under a second, but the spread is wide and an awkward "
                         "layout can still take several. Use --force to insist.\n";
        } else {
            std::cerr << "the full search is exhaustive (the transposition table collapses "
                         "repeated positions but prunes nothing) and this will take a very "
                         "long time. --mode fast prunes hard and answers the nil question "
                         "alone; use --force to insist on this one.\n";
        }
        return 2;
    }

    nil::Solution sol;
    std::vector<nil::MoveScore> scored;
    if (list_moves) {
        if (!nil::solve_moves(pos, nil_seat, opts, sol, scored, err)) {
            std::cerr << "error: " << err << "\n";
            return 3;
        }
    } else if (!nil::solve(pos, nil_seat, opts, sol, err)) {
        std::cerr << "error: " << err << "\n";
        return 3;
    }

    if (compact) {
        // Every key is present in both modes so that a parser written against
        // one keeps working against the other; in fast mode the three counts
        // read -1 (nil::TRICKS_NOT_COMPUTED) and pv is empty, and `mode` is
        // what says so rather than leaving -1 to be guessed at.
        std::cout << "mode=" << (opts.mode == nil::MODE_FAST ? "fast" : "full") << "\n"
                  << "tricks=" << sol.nil_tricks << "\n"
                  << "side_tricks=" << sol.nil_side_tricks << "\n"
                  << "opponent_tricks=" << sol.opponent_tricks << "\n"
                  << "nil_fails=" << (sol.nil_fails ? 1 : 0) << "\n"
                  << "nodes=" << sol.nodes << "\n"
                  << "pv=" << nil::format_pv_compact(sol) << "\n";
        // Extra keys, and only on request: the scripts that parse this build a
        // dictionary and would not mind them, but a diagnostic that appears
        // unasked in a machine-readable stream is how a parser starts depending
        // on it.
        // One `move=` line per card, in canonical order, fields separated by
        // colons: card, whether the nil fails after it, the three trick counts,
        // whether it is one of the best, and the equal cards it stands for.
        for (const nil::MoveScore& m : scored) {
            std::cout << "move=" << nil::card_to_string(m.card) << ':'
                      << (m.nil_fails ? 1 : 0) << ':' << m.nil_tricks << ':'
                      << m.nil_side_tricks << ':' << m.opponent_tricks << ':'
                      << (m.is_best ? 1 : 0) << ':';
            bool first = true;
            for (nil::Hand h = m.equals; h;) {
                const nil::CardId c = nil::take_lowest(h);
                if (c == m.card) continue;
                if (!first) std::cout << ',';
                std::cout << nil::card_to_string(c);
                first = false;
            }
            std::cout << "\n";
        }
        if (tt_stats) {
            std::cout << "tt_probes=" << sol.tt_probes << "\n"
                      << "tt_hits=" << sol.tt_hits << "\n"
                      << "tt_partial=" << sol.tt_partial << "\n"
                      << "tt_stores=" << sol.tt_stores << "\n"
                      << "tt_evictions=" << sol.tt_evictions << "\n";
        }
    } else {
        std::cout << nil::format_solution(pos, sol, opts) << "\n";
        if (list_moves) {
            const bool fast = opts.mode == nil::MODE_FAST;
            std::cout << "Legal cards for " << nil::SEAT_CHARS[(pos.leader + pos.trick_len) & 3]
                      << ":\n";
            for (const nil::MoveScore& m : scored) {
                std::cout << "  " << (m.is_best ? '*' : ' ') << ' '
                          << nil::card_to_string(m.card) << "  "
                          << (m.nil_fails ? "nil FAILS " : "nil holds ");
                if (!fast) {
                    std::cout << "  " << nil::SEAT_CHARS[sol.nil_seat] << '=' << m.nil_tricks
                              << "  side=" << m.nil_side_tricks
                              << "  opp=" << m.opponent_tricks;
                }
                bool first = true;
                for (nil::Hand h = m.equals; h;) {
                    const nil::CardId c = nil::take_lowest(h);
                    if (c == m.card) continue;
                    std::cout << (first ? "   = " : ",") << nil::card_to_string(c);
                    first = false;
                }
                std::cout << "\n";
            }
            std::cout << "  (* marks a card that achieves the position's value; cards after "
                         "'=' are the\n   same move under another name)\n";
        }
        if (tt_stats) {
            // `partial` is the one to read.  It counts probes that found the
            // position but held a bound too weak to settle the window being
            // asked about, which is exactly the set of nodes that could hold a
            // stored move and still have moves to search.  It is zero in both
            // modes today -- that is what closed roadmap item 5 -- and a
            // non-zero value here means the window has started varying.
            std::cout << "\nTransposition table\n"
                      << "  probes       " << sol.tt_probes << "\n"
                      << "  hits         " << sol.tt_hits << "\n"
                      << "  partial      " << sol.tt_partial << "\n"
                      << "  stores       " << sol.tt_stores << "\n"
                      << "  evictions    " << sol.tt_evictions << "\n";
        }
    }
    return 0;
}
