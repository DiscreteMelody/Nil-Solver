# Nil-Solver optimization roadmap

Ordered by impact, highest first, with the constraint that a few items must
precede others to be worth anything at all. As of HEAD `09099f4` plus patch 11.

The target was 13-card hands, and the boolean search reaches them. A median
13-card position answers in well under a second. The estimate before patch 10
was a couple of hours. What is left on this list is therefore no longer about
getting to 13 cards at all — it is about the tail, and about the full
lexicographic search, which is still exhaustive and is still where the trick
counts and the principal variation come from.

Patch 11 is the first item aimed squarely at that tail, and it is worth reading
its entry for the result rather than the headline: two exact proofs, −28% of
nodes on the corpus, and 7-11% of wall time at 13 cards against a prediction of
much more. The reason is measured in item 4 and it generalises — a slow deal is
slow because the nil is genuinely contested, and a static proof is silent
exactly there.

The property that makes this solver trustworthy survived both trips. Full mode
is untouched by patches 10 and 11, node for node at every hand size measured,
and still agrees with `nil_oracle.py` card for card. That is not incidental: it
is what the pruned answer is checked against.

---

## Done

| | Optimization | Landed | Effect |
|---|---|---|---|
| ✅ | Bitboard state representation | patch 1 | `Hand = uint64_t`, `suit * 16 + rank`, ctz move extraction, mask-based `legal_moves` |
| ✅ | Compact canonical state key | patch 7 | 21 + 2n bits at a trick boundary; rank-relative, trick-as-threshold |
| ✅ | Bounded transposition table | patch 7 | 4-way buckets, evict-shallowest, generation-stamped, full 128-bit key stored |
| ✅ | Equivalent-card reduction in the move generator | patch 8 | one representative per run of legal cards contiguous in the relevant set; −18% nodes at 4 cards rising to −55% at 9, and the principal variation is unchanged |
| ✅ | Boolean / lexicographic mode split | patch 9 | `MODE_FAST` zeroes the tie-break weights and gives the primary weight 1, so the value is the nil bidder's trick count and the window is literally `[0, 1]`; no speed change yet, by design |
| ✅ | Nil-specialised alpha-beta / AND-OR search | patch 10 | null window `[0, 1]` in `MODE_FAST`, fail-soft, bounds in the table; −12.8x nodes at 4 cards rising to −303x at 9, and 13-card hands answer in a median 150 ms. `MODE_FULL` searches between unreachable sentinels and is unchanged node for node |
| ✅ | Nil-safe and nil-set static bounds | patch 11 | two proofs at a trick boundary, `MODE_FAST` only: −28.5% nodes on the corpus, −3% to −12% at 13 cards. Wall time −6.5% to −10.7% at 13 cards; throughput unmoved. `MODE_FULL` unchanged node for node |

Patch 7 measured against the 560-position corpus:

| cards | nodes/position before | after | ratio |
|------:|----------------------:|------:|------:|
| 4 | 7,085 | 1,274 | 5.6x |
| 5 | 125,346 | 9,500 | 13.2x |
| 6 | 1,163,389 | 44,309 | 26.3x |

Total corpus wall time 29.9 s to 0.77 s.

Patch 8, measured against `--no-collapse` on the same binary so the two columns
differ in exactly one thing:

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
Per-node throughput costs a few percent, most of which comes back from skipping
the reduction outright at nodes with a single legal card.

Patch 9 is the one entry in this table that bought no speed, and that is the
expected result rather than a disappointment. The transposition table keys on
the position, not on the value, so zeroing the tie-break weights changes what a
node's number *means* without changing which nodes get visited:

| workload | full nodes | fast nodes | ratio |
|---|---:|---:|---:|
| corpus, the 430 rows fast mode searches | 2,151,487 | 2,151,487 | 1.00x |
| random, 7 cards, 6 positions | 2,177,251 | 2,177,251 | 1.00x |

That 1.00x is the baseline item 3 has to beat, and `nil_bench --mode both`
prints it on demand. The other 130 corpus rows are `nil_already_set`, which fast
mode answers without looking at a card; those are excluded from the comparison
rather than counted as free wins.

Patch 10 beat it. Same measurement, same command, same 430 searched rows:

