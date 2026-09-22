# Presolve / Full-Mode Rearchitecture — Work Plan

Reviewed against HEAD (patch 106 — Phase A complete, B1a landed). This document
is a work breakdown, not a decision record; decisions belong in ROADMAP.md as
each phase lands. Where the two disagree, **ROADMAP.md wins** — it is the
authoritative record, and this revision corrects four places where this file
had drifted from it.

---

## 1. Decisions settled

From `Nil_Solver_Notes.docx` plus two rounds of follow-up.

1. **A trick taken by a nil bidder counts toward its team's total.** A live nil
   has taken no tricks by definition, so this only ever binds on a broken bid.
2. **The tertiary lexicon level is therefore dead everywhere** and gets deleted,
   not conditioned. "Which of the two partners holds the pair's tricks" is no
   longer a question anyone can answer differently.
3. **Full mode answers one question:** the maximum (or minimum) trick total for
   each side, subject to the outcome the presolve pinned. Direction is a shared
   parameter — both sides maximise or both minimise, never a mix. Maximisation
   is the focus; minimisation is deferred.
4. **The multi-nil third pass is a forcing question**, not a reachability one:
   *can nil X be forcibly set while nil Y is not set*, both sides adversarial.
5. **An unsatisfiable pin is a hard error.** Full mode reports that it could not
   find the specified answer. No silent fallback to an unconstrained search.
6. **`makes_cooperatively` is off the pipeline entirely.** Not a product field,
   and — superseding the original wording of this decision — not a gate either.
   A4 removed the thing it would have gated. `solve_cooperative` stays as
   correct, cross-checked, callable code with its own ctest; nothing in the
   presolve consults it. See decision 14 and Phase E.
7. **Type 1 means:** maximise the team's tricks subject to this seat taking at
   least one trick.
8. **Seat types are being redefined wholesale.** Fast mode's input alphabet
   deliberately drops type 1.
9. **A passed-in `0` is always a live nil that has taken no tricks yet.** A bid
   already broken in the real position arrives from the caller as type 3 — a
   deal whose nil died at trick 1 is passed as `--seats 3 3 3 3`, never
   `--seats 3 0 3 2`. See §4 for what this removes and what it costs.
10. **Ability beats preference.** A cover's lean is consulted only where neither
    side can *force* its outcome. A preference that cannot be proved does not
    decide anything.

**Phase A is complete (patch 105).** Its four measurements are settled
decisions now, not open questions. Two of them cancelled work that was already
scoped, which is what ordering Phase A first was for.

11. **The `satisfied` bit costs ~3.5% of nodes, and the mechanism is entry
    multiplication** (A1). Median 1.035x nodes over 24 paired 13-card deals,
    aggregate +9.4%. Not key width — 125 → 126 bits at a trick boundary,
    inside the 128-bit budget. Not hit rate — +0.01pp median, flat. Keep the
    bit in the Phase D design and do not design around it. §3 is rewritten
    around this result.
12. **Plain trick maximisation is the expensive shape, not the cheap one**
    (A2). On the 89 statically-doomed 13-card deals: median 1.83x nodes,
    aggregate 1.77x, throughput flat, so nodes are wall. The shape reports
    `presolve_nodes=0` and has `nil_must_take_a_trick` guarded off — it runs
    with almost none of the machinery the nil question gets. **This is a proxy
    for removing nil-aware pruning, not a measurement of a Phase D type-3
    engine**, and must not be quoted as one.
13. **The value support collapses 105 → 14** (A3). B1a delivered the 14 values
    in both directions; the 14-INTEGER window is B1b's, and it comes from
    deleting the tertiary machinery rather than zeroing it.
14. **The cooperative probe finds nothing the static proofs miss** (A4). Of the
    161 doomed deals `bounds.hpp` does not already settle: 0 unreachable, 146
    reachable, 15 exhausted. It is not too slow — only 9.3% exhausted, against
    an exit condition written for *most exhaust*. There is nothing there. This
    is what kills Phase E's downgrade.

---

## 2. What already exists

Listed so none of it gets rebuilt. All of it is shipped and tested.

