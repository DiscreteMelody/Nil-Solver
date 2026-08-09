# Nil-Solver

A double-dummy solver for the questions that matter when someone bids nil.

The objective is **lexicographic**:

> **PRIMARY** — the nil bidder's trick count. The nil bidder and its covering
> partner minimise it; both opponents maximise it. `nil_fails == true` means the
> nil is dead however it is played.
>
> **SECONDARY** — each pair's own trick count, used only to choose among lines
> that are already equally good for the primary. Either direction:
> each pair takes as many tricks as it can, or as few as it can.

The primary is not trick maximisation. Both opponents are trying to push a trick
onto one specific seat and will throw away tricks of their own to do it; the nil
bidder and its partner are trying to stop them. The secondary only breaks ties,
so it can never change the answer to the nil question — it decides what the two
pairs do with the tricks the nil question leaves undetermined.

Both components are strictly opposed, because the two pairs' trick counts sum to
a constant: the nil side taking more is identical to the opponents taking fewer.
That is why one flag sets a coherent direction for both sides at once, and why
plain minimax over the packed pair is still well defined.

`nil_already_set` drops the primary objective. Use it once the nil has actually
been broken in the real game, or when the score makes the nil irrelevant: there
is nothing left to protect or attack, and only the secondary objective matters.

This first iteration is **correctness first**: an exhaustive search with no
alpha-beta, no move ordering, no quick-trick shortcuts and no rank-equivalence
collapsing. It is meant for 4–6 card endings and is validated card-for-card
against `nil_oracle.py`. Speed work comes later, on top of a solver we trust.

## Layout

```
include/nil_solver/nil_solver.h   the C ABI — the only thing the DLL exports
src/nil/cards.hpp|.cpp            cards, hands, canonical ordering
src/nil/rules.hpp                 the four rules, one small function each
src/nil/position.hpp|.cpp         PBN parsing, validation, formatting
src/nil/search.hpp|.cpp           the minimax search and the PV replay verifier
src/nil/corpus.hpp|.cpp           loads tests/corpus/positions.txt
src/api.cpp                       C ABI implementation (marshalling only)
tools/nil_cli.cpp                 command line front end
tools/crosscheck.py               differential test against nil_oracle.py
tests/test_nil_solver.cpp         self-tests, mirroring the oracle's selftest()
tests/corpus/positions.txt        positions with answers from the oracle
tools/nil_bench.cpp               corpus verifier and benchmark
tools/make_corpus.py              regenerates the corpus
tools/bench_history.py            reads back bench-history.csv
scripts/build-and-test.cmd|.sh    one-shot build + test
scripts/run-bench.cmd|.sh         one-shot benchmark, logs to history
```

## Building

### Windows, the short version

Double-click **`scripts\build-and-test.cmd`**. It finds CMake on its own —
including the copy bundled inside Visual Studio — configures, builds, runs the
whole test suite, and leaves the window open so you can read the result.
`scripts\run-bench.cmd` does the same for the benchmark.

If you prefer the IDE: **File > Open > Folder** on the repository root. Visual
Studio 2022 reads `CMakePresets.json`, so the Release configuration and the test
presets appear without any setup, and the tests show up in Test Explorer.

### Windows, by hand

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

Two things bite here and neither is obvious:

* **`cmake` and `ctest` are usually not on `PATH`.** Either run from the
  *Developer Command Prompt for VS 2022* (Start menu), or reinstall CMake with
  "Add CMake to the system PATH" ticked, or add the "C++ CMake tools for
  Windows" component in the Visual Studio Installer. The `.cmd` scripts above
  sidestep this entirely.
* **`--build-config Release` is required for `ctest`.** The Visual Studio
  generator is multi-config, so without it `ctest` does not know which build of
  the tests to run and reports no tests. On Makefiles or Ninja the flag is
  harmlessly ignored, which is why it is safe to always pass it.

Binaries land in `build\bin` — with a `.exe` suffix, so it is
`build\bin\nil_bench.exe`, not `build/bin/nil_bench`.

### Linux and macOS

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Or `scripts/build-and-test.sh`, which is the same thing with the CMake hunt
built in.

### With presets (CMake 3.21+)

```
cmake --preset release
cmake --build --preset release
ctest --preset release      # or --preset quick for just the fast tests
```

### What gets built

Everything lands in `build/bin`:

