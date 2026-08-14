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
| ✅ | ~~Canonical re-derivation: move ordering for `MODE_FULL`~~ | patch 25 | ordering is a pure reorder, so only the TIE-BREAK ever moved. Re-derive the reported move canonically after the search and it stops moving: **1.82x on the 13-card interleaved deal** (263M -> 146M nodes, 104 s -> 57 s), 1.23x on random 9-card, 6.9% on the corpus. All 560 oracle-pinned values AND principal variations reproduce with ordering on, which patch 24 could not manage. Retires the `nil_already_set` exclusion |
| ✅ | ~~Presolve-seeded root window for `MODE_FULL`~~ | patch 23 | a `MODE_FAST` presolve costs a thousandth of the full search and bounds it: nil safe puts the packed value out of reach of the range where it fails, so beta closes onto the answer. **Turns the 13-card interleaved deal from unreachable into 40 s** (110,681,989 nodes), 1.20x on nil-safe 9-card positions. Costs 3% on nil-fails positions and is gated off below 8 tricks, where it returned nothing. Same value, same PV; all 560 oracle-pinned rows reproduce |
| ✅ | ~~Window narrowing: alpha-beta for `MODE_FULL`~~ | patch 22 | the missing half of patch 10. `alpha = max(alpha, best)` at a maximiser, `beta = min(beta, best)` at a minimiser, so full mode's cutoff becomes reachable for the first time. **68.8x fewer nodes and 62.9x less wall at 9 cards** (23,519,702 -> 342,000 nodes/position), **6.09x nodes / 5.36x wall on the corpus**. `MODE_FAST` unchanged node for node, provably. All 560 oracle-pinned values and principal variations reproduce card for card |
| ✅ | ~~Per-card move list across the ABI~~ | patch 21 | `nil_solve_moves` scores every legal card, not just the best: one row per equivalence class with the boolean, the trick counts in full mode, DDS-shaped `equal_ranks`, and an `is_best` flag. **1.0x nodes, +0.4% wall** against the plain call — the root search warms the table and the per-card searches mostly read it back. `MODE_FULL` unchanged node for node |
| ✅ | Bitboard state representation | patch 1 | `Hand = uint64_t`, `suit * 16 + rank`, ctz move extraction, mask-based `legal_moves` |
| ✅ | Compact canonical state key | patch 7 | 21 + 2n bits at a trick boundary; rank-relative, trick-as-threshold |
| ✅ | Bounded transposition table | patch 7 | 4-way buckets, evict-shallowest, generation-stamped, full 128-bit key stored |
| ✅ | Equivalent-card reduction in the move generator | patch 8 | one representative per run of legal cards contiguous in the relevant set; −18% nodes at 4 cards rising to −55% at 9, and the principal variation is unchanged |
| ✅ | Boolean / lexicographic mode split | patch 9 | `MODE_FAST` zeroes the tie-break weights and gives the primary weight 1, so the value is the nil bidder's trick count and the window is literally `[0, 1]`; no speed change yet, by design |
| ✅ | Nil-specialised alpha-beta / AND-OR search | patch 10 | null window `[0, 1]` in `MODE_FAST`, fail-soft, bounds in the table; −12.8x nodes at 4 cards rising to −303x at 9, and 13-card hands answer in a median 150 ms. `MODE_FULL` searches between unreachable sentinels and is unchanged node for node |
| ✅ | Nil-safe and nil-set static bounds | patch 11 | two proofs at a trick boundary, `MODE_FAST` only: −28.5% nodes on the corpus, −3% to −12% at 13 cards. Wall time −6.5% to −10.7% at 13 cards; throughput unmoved. `MODE_FULL` unchanged node for node |
| ⊘ | Two-tier table replacement (item 15) | patch 20 | **evaluated and rejected, reversing patch 12's provisional result.** −1.7% nodes across the three 13-card seeds but +0.3% wall, and it spends `MODE_FULL`'s node-count fixed point. Item 6 is what changed the answer |
| ✅ | Move ordering: the nil bidder off-suit (6d) | patch 19 | discard the highest card that still loses, from the suit closest to running out of covers. −13.5% nodes on top of 6a + 6b across the three 13-card seeds, −32.1% at 11, −15.2% on the corpus. The *leading* half was measured and dropped |
| ⊘ | Move ordering: the cover partner (6c) | patch 18 | **evaluated and rejected.** Covering over the nil bidder's head is a no-op (−16 nodes on 13 million); taking the trick before it plays helps at ≤11 cards and is +18% to +108% worse on all three 13-card seeds. Nothing shipped |
| ✅ | Move ordering: the opponents on lead (6b) | patch 17 | attack the suit whose holding runs short of covers soonest, lowest card. −21.5% nodes on top of 6a across the three 13-card seeds, and it **repairs 6a's seed-11 regression** — every recorded workload is now at or better than canonical. Throughput unmoved |
| ✅ | Move ordering: the nil bidder following suit (6a) | patch 16 | promote the highest card that loses to the current best. −45.7% and −40.8% nodes at 13 cards on two of three seeds, **+1.3% on the third**, −65.2% at 11 cards. Throughput flat, so wall time tracks. `MODE_FULL` unchanged node for node |
| ✅ | A `--no-ordering` control arm | patch 15 | `SearchOptions::order_moves`, `NIL_FLAG_NO_ORDERING`, `--no-ordering` on both tools, `+noordering` on fast rows, and a `corpus_ordering` ctest. Inert by design — the switch, the ABI bit and the differential all land before 6a has anywhere to hide. Zero nodes changed |
| ✅ | Transposition-table move ordering | patch 12 | **closed as refuted, not implemented.** The population is empty and provably so: `tt_partial` ≡ 0, so no node ever holds a stored move and still has moves to search. Zero nodes changed. Shipped instead: `--tt-stats` on both tools, and the premise pinned by a swept selftest |

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

**Chang's depth guard on table replacement.** Measured while closing item 5,
because `--tt-stats` made the eviction rate visible for the first time and it is
alarming: 87.9% of stores displace a live position at 13 cards and **98.5% at
11**. The obvious fix is in the paper. Chang replaces a stored state only when
its `tricks_left` is not greater than the current state's — he declines to
overwrite a deeper entry with a shallower one — and `store()` has no such guard,
so a depth-2 entry can and does evict a depth-20 one.

It is a disaster here. Four policies against the incumbent, 13 cards, seed 3:

| replacement policy | nodes (5 deals) | vs incumbent |
|---|---:|---:|
| **incumbent: evict shallowest live** | **5,526,575** | — |
| Chang's guard: decline to replace deeper | 49,856,325 | **+802%** |
| evict *deepest* live | 12,879,939 | +133% |
| always-replace, one fixed way | 93,781,764 | +1597% |

The guard ossifies a bucket: four deep entries land early, nothing shallower may
displace them, and the bucket is dead for the rest of the search. Chang has an
escape hatch for exactly this — he also replaces once a certain number of hash
collisions has occurred in the entry — and this solver has no collision counter
to hang one on.

**But the deeper reason it fails is a difference in the question being asked,
and it is worth carrying forward.** Chang's top level bisects on `goal` and
calls `ddsearch` three or four times on the same deal, so a deep entry near the
root is paid for again on the next iteration. This solver asks one boolean once.
There is no second pass, the working set is the shallow frontier the search is
currently grinding through, and preferring depth keeps the cold entries while
evicting the hot ones. **Read the paper's table advice as conditioned on its
iterative top level.** The same caution applies to item 12.

*One variant did survive, and it is not rejected — it is item 15 below.*

