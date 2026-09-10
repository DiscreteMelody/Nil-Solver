#!/usr/bin/env python3
"""Measure item 2b's C++ cooperative probe at 13 cards, both cost branches.

PART 1 runs `--cooperative` protecting both bidders on every named 13-card
deal already in the repo (opposed13.txt, opposed13_settled.txt,
opposed13_real.txt, and the hard single deal).  Patch 91 already established
that (make, make) is reachable on all of them -- the band comes back [0,3] on
8 of 8 contested deals and both realistic ones -- so this is the REACHABLE
branch's cost on real, hard positions, not a random sample.

PART 2 hunts for a genuine UNREACHABLE-at-13-cards case, because the standard
corpora cannot supply one: PART 1 confirms they are all reachable, and the
handoff is explicit that "no" is the expensive, common answer in exactly the
cell T's rule reads.  Filtering uses the already-shipped, fast C++
`--conjunction` probe (not the oracle, which is not meant to run at 13 cards
for the unreachable case -- that is the whole reason this port exists) to bias
random deals toward the "neither bidder can force the trade" cell, in both
partner leans -- same-lean (2/2, 3/3) cannot be conjunction-filtered, since
solve() itself refuses those roles, so those are tried directly.
"""
import argparse
import importlib.util
import itertools
import pathlib
import random
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
CLI = str(ROOT / "build/bin/nil_cli")
SEATS = "NESW"
RANKS = "23456789TJQKA"


