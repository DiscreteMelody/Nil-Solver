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
// the seat roles), so a corpus run exercises all of them; --secondary and
// --seats only apply to --random runs.
//
// MODES
// -----
// --mode full (default) is the lexicographic search: every corpus row's trick
// counts get checked.  --mode fast is the boolean nil search, which computes no
// trick counts, so only nils_set gets checked.  --mode both runs each position
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
#include <cctype>
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
#include "nil/seats.hpp"

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

// Deal `cards` to each seat, plus a random leader/nil/broken flag.  `pattern`
// is the role list to hang on the drawn nil seat -- see --seats below.
nil::Position random_position(Rng& rng, int cards, const nil::SeatRoles& pattern,
                              nil::SeatRoles& roles_out) {
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
    // Spades are broken only if one has been PLAYED, and validate() enforces
    // it: a layout still holding all thirteen has had no spade leave a hand.
    // A 13-card deal is always that layout, and a smaller one sometimes is --
    // 44 dealt cards can contain every spade.  Drawing the coin unconditionally
    // made those deals fail the solve outright while the summary line went on
    // dividing by the requested count, so a 13-card run reported a THIRD to a
    // HALF of the work it claimed to have done.
    //
    // The coin is drawn either way and then masked, so the deal stream does not
    // move: a repaired run generates the same deals as before and differs only
    // on the flag, which keeps it comparable with anything banked on deals that
    // already had it false.
    int spades_dealt = 0;
    for (int seat = 0; seat < 4; ++seat)
        spades_dealt += nil::count_cards(pos.hands[seat] & nil::suit_mask(nil::SUIT_SPADES));
    const bool broken_coin = (rng.below(2) != 0);
    pos.spades_broken = spades_dealt < 13 && broken_coin;
    // The draw stays exactly where it was in the stream, because moving it
    // would move every deal after it and orphan every node count banked on this
    // generator.
    const int nil_seat = static_cast<int>(rng.below(4));
    const int pattern_nil = pattern.nil_seat();
    for (int offset = 0; offset < 4; ++offset) {
        roles_out.role[(nil_seat + offset) & 3] = pattern.role[(pattern_nil + offset) & 3];
    }
    return pos;
}

