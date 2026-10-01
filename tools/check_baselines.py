#!/usr/bin/env python3
"""Run every banked workload and check it against its recorded node count.

WHY THIS FILE EXISTS.  Eight workloads are banked in ROADMAP.md and every one
of them has been run BY HAND since it was created.  That has now failed twice
in the same way.  Patch 97 exists because patch 95's re-bank missed three of
the opposed workloads -- they are run by hand rather than by a corpus row, so
nothing pointed the re-bank at them.  Patch 105 then found the three
per-deal worst-case figures in `scripts/run-bench.{sh,cmd}` stale by 0.5-0.9%,
for the same reason and in a place that had been re-banked around twice.

The fix is not more diligence.  It is that

  (a) the numbers live in ONE place rather than in two shell scripts and a
      markdown table, and
  (b) a wrong invocation cannot return a plausible number, because nobody
      reads the number -- this script compares it.

THE INVOCATIONS ARE NOT UNIFORM AND THE DEFAULTS ARE WRONG FOR THREE OF THEM.
The opposed corpora carry their seat roles in a header COMMENT rather than in
their rows, so `nil_bench` cannot read them and `--seats` has to be supplied by
hand.  `opposed13_settled.txt` uses `3 2 0 0` rather than `0 0 3 2` -- the same
cards as opposed13.txt deal 5 with the roles ROTATED, which is the point of the
file and not a typo.  `opposed13_real.txt` is the other way round again: it is
a full corpus whose rows each carry their own roles, so it takes `--corpus` and
must NOT be given `--seats`.

Getting any of that wrong does not error.  `--deals` on opposed13.txt with the
default seats returns 125,516,991; on opposed13_settled.txt it returns
241,340,102 -- which is patch 97's own figure for the hard single deal, a number
a reviewer would RECOGNISE and wave through.  A wrong invocation returning a
FAMILIAR number is not something eyeballing catches.

WHEN A BASELINE MOVES.  A mismatch is not automatically a bug -- half the
entries in ROADMAP.md moved one of these on purpose.  It means the tree moved,
and this project's rule is that a moved fixed point needs a WRITTEN CAUSE
before it is re-banked, with the old figure left standing in the historical
entry that recorded it.  See patches 95 and 97.

THE TABLE BELOW IS NOT ALL ONE THING.  Five rows are the corpus workloads the
bench scripts already ran; three are the opposed workloads patch 97 had to
re-bank by hand; one -- `large.txt 13c only` -- is neither, and was added at
patch 105 purely as a consistency check on the worst-case leg's subtotal.

Usage:
    python3 tools/check_baselines.py [--bench PATH] [--only SUBSTRING]
"""

import argparse
import os
import re
import subprocess
import sys

