#!/usr/bin/env python3
"""Summarise bench-history.csv: what changed, when, and by how much.

Every `nil_bench --history` run appends rows to the history file.  This reads
them back and shows the trend, one line per run, with the change against the
previous run and against the first.

  tools/bench_history.py
  tools/bench_history.py --cards 6            # just the 6-card group
  tools/bench_history.py --last 10
  tools/bench_history.py --host DESKTOP-ABC   # one machine only

WHAT IS COMPARABLE WITH WHAT
----------------------------
Two different questions, two different answers.

*Nodes* are deterministic: the same positions searched the same way give the
same count on any machine, in any build, under any load.  So a node count is
comparable across machines and compilers -- but not across different position
sets.  Runs are therefore grouped into sections by what was actually searched
(corpus, hand size, position count, memo setting), and node deltas are only
computed inside a section.  A run over 400 positions and a run over 560 are two
different measurements, not a regression.

*Milliseconds* need all of that plus the same machine, build configuration,
compiler and repeat count.  Where that does not hold the time delta reads
`n/a` rather than a number that looks meaningful and is not.
"""

from __future__ import annotations

import argparse
import csv
import os
from typing import Dict, List, Optional, Sequence, Tuple

DEFAULT_HISTORY = "bench-history.csv"

# What must match for two node counts to mean the same thing.
NODE_KEYS = ("corpus", "cards", "positions", "memo")
# What must additionally match for two wall times to mean the same thing.
TIME_KEYS = NODE_KEYS + ("host", "build", "compiler", "repeat")

Row = Dict[str, str]


def commas(value: float) -> str:
    return "{:,}".format(int(value))


def delta(now: float, before: Optional[float]) -> str:
    if before is None or before <= 0:
        return "      -"
    return "{:+7.1f}%".format((now - before) / before * 100.0)


def key_of(row: Row, fields: Sequence[str]) -> Tuple:
    return tuple(row.get(field, "") for field in fields)


def load(path: str, cards: Optional[str], host: Optional[str]) -> List[Row]:
    with open(path, newline="") as handle:
        rows = list(csv.DictReader(handle))
    if cards:
        rows = [r for r in rows if r.get("cards") == cards]
    if host:
        rows = [r for r in rows if r.get("host") == host]
    rows.sort(key=lambda r: r.get("timestamp_utc", ""))
    return rows


def describe(key: Tuple) -> str:
    corpus, cards, positions, memo = key
    where = os.path.basename(corpus) if corpus else "?"
    scope = "all hand sizes" if cards == "all" else ("%s-card hands" % cards)
    return "%s, %s, %s positions, memo %s" % (where, scope, positions, memo)


def report_section(key: Tuple, rows: List[Row], last: int) -> None:
    print("\n%s" % describe(key))
    print("  commit    when                       nodes   vs prev  vs first"
          "          ms  vs prev  note")

    shown = rows[-last:] if last > 0 else rows
    baseline = rows[0]
    previous: Optional[Row] = None

    for row in shown:
        nodes = float(row["nodes"])
        ms = float(row["ms"])
        prev_nodes = float(previous["nodes"]) if previous else None
        base_nodes = None if row is baseline else float(baseline["nodes"])

        if previous is None:
            ms_delta = "      -"
        elif key_of(row, TIME_KEYS) == key_of(previous, TIME_KEYS):
            ms_delta = delta(ms, float(previous["ms"]))
        else:
            ms_delta = "    n/a"

        print(
            "  %-8s%s %s %13s  %s  %s %10.1f  %s  %s"
            % (
                row["commit"][:8],
                "*" if row.get("dirty") == "1" else " ",
                row["timestamp_utc"][:19].replace("T", " "),
                commas(nodes),
                delta(nodes, prev_nodes),
                delta(nodes, base_nodes),
                ms,
                ms_delta,
                row.get("note", ""),
            )
        )
        previous = row

    if len(rows) > 1:
        best = min(rows, key=lambda r: float(r["nodes"]))
        latest = float(rows[-1]["nodes"])
        first = float(baseline["nodes"])
        line = "  best %s at %s" % (commas(float(best["nodes"])), best["commit"][:8])
        if first > 0 and latest > 0:
            if latest <= first:
                line += "   |   latest visits %.2fx fewer nodes than the first run" % (
                    first / latest
                )
            else:
                line += "   |   latest visits %.2fx more nodes than the first run" % (
                    latest / first
                )
        print(line)


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--history", default=DEFAULT_HISTORY,
                        help="history csv [%s]" % DEFAULT_HISTORY)
    parser.add_argument("--cards", default="all",
                        help="hand size to report, 'all' for the totals row, "
                             "or 'every' for every group [all]")
    parser.add_argument("--host", default=None, help="restrict to one machine")
    parser.add_argument("--last", type=int, default=0,
                        help="show only the last N runs per section")
    args = parser.parse_args(argv)

    if not os.path.isfile(args.history):
        print("no history yet at %s" % args.history)
        print("create some with:")
        print("  nil_bench --corpus tests/corpus/positions.txt --history %s" % DEFAULT_HISTORY)
        return 0

    cards_filter = None if args.cards == "every" else args.cards
    rows = load(args.history, cards_filter, args.host)
    if not rows:
        print("no rows in %s for cards=%s%s"
              % (args.history, args.cards, " host=%s" % args.host if args.host else ""))
        return 0

    sections: Dict[Tuple, List[Row]] = {}
    for row in rows:
        sections.setdefault(key_of(row, NODE_KEYS), []).append(row)

    print("%s   %d run(s) in %d comparable group(s)" % (args.history, len(rows), len(sections)))

    # Most recently used group first: that is almost always the one you just ran.
    order = sorted(sections.items(), key=lambda kv: kv[1][-1]["timestamp_utc"], reverse=True)
    for key, section in order:
        report_section(key, section, args.last)

    if any(row.get("dirty") == "1" for row in rows):
        print("\n* run against a dirty working tree, so the row does not correspond to a")
        print("  commit anyone can check out.")

    hosts = sorted({row.get("host", "?") for row in rows})
    if len(hosts) > 1:
        print("\nrows span %d machines (%s); node counts still compare, wall times do not."
              % (len(hosts), ", ".join(hosts)))

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BrokenPipeError:  # piping into `head`
        pass