| Need | Existing machinery | Status |
|---|---|---|
| The third pass (force X set, Y alive) | `--conjunction <seat>` (patch 78) | Shipped. `conjunction_crosscheck` 320/320. Widened to both leans in patch 100; rung-1 soundness 280 cases, 0 mismatches |
| `makes_adversarially` | `MODE_FAST` | This *is* fast mode's existing answer. No new search |
| `makes_cooperatively` | `solve_cooperative` (patch 98) | Shipped and cross-checked, budgeted (`same_lean_probe_budget`, default 8M). **Off the pipeline (A4) — nothing consults it.** Kept as callable tested code |
| Cheap doom proofs | `nil_must_take_a_trick` / `cover_deficit_depth` in `bounds.hpp` | Shipped. 89 of 250 doomed deals, free, already wired into fast mode |
| The outcome pin as a filter | `solve_constrained_line` (patch 101) | Shipped. Must-make prunes early and free; must-be-set is terminal-only |
| Tricks under a pin | `solve_constrained_tricks` (patch 102) | Correct, verified, **does not reach 13 cards** |
| Defection-aware residual cell | `search_equilibrium` in `same_lean.cpp` (patch 104) | Correct, budget-blocked at 13 cards |
| `satisfied` bit in the TT key | `encode_state_key(..., nils_broken, carry_nils_broken)` | **Already conditional.** The infrastructure Phase D needs exists. Priced at ~3.5% of nodes by A1 — see §3 |
| Dead-line propagation | `outcome_constraint.cpp`, `cooperative.cpp` | Pattern established: skip the child, propagate "all children dead" as its own signal. Never a sentinel value |
| Strictly-opposed vs. not | `side_rank` / `strictly_opposed` / `seat_shape` in `seats.hpp` | Shipped. Decides which multi-nil shapes need item 60 at all — see Phase F |
| DDS §3 / §4 bounds | `bounds.hpp`, `ctx.quick_tricks`, `ctx.later_tricks` | Shipped **but reshaped for the nil question, and off entirely on multi-nil**. See Phase H |

The two searches that block 13 cards (`solve_constrained_tricks`,
`search_equilibrium`) are blocked for the *same three reasons*, named in patch
102's own entry: no alpha-beta, no transposition table, no move ordering. The
scoring rule change removes the reason all three were absent.

---

## 3. What the `satisfied` bit costs

This section used to argue that the type-1 pin is expensive because the
`satisfied` bit *widens the key and lowers the hit rate on every node*, and
that a proven-dead nil should therefore be downgraded to type 3. **A1 measured
both halves of that mechanism and neither one is what happens.** The text below
replaces the argument rather than annotating it; the downgrade it existed to
justify is rejected, and Phase E says so.

**The price is about one node in thirty.** Median 1.035x nodes over 24 paired
13-card deals, aggregate +9.4%, stores median 1.039x, wall median 1.033x.

**The mechanism is entry multiplication.**

- *Key width is not binding.* The key is `21 + 2n` bits at a trick boundary, so
  13 cards goes **125 → 126** — inside the 128-bit budget. The four-bit
  `carry_nils_broken` field costs the root position; one more bit costs
  nothing.
- *Hit rate does not move.* **+0.01pp median** across the sample, range −0.13
  to +0.25pp. Flat to three significant figures.
- *Stores and nodes track each other:* +3.9% against +3.5%. That is the
  signature of **more distinct entries**, not of worse lookups. The bit splits
  positions that used to merge; it does not degrade the table's ability to
  answer.
- *The cost concentrates where the table is already worst.* `v13-0354` moved
  −0.0% at a 90.7% hit rate — a table that good has nothing left to split —
  against `v13-0316` at +15.1% on 452M nodes and 84.6%.

**Treat this as a proxy, because it is one.** A1 prices the keying mechanism on
the *live-nil* tree, so the merge rate it splits is that tree's rather than the
pinned tree's, and it is not a measurement of the Phase D type-1 engine, which
does not exist yet. What it establishes is the order of magnitude and that
there is no key-width or hit-rate problem to design around.

**Keep the bit.** `encode_state_key`'s `carry_nils_broken` parameter is already
conditional, which is the right shape — the bit is paid for only where a pin is
live. Nothing in Phase D needs to avoid it.

**What does not survive is the conclusion.** The asymmetry this section
proposed writing into the translator header —

> Type 1 is the safe default. Type 3 is an optimisation that requires a proof
> of doom.

— is still true about correctness and is now *irrelevant to speed*, because A2
found type 1 to be the faster of the two on this corpus as well as the safe
one. There is no longer a speed argument sitting on the other side of the
safety argument. The header note should record the measurement, not the
trade-off; a future reader tempted to "optimise" type 1 into type 3 needs to
find the 1.83x, not a caution.

---

## 4. What decision 9 removes, and the one thing it costs

