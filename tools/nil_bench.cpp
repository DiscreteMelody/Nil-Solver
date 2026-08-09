// nil_bench -- verify a corpus and time the search.
//
// This does double duty on purpose.  A benchmark that does not check its
// answers will eventually report a very fast wrong solver, and a correctness
// suite that does not report timings gives you no way to see a change cost you
// 3x.  So every timed run also asserts the expected trick count, and the
// process exits non-zero if any position disagrees.
//
// PRIMARY METRIC: NODES, NOT SECONDS
// ----------------------------------
// Node counts are deterministic and machine independent: the same corpus on the
// same commit gives byte-identical numbers on your laptop, on a CI runner, and
// on a loaded machine.  Wall time does not.  When you add alpha-beta or move
// ordering, the number to watch is nodes/position, because it measures the
// search improvement itself rather than the machine it ran on.  Time is
// reported too, but treat it as the secondary signal -- it catches the case
// where nodes fall but each node got more expensive.
//
//   nil_bench --corpus tests/corpus/positions.txt
//   nil_bench --corpus tests/corpus/positions.txt --repeat 5 --csv after.csv
//   nil_bench --corpus tests/corpus/positions.txt --baseline before.csv
//   nil_bench --random --cards 7 --count 10 --seed 1     # timing only, no answers
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "nil/corpus.hpp"
#include "nil/position.hpp"
#include "nil/rules.hpp"
#include "nil/search.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Row {
    std::string name;
    int cards = 0;
    int tricks = 0;
    std::uint64_t nodes = 0;
    double ms = 0.0;
};

// splitmix64: tiny, and identical on every compiler and platform, which is what
// makes --random reproducible from a seed.  std::mt19937 plus a distribution is
// not, because the distributions are implementation defined.
struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    std::uint64_t below(std::uint64_t n) { return next() % n; }
};

std::string commas(std::uint64_t n) {
    std::string d = std::to_string(n);
    std::string out;
    int c = 0;
    for (auto it = d.rbegin(); it != d.rend(); ++it) {
        if (c && c % 3 == 0) out += ',';
        out += *it;
        ++c;
    }
    return std::string(out.rbegin(), out.rend());
}

std::string pct_delta(double now, double before) {
    if (before <= 0.0) return "     -";
    const double delta = (now - before) / before * 100.0;
    std::ostringstream os;
    os << std::showpos << std::fixed << std::setprecision(1) << delta << "%";
    return os.str();
}

bool read_csv(const std::string& path, std::map<std::string, Row>& out, std::string& err) {
    std::ifstream in(path.c_str());
    if (!in) {
        err = "cannot open '" + path + "'";
        return false;
    }
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) {  // header
            first = false;
            if (line.rfind("name,", 0) == 0) continue;
        }
        if (line.empty()) continue;
        std::vector<std::string> f;
        std::string field;
        for (char ch : line) {
            if (ch == ',') {
                f.push_back(field);
                field.clear();
            } else {
                field += ch;
            }
        }
        f.push_back(field);
        if (f.size() < 5) continue;
        Row row;
        row.name = f[0];
        row.cards = std::atoi(f[1].c_str());
        row.tricks = std::atoi(f[2].c_str());
        row.nodes = std::strtoull(f[3].c_str(), nullptr, 10);
        row.ms = std::atof(f[4].c_str());
        out[row.name] = row;
    }
    return true;
}

void write_csv(const std::string& path, const std::vector<Row>& rows) {
    std::ofstream out(path.c_str());
    out << "name,cards,tricks,nodes,ms\n";
    for (const Row& r : rows) {
        out << r.name << ',' << r.cards << ',' << r.tricks << ',' << r.nodes << ','
            << std::fixed << std::setprecision(3) << r.ms << '\n';
    }
}

// Deal `cards` to each seat, plus a random leader/nil/broken flag.
nil::Position random_position(Rng& rng, int cards, int& nil_seat) {
    int deck[52];
    for (int i = 0; i < 52; ++i) deck[i] = nil::make_card(i / 13, i % 13 + 2);
    for (int i = 51; i > 0; --i) {
        const int j = static_cast<int>(rng.below(static_cast<std::uint64_t>(i) + 1));
        std::swap(deck[i], deck[j]);
    }
    nil::Position pos;
    for (int seat = 0; seat < 4; ++seat) {
        for (int k = 0; k < cards; ++k) pos.hands[seat] |= nil::card_bit(deck[seat * cards + k]);
    }
    pos.leader = static_cast<int>(rng.below(4));
    pos.spades_broken = (rng.below(2) != 0);
    nil_seat = static_cast<int>(rng.below(4));
    return pos;
}