**Move ordering for the cover partner (6c).** The third phase of item 6, built
and measured against 6a + 6b, and the first thing on this list rejected on
*nodes* rather than on throughput. Two arms, because the entry described two
different plays and they are not the same bet:

| workload | 6a + 6b | cover only | cover **and** protect |
|---|---:|---:|---:|
| corpus, 560, fast | 47,877 | 47,877 | 44,890 (−6.2%) |
| random, 9c (20, seed 1) | 564,756 | 564,756 | 542,356 (−4.0%) |
| random, 11c (10, seed 3) | 47,219,449 | — | 34,214,794 (−27.5%) |
| random, 13c (20, seed 3) | 12,952,377 | 12,952,361 | 15,327,933 (**+18.3%**) |
| random, 13c (20, seed 11) | 124,650,866 | — | 176,524,405 (**+41.6%**) |
| random, 13c (20, seed 42) | 70,710,823 | — | 147,254,248 (**+108.2%**) |

**Covering over the nil bidder's head does nothing.** Sixteen nodes on thirteen
million, and byte-identical totals on the corpus and at 9 cards. The play is
correct and it is what a human does; it is simply not *information the search
lacked*. Two reasons, and both are the shape of a promotion that was already
free: when the nil bidder is winning mid-trick and the partner cannot beat it
the node fails high immediately and is never ordered at all, and when the
partner can beat it the cheapest cover is often already the canonically first
card. A promotion that agrees with the canonical order is a branch and a loop
for nothing.

**Taking the trick before the nil bidder plays is worth a great deal up to 11
cards and is a disaster at 13.** −27.5% at 11 cards, and then +18%, +42% and
+108% on the three 13-card seeds — 208,314,066 → 339,106,586 across them, +62.8%.
Not shipped, obviously; but the transition is sharp and it is not explained,
which is worth saying plainly rather than dressing up. The plausible story is
that the partner's high cards *are* its covering resource, so spending one early
to take a trick the nil bidder was not going to lose is precisely wrong once
enough tricks remain for the cover to be needed again — and at 9 to 11 cards
there is no "again". That predicts the sign flip but not how abrupt it is, and
nothing here tests it.

*Where that leaves the partner.* Unordered, and 6a and 6b are why that is
tolerable: between them they order the seat that is in trouble and the seat
attacking it, and the partner's move mostly matters through what those two do
with it. Worth revisiting only with a rule that distinguishes a cheap take from
an expensive one, since the arm that failed spends the cover unconditionally.

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

### 5. Transposition-table move ordering — ⭐⭐⭐⭐ — **RE-OPENED by patch 22, for `MODE_FULL` only**

> **Re-opened.** The closure below is still correct *for `MODE_FAST`*, and the
> theorem that carries it is untouched: a null window admits no integers, so
> every fast entry settles every window it is probed against and `tt_partial`
> stays identically zero there. What changed is the other mode. Patch 22 gave
> `MODE_FULL` a window that narrows, which is exactly the condition the
> *What would revive it* paragraph below names, and `tt_partial` went non-zero
> in full mode on the first run — 40 partials on the alpha-beta selftest
> position alone. The selftest sweep is now split by mode and asserts both
> halves, so the revival is a pinned fact rather than an inference.
>
> The population this item wanted therefore exists now, in the mode that has
> the nodes to spend. The move is still stored in every entry, still survives
> the rank relabelling, and would still be free. **Two things must be settled
> first**: the stored-move-is-in-the-reduced-set invariant at the bottom of
> this entry, which acquires load the moment anything reads a stored move for
> ordering; and the principal-variation question in item 22b, since ordering a
> full-mode node by a table move is ordering, with the same tie-break cost.

Not implemented, and not because it was measured and found weak. It has nothing
to act on, and that is a theorem rather than a measurement.

The item wanted the stored move off entries that `probe` counts as `partial` —
a match holding a bound too weak to settle the window — and the previous version
of this entry said to widen `probe` to hand them back. There are none to hand
back. Patch 10's third bullet already contains the argument, read from the other
side: in `MODE_FAST` every node is asked about `[0, 1]`, and a stored value is
either `BOUND_UPPER` at 0 (≤ alpha) or `BOUND_LOWER` at ≥ 1 (≥ beta), with no
integers in between. So **every entry that matches the position settles the
window**, and therefore:

- a node that finds an entry returns from it and never looks at a move;
- a node that searches its moves never found an entry.

There is no node that holds a stored move *and* has moves left to order. The
population this item wanted is exactly the set counted by `tt_partial`, and
`tt_partial` is identically zero. *(As of patch 22 the sentence that stood here
about `MODE_FULL` — that it stores only exact values, so a hit there is total —
is no longer true. Full mode narrows its window, cuts, and stores bounds like
any other alpha-beta searcher. It is the mode this item is now about.)*

Measured anyway, because a theorem about a search is worth one run against the
search:

| workload | nodes | tt hits | **tt_partial** |
|---|---:|---:|---:|
| corpus, all 560, fast | 50,982 | 6,616 | **0** |
| random, 9 cards (20, seed 1) | 732,753 | 191,825 | **0** |
| random, 13 cards (20, seed 3) | 50,241,863 | 15,922,929 | **0** |
| random, 11 cards (10, seed 3) | 135,880,493 | 43,367,835 | **0** |

Forty-three million table hits at 11 cards and not one of them left a node with
work to do.

*What shipped instead.* Reading `tt_partial` required writing a throwaway
program, because neither tool could print table statistics — which is a poor
state for a list that judges everything on measurement. `--tt-stats` now exists
on `nil_bench` and `nil_cli`, reporting probes, hits, partials, stores and
evictions, and calling out both an all-zero `partial` and an eviction rate over
90%. No search code changed: the corpus is 50,982 nodes in fast mode and
2,647,731 in full, unmoved.

*What would revive it.* `tt_partial` going non-zero, which happens the moment
some item varies the window between nodes — aspiration windows, a second goal,
a non-null window in fast mode. A swept selftest now pins it at zero across 480
solves in both modes, so that change announces itself rather than being noticed
later; `--tt-stats` says the same thing in one line on any workload. **If that
number ever moves, re-open this item first** — the move is still stored in every
entry, still survives the relabelling, and would still be free.

*The one invariant that was never tested, and now never needs to be.* The item
warned that a stored move must still be a member of the reduced move set.
It is — the reduction is a function of the key and the stored move is the
representative the reduction chose — but nothing reads a stored move for
ordering, so the invariant has no load on it. It acquires load the moment this
item re-opens, and it should be asserted then.

### 6. General move ordering — ⭐⭐⭐⭐⭐

**Was worth exactly zero before alpha-beta; patch 10 turned it on.** With no
pruning, ordering only changes which of several equal moves is picked; it cannot
reduce work. After alpha-beta it is worth as much as alpha-beta itself, because
every cutoff the search takes is a cutoff it could have taken sooner.

Note that ordering only affects `MODE_FAST`, since `MODE_FULL` does not cut and
never will. That is a useful narrowing: none of the heuristics below have to be
safe for the principal variation, only for the boolean. It is also why item 7
turned out to be a control arm rather than a mode — see that entry.

**Three roles, three patches.** The heuristics below are independent, they pull
on different seats, and there is no reason to expect them to pay equally. Ship
them one at a time:

| | patch | what it orders |
|---|---|---|
| **6a** | ✅ patch 16 | the nil bidder **following suit** |
| **6b** | ✅ patch 17 | the opponents on lead |
| **6c** | ⊘ patch 18 | the cover partner — *measured, rejected, nothing shipped* |
| **6d** | ✅ patch 19 | the nil bidder **off-suit** — discarding ships; *leading measured and dropped* |