**It removes `ROLE_NIL_SET` from the solver entirely.** If the caller never
declares an already-broken bid, nothing inside the search needs to model one.
That kills `nil_already_set()`, `nil_set_count()`, `nil_set_mask()`, and the
`primary is zero when the roles say the nil is already set` branch of
`objective_weights` — along with the caveat in `search.hpp` about that branch
not being strictly opposed, which was the only place the old objective was not
a clean minimax.

This is not a one-line deletion. **73 references across 20 files**, including
`api.cpp`, `nil_solver.h`, `nil_bench.cpp`, five Python tools and the test
suite. It gets its own step (B3) rather than riding along inside another patch.

**It costs one API contract, and the cost is a wrong score if it goes unnoticed.**
`search.hpp` currently promises:

> `nils_set` is HOW MANY BIDS ARE BROKEN … A bid the caller declared already
> broken with ROLE_NIL_SET counts toward it, since the question is how many are
> down, not how many the search knocked down.

After decision 9 that sentence is false. The solver can only count what it
knocked down, because it is never told about the rest. Same for
`nils_set_mask`, which is the entire point of patch 89.

**Consequence for the C# caller:** it must add back the bids it already knew
were down. If it does not, the site under-reports set nils on exactly the
mid-hand solves that motivated the change. Record this in `nil_solver.h` at the
field, not only in the ROADMAP, and note it for the deferred C# phase.

---

## 5. Open questions — none remaining

### Q3 — Does `makes_cooperatively` stay on the wire? **Answered: no.**

Not on the wire, not computed, not reported. A4 (decision 14) removed the thing
it would have gated, so the tri-state question this raised never arises — there
is no budgeted probe in the pipeline to exhaust. Patch 103's rule that "no line
exists" and "I stopped looking" are different facts still stands everywhere
else it applies.

Heading kept rather than deleted so §1–§4 do not shift under the cross-
references in §4, Phase C, Phase F and the risk table.

---

## Phase A — Measure. Ship nothing. ✅ COMPLETE (patch 105)

Everything downstream was sized by these four numbers, and three of them could
kill work before it was written. Two did. Conclusions are decisions 11–14; full
write-ups with tables are in ROADMAP.md. Recorded here so the phase reads as a
record rather than a to-do.

### A1 — Price the `satisfied` bit ✅

On 13-card single-nil deals from `nil13_verdict.txt` whose nil is forced set,
run full mode with the bidder as (a) type 3, (b) type 1 with the bit in the key.
Report nodes, TT hit rate, TT stores, key width. Measured with a throwaway
instrumented build that appends one state-derived bit and changes nothing else;
the control arm was verified bit-identical before any number was read off it.

**Result:** 24 of 25 paired. **Median 1.035x nodes, aggregate +9.4%**, stores
+3.9%, wall +3.3%. Hit rate flat (+0.01pp), key 125 → 126 bits. **The plan's
stated mechanism was wrong** — see §3, rewritten. The bit stays in the Phase D
design. Nothing from A1 shipped: a `--satisfied-bit` flag whose only consumer
is an experiment is a feature with no user, so the instrumentation is a
separate reproducible artifact.

### A2 — Price the doomed shape ✅

Full-mode cost at 13 cards on `3 3 3 3` versus `0 3 3 3` and `1 3 3 3` on the
same deals. Plain trick maximisation carries no nil pruning at all, so the
downgrade may be moving work *into* the most expensive shape the solver has.

**Result: it is the worst case.** Arm A `2 3 0 3` against arm B `2 3 1 3`, one
process per solve, on all **89 statically-doomed deals** (88 paired at a 280 s
cap): **median 1.83x nodes, geometric mean 1.74x, aggregate 1.77x**
(7,212,816,411 → 12,765,796,137), wall 1.72x. **Throughput flat** (8.42M vs
8.66M nodes/sec), so the node ratio is the real ratio and this is not items 44
/ C1 / C3's throughput failure wearing a new hat. B worse on 66/88, ≥2x on 40,
≥4x on 14, worst 10.61x; B cheaper on 22/88, best 0.26x.

Mechanism read off the output rather than inferred: arm B reports
`presolve_nodes=0` (`presolve_eligible` is gated on `!roles.nil_already_set()`)
and `nil_must_take_a_trick` is guarded off (`per_nil` goes negative). **The
downgrade does not remove a constraint; it removes the only pruning structure
this shape has.**

