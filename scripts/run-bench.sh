#!/usr/bin/env bash
# Verify the corpus, report timings, and append a row to bench-history.csv.
#
#   scripts/run-bench.sh
#   scripts/run-bench.sh "added alpha-beta"
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

echo
echo "=== History ==="
python3 tools/bench_history.py || python tools/bench_history.py || true

echo
echo "Node counts are deterministic, so they compare across machines and commits."
echo "Wall time only compares within one machine and build configuration."
