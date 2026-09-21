#!/usr/bin/env bash
# Verify the corpus, report timings, and append a row to bench-history.csv.
#
#   scripts/run-bench.sh
#   scripts/run-bench.sh "added alpha-beta"
#
# Two legs.  The corpus leg is 560 small oracle-verified positions and takes a
# couple of seconds; it is the correctness net.  The worst-case leg is the three
# 13-card rows in tests/corpus/large.txt and takes about a minute; it is the
# only thing here that measures what a user actually waits for.
#
# They answer different questions and neither substitutes for the other.  Cost
# varies by two orders of magnitude WITHIN a hand size, so a mean over easy
# positions can improve while the deals people complain about get slower.  Set
# NIL_SKIP_WORST=1 to run the corpus leg alone.
#
# A third leg times the two-nil shape on its own 13-card deals.  It is separate
# because it is a separate objective on a separate tree -- a change that helps
# one nil can easily do nothing for two, and averaging them would hide it.
# NIL_SKIP_MULTINIL=1 to skip it.
set -uo pipefail

cd "$(dirname "$0")/.."

BENCH="build/bin/nil_bench"
[ -x "$BENCH" ] || BENCH="build/bin/nil_bench.exe"
if [ ! -x "$BENCH" ]; then
    echo "$BENCH does not exist yet -- run scripts/build-and-test.sh first." >&2
    exit 1
fi

NOTE="${1:-}"

echo "=== Benchmark ==="
"$BENCH" --corpus tests/corpus/positions.txt --repeat 3 \
         --history bench-history.csv --note "$NOTE" || exit 1

if [ "${NIL_SKIP_WORST:-0}" != "1" ]; then
    echo
    echo "=== Worst case (13 cards, ~163M nodes, under half a minute) ==="
    # --cards-only 13 selects the three 13-card rows and nothing else.  Their
    # answers are PINNED FROM THIS SOLVER, not from nil_oracle.py, which cannot
    # reach 13 cards: a mismatch here means something CHANGED, not necessarily
    # that something broke.  Investigate rather than assume either way.
    #
    # Baselines to compare against, deterministic and machine independent.
    # Re-banked at patch 105; the figures that used to sit here were
    # 60,020,405 / 71,253,358 / 32,230,695, summing to 163,504,458.  That total
    # EXCEEDS the whole-file figure patch 95 recorded before it raised the
    # table, so these had already drifted when patch 95 re-banked around them
    # -- at least two re-banks stale.  Nothing was wrong with the solver; the
    # numbers simply lived where no re-bank looked.
    #   c13-0000     59,483,222 nodes
    #   c13-0001     70,957,819 nodes   <- the hardest deal in the repo
    #   c13-0002     32,058,737 nodes
    #   ---------------------------
    #   total       162,499,778 nodes
    #
    # THE STALENESS MATTERED BY THIS SCRIPT'S OWN STANDARD, which is why the
    # correction is worth more than the 0.6%.  The closing text below tells the
    # reader a 1% move on these rows is real and not noise; the figures above
    # were 0.5-0.9% high, so a run that changed nothing read as a small win.
    # `tools/check_baselines.py` is the fix: it holds the numbers in one place
    # and COMPARES them rather than printing them for a human to eyeball.
    #
    # All three run the MAX tie-break, matching the rest of the file.  Worth
    # knowing before you read a win off them: min is the more expensive
    # direction, roughly 2x on these deals, measured on one build with only the
    # flag moved.  These rows are the milder of the two worst cases.  If the
    # application ever solves for bag avoidance, the deals users actually wait
    # on are about twice what this leg reports.
    #
    # They are also all UNBROKEN, and that is not a detail.  Two of the three
    # used to carry broken=1 on a full thirteen-card deal, which validate() now
    # rejects: no card has been played, so no spade has been played.  Clearing
    # it restores the ban on a voluntary spade lead, which prunes hard near the
    # root and took this leg from 290M nodes to 163M.  The old figures measured
    # a game nobody can play.
    "$BENCH" --corpus tests/corpus/large.txt --cards-only 13 --slowest 3 \
             --history bench-history.csv --note "worst-case 13c${NOTE:+ -- $NOTE}" || exit 1
fi

# The banked-baseline leg.  Off by default -- it is ~740M nodes and a few
# minutes -- but it is the only thing here that checks the workloads nobody
# runs: the three opposed corpora each need a DIFFERENT invocation, and two of
# them need --seats that no row in the file supplies.  A wrong invocation does
# not error, it returns a plausible number.  See tools/check_baselines.py.
if [ "${NIL_RUN_BASELINES:-0}" = "1" ]; then
    echo
    echo "=== Banked baselines (~740M nodes, a few minutes) ==="
    python3 tools/check_baselines.py || python tools/check_baselines.py || exit 1
fi

echo
echo "=== History ==="
python3 tools/bench_history.py || python tools/bench_history.py || true

echo
echo "Node counts are deterministic, so they compare across machines and commits."
echo "Wall time only compares within one machine and build configuration."
echo
echo "The worst-case rows are single deals, so their node counts are exact rather"
echo "than averaged -- a change of even 1% there is real and not sampling noise."

if [ -z "${NIL_SKIP_MULTINIL:-}" ] && [ -f tests/corpus/multinil.txt ]; then
    echo
    echo "=== Two nils on one side (13 cards, every bound still gated off) ==="
    # These six rows carry no recorded answer -- the oracle is exhaustive and
    # cannot reach 13 cards -- so nothing is verified here and the node counts
    # are the whole output.  A change means the tree MOVED, which may be a win
    # or a bug; check corpus_multinil for whether the answers survived it.
    "$BENCH" --corpus tests/corpus/multinil.txt --cards-only 13 --slowest 3 \
        || echo "(multi-nil leg failed)"
fi
