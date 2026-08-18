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
//
// Corpus entries carry their own objective settings (tie-break direction and
// the already-set flag), so a corpus run exercises all of them; --secondary and
// --nil-already-set only apply to --random runs.
//
// MODES
// -----
// --mode full (default) is the lexicographic search: every corpus row's trick
// counts get checked.  --mode fast is the boolean nil search, which computes no
// trick counts, so only nil_fails gets checked.  --mode both runs each position
// twice and requires the two to agree -- fast mode has no principal variation
// to replay against itself, so this is what stands in for full mode's internal
// self-check.  The timed and recorded rows in --mode both are the fast ones.
//
// Since alpha-beta landed the two modes no longer visit the same nodes, so
// --mode both is no longer nearly free, and it is no longer nearly a tautology
// either: it holds a pruned answer against an unpruned one that nil_oracle.py
// has already checked.  It also prints the node ratio between them, which is
// the measurement the pruning work is judged on.
#include <algorithm>
#include <chrono>
#include <ctime>
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

#ifndef NIL_BUILD_CONFIG
#define NIL_BUILD_CONFIG "unknown"
#endif

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

// ---------------------------------------------------------------------------
// Run provenance.  A history row is only worth keeping if you can tell later
// which build produced it, so every row carries the commit, whether the tree
// was dirty at the time, the build configuration, the compiler and the host.
// Wall time is not comparable across machines; without these columns a history
// file quietly turns into nonsense the first time someone benchmarks on a
// different laptop.
// ---------------------------------------------------------------------------

#if defined(_WIN32)
#define NIL_POPEN _popen
#define NIL_PCLOSE _pclose
#define NIL_DEVNULL "NUL"
#else
#define NIL_POPEN popen
#define NIL_PCLOSE pclose
#define NIL_DEVNULL "/dev/null"
#endif

std::string run_capture(const std::string& command) {
    FILE* pipe = NIL_POPEN(command.c_str(), "r");
    if (!pipe) return "";
    std::string out;
    char buffer[256];
    while (std::fgets(buffer, sizeof(buffer), pipe)) out += buffer;
    NIL_PCLOSE(pipe);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    return out;
}

std::string directory_of(const std::string& path) {
    const std::size_t cut = path.find_last_of("/\\");
    return (cut == std::string::npos) ? std::string(".") : path.substr(0, cut);
}

struct Provenance {
    std::string timestamp;   // ISO 8601, UTC
    std::string commit;      // short sha, or "unknown"
    std::string branch;
    bool dirty = false;
    std::string build;
    std::string compiler;
    std::string host;
    std::string run_id;
};

std::string compiler_string() {
#if defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
    return "Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
    return "unknown";
#endif
}

std::string host_string() {
    const char* name = std::getenv("COMPUTERNAME");   // Windows
    if (!name || !*name) name = std::getenv("HOSTNAME");  // most shells
    if (name && *name) return std::string(name);
    // Not every shell exports it; `hostname` exists on both platforms.
    const std::string probed = run_capture("hostname 2>" NIL_DEVNULL);
    return probed.empty() ? std::string("unknown") : probed;
}

std::string utc_timestamp(bool compact) {
    const std::time_t now = std::time(nullptr);
    std::tm tm {};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), compact ? "%Y%m%dT%H%M%SZ" : "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer);
}

// `anchor` is a path inside the working tree, so git answers about the right
// repository even when nil_bench is run from somewhere else.
Provenance gather_provenance(const std::string& anchor, const std::string& commit_override) {
    Provenance p;
    p.timestamp = utc_timestamp(false);
    p.build = NIL_BUILD_CONFIG;
    p.compiler = compiler_string();
    p.host = host_string();

    const std::string prefix = "git -C \"" + anchor + "\" ";
    const std::string suffix = " 2>" NIL_DEVNULL;

    p.commit = commit_override.empty() ? run_capture(prefix + "rev-parse --short HEAD" + suffix)
                                       : commit_override;
    if (p.commit.empty()) p.commit = "unknown";
    p.branch = run_capture(prefix + "rev-parse --abbrev-ref HEAD" + suffix);
    if (p.branch.empty()) p.branch = "unknown";
    p.dirty = !run_capture(prefix + "status --porcelain --untracked-files=no" + suffix).empty();

    p.run_id = utc_timestamp(true) + "-" + p.commit;
    return p;
}