# label, expected nodes, extra nil_bench arguments.
#
# Keep this table and ROADMAP.md's banked figures in step.  If you are changing
# a number here, the roadmap entry explaining WHY should already exist.
# RE-BANKED AT THE SEPT 2026 PERFORMANCE PASS, with two causes that have to be
# kept apart.  HEAD c35614b already missed seven of these nine -- the
# double-dummy handoff, the one-live-bid bound and the charge-once rule each
# moved the full-mode trees and none re-banked here -- so the table below
# records THREE figures per moved row: what patch 108 banked, what HEAD
# c35614b measured before the pass, and what the pass measures (the number
# checked).  The pass's own causes are in its ROADMAP.md entry: MTD(f) rows
# (which none of these run -- they are single-answer solves), the two-bound
# table, the tight principal-variation windows, charging doomed bids on
# arrival, the live-bid move ordering, the double-dummy soundness fix, and the
# one-live-bid bound turned OFF by default, which is the one that moves a
# figure UP (large.txt full; c13-0000 alone goes 5,964,148 -> 13,169,403).
# The two fast rows do not move: the one ordering rule that touched MODE_FAST
# is gated off there.
#
# RE-BANKED AGAIN BY THE ADVERSARIAL-PROOFS PATCH (duck or cover, forcing
# lead; ROADMAP.md).  One cause, three routes:
#   * the fast rows move most, because MODE_FAST takes the proofs as values;
#   * the full rows move a little, because MODE_FULL takes them as fail-soft
#     bounds, and its fast presolve takes them as values;
#   * opposed13 and opposed13_real move because the one-bid-per-side presolve
#     asks two SINGLE-NIL fast questions (seat_roles_from_nil), which is
#     exactly the game the proofs are about.
# multinil.txt and opposed13_settled do not move.  Each moved row keeps the
# previous figure in its comment; `--no-adversarial-proofs` reproduces it.
#
# RE-BANKED AGAIN BY THE SEPT 2026 ORDERING STUDY (the trick-oriented order;
# MOVE_ORDERING.md and ROADMAP.md).  One cause: every seat without a live bid
# is now ordered by trick_order_moves, in BOTH modes, so every row moves --
# ordering is the one change that reaches every tree.  Most move down; three
# move up, and they are named rather than hidden: positions.txt full (+4.1%
# of nodes on 4-6 card solves, where there is little to order; wall time is
# 5% LOWER; +3.0% once the killer is in), large.txt fast (+26.6% of an
# 18k-node total, nearly all of it one 9-card row, c9-0002, 13,273 -> 19,287
# nodes, under 3 ms either way), and opposed13_settled (+1.1%).  `--no-trick-order` reproduces every
# previous figure exactly -- checked on all nine before re-banking.
# The killer move that rides on it (full mode only, tried second) then moves
# the six full-mode rows it reaches, all DOWN -- large.txt most, 12.06M -> 7.95M,
# nearly all in its three 13-card rows -- and none of the fast rows or
# opposed13_settled.  Each row below gives the figure with the trick order
# alone, which `--no-killer-order` reproduces exactly (all nine checked), and
# the figure checked.
#
# RE-BANKED BY THE SECOND SEPT 2026 OPTIMIZATION PASS (ROADMAP.md).  Two causes,
# kept apart, and each control arm reproduces the previous figure exactly:
#   * M4, the adversarial proofs in the one-live-bid regions of the two-bid
#     shapes, moves the four two-bid rows DOWN (`--no-multi-live-proofs`
#     reproduces them): multinil -9.0%, opposed13 -16.0%, opposed13_real
#     -3.9%; opposed13_settled does not move (its bids are settled).
#   * A4, the nil-demoted double-dummy bound from 8 tricks up, moves large.txt
#     full UP by 1,987 nodes, +0.02%, in its 8-12 card rows (`--demoted-dd 0`
#     reproduces it); the 13-card rows do not move.
#   * the engine table's aging (a solve's stale entries are replaced first)
#     moves three rows by a few nodes in either direction, because nil_bench
#     solves its positions one after another in one process and the engine's
#     table carries over between them: large.txt full 7,954,566 -> 7,954,436,
#     large.txt 13c only 7,849,554 -> 7,849,414, opposed13 102,479,375 ->
#     102,479,390 (`--no-dd-age` reproduces each).
# The pass's other changes are node-identical (memory layout, prefetching, the
# boundary-fact cache) or confined to solve_moves (row order, values-only
# rows), which none of these run.
BASELINES = [
    # 39,701 before the adversarial-proofs patch (-44.6%); 22,009 after it;
    # 17,564 after the ordering study (-20.2%).
    ("positions.txt fast", 17_564,
     ["--corpus", "tests/corpus/positions.txt", "--mode", "fast"]),
    # 274,270 at patch 106 (B1a); 227,386 at HEAD c35614b; 227,706 after the
    # Sept 2026 pass (+0.14% on HEAD); 219,626 after the adversarial proofs
    # (-3.5%); 228,684 with the trick order (+4.1%, wall time -5%); 226,244
    # with the killer too (+3.0% on 219,626); 145,489 after the Oct 2026
    # minimise-direction study (-35.7%): the shed order, on the corpus's 270
    # `min` rows only -- --no-shed-order reproduces 226,244 exactly, and
    # --shed-single-attack gives 146,271.
    ("positions.txt full", 145_489,
     ["--corpus", "tests/corpus/positions.txt", "--mode", "full"]),
    # 49,084 before the adversarial-proofs patch (-62.4%); 18,466 after it;
    # 23,376 after the ordering study (+26.6%).
    ("large.txt fast", 23_376,
     ["--corpus", "tests/corpus/large.txt", "--mode", "fast"]),
    # 163,134,302 at patch 106 (B1a); 7,778,660 at HEAD c35614b; 14,899,561
    # after the pass (+92% on HEAD, nearly all of it c13-0000 -- see above);
    # 14,723,260 after the adversarial proofs (-1.2%); 12,056,774 with the
    # trick order (-18.1%); 7,952,579 with the killer too (-46.0% on
    # 14,723,260).
    # 7,952,579 before the second Sept 2026 pass (+0.02%, A4; 7,954,566 before
    # the engine's aging, -130; see above).
    ("large.txt full", 7_954_436,
     ["--corpus", "tests/corpus/large.txt", "--mode", "full"]),
    # NOT one of the four hand-run baselines Phase A set out to verify.  Added
    # at patch 105 as a CONSISTENCY CHECK on the worst-case leg's subtotal in
    # scripts/run-bench.{sh,cmd}: those three per-deal figures were the ones
    # found stale, and no other row here covers them.
    # 162,499,778 at patch 105; 7,512,026 at HEAD c35614b; 14,692,504 after
    # the pass; 14,584,088 after the adversarial proofs (-0.7%); 11,953,123
    # with the trick order (-18.0%); 7,849,554 with the killer too (-46.2% on
    # 14,584,088).
    # 7,849,554 before the engine's aging in the second Sept 2026 pass (-140).
    ("large.txt 13c only", 7_849_414,
     ["--corpus", "tests/corpus/large.txt", "--cards-only", "13"]),
    # 4,833,200 at patch 108; 3,958,328 at HEAD c35614b; 2,425,248 after the
    # pass (-39% on HEAD); 2,397,142 with the trick order (-1.2%); 2,331,066
    # with the killer too (-3.9% on 2,425,248).
    # 2,331,066 before the second Sept 2026 pass (-9.0%, M4; see above).
    ("multinil.txt", 2_122_172,
     ["--corpus", "tests/corpus/multinil.txt"]),
    # Roles in the file header, not in the rows -- see the module docstring.
    # 351,156,828 at patch 108; 271,522,655 at HEAD c35614b; 182,407,101 after
    # the pass (-33% on HEAD); 166,580,542 after the adversarial proofs
    # (-8.7%, all of it in the single-nil presolve probes); 123,215,403 with
    # the trick order (-26.0%); 122,012,523 with the killer too (-26.8% on
    # 166,580,542).
    # 122,012,523 before the second Sept 2026 pass (-16.0%, M4; 102,479,375
    # before the engine's aging, +15; see above).
    ("opposed13", 102_479_390,
     ["--deals", "tests/corpus/opposed13.txt", "--seats", "0 0 3 2"]),
    # ROTATED roles, deliberately.  Not a copy of the line above.
    # 55,428,602 at patch 108; 3,437,862 at HEAD c35614b; 3,626,151 after the
    # pass (+5.5% on HEAD); 3,667,407 after the ordering study (+1.1%; the
    # killer does not move it).
    ("opposed13_settled", 3_667_407,
     ["--deals", "tests/corpus/opposed13_settled.txt", "--seats", "3 2 0 0"]),
    # A corpus: roles travel per row, so no --seats.
    # 171,731,064 at patch 108; 172,080,754 at HEAD c35614b; 93,336,454 after
    # the pass (-46% on HEAD); 86,579,369 after the adversarial proofs (-7.2%);
    # 58,292,306 with the trick order (-32.7%); 56,768,828 with the killer too
    # (-34.4% on 86,579,369).
    # 56,768,828 before the second Sept 2026 pass (-3.9%, M4; see above).
    ("opposed13_real", 54_576_580,
     ["--corpus", "tests/corpus/opposed13_real.txt"]),
]

