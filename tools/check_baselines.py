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
BASELINES = [
    # 39,701 before the adversarial-proofs patch (-44.6%).
    ("positions.txt fast", 22_009,
     ["--corpus", "tests/corpus/positions.txt", "--mode", "fast"]),
    # 274,270 at patch 106 (B1a); 227,386 at HEAD c35614b; 227,706 after the
    # Sept 2026 pass (+0.14% on HEAD); 219,626 after the adversarial proofs
    # (-3.5%).
    ("positions.txt full", 219_626,
     ["--corpus", "tests/corpus/positions.txt", "--mode", "full"]),
    # 49,084 before the adversarial-proofs patch (-62.4%).
    ("large.txt fast", 18_466,
     ["--corpus", "tests/corpus/large.txt", "--mode", "fast"]),
    # 163,134,302 at patch 106 (B1a); 7,778,660 at HEAD c35614b; 14,899,561
    # after the pass (+92% on HEAD, nearly all of it c13-0000 -- see above);
    # 14,723,260 after the adversarial proofs (-1.2%).
    ("large.txt full", 14_723_260,
     ["--corpus", "tests/corpus/large.txt", "--mode", "full"]),
    # NOT one of the four hand-run baselines Phase A set out to verify.  Added
    # at patch 105 as a CONSISTENCY CHECK on the worst-case leg's subtotal in
    # scripts/run-bench.{sh,cmd}: those three per-deal figures were the ones
    # found stale, and no other row here covers them.
    # 162,499,778 at patch 105; 7,512,026 at HEAD c35614b; 14,692,504 after
    # the pass; 14,584,088 after the adversarial proofs (-0.7%).
    ("large.txt 13c only", 14_584_088,
     ["--corpus", "tests/corpus/large.txt", "--cards-only", "13"]),
    # 4,833,200 at patch 108; 3,958,328 at HEAD c35614b; 2,425,248 after the
    # pass (-39% on HEAD).
    ("multinil.txt", 2_425_248,
     ["--corpus", "tests/corpus/multinil.txt"]),
    # Roles in the file header, not in the rows -- see the module docstring.
    # 351,156,828 at patch 108; 271,522,655 at HEAD c35614b; 182,407,101 after
    # the pass (-33% on HEAD); 166,580,542 after the adversarial proofs
    # (-8.7%, all of it in the single-nil presolve probes).
    ("opposed13", 166_580_542,
     ["--deals", "tests/corpus/opposed13.txt", "--seats", "0 0 3 2"]),
    # ROTATED roles, deliberately.  Not a copy of the line above.
    # 55,428,602 at patch 108; 3,437,862 at HEAD c35614b; 3,626,151 after the
    # pass (+5.5% on HEAD).
    ("opposed13_settled", 3_626_151,
     ["--deals", "tests/corpus/opposed13_settled.txt", "--seats", "3 2 0 0"]),
    # A corpus: roles travel per row, so no --seats.
    # 171,731,064 at patch 108; 172,080,754 at HEAD c35614b; 93,336,454 after
    # the pass (-46% on HEAD); 86,579,369 after the adversarial proofs (-7.2%).
    ("opposed13_real", 86_579_369,
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