| workload | full nodes | fast nodes | ratio |
|---|---:|---:|---:|
| corpus, 4 cards (308 searched) | 323,088 | 25,314 | 12.8x |
| corpus, 5 cards (94 searched) | 735,493 | 24,888 | 29.6x |
| corpus, 6 cards (28 searched) | 1,092,906 | 21,074 | 51.9x |
| corpus, all 430 searched | 2,151,487 | 71,276 | 30.2x |
| random, 7 cards, seed 1 | 425,937 | 1,762 | 242x |
| random, 8 cards, seed 1 | 2,019,207 | 8,360 | 242x |
| random, 9 cards, seed 1 | 23,519,702 | 77,595 | 303x |

The ratio climbing with hand size is the whole point again, and for a stronger
reason than patch 8's: this is a reduction in the effective branching factor,
not a constant factor taken once. On unseen deals at a different seed the
numbers are of the same order and move about a lot — 120x over thirty deals at
7 cards, 549x over twenty at 8 — which is what a pruned search looks like.
Quote the seed-1 rows when comparing against a baseline and treat any single
figure as indicative.

And the thing the list was written for:

| cards | nodes/position | ms/position |
|------:|---------------:|------------:|
| 10 | 23,820 | 12.6 |
| 11 | 331,920 | 83.5 |
| 12 | 1,249,375 | 273.7 |
| 13 | 2,689,933 | 609 (median ~150, worst of 20 = 8,639) |

13 cards in a median 150 ms, against an estimate of a couple of hours. Note the
shape of that last row rather than its mean: over twenty random deals one
position took 43,040,512 nodes and 8.6 s, which was 80% of the work in the whole
run, and the other nineteen were between 120 ms and 1.1 s. The mean is an
artefact of that one deal. **The tail, not the median, is what items 4 and 6 are
now for.**

Full mode is unchanged by this patch, and that is checked rather than asserted:

| workload | before patch 10 | after |
|---|---:|---:|
| corpus, all 560 | 2,647,731 | 2,647,731 |
| random, 9 cards, seed 1 | 23,519,702/pos | 23,519,702/pos |

---

## Evaluated and rejected

**Side-suit canonicalization.** Hearts, diamonds and clubs are interchangeable,
so canonicalizing them is a genuine symmetry worth up to 6x in theory. Built and
measured: 5.3% fewer nodes at 6 cards, 7.7% at 7, bought at roughly 12% of
throughput because the sort runs at every node. Net wall time came out *worse*.
It also carries a live tie-break hazard, since the move order is suit-major and
a move read back under a permuted labelling can be the other equally good card.
Not worth revisiting unless the key computation gets much cheaper.

**The safe-nil proof run mid-trick.** Unlike the set proof, the safe proof is
sound at every ply, not only at a trick boundary — `relevant_cards` already
carries the one played card that can still decide anything, and the induction
goes through with one extra condition (the nil bidder must not already be
winning the trick in progress, which is one `trick_winner` call). It is also
attractive on paper: the nil bidder's own awkward middling card leaving its hand
is the commonest way for the proof to *become* true, and catching that at the
ply it happens skips the rest of the trick and everything under it.

Built and measured, best of three, twenty deals at 13 cards, seed 3: 49,803,320
nodes against 50,241,863 for the boundary-only version — 0.87% fewer — bought at
5.9% of wall time, because the extra `trick_winner` and `trick_best_card` land on
every node rather than one in four. Net wall time came out *worse*, which is the
same verdict and the same shape as side-suit canonicalization above. Worth
re-measuring only if move ordering (item 6) changes the node mix enough that the
proof starts firing on a different population.

---

## The work, highest impact first

### 1. ~~Equivalent-card reduction in the move generator~~ — ⭐⭐⭐⭐⭐ — **done, patch 8**

Landed. Patch 7 collapsed rank-equivalent positions in the transposition *key*
and did nothing to `legal_moves`, which still enumerated every legal card;
`distinct_moves` now emits one representative per equivalence class — a run of
legal cards with nothing relevant between them — and the branching factor drops
directly. See the Done table above for what it bought.

Two details worth carrying forward, because the next items have to keep faith
with them:

- **The relevant set is the four hands plus the card currently winning the
  trick, and nothing else.** The losing cards of the trick in progress are as
  dead as cards from finished tricks, because `beats` is only ever asked about
  the running best.
- **The representative is the canonically lowest member.** That is what kept the
  principal variation identical, and hence the card-for-card oracle check. It
  was the last item on this list with that property; everything from item 4
  onward changes which equal move gets picked. Item 7 is what replaces the check
  when that happens.