TOTAL_RE = re.compile(r"^\s*total\s+\S+\s+([\d,]+)", re.MULTILINE)


def find_bench(explicit):
    if explicit:
        return explicit
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for name in ("nil_bench", "nil_bench.exe"):
        path = os.path.join(root, "build", "bin", name)
        if os.path.exists(path):
            return path
    return None


def run_one(bench, args):
    """Return the total node count, or None if the run gave us nothing.

    A run that fails is reported as a failure rather than skipped: a check that
    quietly stops checking is the recurring bug this repo keeps finding.
    """
    try:
        out = subprocess.run([bench, *args, "--quiet", "--slowest", "0"],
                             capture_output=True, text=True, timeout=1800)
    except (OSError, subprocess.TimeoutExpired):
        return None
    match = TOTAL_RE.search(out.stdout)
    if not match:
        return None
    return int(match.group(1).replace(",", ""))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bench", help="path to nil_bench")
    parser.add_argument("--only", help="run only baselines whose label contains this")
    opts = parser.parse_args()

    bench = find_bench(opts.bench)
    if not bench:
        print("nil_bench not found -- run scripts/build-and-test.sh first.", file=sys.stderr)
        return 2

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)

    selected = [b for b in BASELINES if not opts.only or opts.only in b[0]]
    if not selected:
        print(f"no baseline matches {opts.only!r}", file=sys.stderr)
        return 2

    failures = []
    for label, expected, args in selected:
        got = run_one(bench, args)
        if got is None:
            print(f"  {label:<22} {'FAILED TO RUN':>15}")
            failures.append(label)
        elif got == expected:
            print(f"  {label:<22} {got:>15,}  ok")
        else:
            delta = (got - expected) / expected * 100.0
            print(f"  {label:<22} {got:>15,}  MISMATCH, banked {expected:,} ({delta:+.2f}%)")
            failures.append(label)
        sys.stdout.flush()

    if failures:
        print()
        print(f"*** {len(failures)} banked baseline(s) moved: {', '.join(failures)} ***")
        print("A moved baseline is not automatically a bug -- but it needs a WRITTEN")
        print("CAUSE in ROADMAP.md before it is re-banked, and the old figure stays")
        print("in the historical entry that recorded it.  See patches 95 and 97.")
        return 1

    print(f"  all {len(selected)} banked baselines match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