// Minimal RFC 4180 quoting, so a --note containing a comma cannot shift every
// column to the right.
std::string csv_field(const std::string& value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) return value;
    std::string out = "\"";
    for (char ch : value) {
        if (ch == '"') out += '"';
        out += ch;
    }
    out += '"';
    return out;
}

bool file_is_empty(const std::string& path) {
    std::ifstream probe(path.c_str(), std::ios::binary | std::ios::ate);
    return !probe || probe.tellg() == std::streampos(0);
}

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

// Append one row per hand size plus a "all" total row, every row tagged with
// the same run_id so a spreadsheet can pivot on it.
void append_history(const std::string& path, const Provenance& prov, const std::string& corpus,
                    const std::map<int, Row>& totals, const std::map<int, int>& counts,
                    std::uint64_t all_nodes, double all_ms, int all_positions, int repeat,
                    const std::string& memo, const std::string& note) {
    const bool need_header = file_is_empty(path);
    std::ofstream out(path.c_str(), std::ios::app);
    if (!out) {
        std::cerr << "warning: cannot append to history file '" << path << "'\n";
        return;
    }
    if (need_header) {
        out << "run_id,timestamp_utc,commit,dirty,branch,corpus,cards,positions,nodes,ms,"
               "nodes_per_pos,ms_per_pos,repeat,memo,build,compiler,host,note\n";
    }

    const auto row = [&](const std::string& cards, int positions, std::uint64_t nodes, double ms) {
        const int divisor = positions ? positions : 1;
        out << csv_field(prov.run_id) << ',' << csv_field(prov.timestamp) << ','
            << csv_field(prov.commit) << ',' << (prov.dirty ? 1 : 0) << ','
            << csv_field(prov.branch) << ',' << csv_field(corpus) << ',' << csv_field(cards) << ','
            << positions << ',' << nodes << ',' << std::fixed << std::setprecision(1) << ms << ','
            << (nodes / static_cast<std::uint64_t>(divisor)) << ',' << std::setprecision(3)
            << (ms / divisor) << ',' << repeat << ',' << csv_field(memo) << ','
            << csv_field(prov.build) << ',' << csv_field(prov.compiler) << ','
            << csv_field(prov.host) << ',' << csv_field(note) << '\n';
    };

    for (const auto& kv : totals) {
        const auto count = counts.find(kv.first);
        row(std::to_string(kv.first), count == counts.end() ? 0 : count->second, kv.second.nodes,
            kv.second.ms);
    }
    row("all", all_positions, all_nodes, all_ms);
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
              << "  --mode full|fast|both   full (default) checks trick counts; fast\n"
              << "                    checks only nil_fails; both runs each position in\n"
              << "                    each mode and requires the two to agree\n"
              << "  --secondary max|min  tie-break direction for --random runs; corpus\n"
              << "                    entries carry their own\n"
              << "  --nil-already-set    likewise, for --random runs\n"
              << "  --check-pv        also require the recorded PV to match (see below)\n"
              << "  --check-moves     also score every legal card at each position and\n"
              << "                    require the list to agree with the position\n"
              << "  --csv <file>      write per-position rows for later comparison\n"
              << "  --baseline <file> compare against a csv written earlier\n"
              << "  --history <file>  APPEND a summary row to a running history csv\n"
              << "  --note <text>     free-text label for the history row\n"
              << "  --commit <sha>    override the commit git reports\n"
              << "  --slowest <n>     list the n slowest positions            [5]\n"
              << "  --tt-mb <n>       transposition table size in MiB         [32]\n"
              << "  --no-memo         no transposition table at all\n"
              << "  --no-collapse     generate every legal card rather than one per class\n"
              << "  --no-static       do not settle positions by proof (fast mode only)\n"
              << "                    of rank-equivalent ones (same answer, many more nodes)\n"
              << "  --no-presolve     do not bound full mode's root window with a fast\n"
              << "                    search (same answer, more nodes; full mode only)\n"
              << "  --no-narrow       do not narrow the window as moves come back\n"
              << "                    (same answer, many more nodes; full mode only)\n"
              << "  --no-ordering     try moves in canonical order rather than a\n"
              << "                    promising-first one (fast mode only)\n"
              << "  --tt-all-plies    consult the transposition table at every ply,\n"
              << "                    not only at a trick boundary (same answer,\n"
              << "                    and much slower)\n"
              << "  --no-last-trick   search the forced final trick instead of\n"
              << "                    evaluating it (same answer, more nodes)\n"
              << "  --tt-stats        also report transposition table behaviour\n"
              << "  --quiet           only print the summary and any failures\n"
              << "\n"
              << "--check-pv compares against the PV a corpus row recorded, so it means\n"
              << "something only in full mode; fast mode produces no principal variation.\n"
              << "It is off by default because most runs are not corpus runs.  Move\n"
              << "ordering does NOT retire it: ordering is confined to --mode fast, and\n"
              << "full mode does not reorder.  Full mode DOES cut, as of patch 22, but\n"
              << "cutting alone cannot move the PV -- every step of it is asked with a\n"
              << "window no value can reach, and only an exact entry can answer one.\n"
              << "That is what --no-narrow is here to keep honest.\n";
}