### 2. ~~Boolean / lexicographic mode split~~ — ⭐⭐⭐⭐⭐ — **done, patch 9**

Landed. `SearchOptions::mode` selects `MODE_FULL` (unchanged: trick counts, a
principal variation, and a value the replay verifier checks itself against) or
`MODE_FAST`, whose weights are `(1, 0, 0)` — primary weight **1**, not `K*K`, so
the search value is the nil bidder's trick count and item 3's window is
literally `[0, 1]` rather than a slice of a scalar spanning thousands.

The fork exists in the C ABI too: `NIL_FLAG_FAST_MODE`, `--mode fast` on
`nil_cli`, `--mode full|fast|both` on `nil_bench`. `nil_fails()` selects fast
mode on its own, since the boolean is that function's entire output.

Three details worth carrying forward:

- **Fast mode withholds the trick counts** (`NIL_TRICKS_UNKNOWN` / `-1`, not
  `0`, because zero is a real answer). They are exactly right today — the value
  *is* the count — but items 3 and 4 turn it into a bound, and a caller who had
  come to read the number would not find out. Withholding it now means nothing
  has to be taken away later.
- **Fast + `nil_already_set` short-circuits** to `nil_fails = 1` with zero
  nodes. That flag asserts the only thing this mode computes.
- **Fast mode has no internal witness.** Full mode replays its own PV and
  re-derives the value from the replayed counts; fast mode has no PV, so what
  stands in is that the two modes must agree on `nil_fails`. That is the
  `corpus_modes` test and `nil_bench --mode both`. It is free evidence while
  nothing prunes and it is the *only* evidence once item 3 lands, so it belongs
  in the default test run rather than in the drawer of things to reach for when
  something already looks wrong.

### 3. ~~Nil-specialized alpha-beta / AND-OR search~~ — ⭐⭐⭐⭐⭐ — **done, patch 10**

Landed, and it is the largest single win on this list. `MODE_FAST` searches the
null window `[0, 1]`, fail-soft, which makes it the AND-OR search the boolean
question always wanted: the opponents need *one* line that forces a trick onto
the nil bidder, the nil side needs *every* opponent line to fail, and the first
answer either way ends the node. See the Done section for what it bought.

Four details worth carrying forward, because items 4 through 7 all have to keep
faith with them:

- **The window is never narrowed, and that is what keeps full mode
  exhaustive.** There is no `alpha = max(alpha, best)` anywhere. Nothing is
  lost by leaving it out — a null window has no integers strictly inside it, so
  a fast-mode node either cuts or leaves the window alone — and the gain is
  large: `MODE_FULL` passes sentinels no value can reach, and with no narrowing
  that means it provably never cuts. Full mode is unpruned *by construction*
  rather than by a flag someone could fail to set, which is why its node
  counts, its move choices and its principal variation came through this patch
  untouched. Restore the narrowing and full mode starts pruning silently.
- **Item 4's first bullet shipped here, as window arithmetic rather than as a
  rule.** At a trick boundary, when what the trick banked already reaches beta
  and no later trick can take it back, the subtree cannot change which side of
  the window the value lands on, so `gained` is returned without recursing.
  That is Chang's `if (goal <= 0) return 1`, arrived at from the window. It is
  guarded on all three objective weights being non-negative, which is a fact
  about the weights and not about the mode.
- **That cutoff also keeps the fast-mode window uniform, and the table whole.**
  The only non-zero gain in fast mode is the nil bidder taking a trick, worth
  exactly 1, and beta is exactly 1 — so the cutoff intercepts every gain that
  could have shifted the window, and *every node of a fast search sees
  `[0, 1]`*. Every entry is therefore a bound on the same window as every
  probe, and no probe that finds an entry fails to be answered by it. Bounded
  entries usually cost a table some of its hits; here they cost nothing.
  `Solution::tt_partial` is what measures this, and a selftest pins it at zero.
  If a later item varies the window, that number is where it will show up.
- **`corpus_modes` is now doing real work.** Before this patch the two modes
  walked the same tree and agreement was close to a tautology about two
  weightings of one enumeration. Now it holds a pruned answer against an
  unpruned one that `nil_oracle.py` has checked, which is the differential test
  a pruning bug would have to survive.

