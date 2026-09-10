#!/usr/bin/env python3
"""Does the C++ port of the cooperative reachability probe (item 2b) agree
with the oracle's solve_cooperative (patch 90)?

Three things are checked, and they are not the same check:

  * THE ANSWER.  For every sampled deal, protecting both bidders together and
    each bidder alone, the C++ CLI's `cooperative=` must equal the oracle's
    `.reachable`.  This is the crosscheck the port exists to pass.

  * COLLAPSING RANK-EQUIVALENT MOVES CHANGES NO ANSWER.  `--collapse` is on
    by default; re-running the same query under `--no-collapse` must reach
    the same verdict.  cooperative.hpp argues this from the rules alone, the
    same way rules.hpp argues it for the minimax search -- this is that
    argument checked against code rather than trusted from precedent.

  * THE MEMO CHANGES NO ANSWER.  Same idea, `--no-memo` against the default.

POPULATION IS ASSERTED TOO, and in the specific sense patch 90 already found
matters: the cell where NEITHER bidder can force the trade (both
`--conjunction` calls answer false) is the one T's `(make, make)` rule reads,
and patch 90 measured it 55-85% unreachable at 4-7 cards -- close enough to a
coin flip that a run seeing only one answer would not be exercising the
question this probe exists to settle.  Sampling is biased toward that cell
(found via the ALREADY-SHIPPED, fast C++ `--conjunction` probe rather than by
calling the slower oracle machinery a second time) so a modest case count
still reaches both branches; the run fails if it does not.
"""
import argparse
import importlib.util
import itertools
import pathlib
import random
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("nil_oracle", ROOT / "nil_oracle.py")
oracle = importlib.util.module_from_spec(spec)
sys.modules["nil_oracle"] = oracle
spec.loader.exec_module(oracle)

CLI = str(ROOT / "build/bin/nil_cli")
SEATS = "NESW"
RANKS = "23456789TJQKA"

# Every one-bid-per-pair arrangement, in all four rotations and BOTH partner
# leans matching as well as opposing -- unlike conjunction_crosscheck.py and
# cooperative_crosscheck.py, which restrict themselves to the strictly
# opposed (opposite-lean) half.  This probe reads no role but the one it is
# asked to protect, so same-lean (2/2, 3/3) is as answerable as opposite-lean
# (2/3, 3/2) for THIS question -- oracle.role_shape agrees, classifying all
# four as SHAPE_OPPOSING_NILS -- even though solve() itself still refuses
# same-lean outright.  Testing exactly the cells solve() cannot reach is the
# point: those are what item 60 needs this probe FOR.
SHAPES = []
for _first in range(4):
    for _a, _b in itertools.product((2, 3), repeat=2):
        _roles = [None] * 4
        _roles[_first] = 0
        _roles[(_first + 1) % 4] = 0
        _roles[(_first + 2) % 4] = _a
        _roles[(_first + 3) % 4] = _b
        try:
            if oracle.role_shape(_roles) == oracle.SHAPE_OPPOSING_NILS:
                SHAPES.append(tuple(_roles))
        except ValueError:
            pass
SHAPES = sorted(set(SHAPES))


def random_deal(rng, cards):
    deck = [(s, r) for s in range(4) for r in RANKS]
    pick = rng.sample(deck, 4 * cards)
    hands = [sorted(pick[j * cards:(j + 1) * cards]) for j in range(4)]
    suits = []
    for hand in hands:
        by_suit = [[], [], [], []]
        for suit, rank in hand:
            by_suit[suit].append(rank)
        suits.append(".".join("".join(sorted(x, key=RANKS.index, reverse=True))
                               for x in by_suit))
    pbn = "N:" + " ".join(suits)
    position = oracle.Position.build(
        [tuple(oracle.Card(s, RANKS.index(r) + 2) for s, r in h) for h in hands],
        leader=0, spades_broken=False)
    return pbn, position