void usage(const char* argv0) {
    std::cout << "usage: " << argv0 << " --corpus <file> [options]\n"
              << "       " << argv0 << " --random --cards <n> --count <n> [options]\n"
              << "\n"
              << "  --corpus <file>   corpus to verify and time\n"
              << "  --random          time random deals instead (no answers to check)\n"
              << "  --cards <n>       cards per hand for --random           [6]\n"
              << "  --count <n>       positions for --random                [10]\n"
              << "  --seed <n>        seed for --random                     [1]\n"
              << "  --repeat <n>      time each position n times, keep the best [1]\n"
              << "  --cards-only <n>  restrict a corpus run to one hand size\n"
              << "  --no-memo         disable the full-state memo\n"
              << "  --check-pv        also require the recorded PV to match (see below)\n"
              << "  --csv <file>      write per-position rows for later comparison\n"
              << "  --baseline <file> compare against a csv written earlier\n"
              << "  --slowest <n>     list the n slowest positions            [5]\n"
              << "  --quiet           only print the summary and any failures\n"
              << "\n"
              << "--check-pv is off by default on purpose: move ordering will change\n"
              << "which of several equal-valued cards the search picks, and that is\n"
              << "not a regression.  Turn it on while the search is still exhaustive.\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string corpus_path;
    std::string csv_path;
    std::string baseline_path;
    bool random_mode = false;
    bool quiet = false;
    bool check_pv = false;
    int cards = 6;
    int count = 10;
    int repeat = 1;
    int slowest = 5;
    int cards_only = 0;
    std::uint64_t seed = 1;
    nil::SearchOptions opts;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_next = (i + 1 < argc);
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (arg == "--corpus" && has_next) {
            corpus_path = argv[++i];
        } else if (arg == "--csv" && has_next) {
            csv_path = argv[++i];
        } else if (arg == "--baseline" && has_next) {
            baseline_path = argv[++i];
        } else if (arg == "--random") {
            random_mode = true;
        } else if (arg == "--cards" && has_next) {
            cards = std::atoi(argv[++i]);
        } else if (arg == "--cards-only" && has_next) {
            cards_only = std::atoi(argv[++i]);
        } else if (arg == "--count" && has_next) {
            count = std::atoi(argv[++i]);
        } else if (arg == "--seed" && has_next) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--repeat" && has_next) {
            repeat = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--slowest" && has_next) {
            slowest = std::atoi(argv[++i]);
        } else if (arg == "--no-memo") {
            opts.use_memo = false;
        } else if (arg == "--check-pv") {
            check_pv = true;
        } else if (arg == "--quiet") {
            quiet = true;
        } else {
            std::cerr << "error: unknown or incomplete argument '" << arg << "'\n";
            usage(argv[0]);
            return 2;
        }
    }

    if (corpus_path.empty() && !random_mode) {
        std::cerr << "error: give either --corpus <file> or --random\n";
        usage(argv[0]);
        return 2;
    }

    // ---- assemble the work list ------------------------------------------
    struct Item {
        std::string name;
        nil::Position position;
        int nil_seat;
        bool forced;
        int expected;
        std::string expected_pv;
        std::string repro;
    };
    std::vector<Item> items;
    std::string err;

    if (!corpus_path.empty()) {
        std::vector<nil::CorpusEntry> entries;
        if (!nil::load_corpus(corpus_path, entries, err)) {
            std::cerr << "error: " << err << "\n";
            return 2;
        }
        for (const nil::CorpusEntry& e : entries) {
            if (cards_only && e.position.cards_per_hand() != cards_only) continue;
            items.push_back(Item{e.name, e.position, e.nil_seat, e.break_on_forced_spade_lead,
                                 e.expected_tricks, e.expected_pv, nil::corpus_repro(e)});
        }
        if (items.empty()) {
            std::cerr << "error: corpus has no positions matching the filter\n";
            return 2;
        }
    } else {
        Rng rng(seed);
        for (int i = 0; i < count; ++i) {
            int nil_seat = 0;
            nil::Position pos = random_position(rng, cards, nil_seat);
            std::ostringstream name;
            name << "r" << cards << "-" << std::setw(4) << std::setfill('0') << i;
            items.push_back(Item{name.str(), pos, nil_seat, false, -1, "", ""});
        }
    }

    if (!quiet) {
        std::cout << (corpus_path.empty() ? "random" : corpus_path) << "   " << items.size()
                  << " positions, memo " << (opts.use_memo ? "on" : "off") << ", best of "
                  << repeat << "\n\n";
    }

    // ---- run --------------------------------------------------------------
    std::vector<Row> rows;
    rows.reserve(items.size());
    int failures = 0;

    for (const Item& item : items) {
        nil::Solution sol;
        double best_ms = 0.0;
        for (int r = 0; r < repeat; ++r) {
            const Clock::time_point t0 = Clock::now();
            nil::SearchOptions run_opts = opts;
            run_opts.break_on_forced_spade_lead = item.forced;
            if (!nil::solve(item.position, item.nil_seat, run_opts, sol, err)) {
                std::cerr << "FAIL " << item.name << ": solve failed: " << err << "\n";
                ++failures;
                break;
            }
            const double ms =
                std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            if (r == 0 || ms < best_ms) best_ms = ms;
        }

        if (item.expected >= 0 && sol.tricks != item.expected) {
            std::cout << "FAIL " << item.name << ": expected " << item.expected << " trick(s), got "
                      << sol.tricks << "\n  " << item.repro << "\n";
            ++failures;
        }
        if (check_pv && !item.expected_pv.empty()) {
            const std::string got = nil::format_pv_compact(sol);
            if (got != item.expected_pv) {
                std::cout << "FAIL " << item.name << ": PV differs\n    got      " << got
                          << "\n    expected " << item.expected_pv << "\n  " << item.repro << "\n";
                ++failures;
            }
        }

        rows.push_back(Row{item.name, item.position.cards_per_hand(), sol.tricks, sol.nodes,
                           best_ms});
    }

    // ---- aggregate --------------------------------------------------------
    std::map<int, Row> totals;   // keyed by cards per hand; nodes/ms summed
    std::map<int, int> counts;
    for (const Row& r : rows) {
        Row& t = totals[r.cards];
        t.cards = r.cards;
        t.nodes += r.nodes;
        t.ms += r.ms;
        ++counts[r.cards];
    }

    std::map<std::string, Row> baseline;
    bool have_baseline = false;
    if (!baseline_path.empty()) {
        if (!read_csv(baseline_path, baseline, err)) {
            std::cerr << "warning: " << err << "\n";
        } else {
            have_baseline = true;
        }
    }

    std::cout << "  cards  positions            nodes      nodes/pos          ms     ms/pos"
              << "    nodes/sec\n";
    std::uint64_t all_nodes = 0;
    double all_ms = 0.0;
    int all_positions = 0;
    for (const auto& kv : totals) {
        const int c = kv.first;
        const Row& t = kv.second;
        const int n = counts[c];
        all_nodes += t.nodes;
        all_ms += t.ms;
        all_positions += n;
        const double nps = t.ms > 0 ? (static_cast<double>(t.nodes) / (t.ms / 1000.0)) : 0.0;
        std::cout << std::setw(7) << c << std::setw(11) << n << std::setw(17) << commas(t.nodes)
                  << std::setw(15) << commas(t.nodes / (n ? n : 1)) << std::setw(12)
                  << std::fixed << std::setprecision(1) << t.ms << std::setw(11)
                  << std::setprecision(2) << (t.ms / (n ? n : 1)) << std::setw(13)
                  << commas(static_cast<std::uint64_t>(nps)) << "\n";
    }
    std::cout << "  ------------------------------------------------------------------"
              << "---------------\n";
    {
        const double nps = all_ms > 0 ? (static_cast<double>(all_nodes) / (all_ms / 1000.0)) : 0.0;
        std::cout << "  total" << std::setw(13) << all_positions << std::setw(17)
                  << commas(all_nodes) << std::setw(15)
                  << commas(all_nodes / (all_positions ? all_positions : 1)) << std::setw(12)
                  << std::fixed << std::setprecision(1) << all_ms << std::setw(11)
                  << std::setprecision(2) << (all_ms / (all_positions ? all_positions : 1))
                  << std::setw(13) << commas(static_cast<std::uint64_t>(nps)) << "\n";
    }

    if (have_baseline) {
        std::uint64_t base_nodes = 0;
        double base_ms = 0.0;
        int matched = 0;
        for (const Row& r : rows) {
            const auto it = baseline.find(r.name);
            if (it == baseline.end()) continue;
            base_nodes += it->second.nodes;
            base_ms += it->second.ms;
            ++matched;
        }
        std::cout << "\n  vs " << baseline_path << " (" << matched << " positions in common)\n"
                  << "    nodes  " << commas(base_nodes) << "  ->  " << commas(all_nodes) << "   "
                  << pct_delta(static_cast<double>(all_nodes), static_cast<double>(base_nodes))
                  << "\n"
                  << "    ms     " << std::fixed << std::setprecision(1) << base_ms << "  ->  "
                  << all_ms << "   " << pct_delta(all_ms, base_ms) << "\n";
    }

    if (slowest > 0 && !quiet) {
        std::vector<Row> sorted = rows;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Row& a, const Row& b) { return a.ms > b.ms; });
        const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(slowest),
                                                    sorted.size());
        std::cout << "\n  slowest positions:\n";
        for (std::size_t i = 0; i < n; ++i) {
            std::cout << "    " << std::left << std::setw(12) << sorted[i].name << std::right
                      << sorted[i].cards << " cards" << std::setw(16) << commas(sorted[i].nodes)
                      << " nodes" << std::setw(10) << std::fixed << std::setprecision(1)
                      << sorted[i].ms << " ms\n";
        }
    }

    if (!csv_path.empty()) {
        write_csv(csv_path, rows);
        if (!quiet) std::cout << "\n  wrote " << csv_path << "\n";
    }

    std::cout << "\n";
    if (failures) {
        std::cout << failures << " position(s) FAILED\n";
        return 1;
    }
    if (!corpus_path.empty()) {
        std::cout << "all " << rows.size() << " expected values matched\n";
    }
    return 0;
}