void usage(const char* argv0) {
    std::cout << "usage: " << argv0 << " --corpus <file> [options]\n"
              << "       " << argv0 << " --random --cards <n> --count <n> [options]\n"
              << "\n"
              << "  --corpus <file>   corpus to verify and time\n"
              << "  --random          time random deals instead (no answers to check)\n"
              << "  --deals <file>    time a plain list of PBN deals, one per line, '#'\n"
              << "                    for comments (no answers to check).  Uses --seats\n"
              << "                    and --leader for every deal, so the file holds the\n"
              << "                    cards and the command line holds the question\n"
              << "  --leader <N|E|S|W>  leader for --deals runs                    [N]\n"
              << "  --cards <n>       cards per hand for --random           [6]\n"
              << "  --count <n>       positions for --random                [10]\n"
              << "  --seed <n>        seed for --random                     [1]\n"
              << "  --repeat <n>      time each position n times, keep the best [1]\n"
              << "  --cards-only <n>  restrict a corpus run to one hand size\n"
              << "  --no-memo         disable the full-state memo\n"
              << "  --mode full|fast|both   full (default) checks trick counts; fast\n"
              << "                    checks only nils_set; both runs each position in\n"
              << "                    each mode and requires the two to agree\n"
              << "  --secondary max|min  tie-break direction for --random runs; corpus\n"
              << "                    entries carry their own\n"
              << "  --seats <r r r r> role list for --random runs, clockwise from the\n"
              << "                    seat the generator draws as the nil bidder; 0 = nil,\n"
              << "                    1 = nil already broken, 2 = cover, 3 = opponent.\n"
              << "                    Corpus rows carry their own    [0 3 2 3]\n"
              << "  --check-pv        also require the recorded PV to match (see below)\n"
              << "  --check-moves     also score every legal card at each position and\n"
              << "                    require the list to agree with the position\n"
              << "  --csv <file>      write per-position rows for later comparison\n"
              << "  --baseline <file> compare against a csv written earlier\n"
              << "  --history <file>  APPEND a summary row to a running history csv\n"
              << "  --note <text>     free-text label for the history row\n"
              << "  --commit <sha>    override the commit git reports\n"
              << "  --slowest <n>     list the n slowest positions            [5]\n"
              << "  --tt-mb <n>       transposition table size in MiB        [256]\n"
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
              << "  --cover-duck-short  lead the cheapest duckable card in the nil\n"
                 "                    bidder's shortest suit (item C5, parked;\n"
                 "                    same answer, different node count)\n"
              << "  --no-settled-gains  do not take the static cutoff once every nil\n"
                 "                    bid is already down (opposed shapes only)\n"
              << "  --no-suit-mix     do not put one card per suit at the head of the\n"
              << "                    move list (same answer, more nodes)\n"
              << "  --no-later-tricks   do not tighten that bound with the opponents'\n"
              << "                    forced trump tricks (full mode only)\n"
              << "  --no-tt-narrow    do not take the cutoff threshold from a table\n"
              << "                    entry that matches but does not settle the\n"
              << "                    window (same answer, more nodes; full mode only)\n"
              << "  --no-target-bounds  do not answer a node from the reachable range\n"
              << "                    of the tricks left (same answer, more nodes;\n"
              << "                    full mode only)\n"
              << "  --tt-all-plies    consult the transposition table at every ply,\n"
              << "                    not only at a trick boundary (same answer,\n"
              << "                    and much slower)\n"
              << "  --no-last-trick   search the forced final trick instead of\n"
              << "                    evaluating it (same answer, more nodes)\n"
              << "  --tt-stats        also report transposition table behaviour\n"
              << "  --rank-stats      also report the winning-rank mask histogram\n"
              << "  --nilset-stats    also report forced-trick proof coverage\n"
              << "  --no-pv-shift     do not shift the principal-variation walk's\n"
              << "                    window by what the line has banked\n"
              << "  --no-opposed-reach  do not answer a one-bid-per-side node from the\n"
              << "                    ranks its broken-bid mask can still reach\n"
              << "  --opposed-stats   also report where a one-bid-per-side search\n"
              << "                    spends its nodes, and what fraction the\n"
              << "                    reachable-rank bound would answer (item 79)\n"
              << "  --spade-matrix    take the forced-trump floor from all four\n"
              << "                    hands rather than the top-spade hand alone\n"
              << "                    (same answer, fewer nodes, and SLOWER;\n"
              << "                    roadmap item 44, off by default)\n"
              << "  --no-quick-tricks   do not spend the opponents' can-cash\n"
              << "                    floor (same answer, more nodes; full only)\n"
              << "  --quick-tricks-stats  also report how often each later-tricks\n"
              << "                    arm's gate opens and how often it cuts\n"
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
    // Full mode only: the bound is inert in fast mode, so the suffix there
    // would split one history into two groups holding identical numbers.
    if (!opts.target_bounds && opts.mode == nil::MODE_FULL) suffix += "+notarget";
    if (!opts.later_tricks && opts.mode == nil::MODE_FULL) suffix += "+nolatertricks";
    if (opts.spade_matrix && opts.later_tricks && opts.mode == nil::MODE_FULL)
        suffix += "+spadematrix";
    if (!opts.quick_tricks && opts.later_tricks && opts.mode == nil::MODE_FULL)
        suffix += "+noquicktricks";
    if (!opts.tt_narrow_window && opts.mode == nil::MODE_FULL) suffix += "+nottnarrow";
    // Both modes: order_moves runs in full mode too, so full-mode rows move
    // under this arm as well and a fast-only suffix would silently merge two
    // different trees.
    if (!opts.suit_mixed_order) suffix += "+nosuitmix";
    if (opts.cover_duck_short) suffix += "+duckshort";
    if (!opts.narrow_window && opts.mode == nil::MODE_FULL) suffix += "+nonarrow";
    if (!opts.presolve_window && opts.mode == nil::MODE_FULL) suffix += "+nopresolve";
    if (!opts.canonical_pv && opts.mode == nil::MODE_FULL) suffix += "+ordered";
    if (!opts.use_memo || opts.tt_megabytes == 0) return "off" + suffix;
    // TT_AUTO is a sentinel, not a size.  Printing it raw put
    // "18446744073709551615mb" in the history file's memo column, which is the
    // column bench_history.py GROUPS on -- so every run that did not pass
    // --tt-mb landed in a bucket of its own name and could not be compared with
    // the identically-sized run next to it.  Resolve it to the size actually
    // used, which is what the column is for.
    const std::size_t mb =
        opts.tt_megabytes == nil::TT_AUTO ? nil::TT_DEFAULT_MEGABYTES : opts.tt_megabytes;
    return std::to_string(mb) + "mb" + suffix;
}

}  // namespace

