#!/usr/bin/env python3
"""Differential test: the C++ nil solver against nil_oracle.py.

The oracle is the source of truth.  For every random position we compare three
things, in increasing order of strictness:

  1. the boolean answer (can the nil bidder be forced to take a trick),
  2. the exact trick count, and
  3. the principal variation, card for card.

(3) is the one that earns its keep.  Two implementations can agree on every
value while disagreeing about a rule that happens not to matter in the sampled
positions; they cannot agree on the PV by accident, because both sides break
ties the same way (canonically lowest card, strict improvement only).

SEAT PARITY
-----------
nil_oracle.py fixes its coalitions by seat parity: N and S always minimise the
designated player's tricks, E and W always maximise.  That is exactly the nil
question when the designated player sits N or S, and something else entirely
when it sits E or W.  The C++ side instead ties the coalitions to the nil
bidder's own seat.  To compare the two we rotate every deal so the nil bidder
sits North before handing it to the oracle, then rotate the PV's seat labels
back.  Rotation is a pure relabelling -- it moves no cards between hands and
changes no rule -- so the answer must be identical.

usage:
  tools/crosscheck.py --exe build/bin/nil_cli --cases 200 --cards 4
  tools/crosscheck.py --exe build/bin/nil_cli --cases 40 --cards 4-6 --trick-prob 0.5
  tools/crosscheck.py --exe build/bin/nil_cli --pbn 'N:A...2 K...3 Q...4 J...5' \
                      --leader N --nil N
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import random
import subprocess
import sys
from typing import List, Optional, Sequence, Tuple

SEAT_CHARS = "NESW"

ORACLE_CANDIDATES = (
    "nil_oracle.py",
    os.path.join("tools", "nil_oracle.py"),
    os.path.join("..", "nil_oracle.py"),
)


def load_oracle(explicit: Optional[str]):
    """Import nil_oracle.py from an explicit path or the usual places."""
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    candidates: List[str] = []
    if explicit:
        candidates.append(explicit)
    else:
        for name in ORACLE_CANDIDATES:
            candidates.append(os.path.join(root, name))
            candidates.append(os.path.join(os.getcwd(), name))

    for path in candidates:
        if os.path.isfile(path):
            spec = importlib.util.spec_from_file_location("nil_oracle", path)
            module = importlib.util.module_from_spec(spec)
            assert spec.loader is not None
            # Register before executing: @dataclass resolves annotations via
            # sys.modules and blows up otherwise.
            sys.modules["nil_oracle"] = module
            spec.loader.exec_module(module)
            return module, path
    return None, candidates[0]


# ---------------------------------------------------------------------------
# Fixture generation
# ---------------------------------------------------------------------------


class Case:
    def __init__(self, oracle, rng: random.Random, cards: int, trick_prob: float):
        deck = list(oracle.FULL_DECK)
        dealt = rng.sample(deck, 4 * cards)
        hands = [
            list(sorted(dealt[i * cards : (i + 1) * cards])) for i in range(4)
        ]

        self.leader = rng.randrange(4)
        self.nil_seat = rng.randrange(4)
        self.spades_broken = bool(rng.getrandbits(1))
        self.break_forced = bool(rng.getrandbits(1))
        self.secondary = "min" if rng.getrandbits(1) else "max"
        # Weighted: the live-nil case is the one that matters most, but the
        # already-set case has to be exercised too or it will rot.
        self.nil_already_set = rng.random() < 0.25

        trick: List = []
        broken = self.spades_broken
        if cards > 1 and rng.random() < trick_prob:
            for offset in range(rng.randrange(1, 4)):
                seat = (self.leader + offset) % 4
                legal = oracle.legal_moves(
                    tuple(sorted(hands[seat])), tuple(trick), broken
                )
                card = rng.choice(legal)
                broken = oracle.spades_broken_after(
                    broken, tuple(trick), card, self.break_forced
                )
                hands[seat].remove(card)
                trick.append(card)

        self.spades_broken = broken
        self.current_trick = tuple(trick)
        self.position = oracle.Position.build(
            hands,
            leader=self.leader,
            spades_broken=self.spades_broken,
            current_trick=self.current_trick,
        )
        self.pbn = self.position.to_pbn()
        self.trick_text = " ".join(str(c) for c in self.current_trick)

    def cli_args(self, exe: str, extra: Sequence[str] = ()) -> List[str]:
        args = [
            exe,
            "--pbn",
            self.pbn,
            "--leader",
            SEAT_CHARS[self.leader],
            "--nil",
            SEAT_CHARS[self.nil_seat],
            "--compact",
        ]
        if self.spades_broken:
            args.append("--spades-broken")
        if self.break_forced:
            args.append("--break-on-forced-lead")
        if self.secondary == "min":
            args += ["--secondary", "min"]
        if self.nil_already_set:
            args.append("--nil-already-set")
        if self.trick_text:
            args += ["--trick", self.trick_text]
        args += list(extra)
        return args

    def repro(self, exe: str) -> str:
        parts = []
        for a in self.cli_args(exe):
            parts.append("'" + a + "'" if " " in a else a)
        return " ".join(parts)


# ---------------------------------------------------------------------------
# The two solvers
# ---------------------------------------------------------------------------


def run_oracle(oracle, case: Case, use_memo: bool) -> Tuple[int, int, int, str]:
    """Solve with nil_oracle.py, rotating so the nil bidder sits North."""
    k = (4 - case.nil_seat) % 4
    pos = case.position
    hands = [None] * 4
    for seat in range(4):
        hands[(seat + k) % 4] = pos.hands[seat]
    rotated = oracle.Position.build(
        hands,
        leader=(pos.leader + k) % 4,
        spades_broken=pos.spades_broken,
        current_trick=pos.current_trick,
    )

    solution = oracle.solve(
        rotated,
        designated=0,  # the nil bidder, now North, so N/S minimise as required
        break_on_forced_spade_lead=case.break_forced,
        use_memo=use_memo,
        secondary=case.secondary,
        nil_already_set=case.nil_already_set,
    )
    pv = " ".join(
        "%s:%s" % (SEAT_CHARS[(seat - k) % 4], card) for seat, card in solution.pv
    )
    return solution.tricks, solution.side_tricks, solution.opponent_tricks, pv


def run_cpp(exe: str, case: Case, extra: Sequence[str] = ()) -> Tuple[int, int, int, int, str]:
    proc = subprocess.run(
        case.cli_args(exe, extra), capture_output=True, text=True
    )
    if proc.returncode != 0:
        raise RuntimeError(
            "nil_cli exited %d\n  stderr: %s" % (proc.returncode, proc.stderr.strip())
        )
    fields = {}
    for line in proc.stdout.splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            fields[key.strip()] = value.strip()
    try:
        return (
            int(fields["tricks"]),
            int(fields["side_tricks"]),
            int(fields["opponent_tricks"]),
            int(fields["nil_fails"]),
            fields["pv"],
        )
    except KeyError as exc:
        raise RuntimeError("unparsable nil_cli output: %r" % proc.stdout) from exc


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------


def parse_card_range(text: str) -> Tuple[int, int]:
    if "-" in text:
        lo, _, hi = text.partition("-")
        return int(lo), int(hi)
    n = int(text)
    return n, n


def compare(case: Case, oracle, cpp) -> List[str]:
    o_nil, o_side, o_opp, o_pv = oracle
    c_nil, c_side, c_opp, c_fails, c_pv = cpp
    problems = []
    if c_nil != o_nil:
        problems.append("nil tricks: c++ %d, oracle %d" % (c_nil, o_nil))
    if c_side != o_side:
        problems.append("nil-side tricks: c++ %d, oracle %d" % (c_side, o_side))
    if c_opp != o_opp:
        problems.append("opponent tricks: c++ %d, oracle %d" % (c_opp, o_opp))
    # nil_fails is asserted rather than computed once the nil is already set.
    want_fails = 1 if (case.nil_already_set or o_nil > 0) else 0
    if c_fails != want_fails:
        problems.append("nil verdict: c++ %d, expected %d" % (c_fails, want_fails))
    if c_side + c_opp != case.position.tricks_remaining:
        problems.append(
            "sides do not sum to %d: %d + %d"
            % (case.position.tricks_remaining, c_side, c_opp)
        )
    if c_pv != o_pv:
        problems.append("PV:\n    c++    %s\n    oracle %s" % (c_pv, o_pv))
    return problems


def main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--exe", default=os.path.join("build", "bin", "nil_cli"),
                   help="path to nil_cli [build/bin/nil_cli]")
    p.add_argument("--oracle", default=None,
                   help="path to nil_oracle.py (searched for if omitted)")
    p.add_argument("--cases", type=int, default=200, help="random positions to try [200]")
    p.add_argument("--cards", default="4", help="cards per hand, e.g. '4' or '4-6' [4]")
    p.add_argument("--seed", type=int, default=1, help="RNG seed [1]")
    p.add_argument("--trick-prob", type=float, default=0.35,
                   help="chance of resuming mid-trick [0.35]")
    p.add_argument("--no-memo", action="store_true",
                   help="run the C++ side without its memo as well")
    p.add_argument("--oracle-no-memo", action="store_true",
                   help="run the oracle with no memo (much slower)")
    p.add_argument("--stop-after", type=int, default=5,
                   help="give up after this many mismatches [5]")
    p.add_argument("--quiet", action="store_true", help="only report problems")
    p.add_argument("--pbn", default=None, help="check one specific deal instead")
    p.add_argument("--leader", default="N")
    p.add_argument("--nil", default="N")
    p.add_argument("--trick", default="")
    p.add_argument("--spades-broken", action="store_true")
    p.add_argument("--break-on-forced-lead", action="store_true")
    p.add_argument("--secondary", choices=("max", "min"), default="max")
    p.add_argument("--nil-already-set", action="store_true")
    args = p.parse_args(argv)

    oracle, path = load_oracle(args.oracle)
    if oracle is None:
        print("crosscheck: nil_oracle.py not found (looked for %s); skipping." % path)
        print("            pass --oracle /path/to/nil_oracle.py to run it.")
        return 0
    if not args.quiet:
        print("oracle: %s" % path)
        print("solver: %s" % args.exe)

    if not os.path.exists(args.exe):
        print("crosscheck: %s does not exist; build first." % args.exe, file=sys.stderr)
        return 2

    cases: List[Case] = []
    if args.pbn:
        case = Case.__new__(Case)
        case.leader = SEAT_CHARS.index(args.leader.upper()[0])
        case.nil_seat = SEAT_CHARS.index(args.nil.upper()[0])
        case.spades_broken = args.spades_broken
        case.break_forced = args.break_on_forced_lead
        case.secondary = args.secondary
        case.nil_already_set = args.nil_already_set
        case.current_trick = tuple(
            oracle.card_from_str(t) for t in args.trick.replace(",", " ").split() if t
        )
        case.position = oracle.Position.build(
            oracle.parse_pbn(args.pbn),
            leader=case.leader,
            spades_broken=case.spades_broken,
            current_trick=case.current_trick,
        )
        case.pbn = case.position.to_pbn()
        case.trick_text = " ".join(str(c) for c in case.current_trick)
        cases.append(case)
    else:
        lo, hi = parse_card_range(args.cards)
        rng = random.Random(args.seed)
        for _ in range(args.cases):
            cases.append(Case(oracle, rng, rng.randint(lo, hi), args.trick_prob))

    mismatches = 0
    for index, case in enumerate(cases):
        oracle_answer = run_oracle(oracle, case, not args.oracle_no_memo)

        runs = [("", run_cpp(args.exe, case))]
        if args.no_memo:
            runs.append(("--no-memo", run_cpp(args.exe, case, ["--no-memo"])))

        problems: List[str] = []
        for label, cpp in runs:
            for problem in compare(case, oracle_answer, cpp):
                problems.append((label + " " + problem).strip())

        if problems:
            mismatches += 1
            print("\nMISMATCH #%d (case %d)" % (mismatches, index))
            print("  " + case.repro(args.exe))
            for problem in problems:
                print("  " + problem)
            if mismatches >= args.stop_after:
                print("\nstopping after %d mismatches" % mismatches)
                return 1
        elif not args.quiet and (index + 1) % 25 == 0:
            print("  %d/%d ok" % (index + 1, len(cases)))

    if mismatches:
        print("\n%d/%d positions disagree." % (mismatches, len(cases)))
        return 1
    if not args.quiet:
        print("\nAll %d positions agree (value and principal variation)." % len(cases))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
