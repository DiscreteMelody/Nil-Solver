#!/usr/bin/env python3
"""Interleaved paired-rep A/B for one search arm, on one binary.

MOVE_ORDERING.md's wall-time protocol as a script, because every ordering item
this project has judged has been judged this way, and doing it by hand invites
the three mistakes it exists to prevent.

  * **One binary, arm toggled at runtime.** Both columns are the same
    executable and the only difference is the `--no-X` flag.  Comparing two
    builds compares two sets of inlining decisions as much as it compares the
    arm.
  * **Interleaved, not sequential.** Container clock drift is slow relative to a
    rep, so four OFFs followed by four ONs partly measures the drift.  Each rep
    here is OFF-then-ON and the pair is compared against itself.
  * **Every rep reported, not the mean.** An arm that wins on average and loses
    one rep has not been shown to win.  The line to read is the count of reps
    the arm won.

Usage:

    tools/ab_interleave.py --arm=--no-forced-covers
    tools/ab_interleave.py --arm=--no-suit-mix --reps 6
    tools/ab_interleave.py --arm=--no-ordering --mode full

Note the `=`: the arm is itself a flag, so argparse needs it attached.

High card counts are what matter, so the 13-card workloads run and report first.
"""
import argparse
import re
import subprocess
import sys
import time

TOTAL = re.compile(r"^\s*total\s+[\d,]+\s+([\d,]+)\s", re.M)

# 13 cards first, deliberately: a change that helps at 11-13 and costs a little
# at 4-6 is a good change, and the reverse is not.
WORKLOADS = [
    ("13 cards, 8 deals, seed 3", ["--random", "--cards", "13", "--count", "8", "--seed", "3"]),
    ("13 cards, 8 deals, seed 11", ["--random", "--cards", "13", "--count", "8", "--seed", "11"]),
    ("13 cards, 8 deals, seed 42", ["--random", "--cards", "13", "--count", "8", "--seed", "42"]),
    ("11 cards, 8 deals, seed 3", ["--random", "--cards", "11", "--count", "8", "--seed", "3"]),
    ("11 cards, 8 deals, seed 42", ["--random", "--cards", "11", "--count", "8", "--seed", "42"]),
    ("corpus, 560", ["--corpus", "tests/corpus/positions.txt"]),
]

# A bid on EACH side.  A separate list rather than extra entries above, because
# nothing in the default set exercises the shape at all -- `--random` draws a
# single nil unless told otherwise -- and an arm that only touches the opposed
# path would otherwise be measured entirely on workloads it cannot move.
#
# The contested corpus comes FIRST and is the one to read.  Random opposed deals
# are a neutral second sample and not a representative one: a hand drawn at
# random usually gives a bidder an ace, so its bid is trivially breakable and the
# search never has to work.  Real nil bids are the deals where the outcome is in
# doubt, which is what `opposed13.txt` holds and what patch 69 learned the hard
# way when its first generator produced six deals that all answered instantly.
OPPOSED = ["--seats", "0", "0", "3", "2"]
OPPOSED_WORKLOADS = [
    ("13 cards, 8 contested, opposed13.txt", ["--deals", "tests/corpus/opposed13.txt", "--leader", "N"] + OPPOSED),
    ("11 cards, 8 random opposed, seed 3",
     ["--random", "--cards", "11", "--count", "8", "--seed", "3"] + OPPOSED),
    ("11 cards, 8 random opposed, seed 42",
     ["--random", "--cards", "11", "--count", "8", "--seed", "42"] + OPPOSED),
    ("9 cards, 8 random opposed, seed 3",
     ["--random", "--cards", "9", "--count", "8", "--seed", "3"] + OPPOSED),
]


def run(bench, args):
    start = time.perf_counter()
    proc = subprocess.run([bench] + args, capture_output=True, text=True)
    elapsed = time.perf_counter() - start
    if proc.returncode != 0:
        sys.exit(f"{bench} failed: {proc.stderr.strip()[:400]}")
    match = TOTAL.search(proc.stdout)
    if not match:
        sys.exit(f"no total row in the output of: {' '.join(args)}")
    return elapsed, int(match.group(1).replace(",", ""))


def measure(bench, label, workload, arm, mode, reps):
    base = workload + ["--mode", mode, "--quiet", "--slowest", "0"]
    off_times, on_times = [], []
    off_nodes = on_nodes = -1
    for _ in range(reps):
        # OFF then ON inside the rep, so any drift is shared by the pair.
        elapsed, off_nodes = run(bench, base + [arm])
        off_times.append(elapsed)
        elapsed, on_nodes = run(bench, base)
        on_times.append(elapsed)

    delta = (on_nodes - off_nodes) / off_nodes * 100 if off_nodes else 0.0
    print(f"\n=== {label} ===")
    print(f"  nodes    off {off_nodes:>14,}   on {on_nodes:>14,}   {delta:+.2f}%")
    wins = 0
    for i, (a, b) in enumerate(zip(off_times, on_times), 1):
        ratio = b / a
        wins += ratio < 1.0
        print(f"  rep {i}    off {a:7.3f}s   on {b:7.3f}s   ratio {ratio:.3f}"
              f"   {'WIN' if ratio < 1.0 else 'loss'}")
    best_off, best_on = min(off_times), min(on_times)
    print(f"  best     off {best_off:7.3f}s   on {best_on:7.3f}s"
          f"   ratio {best_on / best_off:.3f}")
    print(f"  through  off {off_nodes / best_off / 1e6:6.2f}M/s"
          f"   on {on_nodes / best_on / 1e6:6.2f}M/s"
          f"   {(on_nodes / best_on) / (off_nodes / best_off) - 1:+.1%}")
    print(f"  reps won by the arm: {wins}/{reps}")
    return wins, reps, delta


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--arm", required=True,
                        help="the --no-X flag that switches the arm OFF; attach it with '=', e.g. --arm=--no-suit-mix")
    parser.add_argument("--bench", default="./build/bin/nil_bench")
    parser.add_argument("--mode", default="fast", choices=("fast", "full"))
    parser.add_argument("--reps", type=int, default=4,
                        help="paired reps per workload; four is the protocol minimum")
    parser.add_argument("--workloads", default="default", choices=("default", "opposed"),
                        help="'default' is the single-nil set; 'opposed' is the "
                             "one-bid-per-side set, for arms that only touch that shape")
    parser.add_argument("--only", type=int, default=None, metavar="N",
                        help="run workload N only (1-based), so a long set can be "
                             "taken one workload per invocation and still be one protocol")
    args = parser.parse_args()

    print(f"arm {args.arm}   mode {args.mode}   reps {args.reps}")
    print("'off' is the arm disabled; 'on' is the default build.")

    chosen = OPPOSED_WORKLOADS if args.workloads == "opposed" else WORKLOADS
    if args.only is not None:
        chosen = [chosen[args.only - 1]]
    results = [(label, measure(args.bench, label, workload, args.arm, args.mode, args.reps))
               for label, workload in chosen]

    print("\n=== summary ===")
    clean = True
    for label, (wins, reps, delta) in results:
        ok = wins == reps and delta <= 0
        clean = clean and ok
        print(f"  {label:<28} nodes {delta:+7.2f}%   reps won {wins}/{reps}"
              f"{'' if ok else '   <-- not a clean win'}")
    print("\nEvery rep a win." if clean else
          "\nAt least one workload is not a clean win; see MOVE_ORDERING.md.")


if __name__ == "__main__":
    main()
