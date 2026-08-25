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
>
> **TERTIARY** — which of the nil side's two partners holds those tricks. Only
> the covering partner's tricks count towards the partner's bid, so among lines
> where the pair takes the same total, the pair prefers the nil bidder to take
> fewer.

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

The search is **correctness first**: exhaustive, with no alpha-beta, no move
ordering and no quick-trick shortcuts, validated card-for-card against
`nil_oracle.py`. It has two speed mechanisms, and neither of them prunes:

- a transposition table over a canonical position key that collapses
  rank-equivalent and trick-equivalent positions
  ([The transposition table](#the-transposition-table));
- an equivalent-card reduction in the move generator, which refuses to search
  the same move twice under two names
  ([Equivalent cards](#equivalent-cards)).

Both are exact — same answer, same principal variation, fewer nodes — which is
why the cross-check still runs against the search as shipped rather than against
some slower mode kept alive for the purpose. Together they are comfortable to
about nine cards a hand. Pruning comes next, on top of a solver we trust.

## Layout

```
include/nil_solver/nil_solver.h   the C ABI — the only thing the DLL exports
src/nil/cards.hpp|.cpp            cards, hands, canonical ordering
src/nil/rules.hpp                 the four rules plus the equivalent-card reduction
src/nil/position.hpp|.cpp         PBN parsing, validation, formatting
src/nil/search.hpp|.cpp           the minimax search and the PV replay verifier
src/nil/corpus.hpp|.cpp           loads tests/corpus/positions.txt
src/api.cpp                       C ABI implementation (marshalling only)
tools/nil_cli.cpp                 command line front end
tools/crosscheck.py               differential test against nil_oracle.py
tests/test_nil_solver.cpp         self-tests, mirroring the oracle's selftest()
tests/corpus/positions.txt        4-6 card positions, answers from the oracle
tests/corpus/large.txt            7+ card positions, see the provenance column
tools/nil_bench.cpp               corpus verifier and benchmark
tools/make_corpus.py              regenerates the corpus
tools/make_large_corpus.py        builds the 7+ card corpus
tools/invariants.py               transform-based checks, any hand size
tools/refresh_corpus.py           recompute rows an objective change can move
tools/bench_history.py            reads back bench-history.csv
tools/corpus_view.py              browse and spot-check the corpus
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

### Two modes

`--mode full` (the default) is everything above: trick counts, a principal
variation, and a value the replay verifier checks itself against. `--mode fast`
asks the boolean question on its own — can this nil be broken — and answers only
that:

```
$ nil_cli --pbn 'N:K.A.A.K 32.2..A .K.3.32 A.3.K2.' --leader N --nil E --mode fast
Objective      fast mode: the nil question only, no trick counts and no PV
Nil            FAILS  (can be forced to take a trick)
Nodes          456
```

The full objective packs the nil bidder's trick count into a scalar with two
tie-break levels underneath it, which makes the value span thousands and a
window on it worth almost nothing; every tie on the primary has to be searched
to the bottom anyway or the secondary comes out wrong. Fast mode zeroes those
levels and gives the primary weight 1, so the value *is* the nil bidder's trick
count and the window worth searching is `[0, 1]`.

**It is not faster yet.** Nothing prunes, and the transposition table keys on
the position rather than on the value, so both modes visit exactly the same
nodes — `nil_bench --mode both` prints `1.00x` and will keep doing so until
alpha-beta lands. What the split buys today is the shape: the fork exists in the
search and in the C ABI before there is a shipped interface to retrofit it
around.

In fast mode the three trick counts read `-1` and the compact output's `pv` is
empty; `mode=fast` on the first line is what says so, rather than leaving `-1`
to be guessed at. `--secondary` has no effect there — it points at a tie-break
level that mode does not have — and `--nil-already-set` makes the answer `1`
with no search at all, since it asserts the only thing fast mode computes.

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
| `NIL_FLAG_FAST_MODE` | answer `nil_fails` and nothing else |

`nil_side_tricks + opponent_tricks` always equals `tricks_remaining`, and
`nil_tricks` is included in `nil_side_tricks`. When you pass
`NIL_FLAG_NIL_ALREADY_SET`, `nil_fails` comes back as 1 because you said so, not
because the search worked it out.

Under `NIL_FLAG_FAST_MODE` the three trick counts come back as
`NIL_TRICKS_UNKNOWN` (`-1`), which is deliberately not `0`: zero is a real
answer to "how many tricks did the nil bidder take", and a caller that mistook
one for the other would read a broken nil as a made one. The count is withheld
rather than reported because the search stops computing it exactly — the roadmap
items after this one turn the fast value into a bound, and a caller who had come
to depend on the number would not find out.

`nil_solve_pv` also writes the principal variation as a string, and returns
`NIL_ERR_UNSUPPORTED` if you ask for one in fast mode: that mode never chooses
among lines that tie on the nil count, so there is no variation to give, and
quietly running the slow mode instead would be worse than saying so.
`nil_fails(...)` is a one-shot convenience wrapper returning 1, 0 or a negative
error code, and it selects fast mode for you — the boolean is its entire output.

For the C# wrapper, that is the shape of the split: `nil_fails` for "can this
still be broken", which is the question a game client asks on every trick, and
`nil_solve` without the flag when the score needs the actual counts.

### Every card, not just the answer

`nil_solve_moves` fills in the same `nil_result` and additionally writes one
`nil_move` row per legal card: the card, whether the nil fails after it, its
trick counts in full mode, whether it is one of the best, and the other cards it
stands for. Deliberately DDS-shaped, because the question is the same one and a
caller that already unpacks `futureTricks` rows can unpack these.

```
Legal cards for N:
  * S2  nil FAILS   N=2  side=3  opp=1
  * SQ  nil FAILS   N=2  side=3  opp=1   = SK
  * HA  nil FAILS   N=2  side=3  opp=1
```

`nil_cli --moves` prints exactly that, and is the fastest way to check a
position by hand before wiring it into anything.

Rows come one per equivalence class rather than one per card. Holding SK and SQ
with the jack gone, the two are one move under two names, and the `equal_ranks`
bitmask on the queen's row names the king. The encoding is DDS's, including the
part that trips people up: the card's own rank is not set. A caller choosing a
move ignores the mask entirely — the members of a class are interchangeable,
which is the whole reason only one of them was searched.

A caller that has to line up against the cards in a hand wants them expanded, and
the C# wrapper does that for you: `NilSolution.AllMoves` and `.BestMoves` are one
entry per legal card, the analogues of DDS's `DDSSolution.AllMoves` and
`.BestMoves`.

**What it costs**, because the number is better than the shape of the question
suggests: the position is solved first and every card is then scored against the
same transposition table, so the per-card searches spend most of their time
reading back work the first search already did. Over twelve random thirteen-card
deals in fast mode it came to 1.0x the nodes and +0.4% of the wall time of the
plain call. The cases that cost anything are the ones the plain call answered by
proof without looking at a card: there the position is free and the list is not,
1 node against 79.

## From C#

`csharp/` holds a working wrapper — three files, no NuGet package, no build step
on the C# side:

| file | what it is |
| --- | --- |
| `NilSolverNative.cs` | the C ABI transcribed one-to-one |
| `NilSolver.cs` | the managed face: error text, PV buffer sizing, move lists, fast and full as named methods |
| `NilSolverPool.cs` | dedicated solver threads, so per-thread tables stay bounded on a server |

```csharp
var r = Nil.CanBeBroken(pbn, NilSeat.North, currentTrick: null, NilSeat.North,
                        NilFlags.SpadesBroken | NilFlags.ForceLarge);
if (r.Success) Console.WriteLine(r.NilFails ? "the nil is dead" : "the nil survives");
```

The calling convention is `__cdecl` on every platform, and the export surface is
exactly the header (visibility is hidden by default), so there is nothing to
name-mangle around.

One thing worth knowing before wiring this into a request path, because it is
the opposite of the usual native-library caveat: **the solver needs no locking
and no thread-id argument**, so concurrent calls are safe as they stand. What it
does need is a bound on *how many threads ever call it*, since the transposition
table is `thread_local` and a thread that has solved once holds 32 MiB until it
exits. `csharp/README.md` covers that, along with deployment, the flags, and the
three P/Invoke declarations that are easy to write wrongly.

### Building the DLL in Visual Studio

`Nil-Solver.slnx` builds the same library as CMake, to
`build-vs\bin\x64\Release\nil_solver.dll`. Use **Release**: this is a search, and
a Debug build is not marginally slower.

The two trees are deliberately separate — `build\` for CMake, `build-vs\` for the
IDE — so neither can overwrite the other's DLL and leave you guessing which one
got loaded.

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
| `corpus_modes` | every corpus position solved in both modes, required to agree on `nil_fails` | nothing |
| `invariants` | transformed copies of each position that must give the same answer | Python |
| `crosscheck` | live differential test against the oracle on freshly generated positions | Python + `nil_oracle.py` |
| `corpus_large` | the 7-card rows of `tests/corpus/large.txt`; **off by default** | nothing |

Run one at a time with `ctest -R corpus_quick`, or invoke the binaries directly:

```
build/bin/nil_tests                                        # verbose
build/bin/nil_bench --corpus tests/corpus/positions.txt    # verify + time
python3 tools/crosscheck.py --exe build/bin/nil_cli --cases 200 --cards 4
```

### Seeing and checking the tests yourself

The three layers are all readable, and none of them hides its work.

**Unit checks.** `build/bin/nil_tests` prints every assertion by name, passing or
not; `python3 nil_oracle.py --selftest` does the same on the oracle side. Both
are ordinary source files — `tests/test_nil_solver.cpp` and the `selftest()`
function in `nil_oracle.py` — and the interesting cases carry a comment
explaining why the expected number is what it is. Read those first; they are the
ones a person can check by reasoning rather than by running something.

**The corpus.** `tools/corpus_view.py` makes the 560 lines browsable:

```
python3 tools/corpus_view.py                     what is in there
python3 tools/corpus_view.py --list --cards 4 --outcome makes
python3 tools/corpus_view.py --show c4-0001      expand one record
python3 tools/corpus_view.py --random 5          spot-check five at random
python3 tools/corpus_view.py --simplest 10       the ones easiest to check by hand
```

`--show` prints the four hands laid out by suit, the settings in English, the
recorded answer, the line trick by trick, and the exact `nil_cli` and
`nil_oracle.py` commands that reproduce it. It then re-runs `nil_cli` and says
whether the solver still agrees. Add `--verify` to recompute the position from
scratch with the oracle as well. It exits non-zero on any disagreement, so it
works in a script.

Be clear about what each check is worth:

* Re-running `nil_cli` is a **regression** check. It proves the C++ still says
  what the oracle said when the corpus was built — both numbers trace back to
  one source.
* `--verify` is a **second implementation** check. Better, but still two
  programs, and they were written to the same understanding of the rules.
* The only **independent** check is you reading the hands and the trick list and
  deciding whether the line is sensible. `--simplest` exists for that: it sorts
  by fewest cards, no mid-trick resumption, and fewest suits in play, so the top
  of that list is where hand-verification is actually feasible.

**What checks fast mode.** Full mode carries its own witness: it replays the
principal variation it produced, counts the tricks independently, and requires
re-packing them to land back on the search value. Fast mode has no principal
variation, so it has no such witness — what stands in is that the two modes must
agree on `nil_fails` for every position, which is what `corpus_modes` and
`nil_bench --mode both` check. That agreement is free evidence while nothing
prunes; once alpha-beta lands it is the only thing between a pruning bug and a
confidently wrong boolean, so it is worth keeping in the default test run rather
than reaching for it when something already looks wrong.

If you find a position where the answer looks wrong, the fastest thing to do is
paste the two reproduce lines and compare the full outputs — `nil_cli` marks the
winner of every trick, which is usually where the disagreement becomes obvious.
Anything real should then become a named case in `tests/test_nil_solver.cpp` and
in the oracle's `selftest()`, not just another corpus row.

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

### Large hands, and what can actually be verified up there

Cost grows roughly sixfold per extra card, and the oracle is Python with a
dictionary for a transposition table. On one core it is fine to about six cards
and then falls over — on a typical 7-card deal it runs out of *memory* before it
runs out of patience. The C++ side goes further than the oracle can follow,
which is why the invariance checks below exist.

But cost also varies by two orders of magnitude *within* a hand size. Random
7-card deals measured here ranged from one million nodes to seventy-eight. The
cheap ones are the skewed distributions, where following suit is forced most of
the way down and the tree never fans out. They are ordinary Spades endings, just
not average ones — and they are the ones both implementations can still solve.

`tests/corpus/large.txt` exploits that. Rows carry a `provenance` column:

| provenance | what it is worth |
| --- | --- |
| `oracle` | `nil_oracle.py` solved it independently. Real verification. |
| `solver` | pinned from this solver. Catches a future change; if the answer is wrong today it is confidently wrong forever. |
| `unverified` | no answer recorded. The row exists to be timed. |

`nil_bench` prints the breakdown after a corpus run, so a green result cannot be
mistaken for proof. The 4–6 card corpus is entirely `oracle`, and rows there
default to it when the column is absent.

Regenerate or extend with `tools/make_large_corpus.py`, which screens candidate
deals under a wall-clock cap, oracle-verifies the cheap ones, pins the middling
ones and records the rest as timing-only:

```
python3 tools/make_large_corpus.py --cards 7 --verify 4 --out tests/corpus/large.txt
python3 tools/make_large_corpus.py --cards 8 --suits 3 --pin 2 --append --out tests/corpus/large.txt
python3 tools/make_large_corpus.py --cards 9 --suits 3 --timed 3 --append --out tests/corpus/large.txt
```

`--suits 3` deals from three suits instead of four, so hands are longer and
voids rarer, which means following is forced more often and the tree fans out
less. At 8 and 9 cards that is the difference between a position anything can
solve and one nothing can. Spades are always in play, so the trump and breaking
rules stay exercised.

Nine- and thirteen-card rows can only be timing-only until there is pruning. No
implementation here can produce an expected answer for them today; they are
benchmark fodder for the road to 13.

### Invariance checks

These are the only verification that works at any hand size, because they need
no reference answer. Take a position, transform it into a different position
that must have the same answer for reasons that come from the rules rather than
from either implementation, and compare.

```
python3 tools/invariants.py --corpus tests/corpus/positions.txt --cards 4 --limit 40
python3 tools/invariants.py --corpus tests/corpus/large.txt --cards 7 --timeout 60
python3 tools/invariants.py --random 20 --cards 5
```

* **Seat rotation** — move every hand k seats and move the leader and the nil
  bidder with them. Nothing has changed but the names, so every count must be
  identical and the principal variation must be the same cards played by
  correspondingly renamed seats.
* **Suit permutation** — permute hearts, diamonds and clubs. Spades are the only
  suit the rules single out (they trump, and they have the breaking rule), so
  the other three are interchangeable and every count must be identical. The PV
  may legitimately differ, because relabelling changes which card is
  canonically lowest.
* **Rank compression** — relabel the ranks in play down to a contiguous block,
  order preserved. Only relative order can matter, so counts must be identical
  and the PV must correspond card for card.

This is what makes a `solver`-provenance row worth something: a pinned answer
checked by asking the same solver again catches a regression and nothing else,
but a pinned answer that also survives three transforms has been checked against
the rules themselves.

The checks have teeth — swapping spades with hearts instead of permuting the
other three produces violations immediately, which is the sanity check that they
are not vacuous.

**One thing they turned up.** With `--nil-already-set` *and* `--secondary min`,
the two partners are interchangeable — bags accrue to the pair whoever won — so
nothing in the objective decides the split, and it falls out of the tie-break.
`nil_tricks` may therefore move under a suit permutation while
`nil_side_tricks` and `opponent_tricks` hold firm. Everywhere else the split is
pinned: by the primary while the nil is live, and by the tertiary level once it
is set and the pair is taking tricks.

### The live cross-check

Drop `nil_oracle.py` in the repository root and run:

```
python3 tools/crosscheck.py --exe build/bin/nil_cli --cases 200 --cards 4
python3 tools/crosscheck.py --exe build/bin/nil_cli --cases 60  --cards 5 --trick-prob 0.5
python3 tools/crosscheck.py --exe build/bin/nil_cli --cases 20  --cards 6
python3 tools/crosscheck.py --exe build/bin/nil_cli --cases 60  --cards 4 --no-memo --oracle-no-memo
```

`nil_cli` also takes `--no-collapse`, which turns the equivalent-card reduction
off and enumerates every legal card. It is much slower and must produce
byte-identical output; a divergence between the two says the reduction has
started collapsing cards that are not actually equivalent.

`--no-static` is the same idea for the two static bounds. In fast mode the
solver answers a position outright when the nil bidder provably cannot be forced
to take a trick, or provably must take one (see `src/nil/bounds.hpp`); this flag
makes it search for the answer instead. Both must give the same `nil_fails`, and
the proofs are one-sided, so turning them on may only remove nodes and never
add one. `ctest` runs both arms over the whole corpus — `corpus_modes` with them
on, `corpus_static` with them off — and both are checked against the oracle's
recorded answers. The flag is inert outside fast mode, which is the only mode
the proofs apply to.

`--no-narrow` is the control arm for window narrowing (roadmap item 22), and it
is the one with the most riding on it. Full mode narrows its window as a node's
moves come back, which is what makes the alpha-beta cutoff reachable there at
all; this flag turns that off and restores the exhaustive minimax full mode ran
before patch 22. Both arms must agree on the value **and** on the principal
variation, which is why `corpus_narrow` carries `--check-pv` where the other
control arms do not: narrowing is answer-neutral by argument rather than by
measurement, and an argument is the kind of thing that stops being true quietly.
Expect roughly six times the nodes with it on, and about seventy times at nine
cards. Inert in fast mode, whose window is null — there is no integer strictly
between alpha and alpha + 1 for a narrowed bound to land on, so a fast search is
unchanged node for node either way.

`--no-presolve` is the control arm for the presolve-seeded root window (roadmap
item 23). A full solve of eight tricks or more first runs the same position in
fast mode, and if the nil cannot be forced the packed value cannot reach the
range where it can, so beta closes onto the answer instead of sitting at a
sentinel. Same value and same principal variation — the bound comes from the
objective's own weights rather than an estimate — so the two arms must agree on
both, which is why `corpus_presolve` carries `--check-pv`. The presolve is paid
whether or not it collects: it is a 3% tax on positions where the nil fails, and
worth it because the tax is bounded by one fast search while the saving is not.
Gated off below eight tricks, where it was measured as pure overhead.

`--no-tt-narrow` is the control arm for the cutoff bound taken off a partial
table match (roadmap item 41). A partial is an entry that describes the position
but does not settle the window — "the value is at least x" asked about a window
reaching past x. The solver used to discard it; it now spends it on the
threshold the node's own cutoff test reads, when that is tighter than the one
the window carries. The window itself is left alone, so children are searched
under exactly what the caller asked and nothing about their stored entries
changes. That restriction is the whole item and it was measured, not reasoned:
tightening the window so that it propagates down the subtree costs 5.5% of the
tree at 11 cards. Both arms must agree on the value **and** the line, so
`corpus_tt_narrow` carries `--check-pv` — the item cuts a node short on the
strength of a stored bound, and the argument that this is allowed is that the
value it stops at is squeezed exact between that bound and fail-soft's. A search
that returned a bound where the PV walk expects a value would still produce the
right number at the root and the wrong line under it. Inert in fast mode, which
has no partial entries at all: every node there is asked about `[0, 1]` and
every value it stores is a bound at one end or the other, so every match settles
its window.

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

`--mode full` (default) checks each corpus row's trick counts. `--mode fast`
checks only `nil_fails`, because that is all fast mode computes. `--mode both`
runs each position twice, requires the two to agree, and reports the fast run:

```
$ build/bin/nil_bench --corpus tests/corpus/positions.txt --mode both
  mode check: 560 position(s) solved both ways, 130 answered without searching (nil already set)
    nodes over the 430 searched:  full 2,151,487  fast 2,151,487   1.00x
```

The nodes are compared over the searched positions only: fast mode answers a
nil-already-set row without looking at a single card, and counting those zeroes
against full mode's real work would report a speedup that is nothing of the
kind. The memo column gains `+fast` for the same reason `--no-collapse` gains
`+nocollapse` — a fast row sitting next to a full row in the history would read
as a win that never happened.

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

positions.txt, all hand sizes, 560 positions, memo 32mb
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

## The forced spade lead

"Spades break when a spade is played on a trick where the player was void in the
led suit." Read literally, a *forced spade lead* — a player holding nothing but
spades leads one while spades are unbroken — does not break spades, because the
leader is not void in the led suit. Many implementations break spades anyway.

This used to be the one genuine ambiguity in the solver, selectable on both
sides so the oracle and the C++ could be configured to agree. It is gone:
**playing a spade breaks spades**, with no cases and nothing to select. A hand
forced to lead a spade breaks them. The literal reading produced deals that
could be played to the last trick with spades still nominally unbroken, which is
not how the game is scored anywhere this runs, and carrying both readings cost a
parameter on every interface from `spades_broken_after` to the C ABI to the
corpus format. `NIL_FLAG_BREAK_ON_FORCED_SPADE_LEAD` is retired and its bit
`0x2u` is burned rather than recycled, so an old caller passing it gets an
ignored flag rather than a silently different objective.

The re-derivation is recorded because it is a rules change and rules changes
move answers: 261 of the 560 corpus rows were computed under the literal
reading, and **none of their answers moved**. A forced spade lead needs a hand
holding nothing but spades while on lead, which those constructed endings never
reach. The `large.txt` rows did not move either.

## Spades cannot be broken before a spade is played

`validate()` rejects a position that claims broken spades while all thirteen are
still in play — in the four hands plus the current trick. Breaking spades means
one has been played, and a full 13-card deal has had no card played at all.
Below thirteen the count proves nothing, because a smaller layout is a
constructed ending rather than a played-down one: the absent spades were never
dealt, so those positions may legitimately start broken.

This is worth rejecting rather than tolerating, because such a position is not
merely unreachable, it is expensive. Unbroken spades forbid a voluntary spade
lead, which prunes hard near the root; setting the flag on a full deal throws
that away and searches a game nobody can play. Three rows in `large.txt` carried
it, including one at nine cards, and clearing them took the 13-card benchmark
leg from 290M nodes to 163M without moving a single value.

## Equivalent cards

With the jack already played, holding `SK SQ` is not two moves. It is one move
available under two names: every card still in existence is either above both or
below both, so playing the king and playing the queen reach positions that
differ only by swapping two labels. Searching both is searching the same tree
twice, and the second time is free of information.

The move generator therefore emits one representative per class. `src/nil/rules.hpp`
does it in three pieces:

**What counts as relevant.** A card's rank matters only if something can still
be compared against it. Cards from finished tricks are gone. So are the *losing*
cards of the trick in progress — `beats` is only ever asked about the running
best, so nothing will be compared against them again. The card currently
*winning* is the exception: a hand card above it takes the trick and one below it
does not, so it separates ranks that would otherwise be interchangeable. That
one card, plus everything still in the four hands, is the relevant set.

**Finding the classes.** A class is a run of legal cards that is contiguous in
the relevant set, so a card is redundant exactly when the next relevant card
*below* it is also a legal move. Read downwards that is a search per card. Read
upwards it is a fill: flood from every candidate through the ranks no relevant
card occupies, step once more, and whatever the step lands on is the first
relevant card above it. A candidate landed on this way has an equal, lower twin
already in the set. Four widening shifts carry a bit up to fifteen ranks — past
the widest gap thirteen ranks can hold — and `SUIT_PADDING`, the three unused
bits above every suit, keeps the fill from walking out of the spades and into
the hearts.

**Why it is exact.** Let `X > Y` be two cards of one suit in the mover's hand
with no relevant card between them, and let *s* be the relabelling that swaps
them. Nothing relevant separates them, so *s* preserves the order of every card
the game can still compare, and it maps the position after playing `X` onto the
position after playing `Y`. The rules read nothing else: follow-suit sees suits,
the break rule sees suits, and who wins a trick is decided by comparisons alone.
The two positions have the same value.

**Why the principal variation survives.** Candidates are enumerated from the
bottom and replace the incumbent only on a strict improvement, so the
canonically lowest of several equal-valued moves already wins. The card dropped
here is always the *higher* member of a pair that scores identically, which
could never have displaced it. This is the property that keeps the card-for-card
cross-check against `nil_oracle.py` alive, and it is the last optimisation on the
roadmap that has it — move ordering will legitimately pick a different one of
several equal cards.

**Why the transposition table stays consistent.** A stored move is a slot index,
read back against a possibly different position with the same key, so the
reduction has to be a function of the *key* rather than of the literal cards. It
is: the key already records each suit's live cards in order, and the winning
card of the trick as `gap`, its position among them. Those are exactly the two
ingredients above.

Node counts, against `--no-collapse` on the same binary:

| workload | all legal cards | one per class | change |
|---|---:|---:|---:|
| corpus, 4 cards | 1,274 | 1,038 | −18.5% |
| corpus, 5 cards | 9,500 | 7,462 | −21.5% |
| corpus, 6 cards | 44,309 | 33,423 | −24.6% |
| random, 7 cards | 615,474 | 425,937 | −30.8% |
| random, 8 cards | 3,052,071 | 2,019,207 | −33.8% |
| random, 9 cards | 51,860,711 | 23,519,702 | −54.7% |

The ratio climbing with hand size is the point: longer suits hold longer runs,
and the saving compounds down the tree rather than being taken once at the root.
Nine-card deals went from about 45 seconds to about 23. Per-node throughput
costs a few percent, most of which comes back from skipping the reduction
outright at nodes with only one legal card — common in the deep endgame, where
most of the nodes are.

## The transposition table

The search is a pure function of the position, so a cache over it can change the
node count and nothing else. What matters is **what counts as the same
position**, and the answer is in `src/nil/statekey.hpp`.

The first version keyed on the literal state — four 64-bit hand masks plus a
packed word, 288 bits — in an `unordered_map`. That is exact, and it is also
the most verbose possible key: it calls two positions different whenever any
irrelevant detail differs. The key now records only what actually determines
the value.

**Relative ranks, not absolute ones.** Once the `SK` and `SQ` are gone, holding
`SA SJ` is the same position as holding `SA SK`. Each suit is compressed to the
cards still in the four hands, numbered from the bottom, and only the *owner* of
each surviving slot is stored — two bits per live card. This is where nearly
all of the collapsing comes from.

**The trick as a threshold, not as cards.** Cards already played are out of
every hand; the only thing the rest of the deal needs from them is the suit led,
who is winning, and `gap` — how many live cards of that suit the winning card
beats. Two tricks that were played with different cards but trap the same number
of survivors are one position, which the literal key could never see.

The result is 21 + 2n bits at the start of a trick and 30 + 2n inside one, where
n is the number of cards still in hands. A five-card ending is 61 bits: the
whole endgame, which is where all the nodes are, keys into a single machine
word.

Positions live in a fixed-size table — four-way buckets, evict the shallowest
entry, generation-stamped so consecutive solves cannot see each other's values.
Entries store the full 128-bit key, so a hash collision costs one comparison and
never an answer. `--tt-mb` sets the size (default 32); `--no-memo` or
`NIL_FLAG_NO_MEMO` turns the table off entirely and the cross-check passes
either way.

Against the 560-position corpus, versus the old full-state memo:

| cards | nodes/position before | after | ratio |
|------:|----------------------:|------:|------:|
| 4     | 7,085                 | 1,274 | 5.6x  |
| 5     | 125,346               | 9,500 | 13.2x |
| 6     | 1,163,389             | 44,309| 26.3x |

The multiplier grows with hand size, which is the point. Randomly dealt hands
now run about 0.5 M nodes at seven cards and 2 M at eight.

**Deliberately not collapsed: the side suits.** Hearts, diamonds and clubs are
interchangeable — only spades are special — so canonicalising them is a genuine
symmetry of the game and would in principle collapse up to six more positions
into one. It was implemented and measured, and it does not pay:

| | nodes/position now | suit-canonical | change |
|---|---:|---:|---:|
| 6 cards, corpus | 44,309 | 41,942 | −5.3% |
| 7 cards, random | 484,469 | 447,316 | −7.7% |

Node counts are exact; the sort costs roughly 12% of throughput because it runs
at every node, so wall time came out slightly *worse*.

Six times theoretical becomes seven percent actual because every player's
holding in a suit is a subset of what they were dealt: one line's residual
hearts can only look like another line's residual diamonds once both suits are
nearly exhausted, which is where the subtrees are cheap anyway. Rank compression
has already taken the structural win. And when a non-spade is led, that suit is
pinned by the trick and only two suits are free, so most nodes see a group of
order two rather than six.

There is a correctness hazard on top of that. The move order is suit-major, so
the tie-break between two equally good cards in *different* suits depends on
which suit is which; a move read back from an entry stored under a permuted
labelling can be the other equally good card. Legal, optimal, replay-verified —
but not the card `nil_oracle.py` picks. The measurement did not trip it (560
corpus positions and 150 random deals still agreed on the PV), which is
reassuring rather than conclusive.

**A note on benchmarking.** The table is bounded, so once a search overflows it
the node count depends on the table size. `nil_bench` records the size in the
history file's `memo` column, which `bench_history.py` already groups on, so
runs made at different sizes are never compared as regressions.

## Not here yet

Alpha-beta with a `[0, 1]` window, quick-trick evaluation, move ordering,
side-suit symmetry, and 13-card support. Two of the pieces that has to sit on
are in: `TTEntry` already carries a `bound` field so that adding alpha-beta is a
change to the search rather than to the table format, and `--mode fast` already
gives the window something to be a window *on* — with the tie-break levels
zeroed, the search value is the nil bidder's trick count and nothing else, so
`[0, 1]` is the literal window rather than a slice of a scalar spanning
thousands. See `ROADMAP.md` for the order the rest is planned in.
