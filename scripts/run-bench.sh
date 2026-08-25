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
    echo "=== Worst case (13 cards, ~543M nodes, about a minute) ==="
    # --cards-only 13 selects the three 13-card rows and nothing else.  Their
    # answers are PINNED FROM THIS SOLVER, not from nil_oracle.py, which cannot
    # reach 13 cards: a mismatch here means something CHANGED, not necessarily
    # that something broke.  Investigate rather than assume either way.
    #
    # Baselines to compare against, deterministic and machine independent:
    #   c13-0000     60,020,405 nodes
    #   c13-0001    379,357,462 nodes   <- the hardest deal in the repo
    #   c13-0002    104,316,206 nodes
    "$BENCH" --corpus tests/corpus/large.txt --cards-only 13 --slowest 3 \
             --history bench-history.csv --note "worst-case 13c${NOTE:+ -- $NOTE}" || exit 1
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