| artefact | what it is |
| --- | --- |
| `nil_solver.dll` (`libnil_solver.so`) | the shared library for C# |
| `nil_solver.lib` | import library, in `build/lib` |
| `nil_cli` | command line front end |
| `nil_bench` | corpus verifier and benchmark |
| `nil_tests` | self-tests |

`cmake --install build --prefix <dir>` gives you the DLL, the import library and
the header in one place to copy next to your C# project.

## Command line

```
nil_cli --pbn 'N:A...2 K...3 Q...4 J...5' --leader N --nil N
nil_cli --pbn '...' --leader W --nil S --trick 'H4 HK' --spades-broken
nil_cli --pbn '...' --nil S --compact          # machine readable
```

`--pbn` is the usual PBN deal string: `spades.hearts.diamonds.clubs`, hands
running clockwise from the named seat, shortened mid-play hands accepted. Cards
already played to the trick in progress go in `--trick` (in play order starting
from the leader) and must **not** also appear in the hands.

The same layout under all three settings, showing what the secondary buys you:

```
$ nil_cli --pbn 'N:7..6.3 6.J.2. J3.7.. 9..3.9' --leader N --nil N --spades-broken
Objective      nil tricks first, then each pair takes what it can
Tricks for N   0 of 3
Side tricks    NS=2  EW=1
Nil            MAKES  (cannot be forced to take a trick)

$ ... --nil-already-set
Objective      nil already set, so secondary only; each pair takes what it can
Tricks for N   1 of 3
Side tricks    NS=3  EW=0

$ ... --secondary min
Objective      nil tricks first, then each pair sheds what it can
Tricks for N   0 of 3
Side tricks    NS=1  EW=2
```

Protecting the nil here costs N/S exactly one trick: they can take all three if
they stop caring about it.

Output is line-oriented so `diff` localises a divergence:

```
PBN            N:K.A.A.K 32.2..A .K.3.32 A.3.K2.
  N  S:K H:A D:A C:K
  E  S:32 H:2 D:- C:A
  S  S:- H:K D:3 C:32
  W  S:A H:3 D:K2 C:-
Leader         N
Nil bidder     E  (E/W minimise, N/S maximise)
Spades broken  no
Tricks for E   1 of 4
Nil            FAILS  (can be forced to take a trick)
Nodes          2,073
Principal variation:
  T1  N:HA   E:H2   S:HK   W:H3    won by N   [E=0]
  T2  N:CK   E:CA   S:C2   W:SA    won by W   [E=0]
  T3  W:D2   N:DA   E:S2   S:D3    won by E   [E=1] <-- nil takes a trick
  T4  E:S3   S:C3   W:DK   N:SK    won by N   [E=1]
```

## The C API

```c
nil_result r;
char err[256];
if (nil_solve("N:A...2 K...3 Q...4 J...5", NIL_SEAT_NORTH, "", NIL_SEAT_NORTH,
              NIL_FLAG_NONE, &r, err, sizeof err) == NIL_OK) {
    /* r.nil_fails, r.nil_tricks, r.nil_side_tricks, r.opponent_tricks,
       r.tricks_remaining, r.nodes */
}
```

The objective flags:

| flag | effect |
| --- | --- |
| *(none)* | nil tricks first, then each pair takes as many tricks as it can |
| `NIL_FLAG_MINIMISE_OWN_TRICKS` | nil tricks first, then each pair takes as **few** as it can |
| `NIL_FLAG_NIL_ALREADY_SET` | drop the primary; optimise only the secondary |

`nil_side_tricks + opponent_tricks` always equals `tricks_remaining`, and
`nil_tricks` is included in `nil_side_tricks`. When you pass
`NIL_FLAG_NIL_ALREADY_SET`, `nil_fails` comes back as 1 because you said so, not
because the search worked it out.

`nil_solve_pv` also writes the principal variation as a string;
`nil_fails(...)` is a one-shot convenience wrapper returning 1, 0 or a negative
error code. `nil_result.tricks` carries the exact trick count as well as the
bool, which is what the cross-check compares — a bool is a weak thing to diff.

From C# later, the shape is:

```csharp
[StructLayout(LayoutKind.Sequential)]
struct NilResult { public int NilFails, Tricks, TricksRemaining; public ulong Nodes; }

[DllImport("nil_solver", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
static extern int nil_solve(string pbn, int leader, string currentTrick, int nilSeat,
                            uint flags, out NilResult result,
                            StringBuilder err, int errLen);
```