A sampling error nearly set the direction: the first deal measured read 8.38x
and carried `nils_set=0`, so its nil *makes* and it was not in the population
at all. A stratified 25-deal sample then read 1.25x median; the correct 89-deal
population reads 1.83x. Three populations, three answers, same question.

Decision 9 makes `3 3 3 3` common rather than exotic — every mid-hand call on a
dead nil arrives as one. That is now Phase H's justification (see R1).

### A3 — Value-support collapse ✅

Arithmetic, no search. `search.hpp` puts the current reachable set at
(t+1)(t+2)/2 = **105 values at 13 cards**. With the tertiary deleted and the nil
dimension pinned, the value is one team's trick count: **14 values**. Confirm,
and derive the resulting root window width.

**Result: confirmed, 7.5x.** Today at `k = t + 1 = 14` a nil trick is worth
`k*k + 1 - k = 183` and a cover trick `-k = -14`, so the support is the `(n,p)`
pairs with `n + p ≤ 13` — **105 values**, distinct because `gcd(183,14) = 1`,
spread over `[-182, 2379]`: **2,562 integers at 4.1% density**. Under the team
count the value is `S = n + p` in `[0,13]`: **14 values, 14 integers, 100%
dense.**

**Not yet fully realised, and B1a's entry says so.** B1a delivered the 14
values in both directions but the window measures **183**, because `secondary`
is still ±k and k exists to separate the levels above the trick term. With
primary and tertiary both zero there is nothing to separate, so `(0, −1, 0)`
collapses k to 1 and the window with it. **That is B1b's to deliver**, and 183
is deliberately pinned in B1a's test so the deletion has a number to move — the
assertion is meant to be updated by B1b, not removed.

### A4 — Can the probe even answer at 13 cards? ✅

`bounds.hpp` already settles 89 of 250 doomed deals statically and free. Run
`solve_cooperative` at an 8M budget on the other 161. Record the exhaustion
rate.

**Result: it can answer, and there is nothing to find.** On
`nil13_verdict.txt`: 400 rows, 250 forced set, 89 settled statically and free,
161 probed. **0 unreachable, 146 reachable, 15 exhausted** — 180,785,173 nodes
and 106.5 s to learn nothing.

**It failed for a stronger reason than the exit condition anticipated.** That
condition was *most exhaust*; only 9.3% did. The structural reason is that a
nil forced set **adversarially** is almost always still alive on *some*
cooperative line — which is exactly the difference between "the opponents can
force a trick onto it" and "no line exists." The first is common; the second is
rare, and where it holds the cards usually say so plainly enough that the
static proof already fires. The probe is not too slow. There is nothing there
that `bounds.hpp` has not already found.

Phase E therefore reduces to the translator plus an unconditional type-1
default — and, with A2, the downgrade is rejected outright rather than merely
unavailable.

---

## Phase B — Scoring rule and the role removal

This goes **first among the code phases**, not last. Every measurement taken
before it is against a tree that is about to change, and the project's own rule
is that a measurement is only valid against the tree it was taken on.

Three steps, each its own patch.

### B1 — Delete the tertiary

- `objective_weights`: value becomes `primary * nil_tricks + secondary *
  side_tricks`, where `side_tricks` includes the nil bidder's own tricks.
- `outcome_constraint.hpp`: the three-level priority collapses to "maximise my
  side's total."
- `search.hpp` header: recompute the value-support paragraph.
- **`nil_oracle.py` must be forked in the same patch.** It implements the old
  lexicon. Left alone, `crosscheck.py` silently validates nothing — the exact
  failure pattern already on the learnings list. Mutation-check the crosscheck
  to prove it still fails when it should.

**The premise is testable — test it.** Patch 102 measured T's priority against
the zero-sum stand-in and found **3 disagreements in 227 hands**, with a worked
counterexample. Under the new scoring rule that number must be **zero**. Re-run
that comparison. If it is not zero, the zero-sum premise is wrong and Phase D
does not follow.

### B2 — Re-bank

Fixed points move and each move needs a written cause.

**`tools/check_baselines.py` is the authoritative list** — workloads, exact
invocations and banked counts in one place, comparing rather than printing
(patch 105). **There are nine, not eight**: `large.txt 13c only` is a
consistency check on the worst-case leg's subtotal, and it is the figure that
went stale unnoticed across two re-banks because no other row covered it. Run
`python3 tools/check_baselines.py` rather than any hand-run invocation; three
of the nine need non-default arguments and **a wrong invocation returns a
plausible number rather than an error**.

As of patch 106:

```
positions.txt fast        39,701
positions.txt full       274,270   (re-banked at 106; was 278,059)
large.txt fast            49,084
large.txt full       163,134,302   (re-banked at 106; was 163,149,275)
large.txt 13c only   162,499,778
multinil.txt           4,833,200
opposed13            351,156,828
opposed13_settled     55,428,602
opposed13_real       171,731,064
```

**Correction to an earlier revision of this table.** It listed `multinil.txt
49,084` and `large.txt fast 4,833,200`. **The reverse is correct and always
was:** 49,084 is `large.txt` fast, 4,833,200 is `multinil.txt`. ROADMAP.md
never carried the error and confirms the direction at patch 105. The two
re-banked rows above are B1a's, caused by the dead-nil/max canonicalisation
change across 57 rows of `positions.txt` and 4 of `large.txt`; the other seven
are unmoved, because fast mode is `(1,0,0)` regardless and the multi-nil and
opposed branches already set tertiary zero.

**Corpus columns — changed by B1a. Do not undo this.** `side_tricks` is
authoritative everywhere and is the column that carries the team total. On
dead-nil rows (a `1` in `seats`), **`nil_tricks` and the PV are witnesses, not
results**: once the tertiary stops constraining the split, the split is derived
from move ordering, move ordering depends on suit identity, and it becomes an
artifact. Over the six valid non-trump relabellings (spades is trump, so only
the three side suits may be permuted) `side_tricks` is invariant on 11/11 while
the split moves on 6/11. `nil_bench.cpp` gates both the `nil_tricks` and the PV
equality checks on `split_is_a_witness`; `invariants.py`'s
`undetermined_split` now covers `max` as well as `min`. **Do not reintroduce a
fixed-split assertion, and do not re-bank `nil_tricks` to whatever the current
search emits.** The line is still checked for legality and optimality — it is
only equality that is dropped.

### B3 — Remove `ROLE_NIL_SET`

73 references, 20 files (§4). **Fold the corpus migration into this step rather
than into Phase C** — the 210 affected rows (130 in `positions.txt`, 76 in
`multinil.txt`, 4 in `large.txt`) have to be regenerated from the forked oracle
anyway, so relabelling them 1 → 3 is free at that moment and expensive at any
other.

Hazard worth its own check: those rows' recorded `nils_set` includes the bid
that was already down. After the relabel it must not. A pure relabel fails 210
rows; a careless fix makes them pass for the wrong reason. Mutate one row and
confirm the suite catches it.

**Flag:** `--no-team-scoring` restores the old tertiary for one release so the
A/B is runnable on a single binary.

**Exit:** oracle parity restored, `--check-pv --check-moves` clean on every
corpus, all ctests green, every moved count explained.

---

## Phase C — Two role types. Pure refactor. Zero node movement.

The collision to eliminate: one `int[4]` on one `--seats` flag carrying two
alphabets whose values `0`, `1` and `3` each mean different things depending on
which mode reads it. `seats.hpp` states these numbers are part of the corpus
format and the C ABI.

Decision 9 makes this cleaner than it looked. Because a type-1 seat is always
derived by the presolve from a live `0`, that seat has taken zero tricks so far,
so "at least one trick in the remaining play" and "at least one trick in the
deal" coincide. **No separate already-down input field is needed.**

### Tasks

- `BidRoles` — the caller's question: candidate nil (live, no tricks yet),
  cover-mine, set-theirs, no bid.
- `PinnedRoles` — the search's input: must-take-zero, must-take-at-least-one,
  free.
- **No implicit conversion between them.** Translation is one pure function,
  `translate(BidRoles, PresolveVerdict[]) -> PinnedRoles`.
- CLI: `--seats` keeps `BidRoles`. Add `--pinned-seats` for direct `PinnedRoles`
  entry, so full mode is testable without running a presolve.
- ctest: the translation table from `Nil_Solver_Notes.docx`, verbatim and
  table-driven. No search, no cards, so it runs in milliseconds and covers every
  row. Rederive the `0 0 3 2` row first — see Phase F.

**Exit:** every banked node count bit-identical. This phase changes no search.

---

## Phase D — Pinned outcomes on the main engine. The big one.

This is what unblocks 13 cards, and it is the phase that matters. Patches 102,
103 and 104 all stopped here.

### Tasks

- Retire `solve_constrained_tricks`'s standalone search. Route the pin through
  `solve()`'s existing alpha-beta, transposition table and move ordering.
