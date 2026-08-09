// nil_cli -- command line front end for the nil solver.
//
// Deliberately line-oriented so that `diff` localises a divergence, and with a
// --compact mode that tools/crosscheck.py parses.
//
//   nil_cli --pbn 'N:A...2 K...3 Q...4 J...5' --leader N --nil N
//   nil_cli --pbn '...' --leader W --nil S --trick 'H4 HK' --spades-broken
//   nil_cli --pbn '...' --nil S --compact
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "nil/position.hpp"
#include "nil/search.hpp"

namespace {

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
        << "  --break-on-forced-lead  a forced spade lead breaks spades (see README)\n"
        << "  --no-memo               disable the full-state memo (same answer, slower)\n"
        << "  --compact               print only the machine-readable result\n"
        << "  --force                 allow more than 7 cards per hand (will not finish)\n"
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
    bool force = false;
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
        } else if (arg == "--break-on-forced-lead") {
            opts.break_on_forced_spade_lead = true;
        } else if (arg == "--no-memo") {
            opts.use_memo = false;
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
    if (pos.cards_per_hand() > 7 && !force) {
        std::cerr << "error: " << pos.cards_per_hand()
                  << " cards per hand; this is an exhaustive search with no pruning "
                     "and will not finish. Use --force to insist.\n";
        return 2;
    }

    nil::Solution sol;
    if (!nil::solve(pos, nil_seat, opts, sol, err)) {
        std::cerr << "error: " << err << "\n";
        return 3;
    }

    if (compact) {
        std::cout << "tricks=" << sol.tricks << "\n"
                  << "nil_fails=" << (sol.nil_fails ? 1 : 0) << "\n"
                  << "nodes=" << sol.nodes << "\n"
                  << "pv=" << nil::format_pv_compact(sol) << "\n";
    } else {
        std::cout << nil::format_solution(pos, sol) << "\n";
    }
    return 0;
}