6a and 6d split the nil bidder in two because they need different machinery.
Following suit, "the highest card that can still lose" is a bit scan against the
card currently winning. Off-suit it is a comparison ACROSS suits, and that needs
a measure of how dangerous a holding is — see 6d.

Shipped together, a 20% win and a 20% loss cancel and the entry records
"roughly nothing" about three separate ideas. That is not a hypothetical worry
on this list: item 4 came in at a third of its prediction, item 5 at exactly
zero, and both were only legible because they were measured alone. Take 6a
first — it is the clearest rule and probably the largest, since the nil bidder's
own card leaving its hand is what makes the safe-nil proof become true.

Two constraints that apply to all three:

- **A partial order, not a sort.** The move loop is a bitmask and
  `take_lowest`, which costs nothing. Promoting one or two candidates to the
  front keeps that shape. Building an array and sorting it at every node is how
  this becomes the third entry in *Evaluated and rejected*: side-suit
  canonicalization died at 12% of throughput for 5-8% of nodes, and the
  mid-trick proof at 5.9% of wall time for 0.87%. Neither lost on nodes.
- **The control arm ships first, not retrofitted.** Every measurement here
  should be a differential on one binary with one flag between the two columns,
  the way patch 11's was.

For the nil question specifically, the orderings that matter are not the usual
trick-maximizing ones.

**Order by seat, not by side.** An earlier version of this entry had a bullet
headed "nil side" that lumped the nil bidder together with its partner and told
both to prefer the lowest card that cannot win. That is right for the partner
and exactly backwards for the nil bidder, whose whole problem is getting rid of
high cards. The two are separate entities with opposite preferences, and there
are three roles here, not two:

- **The opponents on lead (6b).** ✅ *patch 17.* Attack the suit whose holding
  runs short of covers soonest — `cover_deficit_depth`, the same measure 6d
  wants — and lead its lowest card.

  **Not the suit the nil bidder is short in, and certainly not one it is void
  in.** This entry used to say "short", and short is a proxy for the thing that
  matters rather than the thing itself. A void is the lead the nil bidder most
  *wants*: it discards whatever frightens it most, for free. What the opponents
  want is a suit the nil bidder must follow to and cannot duck in, which is
  where Hall's condition fails soonest — depth 0 meaning the very next lead of
  that suit strands it. Lead low, so the nil bidder's card is the one left
  winning; a high lead does its ducking for it.

  Where no suit is short of covers, 6b declines rather than guessing. There is
  no ranking to be had in that case, and a promotion no better than the
  canonical order costs a branch and buys nothing.

- **The nil bidder (6a following suit, 6d off-suit).** Prefer *the highest card that can still lose*. That one
  rule covers every situation it can be in:

  - *Following, with a card on the trick it cannot beat.* The highest card
    below the current best is a guaranteed-safe shed — it loses this trick
    whatever happens after it — and it sheds more danger than anything lower.
    Try it first, then the cards ranked **above** it, then the low ones.
  - *Why the cards above it, when they win the trick as things stand.* Because
    as things stand is not how the trick ends. A card above the current best
    still loses if a later seat overtakes it or ruffs, and if it does lose, it
    is a bigger shed than the safe one. This is the ordinary nil play of
    dumping a high card under someone else's cover.
  - *Leading.* There is no current best, so there is no guaranteed-safe shed;
    the rule reduces to the highest card some later seat can still beat.
    Leading the king into a known ace sheds the king for nothing.
  - *Discarding.* ✅ *patch 19.* The highest card that still loses, from
    whichever suit is closest to running out of covers. The losing filter is
    not decoration: with a void the hand includes trumps, and a spade thrown on
    a side-suit lead ruffs and takes the trick.
  - *Leading.* ⊘ *measured and dropped, patch 19.* See below.

  **The speculative sheds are conditional, and the condition inverts the
  ordering when it fails.** If the nil bidder plays fourth to the trick, or no
  seat still to play holds a higher card of the suit and none is void with a
  spade it may legally play, then a card above the current best is a guaranteed
  trick-taker. The nil bidder minimises against `[0, 1]` and cuts on finding a
  zero, so a guaranteed winner returns 1 as soon as the trick completes and can
  never cut. Those cards belong at the **back** of the order, not the front.
  Double dummy makes the test exact and it is a few masks: does any later seat
  hold a higher card of the suit, or is any later seat void in it and holding a
  spade.