- **Must-make half:** abandon without recursing, as today. Already measured
  free and already carrying most of the tree — 9,093,017 of 12,056,872 nodes on
  the settled deal were early must-make abandonments.
- **Must-be-set half:** terminal-only, with the `satisfied` bit in the key. Use
  the existing `carry_nils_broken` parameter on `encode_state_key` rather than
  adding a second mechanism — it is already conditional, which is the right
  shape: the bit is only paid for when a pin is live.
- **"No valid line" needs a distinct TT state**, not a sentinel value. The
  pattern is settled in `outcome_constraint.cpp` and `cooperative.cpp`; this is
  the first time it meets a *bounded* value search, so the interaction with
  stored upper and lower bounds is new work and is where a bug would hide.
- **Unsatisfiable pin → hard error** (decision 5). ctest: mutate a pin to a
  provably impossible one and assert the refusal. Mutation-check it, so the test
  cannot silently stop exercising its own subject.

### Regression

On single-nil deals where the presolve says the nil makes adversarially, the
pinned path must reproduce old full mode's trick splits **exactly** — the
constraint is the old primary objective, moved from the value into a filter.
Differential test across `nil13_verdict.txt`. Stronger than any corpus that only
agrees with itself.

**Flag:** `--no-pinned-engine` falls back to `solve_constrained_tricks` for the
A/B.

**Exit:** 13 cards resolves.

---

## Phase E — The presolve pipeline

> **The doom downgrade is struck.** Rejected at patch 105 before a line of it
> was written, with the full write-up in ROADMAP.md under *Evaluated and
> rejected*. Two independent measurements kill it and either would have been
> enough: **A4** says it can almost never fire (0 of 161 deals beyond what the
> static proofs already settle), and **A2** says the places it *can* fire are
> the worst places to do it (median 1.83x). Type 1 is not merely the safe
> default — on this corpus it is the faster one.

What is left is small. Phase E is now a translator with a verdict attached
rather than a phase of its own.

### Order of operations, per nil

1. Static doom proofs from `bounds.hpp` — free, already wired in.
2. `makes_adversarially` — this is existing fast mode, not a new search.
3. A dead nil is pinned **type 1, unconditionally.** No probe, no budget, no
   exhaustion state, no downgrade.

### Tasks

- The seat-type translator, and nothing else.
- Header note for the translator: type 1 for a dead nil is **not** a
  conservative default taken for safety — it is the measured-faster choice
  (A2, decision 12). A future reader tempted to "optimise" it into type 3
  should find the number rather than a caution. §3 explains why the old
  asymmetry wording is the wrong thing to write here.

**Flag:** none. `--no-doom-downgrade` has nothing left to gate.

**What would revive the downgrade** is recorded in ROADMAP.md and needs *both*,
not either: (1) a static doom proof with materially better recall than
`nil_must_take_a_trick`'s 89 of 250, since A4 shows the cooperative probe will
not supply the difference — and note item 83 already failed on the mirror of
this, cheap sufficient conditions not being a decision procedure; and (2) a
demonstration that type 3 beats type 1 **under the Phase D engine**, where
type 1 is a filter rather than a primary weight and the presolve window and
static proofs have been reformulated for it. A2 measures a proxy, not that. If
Phase D's type-1 pin inherits none of the pruning arm A gets, the 1.83x does
not transfer and this should be **re-measured rather than re-rejected on the
existing entry.**

---

## Phase F — Multi-nil

**Do not build a new probe.** The third pass — force nil X set while nil Y
survives — is `--conjunction <seat>` asked from Y's side. Shipped in patch 78,
verified 320/320, widened to both leans in patch 100.

Decision 10 puts it in the structure patch 100 already established: *if either
side can force rank 3, it gets exactly that*; the lean is consulted only in the
residual cell where neither side can force.

That split makes two separable jobs, and the first is much smaller than the
notes imply.

### F1 — Mixed lean. Strictly opposed. No tie-break needed.

`0 0 3 2` and `0 0 2 3`: the partners lean opposite ways, `side_rank` sums to a
constant 3, and `strictly_opposed` is true. Ordinary minimax describes the whole
thing and **the tie-break never fires.**

This means the worked example in the notes needs rederiving. Its `0 0 3 2` row
resolves by seat 4's type-2 preference — reconstructing by hand what a strictly
opposed search produces on its own, and doing it *before* asking whether seats 1
and 3 can force. Under decision 10 the forcing question comes first, and on this
shape it is the only question.