The three things the item said to watch for were all real, and all are handled:

- Full mode does not inherit the narrow window; see the first bullet, and
  `full mode is unchanged by the presence of a window` in the selftests.
- The cross-mode entry hazard is now locked twice. `new_search()` on every
  solve still bumps the generation, and on top of that every entry carries a
  `tag` naming the objective its value is on (the byte that used to be
  `reserved`), which `probe` requires to match. A fast-mode `BOUND_LOWER` read
  by a full-mode search is now a miss rather than a wrong answer. Selftests
  interleave the two modes on one shared table in both orders.
- `corpus_modes` caught nothing, which is the outcome to want and not evidence
  it was unnecessary.

*One gap this opened, and closed:* fast mode can now answer hand sizes full
mode cannot finish, which means `corpus_modes` — its only witness — stops
reaching it somewhere around ten cards. `tools/invariants.py` grew a `--mode`
flag for exactly that: the seat-rotation, suit-permutation and
rank-compression transforms need no reference answer, so they are the only
verification the boolean search has up there. It holds at 12 cards. An
`invariants_fast` ctest runs a small version of it on every build.

### 4. ~~Nil-set and nil-safe static bounds~~ — ⭐⭐⭐⭐⭐ — **done, patch 11**

Landed. Two proofs, checked at a trick boundary before the transposition probe,
that settle a position without searching it. They are Chang's quick-trick check
asked in the shape the nil question wants: he skips the search when a SIDE can
win a trick, and the question here is whether a trick can be forced onto a SEAT,
so what carries over is the idea rather than either of his tests. See
`src/nil/bounds.hpp`, which carries both arguments in full.

- ~~**Nil already set.**~~ Shipped in patch 10, out of the window arithmetic.
- **Nil provably safe.** The nil bidder holds no spades; in every suit it holds,
  every card is below every outstanding card of that suit, or nobody else holds
  that suit at all; and it is not on lead. Then it wins no trick down any line,
  and the subtree's value is zero *exactly* rather than by a bound.
- **Nil provably set.** Rank the outstanding spades with 1 highest. Holding the
  1st, or the 2nd and 3rd, or the 3rd through 5th, or the 4th through 7th — a
  block of k starting at rank k — forces a trick, because only k-1 higher spades
  exist outside the block and each can bury at most one of it. The general form
  the code tests is `∃j : r(j) ≤ 2j - 1`, which also catches slack blocks like
  {2nd, 3rd, 9th}.

**What it bought, and the part that did not go to plan.**

| workload | with | without | change |
|---|---:|---:|---:|
| corpus, 4 cards | 16,418 | 25,314 | −35.1% |
| corpus, 5 cards | 19,082 | 24,888 | −23.3% |
| corpus, 6 cards | 15,482 | 21,074 | −26.5% |
| corpus, all 560 | 50,982 | 71,276 | −28.5% |
| random, 7 cards, seed 1 | 1,342 | 1,762 | −23.8% |
| random, 8 cards, seed 1 | 7,888 | 8,360 | −5.6% |
| random, 9 cards, seed 1 | 43,544 | 46,153 | −5.7% |

Both columns are the same binary, `--no-static` being the only difference, so
this is a differential rather than a comparison across builds.

**This item was written for the tail, and the tail is where it did least.**
Twenty deals at 13 cards, best of three, both arms on the same binary:

| seed | nodes with | without | nodes | wall time |
|---|---:|---:|---:|---:|
| 3 | 50,241,863 | 53,798,662 | −6.6% | 10.0 s → 11.2 s (−10.7%) |
| 11 | 141,136,295 | 160,102,734 | −11.8% | 30.8 s → 33.3 s (−7.5%) |
| 42 | 160,744,139 | 165,550,184 | −2.9% | 34.2 s → 36.6 s (−6.5%) |

Consistently positive, never negative, and modest: call it 7-11% of wall time at
13 cards against 28% of nodes on the corpus. Throughput is flat to slightly up
(4.6-5.0M nodes/sec in both arms), so nothing is being paid for this — the
proofs run at one node in four and the first thing either asks is a single mask
AND, and the nodes they remove were not the cheap ones.

