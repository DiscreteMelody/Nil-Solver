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
//   name | pbn | leader | seats | broken | trick | secondary | nils_set |
//   nil_tricks | side_tricks | pv | provenance
//
//   name        identifier for the record, e.g. "c5-0042"
//   pbn         PBN deal string
//   leader      N/E/S/W, seat that led the current trick
//   seats       four role digits, e.g. "0 3 2 3", running CLOCKWISE FROM THE
//               SEAT THE PBN NAMES -- the same order the hands are in.  0 is a
//               nil bidder that has not taken a trick, 1 a nil already broken,
//               2 the partner covering it, 3 a seat on a side with no nil.
//               This replaced two columns, `nil` and `nilset`, in patch 54; see
//               nil/seats.hpp for why.
//   broken      0 or 1, spades already broken.  A layout still holding all
//               thirteen spades cannot have them broken -- see validate() --
//               so this is 0 on every full 13-card deal.
//   trick       cards already on the trick, e.g. "H4 HK"; may be empty
//   secondary   "max" or "min", the tie-break direction
//   nils_set    how many bids are down under optimal play: 0 or 1 with a
//               single nil, 0..2 for a pair that both bid.  A bid the roles
//               declare already broken counts toward it.  "?" if unknown
//   nil_tricks  tricks taken by SEATS HOLDING A NIL BID -- the bidder's own
//               count with one bidder, the pair's total with two, where it
//               equals side_tricks because there is no cover to add.  "?" if
//               unknown
//
//   PER-SEAT COUNTS ARE NOT RECORDED, and that is not an omission.  An answer
//   column may only hold what the OBJECTIVE pins, and nothing in it constrains
//   how the opponents split tricks between themselves, or how a pair that has
//   already lost both bids splits its own.  Re-searching the two-nil corpus
//   with the opposite tie-break moved no value and moved the per-seat counts on
//   43 rows of 140.
//   side_tricks expected trick count for the nil bidder's pair; "?" if unknown
//   pv          expected principal variation; may be empty
//   provenance  optional; where the expected answers came from.  "oracle" (the
//               default when the column is absent) means nil_oracle.py computed
//               them independently.  "solver" means they were recorded from
//               this solver -- a regression baseline, not a proof.  "unverified"
//               means no answer is recorded at all and the row exists only to
//               be timed.
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
#include "nil/seats.hpp"

namespace nil {

struct CorpusEntry {
    std::string name;
    std::string pbn;        // as written in the file, for reproduction
    std::string trick_text; // as written in the file
    Position position;
    SeatRoles roles;
    bool minimise_own_tricks = false;
    // -1 means "not recorded", matching expected_tricks below.
    int expected_nils_set = -1;
    int expected_tricks = -1;        // -1 when the file says "?"
    int expected_side_tricks = -1;   // -1 when the file says "?"
    std::string expected_pv;         // empty when not recorded
    std::string provenance = "oracle";
};

bool load_corpus(const std::string& path, std::vector<CorpusEntry>& out, std::string& err);

// A nil_cli command line that reproduces one entry, for pasting into a shell.
std::string corpus_repro(const CorpusEntry& entry, const std::string& exe = "nil_cli");

}  // namespace nil

#endif  // NIL_CORPUS_HPP
