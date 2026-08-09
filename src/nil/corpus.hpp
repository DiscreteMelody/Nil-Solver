// A corpus is a flat text file of positions with their known-correct answers,
// generated once by tools/make_corpus.py (which asks nil_oracle.py) and then
// committed.  It is the fast accuracy regression test: replaying it needs no
// Python and no oracle, so it can run on every build and in CI.
//
// FORMAT
// ------
// One record per line, fields separated by '|' and trimmed.  Blank lines and
// lines starting with '#' are ignored.
//
//   name | pbn | leader | nil | broken | forced | trick | tricks | pv
//
//   name    identifier for the record, e.g. "c5-0042"
//   pbn     PBN deal string
//   leader  N/E/S/W, seat that led the current trick
//   nil     N/E/S/W, seat that bid nil
//   broken  0 or 1, spades already broken
//   forced  0 or 1, break_on_forced_spade_lead convention
//   trick   cards already on the trick, e.g. "H4 HK"; may be empty
//   tricks  expected trick count for the nil bidder; "?" if unknown
//   pv      expected principal variation; may be empty
//
// The `tricks` field is the contract.  `pv` is informational and is only
// checked when explicitly asked for: any future move ordering will legitimately
// change which of several equal-valued cards the search picks, and that is not
// a bug.
#ifndef NIL_CORPUS_HPP
#define NIL_CORPUS_HPP

#include <string>
#include <vector>

#include "nil/position.hpp"

namespace nil {

struct CorpusEntry {
    std::string name;
    std::string pbn;        // as written in the file, for reproduction
    std::string trick_text; // as written in the file
    Position position;
    int nil_seat = 0;
    bool break_on_forced_spade_lead = false;
    int expected_tricks = -1;   // -1 when the file says "?"
    std::string expected_pv;    // empty when not recorded
};

bool load_corpus(const std::string& path, std::vector<CorpusEntry>& out, std::string& err);

// A nil_cli command line that reproduces one entry, for pasting into a shell.
std::string corpus_repro(const CorpusEntry& entry, const std::string& exe = "nil_cli");

}  // namespace nil

#endif  // NIL_CORPUS_HPP