def cli_conjunction(pbn, roles, leader_char, attacker_char):
    cmd = [CLI, "--pbn", pbn, "--leader", leader_char,
           "--seats", " ".join(str(r) for r in roles),
           "--conjunction", attacker_char, "--compact"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        return None
    m = re.search(r"conjunction=(\d)", proc.stdout)
    return m is not None and m.group(1) == "1"


def cli_cooperative(pbn, roles, leader_char, protect_letters, extra_flags=()):
    cmd = [CLI, "--pbn", pbn, "--leader", leader_char,
           "--seats", " ".join(str(r) for r in roles),
           "--cooperative", protect_letters, "--compact", *extra_flags]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        return None, proc.stderr.strip()[:80]
    m = re.search(r"cooperative=(\d)", proc.stdout)
    if m is None:
        return None, "no cooperative= line"
    return (m.group(1) == "1"), None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cases", type=int, default=24)
    ap.add_argument("--cards", type=int, default=4)
    ap.add_argument("--seed", type=int, default=17)
    ap.add_argument("--bias-attempts", type=int, default=40,
                     help="extra draws per case tried against the neither-can-force "
                          "cell before falling back to a plain random deal")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    checked = 0
    reachable_seen = 0
    unreachable_seen = 0
    neither_cell_seen = 0
    collapse_checked = 0
    collapse_disagree = 0
    memo_checked = 0
    memo_disagree = 0
    problems = []

    for case in range(args.cases):
        roles = SHAPES[case % len(SHAPES)]
        bidders = [s for s in range(4) if roles[s] == 0]

        # Bias toward the cell T's rule reads: try a handful of random deals
        # and keep the first where NEITHER bidder can force the trade,
        # checked with the already-shipped C++ probe rather than the oracle's.
        chosen = None
        for _ in range(args.bias_attempts):
            pbn, position = random_deal(rng, args.cards)
            both_false = all(
                cli_conjunction(pbn, roles, "N", SEATS[b]) is False for b in bidders)
            if both_false:
                chosen = (pbn, position)
                neither_cell_seen += 1
                break
        if chosen is None:
            pbn, position = random_deal(rng, args.cards)
            chosen = (pbn, position)
        pbn, position = chosen

        protect_sets = [bidders] + [[b] for b in bidders]
        for protect in protect_sets:
            letters = "".join(SEATS[s] for s in protect)
            want = oracle.solve_cooperative(position, list(roles), protect).reachable

            got, err = cli_cooperative(pbn, roles, "N", letters)
            checked += 1
            if got is None:
                problems.append(f"{pbn} roles {roles} protect {letters}: {err}")
                continue
            if got:
                reachable_seen += 1
            else:
                unreachable_seen += 1
            if got != want:
                problems.append(
                    f"{pbn} roles {roles} protect {letters}: c++={got} oracle={want}")

            got_nc, err_nc = cli_cooperative(pbn, roles, "N", letters,
                                              extra_flags=("--no-collapse",))
            collapse_checked += 1
            if got_nc is None:
                problems.append(f"{pbn} roles {roles} protect {letters} --no-collapse: {err_nc}")
            elif got_nc != got:
                collapse_disagree += 1
                problems.append(
                    f"{pbn} roles {roles} protect {letters}: collapse changes the answer "
                    f"({got} against {got_nc})")

            got_nm, err_nm = cli_cooperative(pbn, roles, "N", letters,
                                              extra_flags=("--no-memo",))
            memo_checked += 1
            if got_nm is None:
                problems.append(f"{pbn} roles {roles} protect {letters} --no-memo: {err_nm}")
            elif got_nm != got:
                memo_disagree += 1
                problems.append(
                    f"{pbn} roles {roles} protect {letters}: memo changes the answer "
                    f"({got} against {got_nm})")

    print(f"{checked} protect-queries at {args.cards} cards across "
          f"{len(SHAPES)} one-bid-per-pair role sets (same- and opposite-lean), "
          f"{args.cases} deals")
    print(f"  C++ agrees with the oracle: {checked - len([p for p in problems if 'oracle=' in p])}"
          f" of {checked}")
    print(f"  reachable {reachable_seen}, unreachable {unreachable_seen}"
          f"{'   <-- all one answer, this run is not exercising both' if reachable_seen == 0 or unreachable_seen == 0 else ''}")
    print(f"  deals landing in the neither-can-force cell: {neither_cell_seen} of {args.cases}")
    print(f"  collapsing rank-equivalent moves changes the answer: "
          f"{collapse_disagree} of {collapse_checked}")
    print(f"  the memo changes the answer: {memo_disagree} of {memo_checked}")

    if reachable_seen == 0 or unreachable_seen == 0:
        problems.append(
            f"the probe answered the same way on all {checked} queries; this run is not "
            f"checking both branches")
    if neither_cell_seen == 0:
        problems.append(
            "no sampled deal landed in the neither-can-force cell across all "
            f"{args.cases} cases x {args.bias_attempts} attempts; the bias sampling itself "
            "may need a larger --bias-attempts or --cards")

    print()
    for p in problems[:8]:
        print(f"FAIL: {p}")
    if len(problems) > 8:
        print(f"... and {len(problems) - 8} more")
    if problems:
        return 1
    print("C++ agrees with the oracle on every query, on a population that is not all "
          "one answer, with collapsing and the memo both answer-neutral")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