- **The nil bidder off-suit (6d).** Void in the led suit it discards; on lead it
  picks a suit. Both want the same thing the other way up — discard from the
  most dangerous suit, lead from the safest — so both need one measure of how
  dangerous a holding is, and rank is not it.

  **Danger is ducking supply, not rank.** The 4♥ with only the 2♥ and 3♥
  outstanding is lethal: nothing sits above it, so it wins the moment hearts are
  led. The A♥ held with the 2♥, 4♥ and 6♥ is comparatively mild: hearts must be
  led four times before the ace is ever reached.

  **The measure is already in the codebase.** `nil_must_take_a_trick` walks down
  from the ace keeping `held` (the nil bidder's cards at or above the current
  rank, call it *j*) and `above` (everyone else's strictly above the *j*-th),
  and fires when `held > above`. That is Hall's condition: the nil bidder
  escapes a suit only if each of its top *j* cards can be matched to a distinct
  higher outstanding card, so it is safe exactly when `above_j ≥ j` for every
  *j*. Danger is the slack in that inequality, and the count of cards below the
  first deficit is how many leads it takes to reach the trouble:

  ```
  danger(suit) = min { m - j : above_j < j }        // no deficit => safe
  ```

  with *m* the nil bidder's cards in the suit and *j* indexing from the top.
  Smaller is worse. The 4♥ gives `1 - 1 = 0`, maximal. The ace gives `4 - 1 = 3`,
  three leads of ducking supply first. One downward bit walk per suit, and it is
  the walk `bounds.hpp` already does — **share it rather than reimplement it**,
  on the same grounds as the note that `nil_cannot_be_forced` calls
  `relevant_cards` instead of re-deriving the union. One definition of "how
  covered is this holding", not two that can drift.

  *One caveat to label rather than discover.* The Hall count is a proof in
  spades and only a heuristic in a side suit, because the proof rests on every
  card being played and a spade losing only to a higher spade. In a side suit a
  card can be ruffed, and the suit may simply never be led enough times for the
  trouble to be reached. Both escapes help the nil bidder, so the count
  **overstates** danger off-trump — the right direction of error for an
  ordering heuristic, which can then only misorder and never miscount.

**What 6b bought, measured against `--no-ordering` on the same binary.** The 6a
column is patch 16's, restated so the incremental is visible:

| workload | canonical | 6a | 6a + 6b | 6b alone | total |
|---|---:|---:|---:|---:|---:|
| corpus, 560, fast | 50,982 | 48,730 | 47,877 | −1.8% | −6.1% |
| random, 9c (20, seed 1) | 732,753 | 678,757 | 564,756 | −16.8% | −22.9% |
| random, 11c (10, seed 3) | 135,880,493 | 47,215,409 | 47,219,449 | +0.009% | −65.2% |
| random, 13c (20, seed 3) | 50,241,863 | 27,303,533 | 12,952,377 | −52.6% | −74.2% |
| random, 13c (20, seed 11) | 141,136,295 | 142,923,616 | 124,650,866 | −12.8% | **−11.7%** |
| random, 13c (20, seed 42) | 160,744,139 | 95,228,547 | 70,710,823 | −25.7% | −56.0% |

Across the three 13-card seeds, 352,122,297 → 208,314,066: −21.5% on top of 6a
and −40.8% against canonical. Wall time, both arms on the same binary: seed 3
11.9 s → 3.2 s, seed 11 34.5 s → 31.3 s, seed 42 38.2 s → 17.6 s. Throughput is
unmoved — 4.08M against 4.22M nodes/sec at seed 3, 3.98M against 4.09M at seed
11, 4.03M against 4.20M at seed 42 — so the per-node cost of up to four bit
walks at an opponent's lead is inside the noise, and it is paid at one node in
eight rather than at every node.

**The result to read is seed 11.** 6a made it 1.3% *worse* and this list
recorded that as the first change on it that could cost a particular deal work.
6b more than repairs it: −12.8% on top of 6a and −11.7% against canonical, and
with it **every recorded workload is now at or better than canonical**. That is
not luck. 6a promotes the nil bidder's best defence and 6b promotes the attack
that defeats it, and a search that only knows how to defend spends its time
proving the defence works down lines that were never the threat. The pairing is
the reason these two were sequenced together rather than either being judged on
its own — and it is also a caution: a phase that measures badly alone is not
therefore wrong, and 6a would have looked much worse had seed 11 been the only
seed on this list.

11 cards is the one place 6b does nothing (+0.009%, four thousand nodes on 47
million). Not investigated. 6a already took that workload down by 65%, and a
heuristic that finds nothing left to promote is the expected shape once another
has already collapsed the tree.

  **What 6d bought, measured on one binary against 6a + 6b.** Four arms, because
  the entry named two plays and 6c had just shown what bundling two plays hides:

  | workload | 6a + 6b | + discard | + lead | + both |
  |---|---:|---:|---:|---:|
  | corpus, 560, fast | 47,877 | 40,595 (−15.2%) | 47,707 | 40,417 |
  | random, 9c (20, seed 1) | 564,756 | 556,220 (−1.5%) | 564,756 | 556,220 |
  | random, 11c (10, seed 3) | 47,219,449 | 32,069,478 (**−32.1%**) | — | — |
  | random, 13c (20, seed 3) | 12,952,377 | 12,637,804 (−2.4%) | 13,047,545 | 12,752,975 |
  | random, 13c (20, seed 11) | 124,650,866 | 108,022,631 (−13.3%) | — | 108,022,631 |
  | random, 13c (20, seed 42) | 70,710,823 | 59,534,175 (−15.8%) | — | — |

  Across the three 13-card seeds, 208,314,066 → 180,194,610, −13.5%. Wall time
  tracks and throughput is if anything up — 5.61M against 5.47M nodes/sec at
  seed 11 — because the cost lands only at discard nodes: seed 11 is 22.8 s →
  19.2 s, seed 42 13.0 s → 11.2 s, 11 cards 9.0 s → 6.3 s.

  **Leading was dropped.** −0.36% on the corpus, exactly nothing at 9 cards,
  *+0.7%* at 13 cards seed 3, and at seed 11 the combined arm equals the discard
  arm node for node. It is not a win and it is not clearly a loss; it is
  nothing, and nothing still costs a bit walk per suit at every nil lead. The
  entry above once suggested leading a king into a known ace to shed it for
  free, and that is the likely reason: it is a fine play against a human and
  worth nothing here, because the opponents are maximising the nil bidder's
  tricks and will simply duck and let the king win unless they are *forced* to
  cover. Note this is the same trap that kept speculative sheds out of 6a — the
  second time on this list that a sound-looking play has failed for the reason
  that double-dummy opponents decline to help.

- ~~**The cover partner (6c).**~~ ⊘ *measured and rejected, patch 18.* Prefer
  plays that take the trick over the nil bidder's head; failing that, shed the
  lowest card that cannot win. Both halves turned out to be worth nothing, in
  two different ways, and the measurements are under *Evaluated and rejected*.
  Note in passing that the second half was never a heuristic at all: the
  canonical order is ascending, so trying the lowest card that cannot win first
  is what the search already does — the same free-by-construction effect that
  keeps certain winners at the back in 6a.

*This is ordering, not a reduction.* "Try these first" must not become "try only
these". Dropping the low cards outright would be a claim that a lower card in
the nil bidder's hand dominates a higher one, which is a different and much
stronger statement than the equivalence in `rules.hpp` — that one is an
automorphism and is exact by inspection, while domination has to survive the
fact that which card the nil bidder retains can change who wins a later trick,
and so the lead. If anyone wants it, it needs the `bounds.hpp` treatment and a
proof first, it would hold only under the fast objective, and it changes the
principal variation.

*And note what the reduction has already done.* Move generation emits one
representative per equivalence class, the canonically lowest member. Ordering
therefore sorts class representatives, not cards. Where the safe shed and the
high card fall in one class there is nothing to choose — they are one move under
two names, and the reduction has already said so.

Item 7's control arm shipped ahead of 6a, in patch 15.

**What 6a bought, and the part that is not a clean win.** Both columns are the
same binary with `--no-ordering` as the only difference, so this is a
differential rather than a comparison across builds.

| workload | canonical | 6a | change |
|---|---:|---:|---:|
| corpus, all 560, fast | 50,982 | 48,730 | −4.4% |
| random, 9 cards (20, seed 1) | 732,753 | 678,757 | −7.4% |
| random, 11 cards (10, seed 3) | 135,880,493 | 47,215,409 | **−65.2%** |
| random, 13 cards (20, seed 3) | 50,241,863 | 27,303,533 | −45.7% |
| random, 13 cards (20, seed 11) | 141,136,295 | 142,923,616 | **+1.3%** |
| random, 13 cards (20, seed 42) | 160,744,139 | 95,228,547 | −40.8% |

Across the three 13-card seeds together, 352,122,297 → 265,455,696, −24.6%.
Throughput is flat — 3.50M against 3.54M nodes/sec at seed 3, 3.43M against
3.48M at seed 42, a 1% cost — so the node saving converts almost entirely into
wall time. Seed 3 went 14.2 s → 7.8 s and seed 42 46.3 s → 27.8 s.

**Read seed 11 rather than the average.** It is not noise and it is not a
measurement error; it is what a heuristic looks like. Per deal:

| deal | canonical | 6a | change |
|---|---:|---:|---:|
| r13-0006 | 112,006,077 | 95,645,704 | −14.6% |
| r13-0011 | 11,442,381 | 34,396,982 | **+201%** |
| r13-0004 | 12,480,068 | 8,153,933 | −34.7% |
| r13-0002 | 2,284,511 | 2,031,371 | −11.1% |

Three of the four biggest deals improve and one triples, and the one that
triples is large enough in absolute terms to swallow the other three. That is
the honest shape of this optimisation: a big expected win with a real tail, and
it is a different shape from everything above it on this list. Patches 8, 10 and
11 could not make any position worse — the reduction was an equivalence, the
window never narrowed, the proofs were one-sided. **6a is the first change that
can cost a particular deal work**, because guessing which move to search first
is a guess, and on r13-0011 it guesses wrong for long enough to matter.

Nothing about that makes it unsafe. Ordering removes no move, so every answer is
identical — `corpus_ordering` and the 48-solve selftest sweep both check exactly
that, and `MODE_FULL` still comes in at 2,647,731 nodes. What it means is that
this entry cannot be quoted as a single number, and that a regression on one
deal is not by itself evidence of a bug.

*Two things 6a deliberately does not do,* both left out so that its number
covers one idea:

- **No speculative sheds.** A card ABOVE the current best sheds more, and the
  earlier version of this entry said to try those next. It is a worse bet than
  it looks: such a card only loses if a later seat covers it, and a later
  OPPONENT will decline to, because letting the nil bidder win is the whole of
  what the opponent wants. The speculative shed is good only when the covering
  partner is still to play, which is a lookahead 6a does not do. Worth
  measuring, after 6b and 6c, as a refinement rather than as part of this.
- **No demotion of certain winners.** They want to be last and already are: they
  are the high cards of the suit and the canonical order is ascending.

### 7. ~~A `--no-ordering` control arm~~ — ⭐⭐⭐⭐ — **done, patch 15**

**Re-scoped, then landed. Shipped before 6a, not with it.**

This item used to ask for a `--canonical` mode: a flag disabling every
principal-variation-affecting heuristic and falling back to canonical
enumeration order, so `crosscheck.py` would keep running against something
comparable. It opened by asserting that item 6 destroys the card-for-card oracle
check by design.

**That is no longer true, and it stopped being true in patch 10.** Checked
rather than assumed:

- `crosscheck.py` passes no `--mode`, so the oracle diff runs `MODE_FULL`.
- The `corpus` ctest's `--check-pv` likewise runs `MODE_FULL`.
- Item 6 is confined to `MODE_FAST`, because `MODE_FULL` searches between
  sentinels, never cuts, and therefore gains nothing from ordering.
- `MODE_FAST` reports no principal variation at all.

So ordering confined to fast mode changes no observable output anywhere — not a
value, not a PV, not a boolean — only node counts. The oracle check is not
destroyed; it is not touched. **`MODE_FULL` already is the canonical mode**, and
by construction rather than by a flag someone could fail to set, which is the
property patch 10 went out of its way to buy. A `--canonical` flag would be a
second, weaker spelling of it.

*What was still wanted, and what shipped in patch 15.* A control arm, on the
`--no-collapse` and `--no-static` model, because ordering must be provably
answer-neutral and the way this project shows that is a differential on one
binary:

- `SearchOptions::order_moves`, `--no-ordering` on `nil_cli` and `nil_bench`,
  `NIL_FLAG_NO_ORDERING` in the C ABI.
- `+noordering` appended to `nil_bench`'s memo column, on fast rows only —
  ordering is inert in full mode, and the suffix would otherwise split the
  full-mode history into two groups holding identical numbers. Same reasoning
  as `+nostatic`.
- A `corpus_ordering` ctest, mirroring `corpus_static`: the whole corpus in fast
  mode with ordering off, against the oracle's recorded answers, while
  `corpus_modes` runs it with ordering on. A heuristic that has started changing
  answers can then only agree with both by being unreachable.

All four landed, and the gate is `opts.order_moves && opts.mode == MODE_FAST`.
That reads the MODE rather than the weights, which is a deliberate departure
from `gains_nonnegative` and `value_is_nil_tricks` and is commented as such in
`search.cpp`: what makes reordering unsafe is not a property of the objective
but the fact that `MODE_FULL`'s chosen move is an output the oracle checks card
for card. A selftest holds full mode's value, node count and PV identical across
both settings of the flag.

The arm is inert until 6a, on the patch 9 model — that patch bought no speed
either and was the shape item 3 needed, settled while it was still cheap. The
two selftest lines that today read "ordering saves no nodes" become the real
differential the moment 6a lands.

*What is not wanted yet.* `--canonical` itself. It becomes necessary the moment
anything wants to order moves in **full** mode — a killer table shared across
modes, an ordering that reads the packed value, anything that makes `MODE_FULL`
pick a different card among equals. Nothing does today. Keep the idea; do not
build the flag until something needs it.

*Note, carried forward unchanged:* the equivalent-card reduction does **not**
belong behind any of this. It preserves the PV, and `--no-collapse` already
exists as a separate switch for the one question it answers — whether the
reduction itself is sound.

*Two stale comments this item leaves behind, fixed in the same patch.*
`CMakeLists.txt` told the next reader to drop `--check-pv` when move ordering
lands, and `nil_bench --help` said ordering would change which equal-valued card
the search picks. Both were written before the mode split, both are now wrong,
and the first would have retired the strongest regression test in the project
for no reason at all.

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

### 15. ~~Two-tier table replacement~~ — ⊘ **built, measured, rejected in patch 20**

**Provisionally measured at patch 12, and the answer did not survive item 6.** The one survivor of the replacement sweep above.
Reserve the last of the four ways as an always-replace slot and confine the
depth preference to the other three: pick the victim among ways 0..2 as now, and
if that victim is live and strictly deeper than the entry being stored, write to
way 3 instead of evicting it. The bucket cannot ossify, because way 3 always
accepts, and a shallow entry can no longer throw away a deep one three times out
of four. It is the standard two-tier scheme, and it is what Chang's guard should
have been here.

Built and measured, all three recorded 13-card seeds, twenty deals each:

| workload | incumbent | two-tier | change |
|---|---:|---:|---:|
| random, 13 cards (20, seed 3) | 50,241,863 | 49,415,166 | −1.6% |
| random, 13 cards (20, seed 11) | 141,136,295 | 137,889,771 | −2.3% |
| random, 13 cards (20, seed 42) | 160,744,139 | 157,428,201 | −2.1% |
| random, 9 cards (20, seed 1) | 732,753 | 732,783 | +0.004% |

Wall time, best of three at 13 cards, seed 3: 11.73 s → 11.46 s, −2.3%. So the
node saving is not being paid for in throughput — it tracks it — which is the
same shape as patch 11 and unlike both entries in the rejected section.

**It is small, it is consistent, and it is not free.** The table is a pure memo,
so this changes no value and no principal variation; what it changes is which
entries survive, and therefore how many nodes a search revisits. That includes
`MODE_FULL`. The corpus moves from **2,647,731 to 2,647,760** — twenty-nine
nodes, 0.001% — and the sentence in *Baseline to measure against* that calls
full mode's node count a fixed point stops being true as written.

That is the whole cost, and it is a real one: the fixed point is the sharpest
regression test in this file precisely because it is exact. Anything that moves
it must move it deliberately, once, with the new number recorded and a note
saying which patch moved it and why — otherwise the next person to see 2,647,760
cannot tell an optimisation from a bug. Two percent is a thin reason to spend
that, which is why this is a separate item and not a rider on patch 12.

*Sequencing.* Take it after items 6 and 7. Move ordering changes which nodes get
stored and in what order, so this wants re-measuring on top of it anyway, and
item 7's arrival is the natural moment to re-baseline full mode in any case.

---

**REJECTED, patch 20 — and the sequencing note above is the reason it was
caught.** Re-measured on top of 6a, 6b and 6d, one binary, both arms:

| workload | incumbent | two-tier | nodes | wall |
|---|---:|---:|---:|---:|
| corpus, 560, full | 2,647,731 | 2,647,760 | +0.001% | — |
| corpus, 560, fast | 40,595 | 40,595 | 0 | — |
| random, 9c (20, seed 1) | 556,220 | 556,227 | +7 nodes | — |
| random, 11c (10, seed 3) | 32,069,478 | 31,293,455 | −2.4% | −1.0% |
| random, 13c (20, seed 3) | 12,637,804 | 12,730,737 | +0.7% | +0.9% |
| random, 13c (20, seed 11) | 108,022,631 | 105,534,145 | −2.3% | −1.0% |
| random, 13c (20, seed 42) | 59,534,175 | 58,801,394 | −1.2% | +2.6% |

Across the three 13-card seeds, 180,194,610 → 177,066,276, −1.74% — and 42,088 ms
→ 42,233 ms, **+0.34%**. The nodes still go the right way and the clock does not
follow them. (Seed 3 and 11 cards are best of three; seeds 11 and 42 are single
runs, being a minute apart.) The store path pays for it: even written as a
single pass over the bucket, exactly like the incumbent, it carries the extra
depth test and the redirect, and that is charged at every store while the node
saving is only collected where the table was actually thrashing.

**Patch 12 measured −2.1% nodes AND −2.3% wall, and was not wrong.** It was
measured on a tree twice the size. Item 6 removed half the search and reshaped
what is left, so the table is under far less pressure than it was — there is
much less for a better replacement policy to recover, while the cost per store
is unchanged. A 2% win became a 0.3% loss without a line of the policy changing.

That is the whole argument for having deferred it, and it is worth stating as a
rule rather than an anecdote: **a measurement is only valid against the tree it
was taken on, and anything that changes the shape of the search invalidates every
banked result underneath it.** Two entries above this one were rejected on wall
time and remain rejected; this one was provisionally accepted and had to be
re-tried. The sequencing note is the only reason it was re-tried rather than
shipped on a stale number.

*And the cost it no longer buys anything for.* It still moves `MODE_FULL`'s
corpus from 2,647,731 to 2,647,760. Twenty-nine nodes, for −0.3% of wall time,
against the sharpest exact regression test in this file. Not close.

*If anyone revisits it.* The case would be a workload where the table genuinely
thrashes again — deeper than 13 cards, or a much smaller `--tt-mb`. `--tt-stats`
reports the eviction rate, and the 90%-plus warning it prints is the signal that
this item is worth another look.

---

### 21. ~~Per-card move list across the ABI~~ — ⭐⭐⭐ — **done, patch 21**

Not on this list when it was written. It came from the C# side: the DDS wrapper
the web app already uses returns a `futureTricks` row per legal card, and a nil
solver that returns only the position's answer cannot score a player's actual
choice or colour a hand to show which cards were safe.

`nil_solve_moves` fills in the same `nil_result` and additionally writes one
`nil_move` per legal card. In `MODE_FULL` each row carries the three trick
counts; in `MODE_FAST` it carries the boolean, which is what that mode computes.

**What it cost, and why it is nearly nothing.** The obvious fear is that asking
about every card forbids the very thing the boolean search is for — stopping at
the first card that settles the question. It does forbid that, and it does not
matter, because the root position is solved first and every card is then scored
against the *same* transposition table. The per-card searches spend almost all
their time reading back work the first search already did. Over twelve random
thirteen-card deals in fast mode:

| | nodes | wall |
|---|---:|---:|
| `solve` | 166,184,672 | 33,488 ms |
| `solve_moves` | 166,185,277 | 33,630 ms |
| | **1.0x** | **+0.4%** |

The only positions where it costs anything are the ones the plain call answered
by static proof without looking at a card. There the position is free and the
move list is not — 1 node against 79 — which is not a number anybody has to plan
around.

**Three implementation notes worth carrying forward.**

- *The per-move transition is not a second copy.* `advance` and `value_after`
  were lifted out of `search`'s own loop, and both callers use them. Two copies
  of that arithmetic is exactly how a move list comes to disagree with the search
  that produced it.
- *The classes are recovered rather than recomputed.* `equivalent_moves` reads
  `distinct_moves` backwards — walk up from the representative, stopping at the
  first relevant card that is not a move. It is the same fact stated in the other
  direction, so the two cannot drift.
- *The cross-check replaces the PV replay for this entry point.* Scoring every
  move and taking the extremum has to land where the ordinary search landed:
  exactly in `MODE_FULL`, which never cuts, and on the boolean in `MODE_FAST`,
  where the root may have stopped early and its value is a bound. Per row,
  `MODE_FULL` additionally replays that row's own line and requires the repacked
  tally to equal the row's score — the same self-check `solve` runs, once per
  card.

Verified by `corpus_moves` and `corpus_moves_fast`, which run the whole
560-position corpus through both readings and require the list to agree with the
position, the classes to partition the legal cards rather than a subset of them,
and the chosen line to be the same one. `MODE_FULL` is unchanged node for node:
still 2,647,731 on the corpus.

### 22. ~~Window narrowing: alpha-beta for `MODE_FULL`~~ — ⭐⭐⭐⭐⭐ — **done, patch 22**

The largest single win on this list, and it was sitting inside a function that
already had the other half written.

*What was wrong.* Patch 10 gave `MODE_FAST` a null window and a fail-soft
cutoff, and left `MODE_FULL` searching between `[WINDOW_MIN, WINDOW_MAX]`. Every
Done row since has ended with the same five words — `MODE_FULL` unchanged node
for node — and they were not describing a mode that had been measured and found
unimprovable. They were describing a mode with **no pruning of any kind**. Three
separate mechanisms were inert in it simultaneously:

| mechanism | why it was inert in `MODE_FULL` |
|---|---|
| the fail-soft cutoff | `best >= beta` where beta is a sentinel no value can reach |
| static bounds (item 4) | gated on `value_is_nil_tricks`, false for full mode's weights |
| move ordering (6a/6b/6d) | `ctx.order_moves = opts.order_moves && mode == MODE_FAST` |

Confirmed by flag differential before anything was changed: at 9 cards on a hard
deal, `--no-static` and `--no-ordering` produced **byte-identical node counts**
in full mode. Full mode was the patch-8 algorithm — equivalent-card reduction
and a memo — wearing four patches' worth of comments about pruning.

*The fix.* The cutoff was already there. What was missing is the assignment that
makes it reachable: a maximiser raising alpha to its best so far, a minimiser
lowering beta, and the children that follow inheriting it. Six lines, guarded on
`SearchOptions::narrow_window`.

*Why it is answer-neutral, which is the whole of why it could ship.* Two
different arguments for the two modes, and neither is a measurement:

- **`MODE_FAST` is unchanged node for node.** Its window is null — beta is
  alpha + 1 — so at a maximiser `best > alpha` already implies `best >= beta`,
  and the cutoff fires before the widened alpha reaches a single child.
  Symmetrically at a minimiser. Every narrowing fast mode performs is an
  assignment to a variable the node is about to stop using.
- **`MODE_FULL` keeps its exact values *and* its canonical principal
  variation.** Values, because every entry point that needs an exact number asks
  with the sentinel window — `solve`'s root, each step of `walk_pv`, and the
  per-move loop in `solve_moves` — and a node given an unreachable window cannot
  fail either way, so what it returns is exact. The PV, because a probe under
  that window can only be answered by a `BOUND_EXACT` entry, and an exact entry
  is by definition one that did not cut: it enumerated every move in canonical
  order under strict improvement, so its stored move is still the canonically
  lowest of the best. A move searched after alpha has risen either fails low,
  returning at most alpha and so unable to strictly improve on it, or lands
  inside the window and is exact. Neither can displace the incumbent wrongly.

*The one thing that had to change with it.* Bound classification now reads
`alpha_asked` / `beta_asked`, the window the node was **given**, not the live
pair the loop may have moved underneath it. Classifying `best <= alpha` against a
narrowed alpha would label every maximiser an upper bound, since narrowing sets
alpha to best exactly. This is the classic transposition-table alpha-beta bug and
it is the only subtle line in the patch.

*Measured.* `MODE_FULL`, against the `--no-narrow` control arm on one binary:

| workload | before | after | ratio |
|---|---:|---:|---:|
| random, 9 cards (20, seed 1), nodes/position | 23,519,702 | **342,000** | **68.8x** |
| random, 9 cards (20, seed 1), ms/position | 4,362.45 | **69.36** | **62.9x** |
| corpus, all 560, nodes | 2,647,731 | **434,892** | **6.09x** |
| corpus, all 560, ms | 572.1 | **106.7** | **5.36x** |

The 23,519,702 is not a fresh baseline — it is the number already recorded in
this file from patch 10, reproduced exactly by the control arm, which is the
cleanest confirmation available that the flag restores the old search rather
than approximating it.

Honest note on throughput: it falls 11.9% on the corpus (4,627,869 to 4,076,958
nodes/sec), because a node that cuts does proportionally more setup per node than
one that runs its whole move list. Nodes fall far enough that wall time drops
81% anyway. This is the first optimisation on this list to lose throughput and
be worth taking regardless.

*On the deal that prompted it.* A maximally rank-interleaved 13-card deal —
every suit the same `AT62 / K95 / Q84 / J73` shape, gaps of exactly three, so no
player ever holds two adjacent ranks and equivalent-card reduction has nothing to
collapse (2.1% saving, against 9.8%–29.9% on random deals of the same size):

| cards | before | after |
|---:|---:|---:|
| 9 | 27,903,922 | 556,788 |
| 10 | 654,977,607 | 2,328,801 |
| 11 | — (unreachable) | 4,943,514 (1.18 s) |
| 12 | — (unreachable) | 4,245,840 (1.07 s) |
| 13 | — | still unreachable; ~40 min, no result |

Full mode went from timing out at **10** cards to answering **12** in about a
second. Thirteen is still out, and 22b is what is left to try.

*Verified.* All 560 corpus positions match on value **and** principal variation
under `--check-pv`, all 560 independently oracle-derived and none pinned from
this solver. `crosscheck.py` agrees card for card at 5 and 6 cards across
`--secondary min`, `--nil-already-set`, `--break-on-forced-lead`,
`--spades-broken`, mid-trick and `--no-memo`. `invariants.py` holds in full mode
at 8, 9 and 11 cards and on the corpus. The new `--no-narrow` arm reproduces
every corpus PV.

### 23. ~~Presolve-seeded root window~~ — ⭐⭐⭐⭐⭐ — **done, patch 23**

Patch 22 made full mode's cutoff reachable. It did not give the root anything to
cut against: the first child of the root is still searched between sentinels,
because no sibling has returned yet to raise alpha. On a position whose moves are
close in value that first child is most of the search, and on a position whose
moves are *equal* in value -- which is what a symmetric deal is -- alpha never
strictly improves at all, so sibling narrowing gives the root nothing from
beginning to end.

*The bound.* The two modes answer different questions about one position, and the
cheap answer bounds the dear one. With k = tricks + 1 the packed value is
`(primary + tertiary + secondary) * nil_tricks + secondary * cover_tricks`, and
the two halves do not overlap:

| objective | safe scores at most | failing scores at least | gap |
|---|---:|---:|---:|
| `minimise_own_tricks = false` | 0 | k + 1 | 2k |
| `minimise_own_tricks = true` | k² − k | k² + k | 2k |

Neither gap depends on the deal. So a `MODE_FAST` search -- which patch 10
measured at 12.8x to 303x cheaper -- returns one bit that closes beta onto the
answer, and every line that tries to force a nil trick is refuted the moment it
succeeds instead of being evaluated exactly. `max_value_if_nil_safe` computes the
ceiling from the weights, and the selftest re-derives the separation
independently at all thirteen sizes and both objectives.

*Exactness survives* because the bound is derived, not estimated. The true value
is on the safe side of the threshold, a window ending just above the threshold
still contains it, and a node whose window contains its value returns the value
rather than a bound. `walk_pv` inherits the same window -- asking wide there
against a table filled narrow is not wrong but re-searches everything, which is
how the first version of this was accidentally slower than no window at all.

*Per-card needs one more thing.* A row owes its own exact value, and a card that
loses the nil scores on the far side of the threshold, so the tight window would
hand back a bound for exactly the rows a caller most wants a number on. Those
rows and only those are re-searched against the sentinels, with the table warm.
One re-search per losing card, not one per card.

*What it did to the deal that started this.* The maximally rank-interleaved
13-card deal, `W:AT62.K95.K95.Q84 K95.AT62.Q84.K95 Q84.J73.J73.AT62
J73.Q84.AT62.J73`, nil on S:

| | before | after |
|---|---|---|
| full solve | ~40 min, no result | **40 s**, 110,681,989 nodes |
| full `--moves` | unreachable | **105 s**, 277,298,721 nodes |

Answer: nil holds, S takes 0 of 13, NS 3 and EW 10 side tricks, and all nine
legal leads are equally optimal -- which is what the deal's rotational symmetry
predicts. Fast mode reaches the same nine verdicts through a different tree.

*The honest general case, which is worse.* The presolve is paid always and
collects only when the nil is safe:

| population | `--no-presolve` | default | ratio |
|---|---:|---:|---:|
| random 9-card, 30 positions (2 safe, 28 fail) | 6,610,501 | 6,826,344 | **0.97x** |
| the nil-safe 12 of them | 2,981,516 | 2,483,153 | **1.20x** |

So on a random population this is a 3% tax, because random deals mostly fail the
nil. It is worth taking anyway on the argument that the tax is bounded by one
fast search -- the cheapest question this solver can be asked -- while the
collection is unbounded and grows with difficulty, and that the positions which
take minutes are disproportionately the ones where the nil holds, since proving
a nil safe means refuting every defence. A caller who knows their population
fails the nil should pass `--no-presolve`.

*Gated below 8 tricks*, where it was measured and found to be pure overhead: on
the 560-position corpus, presolving everything cost 5.9% more nodes and 32% more
wall and returned nothing, because those searches finish in under a millisecond
and there is nothing left for a tighter window to save. The corpus is byte-
identical to `--no-presolve` with the gate in, at 434,892 nodes. Hand size is the
only estimate of a search's cost available before running it, which is why the
gate is a constant and not a policy.

*The direction that was dropped.* When the presolve says the nil fails, the same
argument gives `alpha = max_value_if_nil_safe` -- also sound, also free. It was
implemented and measured at 1.00x, 0.99x and 1.00x on three sizes and removed:
it puts alpha under a maximising root, where narrowing was going to raise it on
the first child anyway. Recorded here so it is not rediscovered.

*Verified.* All 560 corpus rows on value and principal variation, oracle-derived
and none pinned from this solver, with the presolve on and again via the new
`corpus_presolve` control arm. `crosscheck.py` agrees at 5 and 6 cards across
`--secondary min`, `--nil-already-set`, `--break-on-forced-lead` and mid-trick.
`invariants.py` holds in full mode at 9 cards. `corpus_moves` and
`corpus_moves_fast` both clean.

*One test changed premise.* "Full mode does not see the static bounds" compared
full-mode node counts with the proofs on and off; a full solve may now contain a
fast presolve, which does see them and does count its nodes, so the test sets
`presolve_window = false` to keep the comparison about the thing it claims.

### 25. ~~Canonical re-derivation~~ — ⭐⭐⭐⭐⭐ — **done, patch 25; closes 22b**

22b measured the prize and could not collect it. Ordering `MODE_FULL` was worth
2.3x and cost three outputs the corpus pins: the principal variation walked a
different line, the move list named a different card as achieving the value, and
`nil_tricks` split differently when `nil_already_set` left the value unable to
constrain the split. Patch 24 shipped it as an opt-in flag and a warning.

*The observation that closes it.* Ordering is a **pure reorder** -- one card
lifted to the front of a mask, nothing added and nothing dropped, which the
assert in this patch's development confirmed never fires. A pure reorder cannot
change what a node is worth. It changes only which of several equally-worthy
moves the node happens to record first. So nothing was ever lost that could not
be recomputed: search fast with ordering, then ask the position again, in
canonical order, which of its moves achieves the value it just returned.

```
CardId canonical_move_for(ctx, st, value, alpha, beta)   // first match wins
```

*Why the comparison is sound.* Every child is scored against the window the
parent was asked about, and every call site has `WINDOW_MIN` beneath it, so no
child can fail low and every value compared is exact. A match is a real tie, not
two bounds that happen to coincide.

*Why it is affordable.* The loop stops at the first match, and the answer ties by
construction, so it never enumerates past the move the search already found. A
node whose canonical move is also its promoted one pays for one lookup. The table
is warm from the search that just ran, and only nodes on the line are asked.

*Measured.* MODE_FULL, one binary, three arms:

| workload | `--no-ordering` | ordering + canonical | ordering raw |
|---|---:|---:|---:|
| 13-card interleaved, `--moves` | 263,294,020 / 104 s | **146,214,395 / 57 s** | 126,274,445 / 49 s |
| random 9-card, 25 positions | 5,551,718 | **4,515,582** | — |
| corpus, all 560 | 434,892 | **404,836** | 387,119 |

**1.82x at thirteen cards** with the canonical line intact. Canonicalisation
itself costs 16% against raw ordering there and 4.6% on the corpus -- the price
of the tie-break, and worth paying, since the alternative was not collecting the
1.82x at all.

*The `nil_already_set` discovery, and why the guard is now derived rather than
flagged.* Patch 24 excluded `nil_already_set` from ordering after seven corpus
positions came back with a different `nil_tricks`. Those seven were all
`nilset=1, secondary=min`, and the reason is arithmetic: the value a nil trick
carries is `primary + tertiary`, which that combination sets to zero. The value
then reduces to `secondary * nil_side_tricks` and says nothing whatever about how
that total splits between the bidder and its partner -- the two are
interchangeable, exactly as this file has said since item 3.

So the condition is not a flag, it is a property of the weights:

```
value_pins_nil_tricks = (weights.primary + weights.tertiary) != 0
```

and when it is false, canonicalisation is **not the caller's to decline**.
Re-deriving the line is the only thing that makes `nil_tricks` deterministic
there, so `NIL_FLAG_FAST_LINE` does not turn it off. That is why `corpus_ordered`
-- the arm that runs the corpus with the flag set -- passes on all 560 including
those seven.

*What `NIL_FLAG_FAST_LINE` means now.* Narrower than in patch 24, and worth
restating: it buys 16% at thirteen cards by skipping the re-derivation, and the
only things that move are the principal variation's line and which single card
the move list names as achieving the value. Every value, every trick count,
`nil_fails`, and the set of cards flagged best are pinned regardless.

*Two pinned guarantees retired, both correctly.* "Full mode ignores the ordering
switch: nodes" was true and is not: full mode orders now. And the replacement
deliberately asserts nothing about node counts, because the two-card position it
runs on is one where ordering has nothing to save and the re-derivation still
costs a few lookups -- the ordered run is legitimately the dearer one there. The
saving is a property of deep positions and belongs in the benchmark. What the
test pins instead is what was always the point: the value and the line do not
move.

*Verified.* All 560 corpus positions on value and principal variation, oracle-
derived, with ordering on -- the comparison patch 24 failed 27 of. `corpus_moves`
likewise. `corpus_ordered` on all 560 with the flag set. `crosscheck.py` at 5 and
6 cards across `--secondary min`, `--nil-already-set` and
`--break-on-forced-lead`. `invariants.py` in full mode at 9 cards.

*Table sizing re-checked at the new node counts*, since patch 24 chose the cap
against a slower search: 256 MiB is 71 s, 512 is 53 s, 1024 is 51 s and 2048 is
59 s on the 13-card `--moves`. 512 is still the knee -- 1024 buys 21% fewer nodes
and 4% of wall for twice the memory -- so the cap stands.

### 22b. ~~Move ordering for `MODE_FULL`~~ — ⭐⭐⭐⭐ — **closed by patch 25**

> Closed. The blocker below -- that ordering costs the canonical principal
> variation -- was real, and item 25 removes it by re-deriving the tie-break
> instead of trusting it. The numbers here are the ones that justified the work;
> the ones that were collected are in item 25.

Now that full mode cuts, ordering it is worth something for the first time, and
it was measured immediately rather than assumed:

| cards | alpha-beta only | + ordering | gain |
|---:|---:|---:|---:|
| 9 | 556,788 | 454,623 | 1.22x |
| 10 | 2,328,801 | 1,238,899 | 1.88x |
| 11 | 4,943,514 | 3,009,909 | 1.64x |
| 12 | 4,245,840 | 1,421,312 | **2.99x** |

**Not shipped, and the reason is not correctness.** The value is right, the
verdict is right, and the line it returns is optimal — diffed at 10 cards, both
principal variations show `[S=0]` on every trick. What changes is *which* of
several equally optimal lines comes back, because the heuristics promote a card
that may tie with a canonically lower one, and full mode's tie-break — canonical
order plus strict improvement — is the thing `--check-pv` compares against the
oracle card for card. Shipping this as-is trades the project's strongest
correctness evidence for a factor of two.

*What unblocks it.* A canonical-PV re-extraction pass: search for the value with
ordering on, then walk the PV re-deriving each step by exact-value match in
canonical order, taking the first move whose full-window value equals the node's
known exact value. Each step is a warm-table probe, and the per-move loop in
`solve_moves` already demonstrates that shape is affordable. That restores the
canonical tie-break exactly, at which point ordering is free to run.

Note that `CMakeLists.txt` around the `corpus` test carries a comment saying
ordering is confined to `MODE_FAST` and so nothing there has to be given up for
it, and pointing at item 7's `--canonical` as the contingency if ordering ever
reaches full mode. Patch 22 made that contingency live; the comment is updated to
say so.

## Suggested sequence

```
1 ✅ → 2 ✅ → 3 ✅ → 4 ✅ → 5 ⊘ → 7 ✅ → 6a ✅ → 6b ✅ → 6c ⊘ → 6d ✅ → 15 ⊘ → 21 ✅ → 22 ✅ → 23 ✅ → 24 ✅ → 22b ✅ → 25 ✅ → 5 (re-opened) → 9 → 8 → 10 → measure → 11..14
```

`⊘` is item 5: closed without being built, because it had no population. **Patch
22 gave it one** — narrowing full mode's window is precisely the condition its
*What would revive it* paragraph named, and `tt_partial` is no longer zero there.
It is back in the sequence, after 22b, because both items turn on the same
question: whether a full-mode node may be ordered at the cost of its canonical
principal variation. Settle that once and both unblock together.

22b comes first because its gain is already measured (up to 2.99x) and item 5's
is not, and because the re-extraction pass 22b needs is the same machinery item 5
would need to assert its stored-move invariant against.

Item 1 went first because it was the last PV-preserving win, and it is banked.
Item 2 is banked too and bought nothing on its own, which was the plan: it is
the shape item 3 needed, settled while it was still cheap to settle — and it
paid for itself many times over the moment item 3 landed on top of it.

Item 7 now comes *before* item 6 rather than beside it. This used to read "6 and
7 together, because shipping move ordering without a verification fallback
trades away the solver's best evidence for speed" — which was right about the
principle and wrong about what it costs. The fallback turned out to already
exist: `MODE_FULL` is the canonical mode, structurally, so what item 7 still
owes is a control arm rather than a second mode, and a control arm is cheap
enough to land first and have ordering measured against from its first commit.

Item 6 is then four patches, because heuristics measured together are
heuristics not measured at all — and 6a proves the point: it is a 45% win on one
seed, a 41% win on another and a 1% loss on a third, and bundling it with 6b
would have made all three unreadable.

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
numbers. `--no-ordering` appends `+noordering` under exactly the same rule and
for exactly the same reason.