**Why the tail resisted, measured rather than guessed.** Instrumenting the
twenty seed-3 deals: 12,274,325 trick-boundary nodes, of which the safe proof
settled 2.87% and the set proof 1.67%. The gate is not the spade condition —
the nil bidder holds no spade at 76.5% of those nodes — it is condition 2. Deep
in a contested 13-card deal the nil bidder holds a scatter of middling cards,
and one card ranked above one outstanding card is enough to stop the proof. An
expensive deal is expensive precisely because the nil is genuinely borderline,
which is the case in which neither proof has anything to say.

That is worth stating plainly because the previous version of this entry
predicted the opposite. The safe-nil proof does fire often — it is the single
most-fired cutoff added since patch 10 — but the deals where it fires early are
the deals that were already cheap.

*Note:* "outstanding" is the same relevant set item 1 computes, and
`nil_cannot_be_forced` calls `relevant_cards` rather than re-deriving it, so the
two cannot drift.

*What checks it.* The proofs are one-sided — each proves its answer when it
fires and says nothing when it does not — so switching them on may only remove
work and can never change a boolean. `--no-static` and `NIL_FLAG_NO_STATIC_BOUNDS`
are the control arm, on the `--no-collapse` model, and the `corpus_static` ctest
runs the whole corpus with the proofs off against the oracle's recorded answers
while `corpus_modes` runs it with them on. Beyond that: static-on against
static-off over random deals at 8 (60), 10 (40), 11 (20) and 13 (11) cards, zero
disagreements; invariance transforms at 10, 12 and 13 cards; 200 crosscheck
cases at 5 cards agreeing with `nil_oracle.py` card for card.

*And the thing that had to stay true.* `MODE_FULL` never takes a static cutoff,
and not because a flag says so. The gate is `value_is_nil_tricks`, read off the
weights — primary 1, secondary 0, tertiary 0 — which is `MODE_FAST`'s objective
and no full-mode weighting, including K = 1 where the primary is also 1 but the
secondary is not zero. Each proof settles the nil bidder's own trick count and
says nothing about the pair's total or the split between the two partners, so it
cannot settle the full objective. The corpus still comes in at 2,647,731 nodes.

### 5. Transposition-table move ordering — ⭐⭐⭐⭐

**Unblocked by patch 10, and now worth something. Nearly free.**

The best move is already stored in each entry and already survives the
relabelling. Try it first at every node with a table hit. In alpha-beta this is
usually the single most effective ordering heuristic and costs one array read.

*Watch for:* the stored move must still be a member of the reduced move set. It
is today, because the reduction is a function of the key and the stored move is
the representative that reduction chose — but it is an invariant now, not an
accident, and it should be asserted rather than assumed.

*And one thing patch 10 changed here.* `probe` now returns an entry only when it
settles the window being asked about; a match it cannot answer is counted as
`partial` and reported as a miss. That is the right call while nothing wants the
move, and it is the wrong call for this item, which wants the move off exactly
those entries. Widen `probe` when this lands — the comment in `tt.hpp` says so —
rather than reading the value off an entry that does not settle anything.

### 6. General move ordering — ⭐⭐⭐⭐⭐

**Was worth exactly zero before alpha-beta; patch 10 turned it on.** With no
pruning, ordering only changes which of several equal moves is picked; it cannot
reduce work. After alpha-beta it is worth as much as alpha-beta itself, because
every cutoff the search takes is a cutoff it could have taken sooner.

Note that ordering only affects `MODE_FAST`, since `MODE_FULL` does not cut and
never will. That is a useful narrowing: none of the heuristics below have to be
safe for the principal variation, only for the boolean.

For the nil question specifically, the orderings that matter are not the usual
trick-maximizing ones:

- Opponents leading: prefer suits where the nil bidder is short or holds a
  card that can be trapped.
- Nil side: prefer the lowest card that cannot win; prefer discarding
  dangerous high cards from the nil bidder's hand.
- Cover partner: prefer plays that take the trick over the nil bidder's head.

Ship this together with item 7.

### 7. A `--canonical` verification mode — ⭐⭐⭐⭐

**Ship with item 6, not after it.**

Item 6 destroys the card-for-card oracle check by design, and that check is
still the strongest evidence the solver has — patch 10 deliberately did not
spend it, and full mode came through node for node. Add a flag that disables every
principal-variation-affecting heuristic — move ordering, killers, history — and
falls back to the canonical enumeration order, so `crosscheck.py` keeps running
against a mode that is still comparable. Let the fast path diverge freely.

Without this, the differential test disappears exactly when the search becomes
complicated enough to need it.