The calling convention is `__cdecl` on every platform, and the export surface is
exactly the header (visibility is hidden by default), so there is nothing to
name-mangle around.

## Testing

There are three layers, cheapest first. `ctest` runs all of them.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

| test | what it covers | needs |
| --- | --- | --- |
| `nil_tests` | the four rules in isolation, small hand-verifiable searches, PBN parsing, position validation, the PV replay verifier, the C ABI | nothing |
| `corpus` | ~560 positions whose answers came from `nil_oracle.py` | nothing |
| `corpus_quick` | the 4-card subset of the same, for the inner loop | nothing |
| `crosscheck` | live differential test against the oracle on freshly generated positions | Python + `nil_oracle.py` |

Run one at a time with `ctest -R corpus_quick`, or invoke the binaries directly:

```
build/bin/nil_tests                                        # verbose
build/bin/nil_bench --corpus tests/corpus/positions.txt    # verify + time
python3 tools/crosscheck.py --exe build/bin/nil_cli --cases 200 --cards 4
```

### The corpus

`tests/corpus/positions.txt` is a flat text file of positions with the trick
counts the oracle gave for each, across a spread of objective settings — both
tie-break directions and both states of the already-set flag. Replaying it needs no Python and no oracle, so
it runs on every build and in CI in a couple of seconds — which is the whole
point, because the oracle itself takes several seconds per 6-card deal.

It is generated once and committed:

```
python3 tools/make_corpus.py --out tests/corpus/positions.txt
python3 tools/make_corpus.py --out tests/corpus/positions.txt --spec 4:400,5:120,6:40
```

Regenerate it deliberately, not casually. The value of a committed expected
answer is that it came from the oracle at a moment when someone was watching; a
corpus that gets silently regenerated whenever it fails is just an echo of the
current code.

Each record carries the nil bidder's tricks, its side's tricks, and the settings
that produced them. It also stores the principal variation, but `nil_bench` only
checks that under `--check-pv`. Once we add move ordering, the search will legitimately pick
a different one of several equal-valued cards, and that is not a regression.
While the search is still exhaustive, `--check-pv` is worth leaving on.

### The live cross-check

Drop `nil_oracle.py` in the repository root and run:

```
python3 tools/crosscheck.py --exe build/bin/nil_cli --cases 200 --cards 4
python3 tools/crosscheck.py --exe build/bin/nil_cli --cases 60  --cards 5 --trick-prob 0.5
python3 tools/crosscheck.py --exe build/bin/nil_cli --cases 20  --cards 6
python3 tools/crosscheck.py --exe build/bin/nil_cli --cases 60  --cards 4 --no-memo --oracle-no-memo
```

The harness generates random positions — random leader, random nil seat, random
broken flag, and roughly a third of them resumed mid-trick — and compares three
things: the bool, the exact trick count, and the **principal variation, card for
card**. The third is the one that earns its keep. Two implementations can agree
on every value while disagreeing about a rule that happens not to bite in the
sampled positions; they cannot agree on the PV by accident, because both sides
break ties identically (canonically lowest card, strict improvement only).

`ctest` runs a small cross-check automatically, and skips rather than fails if
the oracle is not present. Point it somewhere else with
`--oracle /path/to/nil_oracle.py`.

Anything the cross-check finds should become a corpus entry, or a named case in
`tests/test_nil_solver.cpp` if you can reduce it to something a human can check
by hand.

## Benchmarking

```
build/bin/nil_bench --corpus tests/corpus/positions.txt --repeat 3
build/bin/nil_bench --corpus tests/corpus/positions.txt --csv before.csv
#   ... change the search ...
build/bin/nil_bench --corpus tests/corpus/positions.txt --baseline before.csv
build/bin/nil_bench --random --cards 7 --count 10 --seed 1     # timing only
```

`nil_bench` verifies as it times. A benchmark that does not check its answers
eventually reports a very fast wrong solver.

**Track nodes, not seconds.** Node counts are deterministic and machine
independent: the same corpus on the same commit gives identical numbers on your
laptop, on a CI runner, and on a loaded machine. Wall time does not. When we add
alpha-beta or move ordering, nodes per position is the number that measures the
search improvement rather than the machine it ran on. Time is reported too, and
is the check on the other failure mode — nodes fell but each node got more
expensive.

`--random` deals from a splitmix64 seeded by `--seed`, so it reproduces exactly
across compilers and platforms (unlike `std::mt19937` plus a distribution, whose
output is implementation defined). There are no expected answers in that mode,
so it is for timing only — useful for asking how a change scales at 7 cards,
where the oracle can no longer follow.