**Also vacuous here: partner nils** (`0 3 0 3` and friends, `SHAPE_PARTNER_NILS`).
Both bids sit on one side, the opponents want both down, and neither side ever
wants "X set, Y alive" — so the third pass has no owner. Resolve each bid
independently and skip it. Note that patch 89 found the broken-bid mask is *not*
pinned on this shape (11.67–20.00% ambiguous), so `nils_set_mask_determined`
still has to say so.

### F2 — Same lean. This is item 60.

`0 0 2 2` and `0 0 3 3`: partners lean the same way, `side_rank` does not sum to
a constant, and no single scalar describes it. Rung 1 is `--conjunction`; the
residual cell is `search_equilibrium`, which needs Phase D's treatment before it
reaches 13 cards.

Ship F1 first. It needs nothing from D.

---

## Phase H — DDS trick-search optimisations for the maximising seats

Source: `DiscreteMelody/spades-dds` — DDS3, Martin Nygren's modernisation of the
Haglund/Hein 2.9.0 solver, which is the same lineage as `DDS_Algorithms.pdf` in
the project files.

### Why this becomes available only now

The Nil Solver already ports pieces of DDS §3 and §4, but **deliberately
reshaped**, and `bounds.hpp` says so in its own header:

> Chang skips the search when "the side to play would win at least one trick",
> which bounds how many tricks a SIDE takes; the nil question is whether one
> TRICK can be forced onto one SEAT, **so neither of his two tests transfers**.

and of the §4 port:

> This is DDS section 4's LaterTricks rules 1 and 2 **in the only form the nil
> question can use**.

After Phase D, full mode asks *how many tricks does this side take*. That is the
original question, so the original forms become applicable for the first time.

**And they apply where there are currently no bounds at all.**
`disable_single_nil_machinery()` turns off `static_bounds`,
`full_static_bounds`, `later_tricks`, `quick_tricks`, `spade_matrix` and
`tt_narrow` on every multi-nil shape, because their cases are stated in terms of
"the nil bidder" and "the cover partner" and multi-nil has no cover. A
side-trick formulation has no such dependency. **The multi-nil shapes get bounds
for the first time.**

### Two things that lower the porting cost

- **The trump-break house rule is already in the fork.** `TrackType::trumpBroken`
  — *"Only meaningful when the trump-break house rule is active."* Bridge DDS has
  no such rule and this was the largest expected adaptation. It is already done.
- **The fork also carries a misère mode** (`handToPlayIsMax` is always true in
  vanilla, always false in misère). Out of scope here — this phase is the
  maximising seats — but worth knowing it exists before anyone writes one.

### Sub-items, in measured order

Each is its own patch with its own `--no-X` flag and its own A/B against a
bit-identical control.

**H1 — Full QuickTricks (DDS §3).** `quick_tricks.cpp`, 1,243 lines.

Do this first, because patch 86 already sized the gap on this codebase: the
existing `top_spade_run` proof fires on 12.97% / 8.10% of the settled region,
but **45.83% / 61.38% are cases where the top-spade holder IS the side that must
prove something and the run is simply too short.** Side-suit winners and ruffs
address up to 58.8% / 69.5% of the region — 7.3% and 21% of all nodes. That is a
ceiling measured here, not extrapolated from DDS.

