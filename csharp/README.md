# Using the solver from C#

Three files, no NuGet package, no build step on the C# side. Copy them into your
project, put `nil_solver.dll` where the runtime can find it, and call it.

| file | what it is |
| --- | --- |
| `NilSolverNative.cs` | the C ABI transcribed one-to-one. Change it only when the header changes |
| `NilSolver.cs` | the managed face: error text, PV buffer sizing, fast and full as named methods |
| `NilSolverPool.cs` | dedicated solver threads, so per-thread tables stay bounded on a server |

`NilSolverPool.cs` is optional for a desktop app and is the whole point on a web
server; see [Threading](#threading-and-the-per-thread-table) before deciding.

## Getting the DLL

Either build system produces the same library. Use whichever is already on the
machine.

**CMake** — the reference build, and what CI runs:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

`build\bin\nil_solver.dll` and `build\lib\nil_solver.lib`. On Linux and macOS,
`build/bin/libnil_solver.so`. `scripts\build-and-test.cmd` does the same thing
and finds CMake on its own, including the copy inside Visual Studio.

**Visual Studio** — open `Nil-Solver.slnx`, pick **Release** and **x64**, Build:

`build-vs\bin\x64\Release\nil_solver.dll`.

Two notes on that second path. It is a *different* directory from the CMake one
on purpose, so an IDE build and a command-line build cannot overwrite each
other's DLL and leave you guessing which one is loaded. And **Release matters**:
this is a search, and a Debug build is not a bit slower, it is slow enough to
change what the library is for.

> If you have an older checkout, the `.vcxproj` there was the stock Visual Studio
> "Dynamic-Link Library" template — `dllmain.cpp` and `pch.cpp` and none of the
> solver. It built cleanly and produced a DLL with **no exports at all**, which
> fails at the first `DllImport` with `EntryPointNotFoundException` rather than
> at build time. If that is what you are looking at, take this version of the
> project file.

`x64` unless you have a specific reason: a 32-bit DLL cannot be loaded by a
64-bit process, and IIS application pools are 64-bit by default.

## Putting it where the runtime will find it

`DllImport("nil_solver")` resolves to `nil_solver.dll` on Windows and
`libnil_solver.so` elsewhere, so one name covers both. .NET looks next to the
managed assembly first, which for a web app is the `bin` folder of the published
output. Copy the DLL into your project and mark it:

```xml
<ItemGroup>
  <None Update="nil_solver.dll">
    <CopyToOutputDirectory>PreserveNewest</CopyToOutputDirectory>
  </None>
</ItemGroup>
```

The DLL links the dynamic CRT, so the target machine needs the **Visual C++
redistributable**. Servers usually have it already. If you would rather not
depend on that, set `RuntimeLibrary` to `MultiThreaded` in `Nil-Solver.vcxproj`
(or `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` with CMake) and the DLL becomes
self-contained at the cost of a slightly larger file.

## The shortest thing that works

```csharp
using NilSolver;

var r = Nil.CanBeBroken(
    pbn:          "N:8.K5.KT2.KQT9762 AQT643.T.QJ864.8 J9.A943.73.AJ543 K752.QJ8762.A95.",
    leader:       NilSeat.North,
    currentTrick: null,
    roles:        NilSeatRoles.ForNil(NilSeat.North),
    flags:        NilFlags.ForceLarge);

if (!r.Success) throw new InvalidOperationException(r.Error);
Console.WriteLine(r.NilsSet ? "the nil is dead" : "the nil survives");
```

## Building the arguments

**`pbn`** is the usual PBN deal string: `spades.hearts.diamonds.clubs`, hands
running clockwise from the named seat, any seat may lead. If you already build
one for a bridge DDS, the same string works here — but see the two differences
below.

**`leader`** is the seat that *led the current trick*, not the seat to play. Same
convention as DDS's `first`.

**`currentTrick`** is the cards already played to the trick in progress, in play
order starting from the leader: `"H4 HK"`. `null` or `""` for the start of a
trick. At most three.

**`roles`** says what each seat is doing, and it is the whole question — there is
no DDS equivalent. For the ordinary case, `NilSeatRoles.ForNil(seat)` builds it:
that seat bidding, its partner covering, the other pair opposing. Once the nil
has actually been broken in the real game, `NilSeatRoles.ForNil(seat, alreadySet:
true)` gives that seat `NilSeatRole.NilSet`, which drops the primary objective and
optimises only the tie-break. That replaced the retired `NilFlags.NilAlreadySet`.

`NilSeatRoles` is held by absolute seat, so North is North whatever the deal
string says, and the wrapper rotates it into the deal's own order for you. You
only need `NilSeatRoles.FromPbnOrder(pbn, ...)` if you already have the four
values clockwise from the seat the PBN names — which is the order the native ABI
and the corpus both use.

Two nils on the table is a legal spades deal and the shape this type exists to
grow into, but the solver does not answer it yet: a roles value holding two comes
back as `NilStatus.Unsupported` rather than as a wrong answer. Everything else
malformed — no nil, two covers, a cover beside the nil rather than across from it
— is `NilStatus.IllegalPosition`.

Two things about the deal string that the position validator will catch, and
which are worth knowing before it does:

* **Cards already on the trick must not also be in the hands.** Same rule as
  DDS's `remainCards`. If you are reusing a DDS deal builder, this part already
  works.
* **Hand sizes must be consistent with the trick.** Everyone held the same number
  of cards when the trick started, so the seats that have already played hold one
  fewer. Get this wrong and you get `IllegalPosition` with a sentence naming the
  four counts, which is usually enough to find the bug.

Seat numbering is `N=0, E=1, S=2, W=3` — identical to DDS, so if you have a
CGA-seat-to-DDS-seat conversion it carries over unchanged.

## Which call to make

| you want to know | call | reaches |
| --- | --- | --- |
| can this nil still be broken? | `Nil.CanBeBroken` | 13 cards, with `ForceLarge` |
| what is the score if we play on? | `Nil.SolveFull` | 9 cards, more with `ForceLarge` |
| show me the line | `Nil.SolveWithLine` | as above |
| which of my cards keep the nil alive? | `Nil.ScoreMoves` | 13 cards, with `ForceLarge` |
| ...and what does each one cost? | `Nil.ScoreMovesFull` | 9 cards, more with `ForceLarge` |

`CanBeBroken` runs the pruned boolean search — an AND-OR search on a `[0, 1]`
window — and answers a full thirteen-card hand in a median 150 ms. It reports
`NilsSet` and nothing else: the three trick counts come back as `-1`, which is
deliberately not `0` because zero is a real answer to "how many tricks did the
nil bidder take". Check `TrickCountsKnown` before reading them.

`SolveFull` is exhaustive and answers everything, and its cost grows about
sixfold per card. Nine cards a hand is the point where it stops being
interactive, which is why that is the limit it enforces. Both modes always agree
on `NilsSet`; that agreement is a test that runs on every build.

### The card limit is not a mode thing

Both entry points refuse more than nine cards a hand without
`NilFlags.ForceLarge`, including the fast one. For a game client that is a
thirteen-card opening lead, so the flag is not exceptional — pass it, and expect
a median that is fast and a tail that is not. Over twenty random thirteen-card
deals the spread ran from 120 ms to 8.6 seconds, and one deal was 80% of the
work in the run. Budget for the tail, not the median.

## Every card, not just the answer

`ScoreMoves` returns the DDS-shaped answer: one row per legal card, with whether
the nil survives it. `ScoreMovesFull` adds each card's trick counts. This is what
you want for scoring a player's actual choice, or for colouring a hand so a
learner can see which cards were safe.

```csharp
var r = await _pool.ScoreMovesAsync(pbn, leader, trick, roles,
                                    NilFlags.SpadesBroken | NilFlags.ForceLarge);

foreach (var m in r.Moves)
{
    Console.WriteLine($"{m}  {(m.NilsSet ? "throws it away" : "safe")}"
                    + (m.IsBest ? "  <- best" : ""));
}
```

`Moves` is one row per equivalence class. `AllMoves` and `BestMoves` are the same
information expanded to one entry per legal card, which is usually what a UI
wants — see below.

Read `NilsSet` from whichever side you are on: for the nil bidder or its
covering partner, false means the card holds the nil together; for an opponent,
true means it breaks it. One fact rather than two, because a double dummy answer
does not depend on who asked.

**Cost.** Less than it looks. The position is solved first and every card is then
scored against the same transposition table, so the per-card searches spend most
of their time reading back work the first search already did. Measured over
twelve random thirteen-card deals in fast mode: **1.0x the nodes, +0.4% wall**
against the plain `CanBeBroken`. The only cases that cost anything are the ones
the plain call answered by proof without looking at a card — there the position
is free and the list is not, though at 1 node against 79 that is not a number to
plan around.

### Three lists, and which one you want

`Moves` comes back one row per *equivalence class*, not per card. With the jack
already played, holding the king and the queen is one move under two names —
every card still in existence is above both or below both, so the two plays reach
positions that differ only by swapping two labels. The solver searches one and
names the other in `EqualRanks` rather than searching the same tree twice.

| property | one entry per | use it for |
| --- | --- | --- |
| `Moves` | equivalence class | choosing a card — class members are interchangeable |
| `AllMoves` | legal card | lining up against the cards in a player's hand |
| `BestMoves` | legal card, best only | highlighting every card that holds the nil |

`AllMoves` and `BestMoves` are the analogues of DDS's `DDSSolution.AllMoves` and
`BestMoves`, and they are populated the same way — each row contributes its own
card plus one entry per rank in its `EqualRanks`:

```csharp
foreach (var card in r.AllMoves)
    Highlight(card.Suit, card.Rank, safe: !card.NilsSet, best: card.IsBest);
```

Both are computed on first read, so a solve that never asked for a move list pays
nothing, and both come out in canonical order — spades, hearts, diamonds, clubs,
ascending by rank. Cards from one class are genuinely indistinguishable in them:
same `NilsSet`, same counts, same `IsBest`. That is what being in a class means.

`BestMoves` is derived from the solver's own `IsBest` rather than by comparing
each card's score against the position's, which is how the DDS wrapper does it.
Same answer, and it does not go wrong in fast mode, where there are no per-card
scores to compare.

If you would rather expand it yourself — porting a loop written against DDS's
`equals` bitfield, say — the convention is DDS's, including the part that trips
people up: the card's **own rank is not in `EqualRanks`**.

```csharp
foreach (var m in r.Moves)
{
    Emit(m.Suit, m.Rank, m.NilsSet);                       // the card itself
    foreach (var rank in m.EqualRanks) Emit(m.Suit, rank, m.NilsSet);
}
```

`m.Expand()` gives the same ranks in one call. Entries in `AllMoves` have
`EqualRanks` cleared, on purpose: the grouping is already materialised there, and
leaving the mask on would let the same class be expanded twice.

`NilFlags.NoCollapse` turns the reduction off and gives a row per legal card with
`EqualRanks` always empty, so `Moves` and `AllMoves` agree. It is a diagnostic:
same answers, more work.

## The other flags

| flag | when |
| --- | --- |
| `SpadesBroken` | spades are already broken in this position. You will pass this most of the time |
| `MinimiseOwnTricks` | tie-break direction: each pair sheds tricks rather than taking them |
| `ForceLarge` | more than nine cards a hand |

`NoMemo`, `NoCollapse`, `NoStaticBounds` and `NoOrdering` are diagnostics. They
all produce the same answer more slowly, and they exist so a suspected bug can be
bisected against a dumber search. Do not ship with them on.

There is no `enforceTrumpBreak` equivalent: this is a Spades solver and the
breaking rule is always in force.

## Threading and the per-thread table

The solver is safe to call from any thread with no locking and no thread-id
argument — there is no shared scratch state to collide on. That is a real
difference from DDS, where concurrent calls had to be handed distinct `thrId`
slots or they raced on the same native memory, and where this build turned out
to support only slot 0.

The catch is memory rather than correctness. The transposition table is
`thread_local`: the first solve on a thread allocates 32 MiB by default and holds
it until that thread calls `nil_release_table` or exits. On the ASP.NET thread
pool, continuations land on whatever thread is free, so over time every pool
thread that has ever run a solve is holding a table. That is why
`NilSolverPool.cs` runs solves on a fixed set of long-lived threads instead:

```csharp
// Startup.cs / Program.cs
services.AddSingleton(new NilSolverPool(workers: 2, tableMegabytes: 32));

// somewhere in a request
var r = await _pool.CanBeBrokenAsync(pbn, leader, trick, roles,
                                     NilFlags.SpadesBroken | NilFlags.ForceLarge);
```

Exactly `workers` tables exist, for as long as the pool does, and the number is
one you chose. Note that a semaphore of the kind the DDS wrapper uses does *not*
substitute: it bounds how many solves run at once, not which threads they run on.

A search in progress cannot be interrupted. `CancellationToken` is honoured up to
the moment a worker picks the job up and not after — which is worth knowing given
the tail above.

## Reading the answer

```csharp
var r = await _pool.SolveWithLineAsync(pbn, leader, trick, roles, NilFlags.SpadesBroken);

if (!r.Success)
{
    _logger.LogError("nil solve failed: {Status} {Error}", r.Status, r.Error);
    return null;
}

// r.NilsSet            true if the nil can be forced to take a trick
// r.NilTricks           tricks the nil bidder takes from here
// r.NilSideTricks       the nil bidder and its covering partner, NilTricks included
// r.OpponentTricks      NilSideTricks + OpponentTricks == TricksRemaining
// r.PrincipalVariation  "N:HA E:H2 S:HK W:H3 N:CK ..."
```

Always look at `Error`. The native side writes a sentence, not a code — `card HA
is both on the trick and in N`, `inconsistent hand sizes: N=4, E=3, S=4, W=4 with
1 card(s) already on the trick` — and it is usually the entire diagnosis.

## Three ways to get this wrong

* **`CallingConvention.Stdcall`.** The ABI is `__cdecl` on every platform. Stdcall
  is what most native card libraries use and what you will reach for out of
  habit. On x64 the mistake is ignored; on x86 it corrupts the stack on every
  call. `NilSolverNative.cs` gets this right — the hazard is in hand-written
  declarations elsewhere.
* **Declaring `nil_solver_version` as returning `string`.** It returns a pointer
  to a static literal, and the default marshaller frees a returned string with
  `CoTaskMemFree`. Use `IntPtr` and `Marshal.PtrToStringAnsi`, which is what the
  wrapper does.
* **Reordering `NilResult`.** `LayoutKind.Sequential` over five `int` and one
  `ulong`, 32 bytes with four bytes of padding before `Nodes`. Tidying the field
  order silently reads the wrong numbers rather than failing.

## Checking the DLL before you blame the wrapper

From a Developer Command Prompt:

```
dumpbin /exports build-vs\bin\x64\Release\nil_solver.dll
```

Six names, undecorated: `nil_solve`, `nil_solve_pv`, `nil_count_set`,
`nil_set_table_size`, `nil_release_table`, `nil_solver_version`. Anything else —
mangled C++ names, or an empty list — and the problem is the build, not the C#.

`build\bin\nil_cli.exe` answers the same questions from the command line and is
the fastest way to decide whether a disagreement is in the position you are
building or in the solver:

```
nil_cli --pbn "N:K.A.A.K 32.2..A .K.3.32 A.3.K2." --leader N --nil E
```