def load_corpus_view():
    spec = importlib.util.spec_from_file_location("corpus_view", ROOT / "tools/corpus_view.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def run_cli(arg_list, timeout=None):
    return subprocess.run([CLI, *arg_list, "--compact"], capture_output=True, text=True,
                           timeout=timeout)


def cooperative(pbn, leader, seats, protect, timeout=None):
    try:
        proc = run_cli(["--pbn", pbn, "--leader", leader, "--seats", seats,
                         "--cooperative", protect], timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, None, f"TIMEOUT after {timeout}s"
    if proc.returncode != 0:
        return None, None, (f"exit {proc.returncode}: " +
                             (proc.stderr.strip() or "(no stderr)"))
    reach = re.search(r"cooperative=(\d)", proc.stdout)
    nodes = re.search(r"nodes=(\d+)", proc.stdout)
    if reach is None or nodes is None:
        return None, None, "no cooperative=/nodes= line: " + proc.stdout[:120]
    return (reach.group(1) == "1"), int(nodes.group(1)), None


def conjunction(pbn, leader, seats, attacker):
    try:
        proc = run_cli(["--pbn", pbn, "--leader", leader, "--seats", seats,
                         "--conjunction", attacker], timeout=60)
    except subprocess.TimeoutExpired:
        return None
    if proc.returncode != 0:
        return None
    m = re.search(r"conjunction=(\d)", proc.stdout)
    return m is not None and m.group(1) == "1"


def bidders_from_seats(seats_text):
    vals = [int(x) for x in seats_text.split()]
    return "".join(SEATS[i] for i, v in enumerate(vals) if v == 0)


def read_deals(path):
    return [line.strip() for line in path.read_text().splitlines()
            if line.strip() and not line.strip().startswith("#")]


def part1_known_yes(args):
    print("=== PART 1: known reachable cases, standard 13-card corpora ===")
    rows = []

    for i, pbn in enumerate(read_deals(ROOT / "tests/corpus/opposed13.txt")):
        reach, nodes, err = cooperative(pbn, "N", "0 0 3 2", "NE", timeout=args.timeout)
        rows.append((f"opposed13.txt#{i}", reach, nodes, err))

    for i, pbn in enumerate(read_deals(ROOT / "tests/corpus/opposed13_settled.txt")):
        reach, nodes, err = cooperative(pbn, "N", "3 2 0 0", "SW", timeout=args.timeout)
        rows.append((f"opposed13_settled.txt#{i}", reach, nodes, err))

    cv = load_corpus_view()
    for rec in cv.load(str(ROOT / "tests/corpus/opposed13_real.txt")):
        protect = bidders_from_seats(rec["seats"])
        reach, nodes, err = cooperative(rec["pbn"], rec["leader"], rec["seats"], protect,
                                         timeout=args.timeout)
        rows.append((rec["name"], reach, nodes, err))

    hard_pbn = ("N:853.95.K9852.864 7642.J642.3.Q732 KQ.KQ8.T76.AKJ95 AJT9.AT73.AQJ4.T")
    reach, nodes, err = cooperative(hard_pbn, "N", "0 0 3 2", "NE", timeout=args.timeout)
    rows.append(("the hard single deal", reach, nodes, err))

    total_nodes = 0
    all_reachable = True
    for name, reach, nodes, err in rows:
        if err:
            print(f"  {name:28s} ERROR: {err}")
            continue
        total_nodes += nodes
        all_reachable = all_reachable and reach
        print(f"  {name:28s} {'REACHABLE' if reach else 'UNREACHABLE':11s} {nodes:>14,} nodes")
    print(f"  -- {len(rows)} deals, all reachable: {all_reachable}, "
          f"total {total_nodes:,} nodes, mean {total_nodes / len(rows):,.0f} --")
    return rows


def random_pbn(rng, cards=13):
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
    return "N:" + " ".join(suits)


def part2_hunt(args):
    print("\n=== PART 2: hunting for a genuine UNREACHABLE case at 13 cards ===")
    rng = random.Random(args.seed)
    role_sets = [(0, 0, a, b) for a, b in itertools.product((2, 3), repeat=2)]

    tried = 0
    tested = 0
    reachable_nodes = []
    unreachable_nodes = []
    t0 = time.time()
    while tried < args.attempts and len(unreachable_nodes) < args.want:
        pbn = random_pbn(rng, cards=13)
        roles = role_sets[tried % len(role_sets)]
        seats = " ".join(str(x) for x in roles)
        tried += 1
        same_lean = roles[2] == roles[3]
        if same_lean:
            in_cell = True  # cannot be conjunction-filtered; try directly
        else:
            c0 = conjunction(pbn, "N", seats, "N")
            c1 = conjunction(pbn, "N", seats, "E")
            in_cell = (c0 is False) and (c1 is False)
        if not in_cell:
            continue
        reach, nodes, err = cooperative(pbn, "N", seats, "NE", timeout=args.timeout)
        if err is not None:
            print(f"  [{tried}] error/timeout: {err}  seats={seats}")
            continue
        tested += 1
        if reach:
            reachable_nodes.append(nodes)
        else:
            unreachable_nodes.append(nodes)
            print(f"  UNREACHABLE  seats={seats}  nodes={nodes:,}")
            print(f"    {pbn}")
    elapsed = time.time() - t0

    print(f"  -- {tried} deals drawn, {tested} landed in the target cell and were "
          f"tested, {elapsed:.1f}s --")
    if reachable_nodes:
        print(f"  reachable:   {len(reachable_nodes)}, mean "
              f"{sum(reachable_nodes) / len(reachable_nodes):,.0f} nodes, "
              f"range {min(reachable_nodes):,}-{max(reachable_nodes):,}")
    if unreachable_nodes:
        print(f"  unreachable: {len(unreachable_nodes)}, mean "
              f"{sum(unreachable_nodes) / len(unreachable_nodes):,.0f} nodes, "
              f"range {min(unreachable_nodes):,}-{max(unreachable_nodes):,}")
        if reachable_nodes:
            ratio = (sum(unreachable_nodes) / len(unreachable_nodes)) / \
                    (sum(reachable_nodes) / len(reachable_nodes))
            print(f"  unreachable-mean / reachable-mean: {ratio:.1f}x")
    else:
        print("  no unreachable case found in this run")
    return reachable_nodes, unreachable_nodes, tried, tested


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed", type=int, default=17)
    ap.add_argument("--attempts", type=int, default=60,
                     help="random deals to draw looking for the target cell")
    ap.add_argument("--want", type=int, default=6,
                     help="stop early once this many unreachable cases are found")
    ap.add_argument("--timeout", type=float, default=180.0,
                     help="seconds before a single --cooperative call counts as a timeout")
    ap.add_argument("--skip-part1", action="store_true")
    ap.add_argument("--skip-part2", action="store_true")
    args = ap.parse_args()

    if not args.skip_part1:
        part1_known_yes(args)
    if not args.skip_part2:
        part2_hunt(args)


if __name__ == "__main__":
    main()