*Note:* the equivalent-card reduction does **not** belong behind this flag. It
preserves the PV, so canonical mode should keep it on and stay fast enough to be
run in bulk. `--no-collapse` already exists as a separate switch for the one
question it answers — whether the reduction itself is sound — and is not part of
this mode.

### 8. Quick-trick / forced-trick analysis — ⭐⭐

**Downgraded: item 4 took the part of this that applies.** The nil-set proof is
forced-trick analysis for the one seat the question is about, and the safe-nil
proof is its complement; what is left here is the general side-level version.

Weaker here than in a trick-maximizing double-dummy solver. Quick tricks bound
"how many tricks can this side take"; the nil question is "can one trick be
forced onto *this seat*". A side taking six tricks says nothing about whether
the nil bidder took one of them.

Rises to ⭐⭐⭐⭐ in `nil_already_set` mode, where the problem degenerates to
ordinary trick maximization and the standard analysis applies directly.

### 9. Suit and void analysis — ⭐⭐⭐

Tracking who is void in what, and which suits are exhausted, feeds the move
ordering in item 6 and the safe-nil proof in item 4. Useful, but it mostly folds
into those rather than standing alone as a separate win.

Patch 11 gave this a specific target. The safe-nil proof fails at roughly
three-quarters of trick-boundary nodes on a hard 13-card deal, and it fails on
condition 2 — one card of the nil bidder's ranked above one outstanding card.
A void map would not rescue those directly, but knowing which suits can still be
*led* is the ingredient a weaker-and-still-sound condition 2 would need, and
that is the one place a stronger safe proof is likely to come from.

### 10. Killer and history heuristics — ⭐⭐

Cheap to add once alpha-beta is in, but transfer badly between card-game nodes:
the legal move set churns completely from ply to ply, so a killer from a sibling
is often not even legal. Measure rather than assume.

### 11. Incremental state / key updates — ⭐⭐

**In direct tension with the thing that just gave 20x.** Zobrist hashing over
absolute cards is trivially incremental; over *relative* ranks it is not,
because playing a card renumbers every slot above it in that suit. Making the
key incremental means giving up rank compression, which is worth far more.

Profile before spending anything here. At roughly 4M nodes/sec, key construction
is unlikely to be the bottleneck.

Patch 10 moved this a little, in both directions. Fast mode now visits so few
nodes that per-node cost barely registers against fixed setup — at 7 cards a
whole position is 1,762 nodes and the table allocation dominates the timing.
But the deals that still hurt are the ones that visit tens of millions of nodes,
and there per-node cost is the only thing there is. If this is ever worth doing,
it is worth doing for the tail.

*One candidate that is not in tension:* the union of the four hands, which
`relevant_cards` rebuilds at every node and which loses exactly one bit per ply.
Carrying it in the search state is trivially incremental. It was left out of
patch 8 on purpose — it has to be maintained in both the search and the PV walk,
and two copies of an invariant is how a wrong answer gets in. Worth doing behind
a single shared state-transition helper, not before.

### 12. Endgame tablebases — ⭐⭐

The transposition table already captures reuse *within* a search. Cross-search
reuse needs a persisted table of canonical endings, which the state key makes
representable but which is a large amount of storage and build tooling for a
gain that overlaps with what the table already does.

### 13. Parallel root search — ⭐⭐

Blocked on architecture: the table is currently `thread_local`, so this needs a
shared, lock-free or sharded table first. Root parallelism also scales poorly
with alpha-beta, since the whole point of the ordering work above is that the
first move usually refutes. For a DLL answering one query at a time from a game
client, latency matters more than throughput.

### 14. Partition / set search — ⭐

Ginsberg's technique, and the highest ceiling on this list — it is a large part
of what made GIB work. Also the hardest to keep exact, and the hardest to verify
against an oracle. Not now, but worth revisiting once everything above has
landed and the growth curve is known.

---

## Suggested sequence

```
1 ✅ → 2 ✅ → 3 ✅ → 4 ✅ → 5 → (6 + 7 together) → 9 → 8 → 10 → measure → 11..14
```

Item 1 went first because it was the last PV-preserving win, and it is banked.
Item 2 is banked too and bought nothing on its own, which was the plan: it is
the shape item 3 needed, settled while it was still cheap to settle — and it
paid for itself many times over the moment item 3 landed on top of it. Items 6
and 7 together, because shipping move ordering without a verification fallback
trades away the solver's best evidence for speed.

