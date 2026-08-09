#!/usr/bin/env python3
"""Browse and spot-check the corpus.

The corpus is 560 lines of pipe-separated text.  This makes it readable, lets
you filter it, and expands any single record into something you can check by
hand: the four hands laid out by suit, the settings in plain English, and the
principal variation trick by trick with the winner of each.

  tools/corpus_view.py                          summary of what is in there
  tools/corpus_view.py --list --cards 4         one line per record
  tools/corpus_view.py --show c4-0007           expand one record
  tools/corpus_view.py --random 5               five at random, to spot-check
  tools/corpus_view.py --simplest 10            the easiest ones to verify by hand
  tools/corpus_view.py --show c4-0007 --verify  recompute it with the oracle too

WHAT --show ACTUALLY PROVES
---------------------------
By default it re-runs nil_cli and compares against the recorded answer, so it
tells you the C++ solver still agrees with what the oracle said when the corpus
was generated.  That is a regression check, not an independent one: both numbers
trace back to the same source.

--verify re-runs nil_oracle.py live, which is a second implementation but still
a program.  The only genuinely independent check is you reading the hands and
the trick list and deciding whether the line makes sense.  --simplest exists to
give you the positions where that is actually feasible: fewest cards, no
mid-trick resumption, fewest suits in play.
"""

from __future__ import annotations

import argparse
import os
import random
import subprocess
import sys
from typing import Dict, List, Optional, Sequence

SEAT_CHARS = "NESW"
SUIT_CHARS = "SHDC"
DEFAULT_CORPUS = os.path.join("tests", "corpus", "positions.txt")

FIELDS = (
    "name", "pbn", "leader", "nil", "broken", "forced", "trick",
    "secondary", "nilset", "nil_tricks", "side_tricks", "pv", "provenance",
)

# How much a recorded answer is worth.  See tools/make_large_corpus.py.
PROVENANCE = {
    "oracle": "nil_oracle.py computed this independently",
    "solver": "pinned from this solver -- a regression baseline, not a proof",
    "unverified": "no answer recorded; this row exists only to be timed",
}


def load(path: str) -> List[Dict[str, str]]:
    records = []
    with open(path) as handle:
        for line_no, raw in enumerate(handle, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split("|")]
            if len(parts) < 11:
                raise SystemExit(
                    "%s:%d: expected at least 11 fields, got %d. If this corpus "
                    "predates the lexicographic objective, regenerate it with "
                    "tools/make_corpus.py" % (path, line_no, len(parts))
                )
            record = dict(zip(FIELDS, parts))
            if len(parts) < 12:
                record["pv"] = ""
            record.setdefault("provenance", "oracle")
            if not record["provenance"]:
                record["provenance"] = "oracle"
            record["cards"] = str(card_count(parts[1]))
            records.append(record)
    return records


def hands_by_seat(pbn: str) -> List[str]:
    """'N:A.5.J.2 8..97.9 ...' -> one readable line per seat."""
    first = SEAT_CHARS.index(pbn[0].upper())
    groups = pbn[2:].split()
    lines = [""] * 4
    for offset, group in enumerate(groups):
        seat = (first + offset) % 4
        suits = group.split(".")
        cells = " ".join(
            "%s:%s" % (SUIT_CHARS[i], suits[i] if i < len(suits) and suits[i] else "-")
            for i in range(4)
        )
        lines[seat] = "  %s  %s" % (SEAT_CHARS[seat], cells)
    return lines


def card_count(pbn: str) -> int:
    """Cards per hand at the start of the current trick.

    Take the maximum across the four hands, not the first one: in a position
    resumed mid-trick the seats that have already played hold one card fewer.
    """
    return max(
        len(group.replace(".", "").replace("-", ""))
        for group in pbn[2:].split()
    )


def suits_in_play(pbn: str) -> int:
    seen = set()
    for group in pbn[2:].split():
        for index, part in enumerate(group.split(".")):
            if part:
                seen.add(index)
    return len(seen)


