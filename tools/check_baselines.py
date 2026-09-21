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
BASELINES = [
    ("positions.txt fast", 39_701,
     ["--corpus", "tests/corpus/positions.txt", "--mode", "fast"]),
    ("positions.txt full", 278_059,
     ["--corpus", "tests/corpus/positions.txt", "--mode", "full"]),
    ("large.txt fast", 49_084,
     ["--corpus", "tests/corpus/large.txt", "--mode", "fast"]),
    ("large.txt full", 163_149_275,
     ["--corpus", "tests/corpus/large.txt", "--mode", "full"]),
    ("large.txt 13c only", 162_499_778,
     ["--corpus", "tests/corpus/large.txt", "--cards-only", "13"]),
    ("multinil.txt", 4_833_200,
     ["--corpus", "tests/corpus/multinil.txt"]),
    # Roles in the file header, not in the rows -- see the module docstring.
    ("opposed13", 351_156_828,
     ["--deals", "tests/corpus/opposed13.txt", "--seats", "0 0 3 2"]),
    # ROTATED roles, deliberately.  Not a copy of the line above.
    ("opposed13_settled", 55_428_602,
     ["--deals", "tests/corpus/opposed13_settled.txt", "--seats", "3 2 0 0"]),
    # A corpus: roles travel per row, so no --seats.
    ("opposed13_real", 171_731_064,
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