### Tracking improvements over time

`nil_bench --history <file>` appends a summary row instead of overwriting, so
one file accumulates the whole record. `scripts/run-bench.cmd` (or `.sh`) does
this by default, into `bench-history.csv` at the repository root:

```
scripts\run-bench.cmd "added alpha-beta"
python tools\bench_history.py
```

Each row records the commit, whether the working tree was dirty, the branch,
which corpus and how much of it, nodes, milliseconds, the memo setting, the
build configuration, the compiler, the host and your note. The commit comes from
`git` at run time, anchored to the corpus file's directory so it is right even
when `nil_bench` is invoked from elsewhere; `--commit` overrides it for CI.

`tools/bench_history.py` reads it back:

```
bench-history.csv   6 run(s) in 2 comparable group(s)

positions.txt, all hand sizes, 560 positions, memo on
  commit    when                       nodes   vs prev  vs first          ms  vs prev  note
  fd8d062   2026-08-09 01:21:44    77,476,932        -        -     30741.3        -  baseline
  a1b2c3d   2026-08-11 09:14:02    12,004,551   -84.5%   -84.5%      4102.7   -86.7%  alpha-beta
  best 12,004,551 at a1b2c3d   |   latest visits 6.45x fewer nodes than the first run
```

Two things it deliberately refuses to do, because a benchmark history that
quietly lies is worse than none:

* **It groups runs by what was actually measured** — corpus, hand size, position
  count, memo setting — and only computes node deltas within a group. A 400-position
  run and a 560-position run are two different measurements, not a 40% regression.
* **It only computes a time delta when the machine, build configuration,
  compiler and repeat count all match.** Otherwise the column reads `n/a`. Node
  counts still compare across machines, because they are deterministic; wall
  times do not.

Rows from a dirty working tree are marked with `*`, since they do not correspond
to a commit anyone can check out.

Commit `bench-history.csv` — the running total is the point, and it is more
useful in the repository than on one laptop. `.gitattributes` marks it
`merge=union` so two branches that have both benchmarked merge by keeping both
sets of rows rather than conflicting.

If you would rather have every `ctest` run logged as well, add `--history` to
the `corpus` test in `CMakeLists.txt`. It is off by default because CI and
throwaway local runs would then fill the file with rows nobody wants to read.

### One thing to know about seat parity

`nil_oracle.py` fixes its coalitions by seat parity: N and S always minimise the
designated player's tricks, E and W always maximise. That is exactly the nil
question when the designated player sits N or S, and a *different* question when
it sits E or W — there, the oracle has the designated player's own side trying
to hand it tricks.

This solver instead ties the coalitions to the nil bidder's seat, which is the
same thing up to a relabelling. So `crosscheck.py` rotates each deal until the
nil bidder sits North before asking the oracle, then rotates the PV's seat
labels back. Rotation moves no cards between hands and changes no rule, so the
answers must be identical.

Worth checking against the scratch position at the bottom of `nil_oracle.py`,
which designates E: the oracle as written reports 2 tricks, and the nil question
on the same layout is 1. Both are correct answers to different questions.

## The rule ambiguity

"Spades break when a spade is played on a trick where the player was void in the
led suit." Read literally, a *forced spade lead* — a player holding nothing but
spades leads one while spades are unbroken — does not break spades, because the
leader is not void in the led suit. Many implementations break spades anyway.

The literal reading is the default here, matching the oracle. Pass
`--break-on-forced-lead` (or `NIL_FLAG_BREAK_ON_FORCED_SPADE_LEAD`) for the
other convention. The cross-check exercises both.

## The memo

The search memoises on the **full** state: all four hands, the leader, the cards
on the current trick and the broken flag. It is a pure function of exactly that
state, so the cache changes neither the value nor the principal variation — it
is memoisation of a pure function, not alpha-beta or any other search
enhancement. There is still no pruning of any kind.

It is on by default because 6-card hands are otherwise slow. `--no-memo` (or
`NIL_FLAG_NO_MEMO`) turns it off; the cross-check passes either way.

## Not here yet

Alpha-beta with a `[0, 1]` window (the bool answer needs nothing wider), move
ordering, rank-equivalence collapsing, a real transposition table with
replacement policy, partial-hand caching, and 13-card support. All of that
belongs on top of a solver that is already known to be right.