def describe(record: Dict[str, str]) -> str:
    if record["nilset"] == "1":
        primary = "nil already set, so the primary is off"
    else:
        primary = "%s's tricks first" % record["nil"]
    secondary = ("each pair takes what it can" if record["secondary"] == "max"
                 else "each pair sheds what it can")
    return "%s, then %s" % (primary, secondary)


def repro(record: Dict[str, str], exe, nil_flag: str = "--nil") -> List[str]:
    args = list(exe) if isinstance(exe, (list, tuple)) else [exe]
    args += ["--pbn", record["pbn"], "--leader", record["leader"],
             nil_flag, record["nil"]]
    if record["broken"] == "1":
        args.append("--spades-broken")
    if record["forced"] == "1":
        args.append("--break-on-forced-lead")
    if record["secondary"] == "min":
        args += ["--secondary", "min"]
    if record["nilset"] == "1":
        args.append("--nil-already-set")
    if record["trick"]:
        args += ["--trick", record["trick"]]
    return args


def shell(args: Sequence[str]) -> str:
    return " ".join("'%s'" % a if " " in a else a for a in args)


def show(record: Dict[str, str], exe: Optional[str], oracle_path: Optional[str]) -> int:
    print("=" * 72)
    print("%s   %d cards per hand" % (record["name"], card_count(record["pbn"])))
    print("=" * 72)
    print("PBN            %s" % record["pbn"])
    for line in hands_by_seat(record["pbn"]):
        print(line)
    print("Leader         %s" % record["leader"])
    print("Nil bidder     %s" % record["nil"])
    print("Spades broken  %s" % ("yes" if record["broken"] == "1" else "no"))
    if record["forced"] == "1":
        print("Break rule     a forced spade lead breaks spades")
    if record["trick"]:
        print("On the trick   %s   (already played, in order from the leader)" % record["trick"])
    print("Objective      %s" % describe(record))
    print()
    provenance = record["provenance"]
    if provenance == "unverified":
        print("Recorded answer: none (%s)" % PROVENANCE[provenance])
    else:
        print("Recorded answer  [%s: %s]"
              % (provenance, PROVENANCE.get(provenance, "unknown provenance")))
        print("  %s takes %s trick(s); its side takes %s"
              % (record["nil"], record["nil_tricks"], record["side_tricks"]))
        if record["nilset"] != "1":
            print("  so the nil %s"
                  % ("FAILS" if int(record["nil_tricks"]) > 0 else "MAKES"))

    if record["pv"]:
        print()
        print("Line, trick by trick:")
        print_pv(record)

    print()
    print("Reproduce:")
    print("  %s" % shell(repro(record, exe or os.path.join("build", "bin", "nil_cli"))))
    if oracle_path:
        # The oracle names the seat --designated, since it does not know the
        # word "nil"; everything else is spelled the same.
        print("  %s" % shell(repro(record, ["python3", oracle_path], "--designated")))

    problems = 0
    if exe:
        problems += check_against(record, exe)
    return problems


