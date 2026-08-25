#!/usr/bin/env python3
"""Recompute the rows of a corpus whose answers an objective change can move.

When the objective changes, most rows are untouched and a few are not.  Throwing
the whole corpus away and regenerating it would work, but it would also throw
away every position that has ever been checked by hand and replace it with a
fresh set nobody has looked at.  This instead keeps every position exactly where
it is and only recomputes the answers, with the oracle, for the rows that can
have moved -- and it reports the ones that actually did, so a change that was
supposed to affect three rows and turns out to affect three hundred is visible
rather than silent.

  tools/refresh_corpus.py --filter nilset=1,secondary=max
  tools/refresh_corpus.py --filter nilset=1,secondary=max --dry-run
  tools/refresh_corpus.py --corpus tests/corpus/large.txt --filter secondary=max

Rows whose provenance is not "oracle" are left alone unless --include-pinned is
given: recomputing a solver-pinned row with the oracle would silently upgrade it
to something it is not.
"""

from __future__ import annotations

import argparse
import os
import sys
from typing import Dict, List, Optional, Sequence

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import crosscheck  # noqa: E402

SEAT_CHARS = "NESW"
FIELDS = ("name", "pbn", "leader", "nil", "broken", "trick",
          "secondary", "nilset", "nil_tricks", "side_tricks", "pv", "provenance")


def parse_filter(text: str) -> Dict[str, str]:
    out = {}
    for clause in text.split(","):
        clause = clause.strip()
        if not clause:
            continue
        key, _, value = clause.partition("=")
        out[key.strip()] = value.strip()
    return out


def matches(record: Dict[str, str], wanted: Dict[str, str]) -> bool:
    return all(record.get(k, "") == v for k, v in wanted.items())


def recompute(oracle, record: Dict[str, str]):
    case = crosscheck.Case.__new__(crosscheck.Case)
    case.leader = SEAT_CHARS.index(record["leader"])
    case.nil_seat = SEAT_CHARS.index(record["nil"])
    case.spades_broken = record["broken"] == "1"
    case.secondary = record["secondary"]
    case.nil_already_set = record["nilset"] == "1"
    case.current_trick = tuple(
        oracle.card_from_str(t) for t in record["trick"].split() if t
    )
    case.position = oracle.Position.build(
        oracle.parse_pbn(record["pbn"]),
        leader=case.leader,
        spades_broken=case.spades_broken,
        current_trick=case.current_trick,
    )
    case.pbn = record["pbn"]
    case.trick_text = record["trick"]
    return crosscheck.run_oracle(oracle, case, use_memo=True)


def main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--corpus", default=os.path.join("tests", "corpus", "positions.txt"))
    p.add_argument("--oracle", default=None)
    p.add_argument("--filter", required=True,
                   help="comma separated field=value, e.g. 'nilset=1,secondary=max'")
    p.add_argument("--include-pinned", action="store_true",
                   help="also refresh rows whose provenance is not 'oracle'")
    p.add_argument("--dry-run", action="store_true", help="report, do not write")
    p.add_argument("--limit", type=int, default=0, help="stop after N rows")
    args = p.parse_args(argv)

    oracle, path = crosscheck.load_oracle(args.oracle)
    if oracle is None:
        print("refresh_corpus: nil_oracle.py not found (%s)" % path, file=sys.stderr)
        return 2

    wanted = parse_filter(args.filter)
    with open(args.corpus) as handle:
        lines = handle.read().split("\n")

    considered = 0
    changed = 0
    for index, raw in enumerate(lines):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = [f.strip() for f in line.split("|")]
        record = dict(zip(FIELDS, parts))
        record.setdefault("provenance", "oracle")
        if not matches(record, wanted):
            continue
        if record["provenance"] != "oracle" and not args.include_pinned:
            continue
        if args.limit and considered >= args.limit:
            break
        considered += 1

        nil_tricks, side_tricks, _opp, pv = recompute(oracle, record)
        moved = (str(nil_tricks) != record["nil_tricks"]
                 or str(side_tricks) != record["side_tricks"]
                 or pv != record["pv"])
        if not moved:
            continue
        changed += 1
        note = []
        if str(nil_tricks) != record["nil_tricks"]:
            note.append("nil %s->%d" % (record["nil_tricks"], nil_tricks))
        if str(side_tricks) != record["side_tricks"]:
            note.append("SIDE %s->%d" % (record["side_tricks"], side_tricks))
        if not note:
            note.append("pv only")
        print("  %-10s %s" % (record["name"], ", ".join(note)))

        parts[9] = str(nil_tricks)
        parts[10] = str(side_tricks)
        parts[11] = pv
        lines[index] = " | ".join(parts)

    print("\n%d row(s) matched the filter, %d changed" % (considered, changed))
    if args.dry_run:
        print("(dry run, nothing written)")
        return 0
    if changed:
        with open(args.corpus, "w") as handle:
            handle.write("\n".join(lines))
        print("wrote %s" % args.corpus)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