- Include `QuickTricksSecondHand`, the cheap check after the leading card is
  played (Kuijf's addition, DDS §3).
- **Gate before spending**, per patch 87's own recommendation: the size of the
  required claim falls out of the window for free, so attempt a proof only where
  it could succeed. 71.88% of the region needs at most two tricks.

**H2 — LaterTricks in its original side form (DDS §4).** `later_tricks.cpp`,
361 lines. `LaterTricksMIN` / `LaterTricksMAX` bound what the opponents of the
trick-leading hand can take. The trump-contract rules (the Stanojevic additions)
apply directly — spades is always trump, so the "if trump contract" branch is
the only branch.

**H3 — Heuristic move ordering (DDS §5).** `heuristic_sorting.cpp`, 1,430 lines.

**Compare against `MOVE_ORDERING.md` before porting.** The Nil Solver lifts one
card to the front rather than sorting, deliberately: both ordering attempts this
project rejected lost on *throughput*, not on nodes. DDS sorts a weighted list.
That trade has to be re-measured against this codebase's node cost, not
inherited. Suit mixing (item 35) is already ported.

One piece to take regardless of how the sorting question lands: **two best moves
per depth**, one for the alpha-beta cutoff and one for the TT match, with DDS's
rule for which gets stored. Cheap, and orthogonal to the sorting decision.

**H4 — TT winning-rank masks (DDS §6).** `trans_table_*`, ~4,100 lines. Measure
last; `statekey.hpp` already does relative-rank compression, so this is the
smallest expected win. What DDS may have that the Nil Solver does not is storing
only ranks at or above the lowest *winning* rank, so one entry covers many
literal positions. Read `specs/transposition-table.md` first.

### Do not port wholesale

DDS's published figures are for bridge: 13 cards, no trump break, a different
node cost and a different objective. The only numbers that count are the ones
measured here. Per sub-item, report **node count and wall clock** on
`nil13_verdict.txt` and `opposed13.txt` with the arm on and off, and report the
**firing rate separately from the node saving** — patch 86's lesson that a
sufficiency bound is not a firing rate, and a firing rate for a weak proof is
not one for a strong proof.

### Relation to the rest of the plan

H depends on D and on nothing else.

**A2 confirmed `3 3 3 3` is the expensive shape** — median 1.83x, aggregate
1.77x, running with `presolve_nodes=0` and the static proofs guarded off. But
the doom downgrade no longer manufactures it, so **H is not a mitigation for
E**. H is justified by **decision 9**: a bid already broken in the real
position arrives from the caller as type 3, so every mid-hand call on a dead
nil is a `3 3 3 3` search with no bounds at all. That is a bulk production
shape, not an artifact of an optimisation the project declined — which makes it
a better justification than the one it replaces, not a weaker one. Priority
against E is moot; E is a translator.

---

## Phase G — C ABI and C#

Deferred, per standing project rule, until the C++ work is complete. Carries the
`nils_set` contract change from §4 — the caller must add back bids it already
knew were down.

---

## Sequencing

```
A ✅ (patch 105)

B1 ─> B2 ─> B3 ──┬──> D ──┬──> F2
                 │        └──> H1 ─> H2 ─> H3 ─> H4
C ───────────────┤
                 ├──> E   (translator only)
                 └──> F1
```

- B and C can overlap; neither depends on the other's output. A is done.
- D depends on B1 (the zero-sum premise) and C (the type split).
- **E depends on C alone now** — with the downgrade struck it is a translator,
  so it no longer waits on D, and A1/A4 are settled.
- F1 depends only on C. F2 depends on D.
- H depends only on D, and runs in parallel with E and F2.
- **A2's ordering question dissolved rather than resolving.** It was written to
  decide E against H on the grounds that E manufactures H's worst shape. E no
  longer manufactures anything; H stands on decision 9 instead, and the two are
  independent.
- One variable per patch, `--no-X` on each, as always.

---

## Risks

| # | Risk | Detection | Mitigation |
|---|---|---|---|
| R1 | **CONFIRMED (A2), and half of it resolved.** `3 3 3 3` *is* the worst case — median 1.83x, aggregate 1.77x | A2 | The downgrade half is gone: it was rejected, so it moves no work anywhere. The decision-9 half stands and is now **Phase H's justification** rather than a risk H mitigates. Type 1 kept, and it is the faster arm as well as the safe one |
| R7 | DDS's ports are measured on bridge and land flat here, as four earlier heuristics did on throughput | Per-sub-item A/B in H | Firing rate reported separately from node saving; each sub-item behind `--no-X` and reversible |
| R2 | **RESOLVED (A1).** 3.5% median by entry multiplication; key width (125→126) and hit rate (+0.01pp) are both non-issues | A1 | Keep the bit — it is already conditional on a live pin via `carry_nils_broken`. No design change needed. §3 |
| R3 | The zero-sum premise is wrong and Phase D does not follow | The 3-of-227 re-run in B1 | Stop at B; D's whole justification is that premise |
| R4 | `nil_oracle.py` not forked alongside the objective, so the crosscheck silently validates nothing | Mutation-check the crosscheck in B1 | Fork in the same patch, not a follow-up |
| R5 | **RESOLVED (A4) — and not in the predicted way.** The probe *can* answer at 13 cards; only 9.3% exhausted. It finds nothing: 0 of 161 unreachable | A4 | Probe dropped from the pipeline. Static proofs plus an unconditional type-1 default. `solve_cooperative` stays as tested code nothing consults |
| R6 | `nils_set` under-reports after `ROLE_NIL_SET` is removed, and the site quietly scores mid-hand solves wrong | No automated detection — the field still returns a plausible number | Document at the field in `nil_solver.h`; explicit task in Phase G |