The sequence below item 3 is unchanged but its purpose is not. It was a plan for
reaching 13 cards; 13 cards is reached. Read items 5 and 6 as work on the tail —
the deals that still take seconds — and judge them on the slowest positions
rather than on an average that one bad deal already owns.

Item 4 was judged that way and came back with 7-11%, against a prediction of
much more. That is worth carrying forward as a caution about the rest of the
list rather than only as a result: a hard deal is hard because the nil is
genuinely contested, and a static proof is by construction silent exactly there.
Items 5 and 6 do not have that problem — a cutoff taken sooner is a cutoff on
the contested line itself — which is the reason they are next and the reason to
expect more from them. Items 8 and 9 have swapped, because what is left of 8 is
the part item 4 did not take, and 9 now has a concrete target.

---

## Baseline to measure against

Recorded after patch 8, 32 MiB table, single core. Node counts are exact and
deterministic at a fixed table size; timings are indicative.

| workload | nodes/position | ms/position |
|---|---:|---:|
| corpus, 4 cards (400 positions) | 1,038 | 0.24 |
| corpus, 5 cards (120 positions) | 7,462 | 1.54 |
| corpus, 6 cards (40 positions) | 33,423 | 7.20 |
| random, 7 cards | 425,937 | ~104 |
| random, 8 cards | 2,019,207 | ~490 |
| random, 9 cards | 23,519,702 | ~4,645 |

The random rows are `nil_bench --random --seed 1`, ten positions at 7 and 8
cards and five at 9. Node counts are exact; the timings were taken on one core
of a shared machine and move by up to 30% between runs, which is the usual
reason to compare nodes rather than milliseconds.

The *fraction* the reduction removes grows with hand size, so the effective
growth rate per card has come down a little. How much is not worth quoting: five
positions at nine cards is not a sample.

Those rows are full mode, and they are unchanged by patches 9 and 10 — neither
touches the default path, and the 9-card row was re-measured after patch 10 and
came back at exactly 23,519,702. **Full mode's node counts are a fixed point:
any movement in them is a bug, not an optimisation.** That is the sharpest
regression test in this file, and it costs 23 seconds to run.

Fast-mode rows stopped being comparable to them the moment patch 10 landed,
which is what the `+fast` suffix on the memo column has been for. Its own
baseline, recorded after patch 10 at 32 MiB, single core:

| workload | patch 10 | patch 11 | ms/position |
|---|---:|---:|---:|
| corpus, all 560 rows | 127 | 91 | — |
| random, 7 cards (10, seed 1) | 1,762 | 1,342 | ~4 |
| random, 8 cards (10, seed 1) | 8,360 | 7,888 | ~3 |
| random, 9 cards (10, seed 1) | 46,153 | 43,544 | ~12 |
| random, 13 cards (20, seed 3) | 2,689,933 | 2,512,093 | 501 |
| random, 13 cards (20, seed 11) | 8,005,136 | 7,056,814 | 1,542 |
| random, 13 cards (20, seed 42) | 8,277,509 | 8,037,206 | 1,709 |

The three 13-card seeds are there because one is not a sample: seed 3 is 80%
one deal, and quoting it alone would have made patch 11 look either better or
worse than it is depending on which deal moved.

Two warnings about reading that table. The node counts are no longer monotone in
hand size — 10 cards comes in below 9 — because with pruning the cost of a
position depends far more on whether the nil is *contested* than on how many
cards are in it, and five deals is not enough to average that out. And the
millisecond column at the small sizes is mostly the one-off 32 MiB table
allocation rather than search: at 7 cards a whole position is under two thousand
nodes. Compare nodes, at a fixed seed and count, or compare nothing.

**Note on comparability.** The table is bounded, so node counts depend on
`--tt-mb` once a search overflows it. `nil_bench` records the size in the
history file's `memo` column and `bench_history.py` groups on that column, so
runs at different sizes are never compared as regressions. Keep the size fixed
across a comparison. `--no-collapse` now appends `+nocollapse` to that same
column for the same reason: it multiplies the node count, and it must not read
as a regression against a normal run. `--no-static` appends `+nostatic` on the
same grounds, and only on fast rows, since the proofs are inert elsewhere and
the suffix would split the full-mode history into two groups holding identical
numbers.