// Node counts are only comparable between runs that used the same table, so the
// size travels with them.  bench_history.py already groups runs on this column.
std::string memo_label(const nil::SearchOptions& opts) {
    // Turning the equivalent-card reduction off multiplies the node count, so it
    // has to land in a group of its own too or it reads as a huge regression.
    std::string suffix = opts.collapse_equivalents ? "" : "+nocollapse";
    // Same reasoning for the mode.  Fast mode answers a different question with
    // a different node count -- once it prunes, a much smaller one -- and a
    // fast row next to a full row in the history would read as a win that never
    // happened.
    if (opts.mode == nil::MODE_FAST) suffix += "+fast";
    // And again for the static bounds.  They only bite in fast mode, so the
    // suffix would be noise on a full row; there it is left off rather than
    // splitting the history into two groups that hold identical numbers.
    if (!opts.use_static_bounds && opts.mode == nil::MODE_FAST) suffix += "+nostatic";
    // And again for move ordering, on the same grounds and with the same
    // fast-mode guard: ordering is inert in full mode, so the suffix there
    // would split the history into two groups holding identical numbers.
    if (!opts.order_moves && opts.mode == nil::MODE_FAST) suffix += "+noordering";
    // Applies to both modes: the last trick is forced whatever question is
    // being asked about it, so both node counts move and neither history should
    // be merged with the other.
    if (!opts.last_trick_eval) suffix += "+nolasttrick";
    // Both modes again, and for the sharpest version of the same reason: the
    // control arm here moves node counts in BOTH directions depending on the
    // deal, so a history that merged the two groups would read as noise.
    if (!opts.tt_boundaries_only) suffix += "+ttallplies";
    if (!opts.narrow_window && opts.mode == nil::MODE_FULL) suffix += "+nonarrow";
    if (!opts.presolve_window && opts.mode == nil::MODE_FULL) suffix += "+nopresolve";
    if (!opts.canonical_pv && opts.mode == nil::MODE_FULL) suffix += "+ordered";
    if (!opts.use_memo || opts.tt_megabytes == 0) return "off" + suffix;
    return std::to_string(opts.tt_megabytes) + "mb" + suffix;
}

}  // namespace