int main(int argc, char** argv) {
    std::string corpus_path;
    std::string deals_path;
    // The raw --seats argument, kept because --deals re-anchors it onto each
    // deal's own PBN seat the way nil_cli does, where --random anchors on North.
    std::string seats_text = "0 3 2 3";
    int deals_leader = nil::SEAT_NORTH;
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
    bool rank_stats = false;
    bool nilset_stats = false;
    bool opposed_stats = false;
    bool quick_tricks_stats = false;
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
    // The role SHAPE for --random deals.  Re-anchored per deal onto the seat
    // the generator draws, so only the pattern here matters, not the seats.
    nil::SeatRoles random_roles;
    std::string err_text;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_next = (i + 1 < argc);
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (arg == "--corpus" && has_next) {
            corpus_path = argv[++i];
        } else if (arg == "--deals" && i + 1 < argc) {
            deals_path = argv[++i];
        } else if (arg == "--leader" && i + 1 < argc) {
            const std::string text = argv[++i];
            deals_leader = nil::parse_seat(text);
            if (deals_leader < 0) {
                std::cerr << "error: bad --leader '" << text << "'\n";
                return 2;
            }
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
        } else if (arg == "--no-target-bounds") {
            opts.target_bounds = false;
        } else if (arg == "--no-later-tricks") {
            opts.later_tricks = false;
        } else if (arg == "--no-tt-narrow") {
            opts.tt_narrow_window = false;
        } else if (arg == "--cover-duck-short") {
            opts.cover_duck_short = true;
        } else if (arg == "--no-settled-gains") {
            opts.settled_gains = false;
        } else if (arg == "--no-suit-mix") {
            opts.suit_mixed_order = false;
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
        } else if (arg == "--seats" && has_next) {
            std::string text = argv[++i];
            int digits = 0;
            for (char ch : text) {
                if (std::isdigit(static_cast<unsigned char>(ch))) ++digits;
            }
            while (i + 1 < argc && digits < 4) {
                const std::string next = argv[i + 1];
                // Digits, commas and spaces only, and at least one digit -- so
                // '0 3 2 3' quoted into one argument works as well as four
                // separate words, and the next --flag ends the list.
                bool all_role_chars = true;
                bool has_digit = false;
                for (char ch : next) {
                    if (std::isdigit(static_cast<unsigned char>(ch))) {
                        has_digit = true;
                    } else if (ch != ',' && !std::isspace(static_cast<unsigned char>(ch))) {
                        all_role_chars = false;
                    }
                }
                if (!all_role_chars || !has_digit) break;
                for (char ch : next) {
                    if (std::isdigit(static_cast<unsigned char>(ch))) ++digits;
                }
                text += ' ';
                text += next;
                ++i;
            }
            // Read against North, then re-anchored per deal onto whichever seat
            // the generator drew.  Only the SHAPE of the list matters here.
            seats_text = text;
            if (!nil::parse_seat_roles(text, nil::SEAT_NORTH, random_roles, err_text) ||
                !nil::validate_seat_roles(random_roles, err_text)) {
                std::cerr << "error: --seats: " << err_text << "\n";
                return 2;
            }
        } else if (arg == "--check-moves") {
            check_moves = true;
        } else if (arg == "--check-pv") {
            check_pv = true;
        } else if (arg == "--no-conjunction-presolve") {
            opts.conjunction_presolve = false;
        } else if (arg == "--no-pv-shift") {
            opts.pv_shift_window = false;
        } else if (arg == "--no-opposed-reach") {
            opts.opposed_reach = false;
        } else if (arg == "--opposed-stats") {
            opposed_stats = true;
            opts.track_opposed = true;
        } else if (arg == "--nilset-stats") {
            nilset_stats = true;
            opts.track_nilset = true;
        } else if (arg == "--spade-matrix") {
            opts.spade_matrix = true;
        } else if (arg == "--no-quick-tricks") {
            opts.quick_tricks = false;
        } else if (arg == "--quick-tricks-stats") {
            quick_tricks_stats = true;
            opts.track_quick_tricks = true;
        } else if (arg == "--rank-stats") {
            rank_stats = true;
            opts.track_rank_masks = true;
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

    if (corpus_path.empty() && deals_path.empty() && !random_mode) {
        std::cerr << "error: give either --corpus <file>, --deals <file> or --random\n";
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
        nil::SeatRoles roles;
        bool minimise_own;
        int expected_nils_set;
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
            items.push_back(Item{e.name, e.position, e.roles, e.minimise_own_tricks,
                                 e.expected_nils_set, e.expected_tricks,
                                 e.expected_side_tricks, e.expected_pv,
                                 nil::corpus_repro(e)});
        }
        if (items.empty()) {
            std::cerr << "error: corpus has no positions matching the filter\n";
            return 2;
        }
    } else if (!deals_path.empty()) {
        // A PLAIN LIST OF DEALS, one PBN per line.  `opposed13.txt` has been in
        // the repo since patch 76 and nothing could read it: it holds the cards
        // and nothing else, so `load_corpus` refuses it and every measurement
        // taken on it so far was a hand-rolled loop over nil_cli.  The roles and
        // the leader come from the command line, which is the whole difference
        // from a corpus -- there are no recorded answers to check, exactly as
        // with --random, so this times and does not verify.
        std::ifstream in(deals_path);
        if (!in) {
            std::cerr << "error: cannot open " << deals_path << "\n";
            return 2;
        }
        std::string line;
        int index = 0;
        while (std::getline(in, line)) {
            const std::size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos || line[start] == '#') continue;
            const std::size_t stop = line.find_last_not_of(" \t\r\n");
            const std::string pbn = line.substr(start, stop - start + 1);
            nil::Position pos;
            if (!nil::parse_pbn(pbn, pos.hands, err)) {
                std::cerr << "error: " << deals_path << ": " << err << "\n";
                return 2;
            }
            pos.leader = deals_leader;
            nil::SeatRoles roles;
            if (!nil::parse_seat_roles(seats_text, nil::pbn_anchor(pbn), roles, err) ||
                !nil::validate_seat_roles(roles, err)) {
                std::cerr << "error: --seats: " << err << "\n";
                return 2;
            }
            std::ostringstream name;
            name << "d" << pos.cards_per_hand() << "-" << std::setw(4) << std::setfill('0')
                 << index++;
            items.push_back(Item{name.str(), pos, roles, opts.minimise_own_tricks, -1, -1, -1,
                                 "", ""});
        }
        if (items.empty()) {
            std::cerr << "error: " << deals_path << " holds no deals\n";
            return 2;
        }
    } else {
        Rng rng(seed);
        for (int i = 0; i < count; ++i) {
            nil::SeatRoles roles;
            nil::Position pos = random_position(rng, cards, random_roles, roles);
            std::ostringstream name;
            name << "r" << cards << "-" << std::setw(4) << std::setfill('0') << i;
            items.push_back(Item{name.str(), pos, roles, opts.minimise_own_tricks,
                                 -1, -1, -1, "", ""});
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

    // The winning-rank histogram accumulates across every solve in the run, so
    // it starts from zero here rather than per position.
    if (rank_stats) nil::reset_rank_mask_stats();
    if (nilset_stats) nil::reset_nil_set_stats();
    if (opposed_stats) nil::reset_opposed_stats();
    if (quick_tricks_stats) nil::reset_quick_trick_stats();

    for (const Item& item : items) {
        nil::Solution sol;
        bool solved = false;
        double best_ms = 0.0;
        for (int r = 0; r < repeat; ++r) {
            const Clock::time_point t0 = Clock::now();
            nil::SearchOptions run_opts = opts;
            run_opts.minimise_own_tricks = item.minimise_own;
            if (!nil::solve(item.position, item.roles, run_opts, sol, err)) {
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
            if (solved && (item.expected_nils_set >= 0 || item.expected >= 0 ||
                           item.roles.nil_already_set())) {
                // Prefer the recorded count.  Falling back to (nil_tricks > 0)
                // keeps a row that predates the column checkable, but it is a
                // weaker claim: it cannot tell one broken bid from two.
                const int want = item.expected_nils_set >= 0
                                     ? item.expected_nils_set
                                     : ((item.roles.nil_already_set() || item.expected > 0) ? 1 : 0);
                if (sol.nils_set != want) {
                    std::cout << "FAIL " << item.name << ": expected nils_set=" << want
                              << ", got " << sol.nils_set << "\n  " << item.repro
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
            full_opts.minimise_own_tricks = item.minimise_own;
            nil::Solution full;
            if (!nil::solve(item.position, item.roles, full_opts, full, err)) {
                std::cout << "FAIL " << item.name << ": full-mode solve failed: " << err << "\n  "
                          << item.repro << "\n";
                ++failures;
            } else {
                ++mode_checked;
                if (full.nils_set != sol.nils_set) {
                    std::cout << "FAIL " << item.name << ": the modes disagree -- fast says "
                              << (sol.nils_set ? "FAILS" : "MAKES") << ", full says "
                              << (full.nils_set ? "FAILS" : "MAKES") << " (full mode has the nil "
                              << "bidder taking " << full.nil_tricks << ")\n  " << item.repro
                              << "\n";
                    ++failures;
                }
                if (item.roles.nil_already_set()) {
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
            move_opts.minimise_own_tricks = item.minimise_own;
            nil::Solution msol;
            std::vector<nil::MoveScore> scored;
            if (!nil::solve_moves(item.position, item.roles, move_opts, msol, scored, err)) {
                std::cout << "FAIL " << item.name << ": solve_moves failed: " << err << "\n  "
                          << item.repro << "\n";
                ++failures;
            } else {
                ++moves_checked;
                if (msol.nils_set != sol.nils_set || msol.nil_tricks != sol.nil_tricks ||
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

    if (opposed_stats) {
        const nil::OpposedStats& os = nil::opposed_stats();
        const double n = os.nodes ? double(os.nodes) : 1.0;
        std::cout << "\n  opposed node population and the reachable-rank ceiling\n"
                  << "    nodes counted            " << os.nodes << "\n";
        auto row = [&](const char* name, std::uint64_t pop, std::uint64_t fires) {
            std::cout << "    " << std::left << std::setw(24) << name << std::right
                      << std::setw(14) << pop
                      << "  " << std::setw(6) << std::fixed << std::setprecision(2)
                      << 100.0 * double(pop) / n << "%   would answer "
                      << std::setw(13) << fires << "  "
                      << std::setw(6) << (pop ? 100.0 * double(fires) / double(pop) : 0.0)
                      << "%\n";
        };
        row("both bids intact", os.state_intact, os.would_answer_intact);
        row("near bid down", os.state_near_down, os.would_answer_near_down);
        row("far bid down", os.state_far_down, os.would_answer_far_down);
        row("both bids down", os.state_both_down, os.would_answer_both_down);
        std::cout << "    " << std::left << std::setw(24) << "TOTAL would answer"
                  << std::right << std::setw(14) << os.would_answer << "  "
                  << std::setw(6) << 100.0 * double(os.would_answer) / n << "% of nodes\n"
                  << "    " << std::left << std::setw(24) << "PV walk, uncounted"
                  << std::right << std::setw(14) << os.pv_walk_nodes << "  "
                  << std::setw(6) << 100.0 * double(os.pv_walk_nodes) / n
                  << "% of the search\n";
        const double b = os.one_down_boundary ? double(os.one_down_boundary) : 1.0;
        std::cout << "\n  one bid live, at a trick boundary (item 81)\n"
                  << "    nodes                    " << std::setw(14) << os.one_down_boundary
                  << "  " << std::setw(6) << 100.0 * os.one_down_boundary / n << "% of nodes\n"
                  << "    proof: bid is doomed     " << std::setw(14)
                  << os.one_down_proof_doomed << "  " << std::setw(6)
                  << 100.0 * os.one_down_proof_doomed / b << "% of those\n"
                  << "    proof: bid is safe       " << std::setw(14)
                  << os.one_down_proof_safe << "  " << std::setw(6)
                  << 100.0 * os.one_down_proof_safe / b << "% of those\n"
                  << "    answered already         " << std::setw(14)
                  << os.one_down_answered_now << "\n"
                  << "    answered if rank PINNED  " << std::setw(14)
                  << os.one_down_answered_pinned << "  " << std::setw(6)
                  << 100.0 * double(os.one_down_answered_pinned) / n << "% of nodes\n";
        const double sb = os.settled_boundary ? double(os.settled_boundary) : 1.0;
        std::cout << "\n  every bid down, at a trick boundary (item 82)\n"
                  << "    nodes                    " << std::setw(14) << os.settled_boundary
                  << "  " << std::setw(6) << 100.0 * os.settled_boundary / n
                  << "% of nodes\n"
                  << "    no bound can help        " << std::setw(14) << os.settled_hopeless
                  << "  " << std::setw(6) << 100.0 * os.settled_hopeless / sb
                  << "% of those\n";
        for (int i = 0; i <= 6; ++i) {
            std::cout << "    needs " << i << (i == 6 ? "+ tricks proven  " : "  tricks proven  ")
                      << std::setw(14) << os.settled_need[i] << "  " << std::setw(6)
                      << 100.0 * double(os.settled_need[i]) / sb << "% of those\n";
        }
        std::cout << "    -- and of those, does the cheapest proof deliver it? --\n"
                  << "    top-spade run proves it " << std::setw(14)
                  << os.settled_spade_proved << "  " << std::setw(6)
                  << 100.0 * double(os.settled_spade_proved) / sb << "% of the region\n"
                  << "    right side, too few      " << std::setw(14)
                  << os.settled_spade_short << "  " << std::setw(6)
                  << 100.0 * double(os.settled_spade_short) / sb << "%\n"
                  << "    wrong side or no spades  " << std::setw(14)
                  << os.settled_spade_wrong_side << "  " << std::setw(6)
                  << 100.0 * double(os.settled_spade_wrong_side) / sb << "%\n";
        std::cout << "    -- stronger proofs (patch 87) --\n"
                  << "    forced spades, side-wide " << std::setw(14)
                  << os.settled_forced_proved << "  " << std::setw(6)
                  << 100.0 * double(os.settled_forced_proved) / sb << "% of the region\n"
                  << "    can-cash, side on lead   " << std::setw(14)
                  << os.settled_cash_proved << "  " << std::setw(6)
                  << 100.0 * double(os.settled_cash_proved) / sb << "%\n"
                  << "    EITHER                   " << std::setw(14)
                  << os.settled_either_proved << "  " << std::setw(6)
                  << 100.0 * double(os.settled_either_proved) / sb << "% of the region\n";
        std::cout << std::defaultfloat;
    }
    if (nilset_stats) {
        const nil::NilSetStats& ns = nil::nil_set_stats();
        const auto pc = [&](std::uint64_t part) {
            std::ostringstream os;
            os << std::fixed << std::setprecision(2)
               << (ns.boundaries ? 100.0 * static_cast<double>(part) /
                                       static_cast<double>(ns.boundaries)
                                 : 0.0)
               << "%";
            return os.str();
        };
        std::cout << "\n  forced-trick proof at trick boundaries (roadmap item 32)\n"
                  << "    boundaries with a nil spade  " << commas(ns.boundaries) << "\n"
                  << "    proof fires today            " << std::setw(12)
                  << commas(ns.proof_fires) << "   " << pc(ns.proof_fires) << "\n"
                  << "    adversarial ceiling adds     " << std::setw(12)
                  << commas(ns.ceiling_only) << "   " << pc(ns.ceiling_only) << "\n"
                  << "    neither                      " << std::setw(12) << commas(ns.neither)
                  << "   " << pc(ns.neither) << "\n";
    }

    if (quick_tricks_stats) {
        const nil::QuickTrickStats& qs = nil::quick_trick_stats();
        const auto pc = [&](std::uint64_t part) {
            std::ostringstream os;
            os << std::fixed << std::setprecision(2)
               << (qs.boundaries ? 100.0 * static_cast<double>(part) /
                                       static_cast<double>(qs.boundaries)
                                 : 0.0)
               << "%";
            return os.str();
        };
        const auto row = [&](const char* label, std::uint64_t part) {
            std::cout << "    " << std::left << std::setw(30) << label << std::right
                      << std::setw(14) << commas(part) << "   " << pc(part) << "\n";
        };
        std::cout << "\n  later-tricks arms at trick boundaries the reach bound left open"
                  << " (roadmap items 43, 44)\n"
                  << "    boundaries still open        " << std::setw(14)
                  << commas(qs.boundaries) << "\n";
        row("forced floor: gate opens", qs.gate_forced);
        row("forced floor: cuts", qs.fire_forced);
        row("can-cash: gate opens", qs.gate_cash);
        row("can-cash: cuts", qs.fire_cash);
    }

    if (rank_stats) {
        const nil::RankMaskStats& rk = nil::rank_mask_stats();
        const auto share = [&](std::uint64_t part) {
            std::ostringstream os;
            os << std::fixed << std::setprecision(1)
               << (rk.stores ? 100.0 * static_cast<double>(part) / static_cast<double>(rk.stores)
                             : 0.0)
               << "%";
            return os.str();
        };
        std::cout << "\n  winning-rank masks (roadmap item 31)\n"
                  << "    entries    " << commas(rk.stores) << "\n";
        if (rk.stores) {
            // `need` is the truncation level an entry requires: the largest
            // number of top slots it would have to pin in any one suit.  The
            // cumulative column is the one that decides the item -- it is the
            // fraction of the table a design pinning the top N slots per suit
            // could hold without lying.
            std::cout << "    need   entries        share    cumulative\n";
            std::uint64_t running = 0;
            for (int n = 0; n < nil::RankMaskStats::MAX_NEED; ++n) {
                if (!rk.by_need[n]) continue;
                running += rk.by_need[n];
                std::cout << "    " << std::setw(4) << n << "   " << std::setw(12)
                          << commas(rk.by_need[n]) << "   " << std::setw(6) << share(rk.by_need[n])
                          << "   " << std::setw(11) << share(running) << "\n";
            }
            std::cout << "    pinned slots " << commas(rk.pinned_slots) << " of "
                      << commas(rk.live_slots) << " live   "
                      << std::fixed << std::setprecision(1)
                      << (rk.live_slots ? 100.0 * static_cast<double>(rk.pinned_slots) /
                                              static_cast<double>(rk.live_slots)
                                        : 0.0)
                      << "%\n";
            // Per depth, because an item that only pays in the deep endgame and
            // an item that pays at the root are different items.
            bool any_depth = false;
            for (int c = 0; c < nil::RankMaskStats::MAX_CARDS; ++c) {
                std::uint64_t total = 0;
                for (int n = 0; n < nil::RankMaskStats::MAX_NEED; ++n) total += rk.by_need_at[c][n];
                if (!total) continue;
                if (!any_depth) {
                    std::cout << "    cards      entries     need<=2   need<=3   need<=4\n";
                    any_depth = true;
                }
                std::uint64_t c2 = 0, c3 = 0, c4 = 0;
                for (int n = 0; n <= 4 && n < nil::RankMaskStats::MAX_NEED; ++n) {
                    if (n <= 2) c2 += rk.by_need_at[c][n];
                    if (n <= 3) c3 += rk.by_need_at[c][n];
                    c4 += rk.by_need_at[c][n];
                }
                const auto p = [&](std::uint64_t part) {
                    std::ostringstream os;
                    os << std::fixed << std::setprecision(1)
                       << 100.0 * static_cast<double>(part) / static_cast<double>(total) << "%";
                    return os.str();
                };
                std::cout << "    " << std::setw(5) << c << "   " << std::setw(12)
                          << commas(total) << "   " << std::setw(7) << p(c2) << "   "
                          << std::setw(7) << p(c3) << "   " << std::setw(7) << p(c4) << "\n";
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