def print_pv(record: Dict[str, str]) -> None:
    """Group the flat PV into tricks.  The seat labels come from the PV itself,
    so this needs no knowledge of the rules -- but it also cannot tell you who
    won.  Run nil_cli for that; it marks each winner."""
    plays = record["pv"].split()
    pre = record["trick"].split()
    if pre:
        leader = SEAT_CHARS.index(record["leader"])
        plays = ["%s:%s" % (SEAT_CHARS[(leader + i) % 4], c) for i, c in enumerate(pre)] + plays
    for index in range(0, len(plays), 4):
        chunk = plays[index:index + 4]
        print("  T%-2d %s" % (index // 4 + 1, "  ".join("%-6s" % p for p in chunk)))


def check_against(record: Dict[str, str], exe: str) -> int:
    """Re-run the C++ solver and compare with the recorded answer."""
    if record["provenance"] == "unverified":
        print("\n  nothing recorded to check against")
        return 0
    args = repro(record, exe) + ["--compact", "--force"]
    proc = subprocess.run(args, capture_output=True, text=True)
    if proc.returncode != 0:
        print("\n  nil_cli FAILED: %s" % proc.stderr.strip())
        return 1
    fields = {}
    for line in proc.stdout.splitlines():
        key, _, value = line.partition("=")
        fields[key.strip()] = value.strip()

    problems = []
    if fields.get("tricks") != record["nil_tricks"]:
        problems.append("nil tricks: solver %s, corpus %s"
                        % (fields.get("tricks"), record["nil_tricks"]))
    if fields.get("side_tricks") != record["side_tricks"]:
        problems.append("side tricks: solver %s, corpus %s"
                        % (fields.get("side_tricks"), record["side_tricks"]))
    if record["pv"] and fields.get("pv") != record["pv"]:
        problems.append("PV differs:\n    solver %s\n    corpus %s"
                        % (fields.get("pv"), record["pv"]))

    print()
    if problems:
        print("  SOLVER DISAGREES WITH THE CORPUS:")
        for problem in problems:
            print("    %s" % problem)
        return 1
    print("  solver agrees with the recorded answer")
    return 0


def verify_with_oracle(record: Dict[str, str], oracle_path: str) -> int:
    """Recompute from scratch with nil_oracle.py -- a second implementation."""
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    import crosscheck  # noqa: E402

    oracle, _ = crosscheck.load_oracle(oracle_path)
    if oracle is None:
        print("  oracle not found; skipping --verify")
        return 0

    case = crosscheck.Case.__new__(crosscheck.Case)
    case.leader = SEAT_CHARS.index(record["leader"])
    case.nil_seat = SEAT_CHARS.index(record["nil"])
    case.spades_broken = record["broken"] == "1"
    case.break_forced = record["forced"] == "1"
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

    nil_tricks, side_tricks, _opponents, pv = crosscheck.run_oracle(
        oracle, case, use_memo=True
    )
    problems = []
    if str(nil_tricks) != record["nil_tricks"]:
        problems.append("nil tricks: oracle %d, corpus %s" % (nil_tricks, record["nil_tricks"]))
    if str(side_tricks) != record["side_tricks"]:
        problems.append("side tricks: oracle %d, corpus %s" % (side_tricks, record["side_tricks"]))
    if record["pv"] and pv != record["pv"]:
        problems.append("PV differs:\n    oracle %s\n    corpus %s" % (pv, record["pv"]))

    if problems:
        print("  ORACLE DISAGREES WITH THE CORPUS:")
        for problem in problems:
            print("    %s" % problem)
        return 1
    print("  oracle recomputes the same answer from scratch")
    return 0


def summarise(records: List[Dict[str, str]]) -> None:
    print("%d positions\n" % len(records))
    by_cards: Dict[str, List[Dict[str, str]]] = {}
    for record in records:
        by_cards.setdefault(record["cards"], []).append(record)

    provenance = {}
    for record in records:
        provenance[record["provenance"]] = provenance.get(record["provenance"], 0) + 1
    print("  provenance: " + ", ".join("%d %s" % (v, k) for k, v in sorted(provenance.items())))
    for key in sorted(provenance):
        print("    %-11s %s" % (key, PROVENANCE.get(key, "")))
    print()
    print("  cards  count   nil makes  nil fails   already set   take   shed   mid-trick")
    for cards in sorted(by_cards):
        group = by_cards[cards]
        live = [r for r in group if r["nilset"] != "1" and r["nil_tricks"] != "?"]
        makes = sum(1 for r in live if int(r["nil_tricks"]) == 0)
        fails = len(live) - makes
        print("  %5s  %5d   %9d  %9d   %11d   %4d   %4d   %9d"
              % (cards, len(group), makes, fails,
                 sum(1 for r in group if r["nilset"] == "1"),
                 sum(1 for r in group if r["secondary"] == "max"),
                 sum(1 for r in group if r["secondary"] == "min"),
                 sum(1 for r in group if r["trick"])))
    print("\n(nil makes/fails counts exclude the already-set rows, where the")
    print(" primary objective was switched off and the question does not apply.)")


def one_line(record: Dict[str, str]) -> str:
    verdict = ("  -  " if record["nilset"] == "1" or record["nil_tricks"] == "?"
               else ("MAKES" if int(record["nil_tricks"]) == 0 else "FAILS"))
    return "%-10s %2sc  lead %s  nil %s  %-3s %s  %s  nil=%-2s side=%-2s %-10s %s" % (
        record["name"], record["cards"], record["leader"], record["nil"],
        record["secondary"], "set " if record["nilset"] == "1" else "live",
        verdict, record["nil_tricks"], record["side_tricks"], record["provenance"],
        ("resumed mid-trick" if record["trick"] else ""),
    )


def main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--corpus", default=DEFAULT_CORPUS, help="corpus file [%s]" % DEFAULT_CORPUS)
    p.add_argument("--exe", default=os.path.join("build", "bin", "nil_cli"),
                   help="nil_cli, used to re-check what --show displays")
    p.add_argument("--oracle", default=None, help="path to nil_oracle.py")
    p.add_argument("--show", nargs="+", metavar="NAME", help="expand these records")
    p.add_argument("--random", type=int, default=0, metavar="N",
                   help="expand N records chosen at random")
    p.add_argument("--simplest", type=int, default=0, metavar="N",
                   help="expand the N records easiest to check by hand")
    p.add_argument("--list", action="store_true", help="one line per record")
    p.add_argument("--verify", action="store_true",
                   help="also recompute with nil_oracle.py")
    p.add_argument("--seed", type=int, default=None, help="seed for --random")
    p.add_argument("--cards", default=None, help="filter by cards per hand")
    p.add_argument("--secondary", choices=("max", "min"), default=None)
    p.add_argument("--nilset", choices=("yes", "no"), default=None)
    p.add_argument("--outcome", choices=("makes", "fails"), default=None)
    p.add_argument("--midtrick", choices=("yes", "no"), default=None)
    p.add_argument("--provenance", choices=("oracle", "solver", "unverified"), default=None)
    p.add_argument("--limit", type=int, default=40, help="cap for --list [40]")
    args = p.parse_args(argv)

    if not os.path.isfile(args.corpus):
        print("no corpus at %s" % args.corpus, file=sys.stderr)
        return 2
    records = load(args.corpus)

    if args.cards:
        records = [r for r in records if r["cards"] == args.cards]
    if args.secondary:
        records = [r for r in records if r["secondary"] == args.secondary]
    if args.nilset:
        want = "1" if args.nilset == "yes" else "0"
        records = [r for r in records if r["nilset"] == want]
    if args.midtrick:
        records = [r for r in records if bool(r["trick"]) == (args.midtrick == "yes")]
    if args.provenance:
        records = [r for r in records if r["provenance"] == args.provenance]
    if args.outcome:
        records = [r for r in records
                   if r["nilset"] != "1" and r["nil_tricks"] != "?"
                   and (int(r["nil_tricks"]) == 0) == (args.outcome == "makes")]
    if not records:
        print("no records match those filters")
        return 0

    exe = args.exe if os.path.exists(args.exe) else None
    if args.show or args.random or args.simplest:
        if args.show:
            index = {r["name"]: r for r in records}
            chosen = []
            for name in args.show:
                if name not in index:
                    print("no record named %s (after filters)" % name, file=sys.stderr)
                    return 2
                chosen.append(index[name])
        elif args.simplest:
            # Fewest cards, no mid-trick resumption, fewest suits in play: the
            # ones a person can actually reason through.
            ranked = sorted(
                records,
                key=lambda r: (int(r["cards"]), bool(r["trick"]), suits_in_play(r["pbn"])),
            )
            chosen = ranked[: args.simplest]
        else:
            rng = random.Random(args.seed)
            chosen = rng.sample(records, min(args.random, len(records)))

        problems = 0
        for record in chosen:
            problems += show(record, exe, args.oracle)
            if args.verify:
                problems += verify_with_oracle(record, args.oracle)
            print()
        if exe is None:
            print("(%s not found, so nothing was re-checked; build first or pass --exe)"
                  % args.exe)
        return 1 if problems else 0

    if args.list:
        for record in records[: args.limit]:
            print(one_line(record))
        if len(records) > args.limit:
            print("... %d more (raise --limit)" % (len(records) - args.limit))
        return 0

    summarise(records)
    print("\nExpand one with:  tools/corpus_view.py --show %s" % records[0]["name"])
    print("Or spot-check at random:  tools/corpus_view.py --random 5")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
