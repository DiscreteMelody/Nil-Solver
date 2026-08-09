# Nil-Solver

A double-dummy solver for the one question that matters when someone bids nil:

> **Can the opponents force the nil bidder to win a trick, assuming the nil
> bidder and its covering partner play perfectly to prevent it?**

That is not trick maximisation. Both opponents are trying to push a trick onto
one specific seat and will happily throw away tricks of their own to do it; the
nil bidder and its partner are trying to stop them. The two coalitions have
exactly opposed objectives over a single scalar — the nil bidder's trick count —
so plain minimax is well defined and no notion of "own tricks" appears anywhere
in the search.

The answer comes back as a bool. `nil_fails == true` means the nil is dead
however it is played from here.

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
src/api.cpp                       C ABI implementation (marshalling only)
tools/nil_cli.cpp                 command line front end
tools/crosscheck.py               differential test against nil_oracle.py
tests/test_nil_solver.cpp         self-tests, mirroring the oracle's selftest()
```

## Building

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

On Windows with Visual Studio:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Everything lands in `build/bin`:

| artefact | what it is |
| --- | --- |
| `nil_solver.dll` (`libnil_solver.so`) | the shared library for C# |
| `nil_solver.lib` | import library, in `build/lib` |
| `nil_cli` | command line front end |
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
    /* r.nil_fails, r.tricks, r.tricks_remaining, r.nodes */
}
```

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

## Checking against nil_oracle.py

Drop `nil_oracle.py` in the repository root and run:

```
python3 tools/crosscheck.py --exe build/bin/nil_cli --cases 200 --cards 4
python3 tools/crosscheck.py --exe build/bin/nil_cli --cases 60  --cards 5 --trick-prob 0.5
python3 tools/crosscheck.py --exe build/bin/nil_cli --cases 20  --cards 6
```

The harness generates random positions — random leader, random nil seat, random
broken flag, and roughly a third of them resumed mid-trick — and compares three
things: the bool, the exact trick count, and the **principal variation, card for
card**. The third is the one that earns its keep. Two implementations can agree
on every value while disagreeing about a rule that happens not to bite in the
sampled positions; they cannot agree on the PV by accident, because both sides
break ties identically (canonically lowest card, strict improvement only).

`ctest` runs a small cross-check automatically when Python is available, and
skips rather than fails if the oracle is not present.

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