int main(int argc, char** argv) {
    std::string corpus_path;
    std::string csv_path;
    std::string baseline_path;
    std::string history_path;
    std::string note;
    std::string commit_override;
    bool random_mode = false;
    bool quiet = false;
    bool check_pv = false;
    bool check_moves = false;
    bool tt_stats = false;
    // --mode both: solve every position in the other mode as well and require
    // the two to agree on nil_fails.
    bool cross_check_modes = false;
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
        } else if (arg == "--history" && has_next) {
            history_path = argv[++i];
        } else if (arg == "--note" && has_next) {
            note = argv[++i];
        } else if (arg == "--commit" && has_next) {
            commit_override = argv[++i];
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
        } else if (arg == "--tt-mb" && has_next) {
            const long long value = std::atoll(argv[++i]);
            opts.tt_megabytes = value > 0 ? static_cast<std::size_t>(value) : 0u;
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
        } else if (arg == "--no-narrow") {
            opts.narrow_window = false;
        } else if (arg == "--no-presolve") {
            opts.presolve_window = false;
        } else if (arg == "--no-canonical-pv") {
            opts.canonical_pv = false;
        } else if (arg == "--mode" && has_next) {
            const std::string mode = argv[++i];
            if (mode == "full") {
                opts.mode = nil::MODE_FULL;
            } else if (mode == "fast") {
                opts.mode = nil::MODE_FAST;
            } else if (mode == "both") {
                // The fast run is the one that gets timed and recorded; full
                // mode rides along as the thing that says the fast answer is
                // right.
                opts.mode = nil::MODE_FAST;
                cross_check_modes = true;
            } else {
                std::cerr << "error: --mode takes 'full', 'fast' or 'both'\n";
                return 2;
            }
        } else if (arg == "--secondary" && has_next) {
            const std::string mode = argv[++i];
            if (mode != "max" && mode != "min") {
                std::cerr << "error: --secondary takes 'max' or 'min'\n";
                return 2;
            }
            opts.minimise_own_tricks = (mode == "min");
        } else if (arg == "--nil-already-set") {
            opts.nil_already_set = true;
        } else if (arg == "--check-moves") {
            check_moves = true;
        } else if (arg == "--check-pv") {
            check_pv = true;
        } else if (arg == "--tt-stats") {
            tt_stats = true;
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
    if (check_pv && opts.mode == nil::MODE_FAST) {
        // Silently checking nothing is how a test starts passing for the wrong
        // reason, so say it out loud and carry on.
        std::cerr << "warning: --check-pv does nothing in fast mode, which produces no "
                     "principal variation; ignoring it\n";
        check_pv = false;
    }

    // ---- assemble the work list ------------------------------------------
    std::map<std::string, int> provenance_counts;
    struct Item {
        std::string name;
        nil::Position position;
        int nil_seat;
        bool forced;
        bool minimise_own;
        bool nil_already_set;
        int expected;
        int expected_side;
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
            ++provenance_counts[e.provenance];
        }
        for (const nil::CorpusEntry& e : entries) {
            if (cards_only && e.position.cards_per_hand() != cards_only) continue;
            items.push_back(Item{e.name, e.position, e.nil_seat, e.break_on_forced_spade_lead,
                                 e.minimise_own_tricks, e.nil_already_set, e.expected_tricks,
                                 e.expected_side_tricks, e.expected_pv, nil::corpus_repro(e)});
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
            items.push_back(Item{name.str(), pos, nil_seat, false, opts.minimise_own_tricks,
                                 opts.nil_already_set, -1, -1, "", ""});
        }
    }

    if (!quiet) {
        std::cout << (corpus_path.empty() ? "random" : corpus_path) << "   " << items.size()
                  << " positions, memo " << memo_label(opts) << ", best of "
                  << repeat << "\n\n";
    }

    // ---- run --------------------------------------------------------------
    std::vector<Row> rows;
    rows.reserve(items.size());
    int failures = 0;

    // --mode both bookkeeping.  Nodes are compared only over positions both
    // modes actually searched: fast mode answers a nil-already-set row without
    // looking at a single card, and counting those zeroes against full mode's
    // real work would report a speedup that is nothing of the kind.
    int mode_checked = 0;
    int moves_checked = 0;
    int mode_unsearched = 0;
    std::uint64_t cmp_full_nodes = 0;
    std::uint64_t cmp_fast_nodes = 0;

    // --tt-stats bookkeeping, summed over the runs that produced the timed
    // rows.  These are the numbers a table change has to be judged on, and
    // until patch 12 there was no way to see them from either tool: the work
    // that closed roadmap item 5 needed a throwaway program to read `partial`,
    // which is exactly the sort of measurement that should not need one.
    std::uint64_t tt_probes = 0;
    std::uint64_t tt_hits = 0;
    std::uint64_t tt_partial = 0;
    std::uint64_t tt_stores = 0;
    std::uint64_t tt_evictions = 0;

    for (const Item& item : items) {
        nil::Solution sol;
        bool solved = false;
        double best_ms = 0.0;
        for (int r = 0; r < repeat; ++r) {
            const Clock::time_point t0 = Clock::now();
            nil::SearchOptions run_opts = opts;
            run_opts.break_on_forced_spade_lead = item.forced;
            run_opts.minimise_own_tricks = item.minimise_own;
            run_opts.nil_already_set = item.nil_already_set;
            if (!nil::solve(item.position, item.nil_seat, run_opts, sol, err)) {
                std::cerr << "FAIL " << item.name << ": solve failed: " << err << "\n";
                ++failures;
                break;
            }
            const double ms =
                std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            if (r == 0 || ms < best_ms) best_ms = ms;
            solved = true;
        }

        if (opts.mode == nil::MODE_FAST) {
            // Fast mode computes no trick counts, so what a corpus row pins
            // here is the boolean those counts imply.  A row with no recorded
            // answer at all pins nothing, in either mode.
            if (solved && (item.expected >= 0 || item.nil_already_set)) {
                const bool want = item.nil_already_set || item.expected > 0;
                if (sol.nil_fails != want) {
                    std::cout << "FAIL " << item.name << ": expected nil_fails=" << (want ? 1 : 0)
                              << ", got " << (sol.nil_fails ? 1 : 0) << "\n  " << item.repro
                              << "\n";
                    ++failures;
                }
            }
        } else {
            if (item.expected >= 0 && sol.nil_tricks != item.expected) {
                std::cout << "FAIL " << item.name << ": expected " << item.expected
                          << " nil trick(s), got " << sol.nil_tricks << "\n  " << item.repro
                          << "\n";
                ++failures;
            }
            if (item.expected_side >= 0 && sol.nil_side_tricks != item.expected_side) {
                std::cout << "FAIL " << item.name << ": expected " << item.expected_side
                          << " side trick(s), got " << sol.nil_side_tricks << "\n  " << item.repro
                          << "\n";
                ++failures;
            }
        }

        if (cross_check_modes && solved) {
            nil::SearchOptions full_opts = opts;
            full_opts.mode = nil::MODE_FULL;
            full_opts.break_on_forced_spade_lead = item.forced;
            full_opts.minimise_own_tricks = item.minimise_own;
            full_opts.nil_already_set = item.nil_already_set;
            nil::Solution full;
            if (!nil::solve(item.position, item.nil_seat, full_opts, full, err)) {
                std::cout << "FAIL " << item.name << ": full-mode solve failed: " << err << "\n  "
                          << item.repro << "\n";
                ++failures;
            } else {
                ++mode_checked;
                if (full.nil_fails != sol.nil_fails) {
                    std::cout << "FAIL " << item.name << ": the modes disagree -- fast says "
                              << (sol.nil_fails ? "FAILS" : "MAKES") << ", full says "
                              << (full.nil_fails ? "FAILS" : "MAKES") << " (full mode has the nil "
                              << "bidder taking " << full.nil_tricks << ")\n  " << item.repro
                              << "\n";
                    ++failures;
                }
                if (item.nil_already_set) {
                    ++mode_unsearched;
                } else {
                    cmp_full_nodes += full.nodes;
                    cmp_fast_nodes += sol.nodes;
                }
            }
        }
        // The move list is a second reading of the same search, so what it owes
        // is consistency with the first: the same answer, the same principal
        // variation, a partition of the legal cards rather than a subset, and a
        // best row that scores what the position scores.  Cheap enough to run
        // over the whole corpus in either mode.
        if (check_moves && solved) {
            nil::SearchOptions move_opts = opts;
            move_opts.break_on_forced_spade_lead = item.forced;
            move_opts.minimise_own_tricks = item.minimise_own;
            move_opts.nil_already_set = item.nil_already_set;
            nil::Solution msol;
            std::vector<nil::MoveScore> scored;
            if (!nil::solve_moves(item.position, item.nil_seat, move_opts, msol, scored, err)) {
                std::cout << "FAIL " << item.name << ": solve_moves failed: " << err << "\n  "
                          << item.repro << "\n";
                ++failures;
            } else {
                ++moves_checked;
                if (msol.nil_fails != sol.nil_fails || msol.nil_tricks != sol.nil_tricks ||
                    msol.nil_side_tricks != sol.nil_side_tricks) {
                    std::cout << "FAIL " << item.name
                              << ": the move list disagrees with the position\n  " << item.repro
                              << "\n";
                    ++failures;
                }
                if (opts.mode == nil::MODE_FULL &&
                    nil::format_pv_compact(msol) != nil::format_pv_compact(sol)) {
                    std::cout << "FAIL " << item.name << ": the move list picks a different line\n"
                              << "    plain " << nil::format_pv_compact(sol) << "\n"
                              << "    moves " << nil::format_pv_compact(msol) << "\n  "
                              << item.repro << "\n";
                    ++failures;
                }

                const int seat =
                    (item.position.leader + item.position.trick_len) & 3;
                const nil::Hand legal = nil::legal_moves(
                    item.position.hands[seat], item.position.trick_len,
                    item.position.trick_len ? nil::card_suit(item.position.trick[0]) : -1,
                    item.position.spades_broken);
                nil::Hand covered = 0;
                bool overlap = false;
                bool any_best = false;
                for (const nil::MoveScore& m : scored) {
                    if (covered & m.equals) overlap = true;
                    covered |= m.equals;
                    if (m.is_best) any_best = true;
                }
                if (covered != legal || overlap) {
                    std::cout << "FAIL " << item.name
                              << ": the classes do not partition the legal cards\n    legal   "
                              << nil::hand_to_string(legal) << "\n    covered "
                              << nil::hand_to_string(covered) << (overlap ? "  (overlapping)" : "")
                              << "\n  " << item.repro << "\n";
                    ++failures;
                }
                if (!any_best && !scored.empty()) {
                    std::cout << "FAIL " << item.name << ": no card achieves the position's value\n  "
                              << item.repro << "\n";
                    ++failures;
                }
            }
        }

        if (check_pv && !item.expected_pv.empty()) {
            const std::string got = nil::format_pv_compact(sol);
            if (got != item.expected_pv) {
                std::cout << "FAIL " << item.name << ": PV differs\n    got      " << got
                          << "\n    expected " << item.expected_pv << "\n  " << item.repro << "\n";
                ++failures;
            }
        }

        tt_probes += sol.tt_probes;
        tt_hits += sol.tt_hits;
        tt_partial += sol.tt_partial;
        tt_stores += sol.tt_stores;
        tt_evictions += sol.tt_evictions;

        rows.push_back(Row{item.name, item.position.cards_per_hand(), sol.nil_tricks, sol.nodes,
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

    if (moves_checked) {
        std::cout << "\n  move list: " << moves_checked
                  << " position(s) scored card by card, each list agreeing with the "
                     "position it came from\n";
    }
    if (cross_check_modes) {
        std::cout << "\n  mode check: " << mode_checked << " position(s) solved both ways, "
                  << mode_unsearched << " answered without searching (nil already set)\n";
        const int searched = mode_checked - mode_unsearched;
        if (searched > 0) {
            const double ratio =
                cmp_fast_nodes ? static_cast<double>(cmp_full_nodes) /
                                     static_cast<double>(cmp_fast_nodes)
                               : 0.0;
            std::cout << "    nodes over the " << searched << " searched:  full "
                      << commas(cmp_full_nodes) << "  fast " << commas(cmp_fast_nodes) << "   "
                      << std::fixed << std::setprecision(2) << ratio << "x\n";
            if (cmp_full_nodes == cmp_fast_nodes) {
                // Before alpha-beta this was the expected result and the line
                // below said so.  Now it means the boolean search cut nothing
                // anywhere, which at any real hand size is a broken window
                // rather than a hard corpus.
                std::cout << "    (identical -- the boolean search cut nothing at all, which is\n"
                          << "     worth looking into unless every position here is trivial.)\n";
            } else {
                std::cout << "    (the two no longer walk the same tree, so this agreement is a\n"
                          << "     pruned answer held against an unpruned, oracle-checked one.)\n";
            }
        }
    }

    if (tt_stats) {
        const auto pct = [](std::uint64_t part, std::uint64_t whole) {
            std::ostringstream os;
            os << std::fixed << std::setprecision(1)
               << (whole ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0)
               << "%";
            return os.str();
        };
        std::cout << "\n  transposition table, memo " << memo_label(opts) << "\n"
                  << "    probes     " << commas(tt_probes) << "\n"
                  << "    hits       " << commas(tt_hits) << "   " << pct(tt_hits, tt_probes)
                  << " of probes\n"
                  << "    partial    " << commas(tt_partial) << "   " << pct(tt_partial, tt_probes)
                  << " of probes\n"
                  << "    stores     " << commas(tt_stores) << "\n"
                  << "    evictions  " << commas(tt_evictions) << "   "
                  << pct(tt_evictions, tt_stores) << " of stores\n";
        // `partial` is the load-bearing number here, not a curiosity.  It
        // counts probes that found the position and held a bound too weak to
        // settle the window -- the only nodes that could have a stored move AND
        // moves left to search.  It is zero by construction in both modes
        // today, which is what closed roadmap item 5; see ROADMAP.md.  A
        // non-zero value means some item has varied the window, and item 5
        // becomes live again at that moment.
        if (tt_partial == 0) {
            std::cout << "    (partial 0: every entry that matched settled the window it was\n"
                      << "     asked about, so no node ever holds a stored move and searches.)\n";
        } else {
            std::cout << "    (partial is NON-ZERO: the window now varies between nodes, so\n"
                      << "     table move ordering -- roadmap item 5 -- has a population again.)\n";
        }
        // An eviction rate near 100% says the table is far too small for the
        // depth being attempted, not that the replacement policy is wrong.
        if (tt_stores && tt_evictions * 10 > tt_stores * 9) {
            std::cout << "    (over 90% of stores displaced a live position: the table is\n"
                      << "     undersized for this workload -- try --tt-mb.)\n";
        }
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

    if (!history_path.empty()) {
        const std::string anchor_dir =
            corpus_path.empty() ? std::string(".") : directory_of(corpus_path);
        const Provenance prov = gather_provenance(anchor_dir, commit_override);
        const std::string label =
            corpus_path.empty()
                ? ("random-" + std::to_string(cards) + "c-seed" + std::to_string(seed))
                : corpus_path;
        append_history(history_path, prov, label, totals, counts, all_nodes, all_ms,
                       all_positions, repeat, memo_label(opts), note);
        if (!quiet) {
            std::cout << "\n  logged to " << history_path << "  [" << prov.commit
                      << (prov.dirty ? " DIRTY" : "") << " on " << prov.host << ", " << prov.build
                      << "]\n";
            if (prov.dirty) {
                std::cout << "  (working tree has uncommitted changes, so this row does not\n"
                          << "   correspond to a commit anyone else can reproduce)\n";
            }
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
        // Say where those expectations came from: a row this solver generated
        // agreeing with this solver is a regression check, not a proof.
        const auto oracle = provenance_counts.find("oracle");
        const auto solver = provenance_counts.find("solver");
        const auto unver = provenance_counts.find("unverified");
        std::cout << "  " << (oracle == provenance_counts.end() ? 0 : oracle->second)
                  << " independently verified by nil_oracle.py, "
                  << (solver == provenance_counts.end() ? 0 : solver->second)
                  << " pinned from this solver, "
                  << (unver == provenance_counts.end() ? 0 : unver->second)
                  << " timed only\n";
    }
    return 0;
}
