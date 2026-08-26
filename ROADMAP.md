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

**Patch 30 is the exception to the framing above and is worth reading first.**
Everything else on this list makes the tree smaller. That one makes the nodes
cheaper — 2.79x the throughput, by declining to build a transposition key on the
three nodes in four that are mid-trick — and it is worth 2.3x to 3.6x of wall
time from four cards to thirteen. It is also the first entry here bought *with*
nodes rather than in them, and the reason that trade is available is the reason
four earlier items were rejected: the per-node budget was dominated by key
construction, so every heuristic charged per node was being charged against an
expensive node. Read items 11 and 31 in that light.

---

## Done

| | Optimization | Landed | Effect |
|---|---|---|---|
| ✅ | ~~`--break-on-forced-lead` removed from every interface~~ | patch 46 | patch 45 made the rule universal but kept two pieces of scaffolding for callers that no longer exist: ABI bit `0x2u` was documented as retired-and-burned so an old caller would get an ignored flag rather than a silently different objective, and the corpus parser recognised the old thirteen-field layout to explain the missing `forced` column. Nothing ships against either, so both are gone and the call surface is one flag shorter. The old-layout branch was **dead code besides** -- it sat inside a `f.size() < 10` guard and tested for 11, 12 and 13, so it could never fire; an old row now fails where it always really failed, on the `forced` value not parsing as a trick. The stale `expected at least 11` in that same message is corrected to 10, which is what the guard has checked since the column went. **No behaviour change and no answers move**: 22/22 tests, all 560 corpus rows and all 19 large rows reproduce byte-identical. Earlier entries on this list still mention `--break-on-forced-lead` and are deliberately left alone -- they record what was verified at the time, and rewriting them would make the log claim a history it did not have |
| ✅ | ~~A spade played always breaks spades; broken flag validated~~ | patch 45 | **a rules change, not an optimisation, and it pays like one.** `spades_broken_after` had a `break_on_forced_lead` parameter selecting whether a hand forced to lead a spade left spades unbroken -- the literal reading of the rule, and the default. It collapses to `broken \|\| suit == SPADES`, with the `trick_len` and `led_suit` arguments gone too since nothing else consulted them. Removed from `SearchOptions`, `Ctx`, `replay_pv`, the C ABI (bit `0x2u` **burned, not recycled**), both CLIs, `CorpusEntry`, the corpus format's sixth column, `nil_oracle.py` and seven Python tools. **261 of 560 corpus rows were computed under the literal reading and none of their answers moved**: a forced spade lead needs a hand holding nothing but spades while on lead, which constructed endings never reach. Separately, `validate()` now rejects a position claiming broken spades while all thirteen are still in play -- a full deal has had no card played, so no spade has. Three `large.txt` rows carried it, one at nine cards. Unbroken spades forbid a voluntary spade lead, which prunes hard near the root, so clearing them took the 13-card benchmark leg **from 290M nodes to 163M (−44%)** and c13-0001 from 184,547,569 to **71,253,358 (−61%)** with no value moving -- two principal variations did, and were re-pinned |
| ⊘ | Side-suit canonicalization, re-measured | patch 42 | **rebuilt on the strength of an expired rejection, re-measured, rejected again.** Two of the three reasons the first attempt failed had genuinely lapsed -- patch 30 put the key on 38.6% of nodes instead of 100%, and patch 25's canonical re-derivation retires the tie-break hazard -- and the symmetry available is now LARGER, since a trick boundary pins no suit. The prediction was −5% to −8% of nodes. It is **−0.2% to −0.75%**, because the symmetry identifies only **0.53-0.66% of distinct positions**: a group of order six collapses six positions in a thousand. Bought at 3.2% of throughput in full mode and 9.6% in fast, **slower in 7 of 7 interleaved reps on both**. The first measurement's 5.3% came from a tree 20-300x larger made of cheap nearly-exhausted positions; what is left after patches 22-41 is made of hard ones. Closed on the size of its population rather than the price of its machinery, so no cheaper sort revives it. Verified correct before being timed -- 20,000 permuted pairs give bit-identical keys -- and nothing shipped |
| ✅ | ~~The cutoff bound from a partial table match~~ | patch 41 | `probe()` found an entry, saw that its one-sided bound did not settle the window, counted it in `partial` and **threw it away** — 6.8% of 26.6M probes at 13 cards. It is not a miss: "the value is at least x" is a fact about the POSITION, true whatever window is asking. Spent on the threshold this node's own fail-soft test reads — beta at a maximiser, alpha at a minimiser — and **on nothing else**. **−0.48% to −1.21% nodes across seven full-mode workloads, largest at 13 cards**, and `MODE_FAST` byte-identical because it has no partials to spend. Wall follows weakly: medians 0.5-0.7%, faster in 3 of 5 interleaved reps on two 13-card seeds, one binary with the arm toggled at runtime. Sound because the value an early cutoff stops at is **squeezed exact** between the entry's bound and fail-soft's, so `walk_pv` still compares true values; all 560 oracle-pinned values AND principal variations reproduce. **The textbook version of this item is a LOSS and that is the more valuable result** — see "Evaluated and rejected" for the sweep and the mechanism |
| ⊘ | Ordering skipped on forced nodes | patch 40 | **built, measured, withdrawn.** 34-39% of all search nodes have exactly one legal move after equivalence collapse, and each ran the full ordering block to promote the only card there is. Gating it is provably tree-neutral and verifies byte-identical -- but it buys **nothing measurable**, and the first measurement saying it did was a methodology error worth more than the patch. See "Evaluated and rejected" |
| ✅ | ~~Later tricks: the opponents' and the nil side's forced trump tricks~~ | patch 39 | DDS §4 wired into patch 31's simplex. `top_spade_run()` finds the hand holding the top outstanding spade and counts its run from the top; that hand wins exactly that many tricks **down every line of play**, because each card is played eventually, one hand plays one card per trick so the tricks are distinct, spades is trump so a trick carrying a spade goes to the highest spade on it, and the only spades above it sit in the same hand. Which constraint it adds depends on whose hand it is -- three cases, three vertices each, no cards searched. **−16.73% nodes at 13 cards on seed 3** (1.396G -> 1.163G over eight deals), and the deal that dominates that workload -- 1.11 **billion** nodes, 79.8% of the tree -- takes **−19.69%**, so the gain lands on the slowest position rather than beside it. Wall follows nodes almost exactly (181,512 ms -> 150,140 ms, **1.21x**) because throughput is flat: 7.69M nodes/sec against 7.74M, i.e. the predicate is free per node. **Seed 42 gets essentially nothing** (−0.27% over eight deals, two deals slightly worse) and **11 cards is a wash** (+0.02%) -- the variance is deal-to-deal, not size-to-size, and no account of what separates them. `MODE_FAST` byte-identical and provably so. All 560 oracle-pinned values AND principal variations reproduce; 800 random deals agree with `--no-later-tricks` on value, split and PV while node counts differ on 777 of them |
| ⊘ | Adversarial nil-set proof (item 32) | patch 38 | **measured, not built.** The population is large -- 54% of the positions where today's forced-trick proof stays silent are genuinely forced and an adversarial spade count spots them -- but no static count reaches it soundly. The most conservative variant tested at **96.2% precision**, which is a bug that passes testing rather than a proof. Counterexample and the full table in item 32; `--nilset-stats` and `tools/nilset_population.py` shipped so the next attempt starts from evidence |
| ✅ | ~~Winning-rank backup and the `need` histogram~~ | patch 36 | DDS §6.1-6.2 and Ginsberg's partition search: record which cards won a trick **by rank**, back them up through the tree, and read off `need` -- how many of a suit's live cards, counted from the top, an entry would have to pin. Merges follow the paper: union across siblings, **the cutting move alone** at a cutoff. `need` rides in `TTEntry::bound`'s spare bits, so the entry stays **24 bytes** and every node count this project has banked is unmoved -- verified byte-identical, 39,701 fast and 325,975 full on the corpus. Off by default, and **free when off**, which took a second attempt: carried as a runtime pointer -- an extra argument on a hot recursive function, a null test at every return path and a zero-initialised `Hand` per move -- it cost **4-8% of wall time with the feature disabled** on three workloads, patched slower in 11 of 11 interleaved paired runs against a pristine binary. Making `TRACK` a template parameter and instantiating both paths returns it to parity (11-card fast: 68.6 ms pristine against 67.6 ms patched, patched faster in 4 of 5). `--rank-stats` turns it on. Shipped once with an open-coded `__builtin_clz` that built clean on GCC and broke the MSVC leg of `scripts/build-and-test.cmd` outright, when `cards.hpp` already carried the four-compiler `highest_card()` (fixed, patch 37) -- **this repo has two toolchains and a Linux-only build is half a verification.** What it bought is the number item 31 was blocked on, and that number **closed the item** -- see 31b in "Evaluated and rejected" |
| ⊘ | Masked table matching by canonical key (item 31b) | patch 36 | **built, refuted by counterexample.** DDS §6.3 stores a mask beside each entry and linear-scans; folding the mask into the key instead -- sorting the don't-care region's owner bits, which is a normal form for the multiset a mask leaves visible -- keeps the probe at one hash and one bucket, and would have been strictly better than the paper's scheme. It is **unsound**, and not marginally: a card's rank can matter without that card ever winning a trick by rank, so the criterion under-approximates. Two reproducible counterexamples below. Nothing shipped |
| ✅ | ~~Suit-mixed move ordering~~ | patch 35 | DDS §5 puts the best card of **each** present suit at the head of the move list, and says why: *"good mixture of moves (i.e. not all cards from the same suit first) in case the heuristic is not good for a particular set-up"*. This loop promoted one card and then enumerated suit-major -- every spade, then every heart -- which is the shape the paper warns against. A hedge, not a bet: the same moves are searched and nothing is spent to decide the order, which is what separates it from the rejected 6c. **−5.4% to −9.0% nodes at 11 and 13 cards in fast mode, −7.9% at 11 in full.** It *costs* nodes on easy deals (+11.5% at 9 cards) and wall time is a much weaker result than nodes -- see the measurement section, which is the honest version |
| ⊘ | Repeated null-window search for `MODE_FULL` (item 34) | patch 34 | **built as an experiment, measured and rejected.** The mechanism works exactly as predicted -- the reachable value set is 105 wide at 13 cards, bisection converges in 6-7 probes, and convergence costs **26% fewer nodes than the incumbent wide search at 13 cards**. It is still net negative, because a table left full of BOUNDS cannot answer the exact-value walk the principal variation and the replay check require: recovering those costs a further 33.5M nodes on the same three deals, for **119.6M against the incumbent's 108.2M -- 1.11x worse**. The floor is measured too, and it is not far below: seeded with the true value by oracle, merely *verifying* it costs 73% of the incumbent on the deal that dominates the workload. Nothing shipped |
| ✅ | ~~One fixed table size, and the memo column repaired~~ | patch 33 | `TT_AUTO` was a schedule that sized the table to the position. `resize()` reuses the allocation when the size is unchanged and **re-zeroes the whole table when it is not**, and a hand played out asks for a smaller table every trick or two -- so a worker following a live game walked the schedule down and back up every deal at **51.3 ms per hand** of pure memset, charged against solves that are under a millisecond at the small end. One size for every hand and both modes makes every resize after the first the free branch (0.00001 ms). Node counts are unchanged or slightly better; fast mode at 13 cards goes 3,492,640 -> 3,485,739 because 256 MiB is more table than the old fast cap. Also fixes the history file's `memo` column, which printed the `TT_AUTO` sentinel as `18446744073709551615mb` and put every auto-sized run in a group of its own |
| ✅ | ~~`MODE_FULL` table cap lowered to 256 MiB~~ | patch 32 | patch 28b raised it to 1024 because full mode had not saturated at 13 cards, and that was true of the tree it measured. Patches 30 and 31 rebuilt that tree twice, and what is left saturates at a quarter of the cap: **1.18% more nodes for 1.24x less wall time**, across seven 13-card deals on three seeds, interleaved. Also **takes `NilSolverPool` from 2 GiB to 512 MiB** across its two workers, because the table is `thread_local` and never shrinks below a size it has held. The fast-mode half of the item was measured and declined -- see "Evaluated and rejected" |
| ✅ | ~~`TargetReached`: the reach bound on the tricks left~~ | patch 31 | the direction of DDS §2's check this search never had -- *tricks won plus tricks left cannot reach the target*. A trick is worth `per_nil` to the bidder, `per_partner` to the cover and nothing to either opponent, so a subtree with `t` tricks left is worth `per_nil*n + per_partner*p` over `n + p <= t`: linear over a simplex, extremes at its vertices, **no cards read at all**. **−22.4% nodes at 13 cards** and at 12, −9.2% at 11, −11.4% on the corpus; 1.11x to 1.33x wall. Fires on 3.1-11.2% of trick-boundary nodes, nine tenths of it on `lo >= beta`. `MODE_FAST` byte-identical and provably so. All 560 oracle-pinned values AND principal variations reproduce |
| ✅ | ~~Transposition table confined to trick boundaries~~ | patch 30 | the paper's rule — *positions stored always consist of completed tricks* — which this solver never followed, because the key was built to describe a mid-trick position too. Building that key is O(live cards) and three nodes in four are mid-trick, so the table's own cost was most of the per-node cost of the search. Hit rates say what it bought: **62-70% at a boundary against 5-11% one ply in**. **2.3x to 3.6x less wall time at 11 to 13 cards** and faster at every size measured, on **2.79x the throughput**. Nodes move both ways — −22.8% on seed 11, +31.4% on seed 3, **−2.5% across all forty 13-card deals** — and that is the result, not noise. Answer-neutral by construction: a memo half-declined changes duration and nothing else, and all 560 oracle-pinned values AND principal variations reproduce |
| ✅ | ~~Static bounds spent in `MODE_FULL`~~ | patch 29 | the two proofs settle a fast node because that mode's value IS the nil trick count; full mode's value also carries the pair's tricks, so the same proof yields a fail-soft BOUND, returned only when it already clears the window. **−6.8% nodes on the corpus** (339,573 -> 316,333), **−5.7% across all three 13-card seeds** (293,841,428 -> 277,143,048), ranging from −15.9% on seed 3 to +0.2% on seed 42. Spends `MODE_FULL`'s node-count fixed point, held since patch 8. Values and principal variations pinned unmoved by test, corpus and oracle |
| ✅ | ~~Table cap raised for `MODE_FULL`~~ | patch 28b | full mode has not saturated at 13 cards, and the 512 MiB cap was cutting it off mid-curve. Raised to 1024: **−11.8% nodes on a hard 13-card deal** (115,623,205 -> 101,960,580), −0.55% on an easy one, and 2048 buys a further 2.1% which is where it flattens. Nothing below 13 cards changes -- the doubling schedule already returns 512 at 12 -- so this is a pure high-card-count change. Node counts only: whether they buy wall time depends on the host's memory hierarchy |
| ✅ | ~~Last-trick evaluation~~ | patch 27 | Chang's `if (tricks_left == 1) return LastTrick(sp)`, which this search never had. At a trick boundary all hands are equal, so four cards left means one card each and the trick is forced: five nodes (four plies plus the terminal) collapse to one, with no key encoded, no probe and no store. **−16.1% nodes on the corpus** (404,836 -> 339,573), −9.1% on the corpus in fast mode, −1.08% at 9 cards, −1.26% at 11. Exact rather than a bound — a forced line has one value and no window disagrees — so it cannot cost nodes at any depth. All 560 oracle-pinned values AND principal variations reproduce |
| ⊘ | Transposition-table move ordering (item 5), second attempt | patch 26 | **evaluated and rejected on measurement, having been reopened by patch 22.** The population is real this time — `tt_partial` is 8.8% of probes at 9 cards — and the never-tested membership invariant holds exactly (0 rejections in 585,271 promotions). The move is simply a worse hint than what it displaces: **+0.84% nodes at 9 cards, +1.57% at 11**, and ~4% slower on interleaved medians. Nothing shipped |
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

Patch 35, measured against `--no-suit-mix` on the same binary, arms interleaved,
median of three for wall and exact for nodes:

| workload | canonical | suit-mixed | nodes | wall |
|---|---:|---:|---:|---:|
| corpus 560, fast | 40,102 | 39,701 | −1.00% | 0.98x |
| random 9c x20, seed 1, fast | 653,784 | 728,920 | **+11.49%** | **0.87x** |
| random 11c x10, seed 3, fast | 15,400,031 | 14,104,350 | **−8.41%** | **1.08x** |
| random 12c x10, seed 3, fast | 2,277,389 | 2,329,552 | +2.29% | 0.96x |
| random 13c x20, seed 3, fast | 15,525,269 | 14,529,924 | **−6.41%** | 1.03x |
| random 13c x10, seed 11, fast | 34,857,391 | 32,963,937 | **−5.43%** | 1.00x |
| random 13c x10, seed 42, fast | 33,426,881 | 30,407,921 | **−9.03%** | 1.01x |
| corpus 560, full | 321,144 | 325,975 | +1.50% | 1.00x |
| random 9c x20, seed 1, full | 7,681,172 | 7,772,036 | +1.18% | 0.92x |
| random 11c x6, seed 3, full | 96,733,104 | 89,118,687 | **−7.87%** | **1.09x** |
| random 12c x6, seed 3, full | 37,698,015 | 35,940,769 | −4.66% | 0.96x |
| random 13c x3, seed 3, full | 108,175,696 | 106,451,046 | −1.59% | 0.94x |

**It pays on hard deals and charges on easy ones, and that is the shape rather
than the noise.** 11-card fast (15.4M nodes) and the three 13-card sets (15M to
35M) all improve 5-9%; 9-card fast (0.65M over twenty deals) and 12-card fast
(2.3M over ten) both get worse. A hedge earns its keep where the primary
heuristic misfires, and deep trees are where that happens -- which is also why
hand size is not the discriminator. 12-card fast on seed 3 is an *easy* set and
loses; 11-card fast is a hard one and wins the most.

**The wall-time column is weaker than the node column and the entry says so.**
The gate costs two bit scans and the rotation costs a four-way suit test on the
first few moves of every ordered node, and that eats most of what the nodes
save: 1.08x to 1.09x where the saving is largest, ~1.00x on the hardest 13-card
seed, and *below* 1.00x wherever nodes went up. Five interleaved reps on 13-card
seed 11 came out 3909.5 ms against 3920.5 -- a wash. An earlier read of a flat
1.03x across the board was taken under lighter load and did not survive repeats,
which is recorded here because it nearly went into this table.

**Shipped on the node result at 11 and 13 cards**, per the standing rule that
those sizes are worth more than time lost at small ones -- the 9-card
regression is 26 ms across twenty deals. A reader who disagrees with that
weighting should turn it off with `--no-suit-mix`, and the numbers to argue
from are above rather than buried.

**The tail is deliberately not mixed.** Rotating the *whole* move list rather
than its head saves marginally more nodes -- 77.4M against 77.9M over the three
13-card fast seeds -- and gives them back on throughput, because it charges the
suit scan on every move instead of on the first four. Interleaved wall time came
out between 0.96x and 1.03x for the rotated tail against a steadier read for the
head-only form. DDS's literal wording is the head-only version and the
measurement agrees with it.

**A correction this patch had to make to itself.** The first draft asserted in
four places that the change was inert outside `MODE_FAST`, on the reasoning that
the seat-specific rules in 6a/6b/6d read that way. `order_moves` runs in **both**
modes; full mode moved 7.9% at 11 cards. The comment, the ABI note, the bench
memo suffix gate and the ctest were all wrong together, which is what happens
when a claim is reasoned from neighbouring code instead of measured. The suffix
now applies to both modes and the full-mode arm carries `--check-pv`, since the
canonical re-derivation of patch 25 should pin the line whatever order the moves
were tried in -- and now asserts it.

Patch 33, measured directly against the schedule it replaced:

| | |
|---|---:|
| first resize to 256 MiB | 133.1 ms |
| 100 resizes to the same size | 0.001 ms total (0.00001 ms each) |
| five hands, 13 -> 3 -> 13 cards | 256.5 ms (**51.3 ms per hand**) |

**Sizing the table to the question is a worse idea than it looks, and the reason
only shows up in a process that solves more than one position.** A benchmark run
solves 560 positions all of one size and never resizes; a service following a
live game resizes every trick or two, and each step is a memset of the whole
table. The third row is the schedule being walked down as a hand plays out and
back up for the next deal.

**What it costs, stated plainly.** A process that solves one small position and
exits now spends 133 ms allocating a table it barely uses, where the schedule
spent about twelve. That lands on the test suite, which is made of exactly such
processes: 9.9 s to 14.9 s, all of it one-off allocation. `tools/invariants.py`
and `tools/crosscheck.py` now pass `--tt-mb 32` explicitly, which took them back
to baseline and is better practice anyway -- a correctness harness on four-card
positions should not depend on what the library's default happens to be this
month.

**Node counts are unchanged or better.** Every hand size now gets at least as
much table as the schedule gave it, and more table never costs a search nodes:
the corpus is identical in both modes, 9-card full mode is identical, and
13-card fast mode improves 0.2% because 256 MiB is more than the old fast cap of
128.

**The memo column.** `nil_bench` printed `opts.tt_megabytes` raw, so any run
that did not pass `--tt-mb` recorded `18446744073709551615mb` -- the `TT_AUTO`
sentinel -- in the column `bench_history.py` groups on. Every auto-sized run
therefore sat in a bucket named after a sentinel and could not be compared with
the identically-sized run beside it. It now resolves to the size actually used,
so an auto run and a `--tt-mb 256` run both read `256mb` and group together,
which is correct because they are the same table. Both tools' help text also
said the default was 32 MiB, which it had not been since the schedule landed.

Patch 32, seven 13-card deals across three seeds, full mode, sizes interleaved
on one binary, two reps:

| workload | 256 MiB | 1024 MiB | nodes | wall |
|---|---:|---:|---:|---:|
| 13c x3, seed 3 | 108,175,696 / 9.51 s | 108,076,727 / 12.73 s | +0.09% | **1.34x** |
| 13c x2, seed 11 | 239,604,003 / 23.98 s | 235,183,781 / 27.29 s | +1.88% | 1.14x |
| 13c x2, seed 42 | 40,488,830 / 3.85 s | 40,485,032 / 6.29 s | +0.01% | **1.63x** |
| **all seven** | **388,268,529 / 37.3 s** | **383,745,540 / 46.3 s** | **+1.18%** | **1.24x** |

**The node column still slopes the way patch 28b found it sloping.** What
changed is the gradient. Under the old tree the step from 512 to 1024 was worth
11.8% of nodes on a hard deal; under this one the step from 256 to 1024 is worth
1.18% across seven of them, and a slope that shallow is outweighed by the
allocation and the cache footprint. This is the ROADMAP's own rule collecting on
itself: *a measurement is only valid against the tree it was taken on*, and two
patches in a row changed the tree.

**The residual cost is where it should be.** Seed 11 is the hardest of the three
and the only one where 1024 still buys anything worth naming -- 1.88% of nodes
-- and it is also the seed where the wall-time win is smallest, 1.14x against
1.63x on seed 42. Table pressure scales with the tree and so does the price of a
table too big for its cache.

**The full curve, for the record**, seed 11 at 13 cards in full mode, which is
the deal set that binds:

| cap | nodes | wall |
|---:|---:|---:|
| 128 MiB | 265,361,894 | 25.2 s |
| **256 MiB** | **239,604,003** | **24.0 s** |
| 512 MiB | 235,613,108 | 24.8 s |
| 1024 MiB | 235,183,781 | 27.3 s |

256 is the minimum of both columns on the hard seed, which is what settled it
against 128 -- 128 is a wash on wall across all seven deals and 6.4% worse on
nodes.

**What this does to a process.** `TranspositionTable::resize()` reassigns its
vector and never `shrink_to_fit()`s below a size it has already held, so a
thread's resident set is the LARGEST cap it has ever asked for rather than the
one currently in use. The table is `thread_local` and `NilSolverPool` runs two
workers in production, so the pool's high-water mark goes from 2 GiB to 512 MiB.
That behaviour is now documented at `NIL_TABLE_AUTO` rather than left to be
discovered.

Patch 31, measured against `--no-target-bounds` on the same binary, arms
interleaved, median of three for wall and exact for nodes:

| workload | no reach bound | with it | nodes | wall |
|---|---:|---:|---:|---:|
| corpus 560, full | 647 / 0.09 ms | 573 / 0.08 ms | **−11.4%** | 1.12x |
| random 9c x20, seed 1, full | 424,019 / 30.3 ms | 384,058 / 26.6 ms | −9.4% | 1.14x |
| random 11c x6, seed 3, full | 17,751,351 / 1,657 ms | 16,122,184 / 1,492 ms | −9.2% | 1.11x |
| random 12c x6, seed 3, full | 8,100,780 / 689 ms | 6,283,002 / 529 ms | **−22.4%** | **1.30x** |
| random 13c x3, seed 3, full | 46,475,507 / 4,159 ms | 36,058,565 / 3,138 ms | **−22.4%** | **1.33x** |

`MODE_FAST` is unchanged **byte for byte** -- 35,560,973 nodes on both arms at
13 cards -- and that is a proof rather than a measurement. There the value is
the nil bidder's trick count, so the reachable range is `[0, t]` against a
window that is `[0, 1]` at every node: `hi <= alpha` needs `t <= 0`, which is
the empty position handled at the top of search(), and `lo >= beta` needs
`0 >= 1`.

**The fire rate, and which direction pays.** Counted at every full-mode
trick-boundary node:

| workload | boundary nodes | `hi <= alpha` | `lo >= beta` | rate |
|---|---:|---:|---:|---:|
| corpus 560 | 94,429 | 5,317 | 3,444 | 9.28% |
| random 11c x6, seed 3 | 31,669,350 | 30,930 | 960,342 | 3.13% |
| random 12c x6, seed 3 | 13,791,760 | 174,813 | 821,472 | 7.22% |
| random 13c x3, seed 3 | 44,132,630 | 241,860 | 4,697,788 | 11.19% |

**It is a minimiser's bound, thirty to one.** `lo >= beta` is the nil side
being told that even winning every remaining trick with the cover partner
cannot beat the line already found; `hi <= alpha` is the opponents being told
the same in reverse. The first outnumbers the second by 31:1 at 11 cards and
19:1 at 13, and the corpus is the only workload where they are comparable. The
asymmetry is the objective's: `secondary` is `-k` and `per_nil` is `k*k + 1 -
k`, so the reachable range is lopsided -- it stretches far below zero and much
further above it, and a lopsided range clears a narrow window from its short
end first.

**Node savings run two to three times the fire rate**, which is what a bound
that prunes a subtree rather than a node should do, and the ratio is the reason
this was worth building despite firing on one node in ten.

**Sequenced ahead of item 34 on purpose.** A tighter node-level bound makes
every null-window probe cheaper, so it is worth having before the repeated-search
driver rather than after it.

Patch 30, measured against `--tt-all-plies` on the same binary, arms
interleaved within one loop so the container's clock drift cancels, median of
three for wall and exact for nodes, 64 MiB fixed (256 in full mode):

| workload | all plies | boundaries only | nodes | wall |
|---|---:|---:|---:|---:|
| corpus 560, full | 564 / 0.17 ms | 647 / 0.09 ms | +14.7% | **1.89x** |
| corpus 560, fast | 65 / 0.04 ms | 71 / 0.03 ms | +9.2% | 1.33x |
| random 9c x20, seed 1, fast | 27,717 / 5.66 ms | 32,689 / 2.99 ms | +17.9% | **1.89x** |
| random 11c x10, seed 3, fast | 1,873,968 / 473 ms | 1,546,965 / 129 ms | **−17.4%** | **3.68x** |
| random 12c x10, seed 3, fast | 178,786 / 45.9 ms | 227,738 / 19.9 ms | +27.4% | **2.31x** |
| random 13c x20, seed 3, fast | 590,617 / 147 ms | 776,288 / 58.8 ms | +31.4% | **2.49x** |
| random 13c x10, seed 11, fast | 4,606,934 / 1,057 ms | 3,556,097 / 292 ms | **−22.8%** | **3.62x** |
| random 13c x10, seed 42, fast | 2,881,309 / 680 ms | 3,343,091 / 251 ms | +16.0% | **2.71x** |
| random 11c x6, seed 3, full | 13,917,768 / 4,664 ms | 17,751,351 / 1,739 ms | +27.5% | **2.68x** |
| random 12c x6, seed 3, full | 5,319,213 / 1,711 ms | 8,100,780 / 740 ms | +52.3% | **2.31x** |
| **all forty 13-card deals** | **86,694,770 / 20.3 s** | **84,517,640 / 6.6 s** | **−2.5%** | **3.07x** |

**This is the first patch on this list that is bought with nodes rather than
with them**, and the entry is written that way on purpose. Every earlier
rejection here — side-suit canonicalization, 8-way associativity, the mid-trick
static proof — lost because it spent throughput to save nodes. (Side-suit
canonicalization was re-measured on top of this patch and lost again, for a
different and stronger reason: on the tree patch 30 leaves there are barely any
nodes there to save. See "Evaluated and rejected".) This is the same
trade run backwards, and the reason it wins is that the thing being bought back
is not small: throughput goes from 4,094,618 to 11,444,318 nodes/sec at 13
cards, because `encode_state_key` walks every live card of every suit and it
was running on three nodes in four.

**Hit rate by ply is what made the case, and it is worth recording.** Measured
at 13 cards in fast mode across three seeds:

| ply | hit rate | share of all stores |
|---:|---:|---:|
| 0 (trick boundary) | 62-70% | 12.6% |
| 1 | **5-11%** | **27.3%** |
| 2 | 19-27% | 27.0% |
| 3 | 25-32% | 33.1% |

**Ply 1 cannot repay its cost and the reason is structural, not empirical.** The
only route to a ply-1 node is its boundary parent, and a boundary parent reached
a second time is answered by the table *before* it regenerates any child. So a
ply-1 entry memoises a position that, by construction, nothing walks twice; the
5-11% that do hit are the ones whose boundary parent had been evicted. It is
also 27% of all stores, so what a ply-1 entry mostly does is evict a boundary
entry that would have been hit.

Plies 2 and 3 genuinely transpose — the `gap` field in the key makes two
different played cards equal when they trap the same number of survivors, which
is exactly what statekey.hpp claims for it. They still lose, and a ply sweep on
one binary shows the margin is not close (13 cards, seed 11, ten deals):

| plies using the table | nodes/position | ms/position |
|---|---:|---:|
| 0,1,2,3 (the old default) | 4,606,934 | 1,075 |
| 0,2,3 | 4,192,289 | 846 |
| 0,2 | 3,679,073 | 499 |
| **0 only (shipped)** | **3,556,097** | **297** |

**Why nodes move in both directions, and why the sign is a property of the
deal.** Removing entries can only cost hits, so a table under no pressure loses
nodes to this change — which is what seed 3 and the 4-6 card corpus show. A
table under pressure gains them, because three quarters of its stores were
displacing the entries with a 65% hit rate. Seed 11 is the hardest of the three
13-card sets and it gains 22.8%; the corpus, which is small enough that nothing
is ever evicted, loses 9-15%. Both are the same mechanism read from two ends,
and the wall-time column does not care which end a deal is on.

**What this invalidates.** Patch 28b raised the `MODE_FULL` cap to 1024 MiB on
the strength of full mode not having saturated at 13 cards. It has now, at a
quarter of that: see item 28c below for the re-measurement, which is a separate
patch because it is a separate lever.

Patch 29, measured against `--no-full-static` on the same binary:

| workload | fixed point | bounds spent | change |
|---|---:|---:|---:|
| corpus 560, full | 339,573 | 316,333 | **−6.84%** |
| random 11-card x6, seed 3 | 84,567,384 | 83,506,613 | −1.25% |
| random 12-card x6, seed 3 | 33,140,586 | 31,845,677 | −3.91% |
| random 13-card x3, seed 3 | 101,960,580 | 85,800,774 | **−15.85%** |
| random 13-card x2, seed 11 | 159,204,524 | 158,596,352 | −0.38% |
| random 13-card x2, seed 42 | 32,676,324 | 32,745,922 | **+0.21%** |
| **all seven 13-card deals** | **293,841,428** | **277,143,048** | **−5.68%** |

**The variance is the result, not noise.** Node counts are deterministic and
these are exact. What varies is how often the safe proof fires in the subtrees,
which is a property of the deal rather than of its size: seed 3's deals reach
provably-safe positions constantly and seed 11's and 42's hardly at all. A
number quoted from seed 3 alone would be a 15.9% win and would be dishonest.

**Nodes can rise, and the first draft of this patch wrongly asserted they could
not.** A one-sided proof cannot change an answer, but a node answered by a bound
stores a BOUND where it would otherwise have stored an exact value, and a later
probe the exact entry would have settled is not settled by the bound. Pruning
here buys work there. Seed 42 is where that came out negative; the test now
asserts the value and the principal variation and says nothing about nodes.

**What this measures for item 29.** This was run first as a prerequisite: full
mode could not use a static bound at all, so there was no way to tell whether a
STRONGER proof would pay there. It can, and the existing weak proof is worth
5.7% at 13 cards off a fire rate of about 5% of trick-boundary nodes (measured
by instrumentation at 11 cards: 5.06% would prove safe, 0.98% would prove set).
The safe proof fails at roughly three-quarters of trick-boundary nodes on a hard
13-card deal, all on condition 2. So the headroom for a condition 2 that fires
more often is roughly an order of magnitude above what this patch collected,
and item 29 now has a measured target rather than a hoped-for one.

Patch 27, measured against `--no-last-trick` on the same binary, interleaved so
that the container's clock drift cancels:

| workload | searched | evaluated | change |
|---|---:|---:|---:|
| corpus 560, full | 404,836 | 339,573 | **−16.1%** |
| corpus 560, fast | 40,595 | 36,913 | **−9.1%** |
| random 9-card x20, seed 1 | 6,599,223 | 6,527,925 | −1.08% |
| random 11-card x10, seed 3 | 231,790,842 | 228,863,611 | −1.26% |
| random 13-card x20 fast, seed 3 | 11,816,337 | 11,812,359 | −0.03% |

**The gain decays with depth, and the reason is worth recording.** The shortcut
removes four nodes per distinct four-card endgame reached, so its yield is set
by how many of those the search reaches rather than by how big the tree is. At
4-6 cards the last trick is most of the tree. Deeper, two things that are
already here have got there first: the table collapses the endgame hard, because
the number of distinct four-card positions is small and the paths into them are
many, and in fast mode `nil_cannot_be_forced` settles most lines several tricks
above the bottom. What is left to collapse at 13 cards is 0.03%.

That is not an argument against taking it. The check is one comparison on a node
that is not at a trick boundary and one AND on a node that is, it can never cost
a node at any depth, and the corpus -- which is what the C# integration actually
solves -- is 4-6 cards.

Throughput was unchanged: 83.29 / 85.19 ms with the shortcut against 81.97 /
87.33 ms without it at 9 cards, which is inside the noise floor measured below.

**A note on measuring this at all.** Wall-clock readings taken minutes apart on
the development container are not comparable: five identical back-to-back runs
held 103.70-107.18 ms, while the same configuration measured across separate
bursts ranged 85-118 ms. Node counts are deterministic to the digit. Every
timing number in this section is an interleaved A/B on one binary, and any
timing claim that is not should be treated as unmeasured.

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

**Narrowing the window onto a partial table match (item 41, first version).**
Built, measured across three axes, refuted -- and the mechanism is worth more
than the patch that eventually shipped, because it prices several other items on
this list.

*The population is real.* A partial match is an entry that describes the
position and does not settle the window. `--tt-stats`, 13 cards, full mode,
three deals on seed 3: **26,599,749 probes, 80.1% hits, 6.8% partial**. Patch 12
closed item 5 on `partial` being identically zero; patch 22 gave it a
population, and this is the size of it.

*What was built.* The textbook move -- alpha-beta with memory as Plaat writes
it: raise alpha onto a `BOUND_LOWER`, lower beta onto a `BOUND_UPPER`, search
the node under the tightened window. It **cost 20.3% of the tree** at 11 cards.

*The first mechanism, and it is fixable.* The node then stores its own entry
classified against the TIGHTENED window, so results that would have been
`BOUND_EXACT` come out as one-sided bounds. Snapshotting the asked window BEFORE
the probe instead recovers most of it -- **+20.3% becomes +5.5%** -- and is sound
by a squeeze: if the entry pins `V >= x` and the node comes back at or below `x`,
fail-soft gives `V <= best` and the entry gives `V >= x >= best`, so `V = best`
and EXACT is the truth rather than an over-claim.

*The second mechanism, and it is not.* What is left is the tightened window
propagating DOWN the subtree, where descendants store weaker entries for the
same reason and no squeeze is available to them -- a child does not know why its
window is tight. **An exact entry answers every window; a bound answers almost
none.** This table runs an 80% hit rate at roughly five probes per store, so
entry QUALITY is worth more than window tightness, and by a wide margin.

*The sweep.* 11 cards, full mode, seed 3, six deals, all arms from one binary:

| variant | nodes | vs off |
|---|---:|---:|
| off | 89,134,281 | -- |
| both bounds, every depth | 94,063,649 | **+5.53%** |
| cutoff-side bound only | 91,173,026 | +2.29% |
| both bounds, `t <= 5` | 93,024,453 | +4.36% |
| both bounds, `t <= 4` | 91,595,863 | +2.76% |
| both bounds, `t <= 3` | 89,895,365 | +0.85% |
| both bounds, `t <= 2` | 89,117,003 | -0.02% |

**Monotone on both axes.** Every increment of propagation costs, and the only
gate that breaks even is the one narrow enough to do nothing. That is what makes
this a refutation rather than one bad arm: there is no tuning left to try.

*And the sign flips with hand size, the wrong way.* The propagating version is a
**win** on the 4-6 card corpus -- 277,853 nodes against 281,287, -1.2% -- and a
loss at 11 and 13. Entries at four cards are cheap to rebuild and barely reused;
entries at eleven are neither. A change that pays at the small end and charges at
the large one is the exact shape this project's standing rule rejects.

*What survived.* Only one of the two bounds can end a node -- beta at a
maximiser, alpha at a minimiser -- and the other is precisely the one whose only
effect is to propagate. Taking the first as a CUTOFF THRESHOLD while leaving
`alpha` and `beta` untouched keeps the benefit and pays none of the cost. That
is patch 41; see Done and item 41 below.

*The general lesson, which is the part to carry forward.* Four items have now
been measured against the transposition table and three lost: table move
ordering twice (patches 12 and 26), two-tier replacement (patch 20), and this.
The one that won -- patch 30, confining the table to trick boundaries -- won by
making the table CHEAPER rather than by asking more of it. **On this solver the
table's value is concentrated in its exact entries, and anything that trades
entry quality for anything else should be assumed negative until measured.** The
next attempt at anything table-shaped owes a population measurement AND an
entry-quality measurement, not just the first. Items 29b and 32 are unaffected:
both add proofs and neither touches the table.

**Skipping move ordering on forced nodes (patch 40).** Built, measured twice,
withdrawn -- and the *reason the two measurements disagreed* is the part worth
keeping.

*The population is real.* A node-population sweep says **34-39% of all search
nodes have exactly one legal move** after equivalence collapse, at 11 and 13
cards, in both modes. Every one of them ran the ordering block to promote the
only card available: `nil_bidder_discard`'s move loop, a `relevant_cards`, and
up to four `cover_deficit_depth` scans, reaching a conclusion the move loop
reaches for free. Gating on `(moves & (moves - 1)) != 0` is **provably
tree-neutral** -- the promoted card would be the only card -- and verifies so:
node counts byte-identical on every workload, fast and full, 11 through 13.

*First measurement: faster in 7 of 7, ~1.2% at 13 cards.* **This was wrong.** It
compared a pristine binary against a separately compiled patched binary. Two
builds of a program this size differ in code layout -- alignment, inlining
decisions, branch placement -- and those differences move wall time by 1-2% on
their own, which is larger than the effect being measured. Interleaving the runs
does nothing about it: the confound is in the binaries, not in the schedule.

*Second measurement: the same binary, guard toggled at runtime.* **Faster in 2
of 7**, medians 2,835 ms against 2,804 ms. A wash, if anything slightly
negative -- which is explicable, since the guard trades ordering work on 36% of
nodes for a branch test on 100% of them.

*The lesson generalises past this patch.* **A cross-binary comparison is not a
measurement of a sub-percent change.** This repo's convention of putting every
heuristic behind a control-arm flag exists for exactly this reason, and patch
39 was measured correctly *because* it had `--no-later-tricks` and both arms
came from one binary. Patch 40 had no flag -- the change is meant to be
unconditional, being tree-neutral -- and so it skipped the convention and got a
false positive. **A change with no control arm needs one built temporarily
before it can be timed at all**, even when shipping it as a switch would make no
sense.

*What would revive it.* Nothing at this size; the work removed is real but too
small to see. It becomes interesting only as part of the larger version --
flattening a forced ply into a loop rather than a `search_impl` frame, removing
the stack frame and the node-counter increment as well as the ordering. That
spends `MODE_FULL`'s and `MODE_FAST`'s node-count fixed points and is a separate
decision, and it should be measured with a runtime toggle from the first run.

**Masked table matching (item 31b), by canonical key.** The largest structural
gap between this solver and the literature, and it is closed the other way: the
criterion does not transfer.

*What was built.* The winning-rank backup landed and is described in Done. On
top of it, a second key: pin the top `N` slots of each suit exactly and
**canonicalise** the rest by sorting the don't-care owners into ascending seat
order. That is worth stating on its own, because it is better than what the
paper does. DDS stores a mask beside each entry and scans a flat list applying
them -- 125 entries per distribution, overwritten cyclically -- because a
probing position does not know which mask to look itself up under. It does not
need to: two positions are equal under the mask exactly when their canonical
forms are equal as bit strings, so the masked key is still exactly hashable and
the probe stays one hash and one bucket. The suit distribution survives intact,
which is the part DDS is emphatic about; only *which* low card is whose is
thrown away. Coarse entries got their own `ValueTag`, since a coarse probe
reaches a class and an exact entry describes one position, and letting the first
read the second is a wrong answer while the reverse is harmless.

*Why it is unsound.* At `--mask-top 3`, full mode, these two positions have the
same distribution, the same owners at the top three heart slots and the same
census below -- the mask calls them one entry:

```
    N ♠875 ♥6 | E ♥KQJT | S ♠Q ♥7 ♦9 ♣T | W ♥95 ♦43   →  side_tricks 3
    N ♠875 ♥6 | E ♥KQJ2 | S ♠Q ♥7 ♦9 ♣T | W ♥95 ♦43   →  side_tricks 4
```

(South leads, North bids nil, spades broken, `--secondary min`.) They are not
one entry. **E's fourth heart never wins a trick by rank in the first position
-- it goes as a discard on trick one -- but sitting above W's ♥9 it changes what
E can still do, and the value with it.** A card's rank can matter without that
card ever winning, and "won by rank" does not see that. The counterexample holds
with `--no-collapse`, so it is not an artifact of equivalence reduction.

*The repair that looked like one.* Restricting coarse stores to `BOUND_EXACT`
nodes -- so that the union over every move, rather than a single cutoff witness,
backs each coarse entry -- passes all 560 corpus positions at every truncation
level, and passes 600 random 6-card deals and 300 random 7-card deals at
`--mask-top 2` and `3`, the same samples that fail without it. **It is still
unsound.** At `--mask-top 1` it fails twice in 700 random 6-card deals, e.g.
`N:Q6.7.Q.96 2.J.85.82 A.6.92.A3 J94.A..QT`, S leads, S nil, `min`, broken:
3 side tricks exact against 2 coarse. The restriction lowers the failure rate
and does not remove it, which is the worst possible outcome for a correctness
lever and the reason a corpus pass was not accepted as evidence here.

*And the population says it would not have paid anyway.* The `need` histogram is
the measurement the item asked for, and it has the shape this file keeps
finding. At 13 cards in fast mode, **57.8% of live owner slots still need
pinning**, and the entries that could be truncated are the cheap ones:

| cards still in hands | entries | need≤2 | need≤3 | need≤4 |
|---|---|---|---|---|
| 8 | 811 | 82.5% | 96.5% | 100.0% |
| 12 | 2,015 | 16.9% | 54.1% | 84.9% |
| 16 | 1,926 | 6.9% | 22.8% | 43.1% |
| 20 | 975 | 4.9% | 33.7% | 41.6% |
| 24 | 411 | 1.5% | 9.0% | 22.6% |
| 32 | 164 | 8.5% | 11.0% | 16.5% |
| 40 | 114 | 2.6% | 2.6% | 8.8% |

Masks are coarse near the leaves and fine near the root, which is the opposite
of where a bigger equivalence class is worth paying for. So the item closes on
two independent grounds, and a repaired criterion -- one that pinned every rank
that was *compared*, not merely every rank that won -- would pin very nearly
everything and collapse very nearly nothing.

*What survives.* The backup machinery itself, and `--rank-stats`. Anyone
reopening this should start from the counterexample above rather than from DDS
§6.1.

*One measurement note, because it nearly shipped wrong.* The backup was written
as a runtime `Hand*` threaded through `search()`, which reads as free when the
pointer is null and is not: an extra argument on a hot recursive function, a
null test on every return path and a zero-initialised `Hand` per move cost
**4-8% of wall time with the feature switched off** -- 68.5 ms against 71.9 ms
at 11 cards, 6.5 against 7.5 at 9, 19.6 against 20.8 on the corpus in full mode,
with the patched binary slower in **11 of 11 interleaved paired runs**. Node
counts were byte-identical throughout, which is exactly why nodes are not the
whole measurement: a change can be provably answer-neutral and still be a
regression. A compile-time `TRACK` parameter with both paths instantiated
returns the disabled path to parity. **Measurement machinery has to be free when
it is off, or it is not measurement machinery, it is a tax.**

**Side-suit canonicalization.** Hearts, diamonds and clubs are interchangeable,
so canonicalizing them is a genuine symmetry worth up to 6x in theory. Built and
measured twice, four patches apart, and rejected both times -- **for different
reasons, which is the part worth reading.**

*The first attempt, before patch 30.* 5.3% fewer nodes at 6 cards, 7.7% at 7,
bought at roughly 12% of throughput because the sort ran at every node. Net wall
time came out worse. The entry closed with *"not worth revisiting unless the key
computation gets much cheaper"* -- and the key computation then got much cheaper.

*Why it was reopened (patch 42).* Two of the three things that closed it had
expired. Patch 30 confined the key to trick boundaries, which a node-population
sweep puts at **38.6% of nodes**, so the same 12% is charged against two nodes in
five rather than five in five. And patch 25's canonical re-derivation retires the
tie-break hazard outright: `walk_pv` re-derives the line at every step, so a move
read back under a permuted labelling can no longer reach the reported PV. Better
still, the symmetry *available* is now strictly larger than it was: the old entry
noted that a non-spade lead pins its suit and leaves a group of order two, and a
trick boundary has no lead, so patch 30 deleted exactly the nodes where the group
was small. The prediction was −5% to −8% of nodes for ~4.6% of throughput.

*What the re-measurement found.* **The cost model was right and the benefit model
was wrong, by an order of magnitude.**

| workload | off | on | nodes |
|---|---:|---:|---:|
| corpus 560, full | 279,941 | 278,521 | −0.51% |
| corpus 560, fast | 39,701 | 39,409 | −0.74% |
| random 9c x20, seed 1, full | 7,477,320 | 7,420,903 | −0.75% |
| random 11c x6, seed 3, full | 88,491,025 | 88,176,205 | −0.36% |
| random 12c x6, seed 3, full | 35,059,198 | 34,861,862 | −0.56% |
| random 13c x3, seed 3, full | 93,581,425 | 93,269,285 | **−0.33%** |
| random 11c x10, seed 3, fast | 14,104,350 | 14,038,669 | −0.47% |
| random 13c x10, seed 11, fast | 32,963,937 | 32,839,732 | −0.38% |
| random 13c x10, seed 42, fast | 30,407,921 | 30,346,212 | −0.20% |

Against a measured throughput cost of **3.2% in full mode and 9.6% in fast**
(9,551,833 nodes/sec against 9,249,209; 10,198,993 against 9,218,182). Wall time
is not close and does not need repeats to see: **slower in 7 of 7 interleaved
reps on both**, medians 3343.6 ms against 3212.8 at 13 cards full and 339.1
against 326.0 at 13 cards fast, and in the fast case the two arms' ranges do not
overlap at all.

*Why the collapse is so small, measured rather than guessed.* `--tt-stats`
gives it directly: the number of DISTINCT positions stored, which is what a
symmetry that identifies positions is supposed to reduce.

| workload | stores off | stores on | collapsed |
|---|---:|---:|---:|
| corpus 560, full | 25,667 | 25,497 | −0.66% |
| random 11c x6, seed 3, full | 5,114,147 | 5,082,570 | −0.62% |
| random 13c x3, seed 3, full | 5,278,339 | 5,250,467 | −0.53% |
| random 13c x10, seed 11, fast | 1,761,944 | 1,750,699 | −0.64% |

**A group of order six identifies about six positions in a thousand.** The
reason is the one the old entry gave second and which patch 30 did not touch:
every player's holding in a suit is a subset of what they were dealt, so one
line's residual hearts can only look like another line's residual diamonds once
both suits are nearly exhausted -- and those subtrees are cheap. The first
measurement's 5.3% came from a tree twenty to three hundred times larger, full of
exactly those cheap nearly-exhausted positions; the tree that is left after
patches 22 through 41 is made of hard ones.

*What closes it permanently.* **0.53-0.66% is a ceiling on the benefit, not an
artefact of this implementation.** It is the count of position pairs the symmetry
identifies at all, so no cheaper sort can beat it -- and a perfect zero-cost
implementation would therefore be worth under one percent, against a measured
cost of three to ten. There is no version of this that pays. The item is closed
on the size of its population rather than on the price of its machinery, which is
the difference between "not worth revisiting unless X" and "not worth
revisiting".

*Correctness, recorded because the implementation was verified before it was
timed and the result should not have to be re-established.* A permutation
harness over 20,000 permuted pairs -- random deals, 1 to 6 cards each, all six
side-suit permutations applied to the hands -- confirms every permuted pair
produces a **bit-identical key**, so the collapse is complete and not merely
partial. `to_relative`/`from_relative` round-trip to the literally permuted card
in 19,958 of 20,000; the other 42 land in a suit that is bit-for-bit
indistinguishable from the intended one (same length, same owner of every live
slot), which is the tie-break case and is two names for one card rather than a
bug. All 560 oracle-pinned values AND principal variations reproduce on both
arms, with `--check-pv` and `--check-moves`. **The idea is sound and shippable;
it is simply not worth its price.**

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

**Repeated null-window search for `MODE_FULL` (item 34).** Built as an
experiment, measured, and rejected. The item's arithmetic was right and its
conclusion was wrong, which is worth separating.

*What was right.* The support of the packed value is `(t+1)(t+2)/2` -- 78
distinct values at 11 cards and 105 at 13, not the thousands the `search.hpp`
header used to imply. Bisection over that set converged in **3-6 probes at 11
cards and 6-7 at 13**, which is `log2(support)` to the digit. The presolve
collapses it further on a nil-safe deal: `root_beta` excludes every value with
`n >= 1`, leaving 12 values and 3-4 probes.

*What was wrong.* Convergence is cheaper than the incumbent, and not by enough
to pay for what it destroys.

| workload | incumbent | bisection converge | after +exact+PV |
|---|---:|---:|---:|
| 11c x6, seed 3, full | 96,733,104 | 86,644,215 (1.12x) | 112,035,262 (**1.16x worse**) |
| 13c x3, seed 3, full | 108,175,696 | 86,115,202 (1.26x) | 119,571,000 (**1.11x worse**) |

**A converged MTD run leaves the table full of bounds, and this solver needs an
exact value and a line.** `walk_pv()` re-searches each step and `replay_pv()`
re-derives the trick counts from it and checks them against the value -- that is
the project's strongest correctness evidence and it is not negotiable. A
BOUND_LOWER entry saying "at least 465" does not answer a step that needs to know
the value is exactly 465, so the walk re-searches, and on these three deals that
cost 33.5M nodes on top of convergence.

*The floor was measured too, and it is close.* Given the true value by oracle --
a seed no driver can have -- merely proving it costs 65,720,110 nodes against
the incumbent's 90,172,097 on `r11-0004`, the deal that owns that workload.
**73%.** So the absolute ceiling of this family, with a free perfect seed and
free PV recovery, is about 1.37x on the deal that matters, and every real driver
pays for both.

*Why the ceiling is that low, which is the part to carry forward.* Patches 22
and 23 already took most of what MTD(f) offers. Narrowing means the root's alpha
rises to near the true value after its first child, so every later child is
already searched with a near-null window; the presolve already excludes half the
support before the first node. What is left for a repeated-probe driver to
capture is the first child's window and the ordering of the probes, and that is
worth 12-26% -- against a PV recovery that costs more.

*What would revive it.* Only a way to recover an exact value and a canonical
line from a bound-valued table without re-searching. That is not a tweak to this
item; it is a different item, and item 31's winning-rank entries would not have
supplied it either -- doubly so now that 31b is closed and no masked entry
exists to recover anything from.

**The fast-mode table cap, lowered to 64 MiB.** The other half of item 28c,
measured alongside the full-mode cap in patch 32 and declined. The curve says
take it: at 13 cards on seed 11, 64 MiB holds 3,556,097 nodes against 3,492,640
at 128, and 64 is faster on interleaved medians (291 ms against 303).

It buys nothing. `TranspositionTable::resize()` never returns capacity below a
size the thread has already held, and `MODE_FULL` asks for 128 MiB at ten tricks
and 256 at eleven, so any thread that runs a full solve is already holding more
than the fast cap and lowering it changes no allocation at all. What is left is
1.8% more nodes on the hardest 13-card seed in exchange for 4% of wall time on
the same workload -- a trade worth making only in a process that never calls
full mode, and `NilSolverPool` is not one.

*What would revive it:* making `resize()` shrink, which is a different item and
a bad one on its face -- the pool would then free and re-allocate a quarter of a
gigabyte between alternating fast and full solves. If a fast-only entry point is
ever given its own pool, this comes back with it.

**Chang's 8-way rehashing.** The other half of his section 3, measured at the
same time as the cap change in patch 28 and for the same reason: an eviction
rate of 87.9% at 13 cards and 98.5% at 11 makes the bucket geometry look like
the problem. Chang tested 2, 4, 8 and 16 way and reports 8 as the best on CPU
time, with 16 slightly better on nodes but not worth its overhead. This table is
4-way and has been since patch 7, and the width had never been measured.

Twenty 13-card deals, seed 3, fast mode, 64 MiB, one binary and one constant
changed:

| ways | nodes | vs 4-way | wall |
|---:|---:|---:|---:|
| 2 | 11,907,367 | +0.80% | 2801 ms |
| **4 (incumbent)** | **11,812,359** | — | **2925 ms** |
| 8 | 11,768,588 | −0.37% | 3060 ms |
| 16 | 11,747,467 | −0.55% | 3382 ms |

The node curve moves in Chang's direction and is worth nothing: 0.37% for
double the bucket walk, and wall time gets monotonically worse with width. Same
verdict as his depth guard, and the same lesson -- the eviction rate is not
measuring what it looks like it measures. **Capacity is not the binding
constraint at these sizes**, which the memory sweep in patch 28 shows directly:
at 12 cards nodes are flat from 256 MiB upward. A high eviction rate against a
table that is not capacity-starved means the entries being displaced were not
worth keeping, and widening the bucket only makes them harder to displace.

**Item 28, bound estimation right after the lead -- dropped without building.**
The item is written up below and the reasoning behind it still looks right; what
kills it is that the measurement already exists. *The safe-nil proof run
mid-trick*, above, ran the proof at every ply and found **0.87% of nodes** at 13
cards. That is the whole mid-trick population, across all three non-boundary
plies. Item 28 proposes to harvest one of those three plies, so 0.87% is its
ceiling and its realistic take is a fraction of that -- against a cost of
roughly half the 5.9% throughput the rejected arm paid, since it lands on two
plies in four rather than four in four. A best case of well under 0.87% nodes
for around 3% of wall time is the same losing trade in a smaller package.

Worth building only if some later item changes the node mix enough that the
proof starts firing on a different population -- which is exactly the condition
the rejected arm already named. Left in the sequence as a documented
non-starter rather than deleted, because the *reason* Chang gives for ply 1
(the side that did not start the trick gets its bound a full trick earlier) is
sound and may matter to a future bound that is more expensive to reach than this
one.

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

**Weakening condition 1 of the safe-nil proof -- the spade gate.** Investigated
and closed BEFORE building, on a counterexample. This was going to be the first
half of item 29 and it is not available at all; the entry below is now scoped to
condition 2 alone.

*Why it looked like the bigger prize.* Item 29 was written against the measured
observation that the safe proof fails on condition 2. Instrumenting the actual
three-way outcome at every trick-boundary node says something the entry did not
record -- there is a second population, nearly as large, that never reaches
condition 2 at all. Sixty random 13-card deals, three seeds, fast mode:

| seed | boundary nodes | condition 1 fails | proof fires | condition 2 fails |
|---|---:|---:|---:|---:|
| 3 | 3,081,243 | 39.2% | 3.5% | 57.3% |
| 11 | 10,333,331 | 51.7% | 2.0% | 46.4% |
| 42 | 9,558,291 | 26.9% | 3.8% | 69.4% |
| **combined** | **22,972,865** | **39.7%** | **2.9%** | **57.4%** |

Two thirds the size of condition 2's population, gated by a single mask test,
and **82.0% of it is genuinely nil-safe** -- 3,039 sampled condition-1 failures
solved individually, 2,492 safe. That is a lot of correct answers behind one
`if`.

*And none of it is reachable.* Every candidate predicate sits at the base rate.
Numbers are over the same 3,039 samples:

| candidate | n | actually safe |
|---|---:|---:|
| all condition-1 failures (base rate) | 3,039 | 82.0% |
| every spade below every outstanding spade | 539 | 85.5% |
| that, and side suits clean (condition 2 passes) | 68 | 82.4% |
| that, and exactly one spade | 50 | 94.0% |

The third row is the interesting one. It is the whole of the case analysis --
a spade lead is covered by the rank condition, a side-suit lead the nil bidder
can follow is covered by condition 2, a side-suit lead it is void in it
discards on -- and it lands on the base rate. A proof needs 100%.

*The hole, and why it does not close.* The case analysis leaves exactly one
gap: void in the led suit AND holding nothing but spades, where the discard
becomes a forced ruff. `N:..A97.Q .K..A84 9..65.3 K..T.76`, spades broken,
leader N, nil on S. South holds S9, D65, C3. The S9 is below the only
outstanding spade, both side holdings are below everything outstanding in their
suits, and South is not spade-tight. The nil still fails:

```
T1  N:DA  E:C4  S:D5  W:DT   won by N
T2  N:D7  E:HK  S:D6  W:SK   won by W
T3  W:C6  N:CQ  E:CA  S:C3   won by E
T4  E:C8  S:S9  W:C7  N:D9   won by S   <-- forced ruff
```

West holds the SK and never leads a spade. South therefore never gets a legal
chance to shed the S9, arrives at the last trick spade-tight, and ruffs a club
it cannot follow. **The opponents choose whether spades are led, so they can
always manufacture the run-out.** Rank does not help, side-suit cleanliness does
not help, and holding side cards now does not help, because they get stripped.
Any spade at all is a live threat that depends on the play sequence rather than
on the distribution -- which is what `bounds.hpp` already says, more briefly,
and it is tighter than it reads. The gate is not conservative. It is exact.

*What survives.* The 82% is real and it is the right shape for the static
heuristics corpus, which does not need soundness -- and it is the complement of
the strongest discriminator that work has already found. Nothing survives for
the search.

*One incidental finding.* Condition 2's on-lead/void clause fired **zero** times
in 22.9 million boundary nodes. That confirms the reasoning in `bounds.hpp` --
below the root a nil bidder reaches the lead only by winning a trick, and a fast
search never recurses past that -- and it means a rewrite of condition 2 can
ignore that branch entirely rather than carrying it forward.

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

### ~~5. Transposition-table move ordering~~ — **BUILT, MEASURED AND REJECTED (patch 26)**

> **Closed for the second time, on measurement rather than on a theorem.**
> Patch 22 gave the item a population and patch 26 spent it. The mechanism
> works: `probe()` hands back the move off a partial match, and the invariant
> this item asked to have checked rather than argued — that a stored move is
> still a member of the reduced move set at the node reading it — held
> exactly, **0 rejections in 585,271 promotions** at 9 cards. What failed is
> the premise that the move is a good hint. Three slot orders were built and
> measured against `--no-tt-ordering` on one binary:
>
> | arm | nodes/position at 9 cards | vs canonical |
> |---|---:|---:|
> | no table ordering (control) | 329,961 | — |
> | table move first | 332,719 | **+0.84%** |
> | heuristic first, table move second | 342,160 | **+3.70%** |
> | table move only where no heuristic fires | 343,236 | **+4.02%** |
>
> At 11 cards the best arm is +1.57% (23,542,340 against 23,179,084), and on
> interleaved medians it is ~4% slower in wall time. Nothing shipped.
>
> **Why it loses, which is the part worth keeping.** The third arm is the
> informative one. It confines the table move to the seats and plies the 6a/6b/6d
> heuristics leave alone — the cover partner, the nil bidder on lead, an
> opponent following suit — and it is the *worst* of the three. So the canonical
> ascending order is not a gap waiting to be filled at those nodes: it is
> already close to right, which is the same reason 6c measured flat. Ascending
> order plays the lowest card first, and low is what an opponent wants when it
> is ducking under the nil bidder and what the nil bidder wants when it is
> shedding. The table move displaces a good order with a stale one.
>
> The other half of the answer is what the stored move *is*. A partial entry is
> one whose bound was too weak to settle the window, which is dominated by
> nodes that failed low: the move recorded there is the best of a set of moves
> that were all refuted, chosen under a different window. That is a weaker
> claim than it looks, and weaker than a domain heuristic that knows what a nil
> bidder is trying to do.
>
> The measurement is cheap to reproduce and the diff is small, so if a future
> item changes the shape of the tree enough to matter — the bar is a change to
> what a partial entry *is*, not merely how many there are — it is worth ten
> minutes to re-run. **Measurement validity is tree-specific**, and this result
> is banked against the patch-25 tree.


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

### ~~28. Bound estimation immediately after the lead~~ — **DROPPED WITHOUT BUILDING (patch 28)**

> Its ceiling is already measured and it is 0.87% of nodes at 13 cards -- the
> whole mid-trick population, of which this item harvests one ply in three.
> See "Evaluated and rejected" for the arithmetic. The write-up below is kept
> because Chang's *reason* for ply 1 is sound and may apply to a future bound
> that is dearer to reach than this one.

Run the safe-nil proof at `trick_len == 1` as well as at `trick_len == 0`, and
nowhere else.

**This is not the arm already rejected.** *The safe-nil proof run mid-trick*
under "Evaluated and rejected" tested the proof at every ply and paid 5.9%
throughput for 0.87% nodes. Chang does not do that. He runs his lower-bound
estimate at exactly two plies -- when a player starts a trick, or when he plays
"right after the leading card in current trick" -- and gives the reason: because
the strength of the cards usually falls on one side, estimating right after the
lead is what lets the stronger side get a large bound *early on the tricks it did
not start*. A side that only ever estimates at trick boundaries gets its bound
one full trick later on exactly those lines.

So the claim is not that mid-trick estimation pays. It is that ply 1
specifically is where the information arrives -- the led suit is known, which is
most of what the proof needs and all of what it lacks at ply 0 -- and that plies
2 and 3 are the ones paying throughput for nothing. Two of four plies at roughly
half the measured cost, against a saving concentrated where the rejected arm
never separated it out.

Cheap to settle: the mid-trick machinery from the rejected attempt is the same
machinery, gated on `st.trick_len <= 1` instead of unconditionally. Note that
only condition 1 of the proof survives mid-trick (see bounds.hpp), which is
already the constraint the rejected arm ran under.

### 29. Single-suit analysis — ⭐⭐⭐⭐ — **from Chang §4 and BIS §4.2**

Precompute per-suit results into a table and use them for bounds and for suit
ordering. This is the largest structural gap between this solver and the
literature, and both reference papers arrive at it independently.

**Chang's version.** For every distribution of relative ranks in a suit and
every leading player, precompute *sure tricks* (tricks the leader's side wins in
a row without discarding or losing control), *long-suit tricks*, and *controls*
(who holds the lead afterwards). Suits of length <= 9 fit in under 3 MB. The
results feed two places: a lower-bound estimate that is just the sum of sure
tricks plus the maximal long-suit tricks, and the suit ordering in move
generation.

**BIS's version, which is the one specialised to nil.** BIS estimates nil
success as a product over suits under an explicit *almost-suit-independence*
relaxation, `Pr(nil|hand) ~= prod Pr(nil(suit)|hand ∩ suit)`, on the grounds
that suit-independence holds exactly while players can follow suit and breaks
only on a void. Crucially it does not evaluate "nil" per suit but a weaker
*cards-only* event it calls `cnil(suit)`: on every trick of that suit, both
opponents can play under one of the nil bidder's cards **and** the partner
cannot cover it. That event depends only on who holds which cards, not on how
anyone plays.

**Why that matters here.** BIS needs `cnil` to be probabilistic because it is
bidding under hidden information. This solver is double-dummy: every card's
location is known, so `cnil(suit)` is a *decidable predicate*, not an estimate.
That makes it a candidate static bound in the shape of the two already in
bounds.hpp -- and a per-suit one, where both current proofs are whole-hand and
`nil_cannot_be_forced` is documented as failing at roughly three-quarters of
trick-boundary nodes on a hard 13-card deal, on condition 2. A predicate that
decomposes by suit is exactly the shape a weaker-and-still-sound condition 2
would take.

BIS contributes one more usable heuristic: cards beyond the three lowest in a
suit are not dangerous for a nil bid. If that survives contact with the
double-dummy setting it is a rank-truncation, and rank-truncation is the one
lever this project has not pulled that acts on the *state space* rather than on
the search order.

**Scoped to condition 2 only.** Weakening condition 1 -- the spade gate -- was
the other candidate and is closed on a counterexample; see "Evaluated and
rejected" above for the population measurement, the 82% safe rate behind the
gate, and the forced-ruff line that shuts it. Condition 2's population is 57.4%
of trick-boundary nodes at 13 cards against the proof's current 2.9% fire rate,
so the headroom the entry claims is confirmed and now has a denominator.

**Item 9 is an ingredient, not a successor.** A weaker condition 2 has to decide
whether a low lead can strand a middling card, and that is a question about who
can still FOLLOW the suit -- the void map. It is sequenced after 29 below and
the dependency runs the other way.

Sequenced after 28 because 28 is a one-line gate on machinery that exists and
this is a new subsystem, and before item 8, which it largely subsumes.

### 30. ~~Transposition table confined to trick boundaries~~ — ⭐⭐⭐⭐⭐ — **done, patch 30; from DDS §6**

The paper states it as a fact about its own design rather than as advice:
*"Positions stored in the Transposition Table always consist of completed
tricks. Positions stored start at depth=4, then 8, 12, and so on."* This solver
did not do it, and there was a reason — `statekey.hpp` was deliberately built to
describe a mid-trick position, and the `gap` field that does it is one of the
better ideas in the file. It works. It is still not worth what it costs.

The cost is that `encode_state_key` walks every live card of every suit, so it
is O(cards remaining), and it runs before the probe on every node that consults
the table. Three nodes in four are mid-trick. The measurement is in the Done
section above: 2.3x to 3.6x of wall time at 11 to 13 cards, on 2.79x the
throughput, against node counts that move both ways and come out 2.5% down
across forty 13-card deals.

**The general lesson, which is the reverse of the one this file kept
recording.** Four items have now been rejected here for spending throughput to
save nodes. The reason that trade kept losing is visible from this patch: the
per-node budget was dominated by key construction, so anything charged per node
was charged against a node that was already expensive, and anything that saved
nodes without touching the key saved the cheap part. Item 11 (incremental
state/key updates) is the item that follows from that and it should be re-read
in this light — it is now the only remaining per-node cost of any size, and it
is charged on a quarter as many nodes as before, which cuts both ways.

Control arm `--tt-all-plies` on both tools, `NIL_FLAG_TT_ALL_PLIES` across the
ABI, `+ttallplies` on the bench memo column, and `corpus_tt_plies` /
`corpus_tt_plies_fast` on every build.

### 33. ~~`TargetReached`: the reach bound on the tricks left~~ — ⭐⭐⭐⭐ — **done, patch 31; from DDS §2**

DDS runs `TargetReached` at the start of every trick and tests both directions:
tricks already won against the target, and tricks already won *plus tricks left
to play* against the target. This search had the first only, and only at the
last ply -- the `gained >= beta` return in `value_after()`. The second is the
one that says *even the best case from here cannot reach the window*, and it
reads no cards.

The window arriving at a node is already residual, because `value_after()`
shifts it by what the path has banked, so comparing the reachable range against
it IS Chang's "currently won plus tricks left against target" with the
subtraction done on the way down instead of the way up.

**Why it was not available before.** `MODE_FULL` searched between sentinels
until patch 22 and had no window to test anything against; patch 23 then gave
its root a real one. This item is the third thing to fall out of that pair, after
patches 25 and 29, and it is the cheapest of the three.

**Why it lives at a trick boundary and not at every ply.** The bound depends
only on `t`, which does not change within a trick, so a mid-trick node tests the
same `hi` and `lo` its boundary parent already tested -- against a window that
narrowing may have tightened in between. At a maximiser that tightening cannot
help: `hi` bounds every child, so `alpha` reaching `hi` means the parent already
holds the maximum and has cut. Symmetrically at a minimiser. This is reasoning
rather than measurement and is flagged as such, but it is the same reasoning
that made patch 30's ply-1 finding structural rather than empirical.

Control arm `--no-target-bounds` on both tools, `NIL_FLAG_NO_TARGET_BOUNDS`
across the ABI, `+notarget` on full-mode bench rows, and `corpus_target` with
`--check-pv` on every build.

### ~~34. Repeated null-window search for `MODE_FULL`~~ — ⊘ **built, measured and rejected in patch 34**

§1 closes by noting that the boolean search does not say how many tricks a side
takes, and that DDS gets the count by *repeated calls* with different targets.
The reference list carries Plaat, Schaeffer, Pijls and de Bruin 1996, which is
the MTD(f) lineage. Patch 23 took the first step -- one `MODE_FAST` presolve
seeding `beta` -- and stopped there.

**The header comment in `search.hpp` is what has been blocking this, and it is
wrong in a specific way.** It says the packed scalar "spans thousands of values,
so a window on it excludes almost nothing", and that is true of the RANGE and
false of the SUPPORT. From `advance()` and `objective_weights()` the value is
`per_nil * n + per_partner * c` with `n + c <= t`, and the file already notes
that `gcd(k*k + 1, k) = 1` so no two `(n, c)` pairs collide. The reachable set
is therefore one value per pair:

    (t + 1)(t + 2) / 2  =  105 values at 13 cards

not thousands. Bisecting 105 values is about seven null-window probes, and a
null-window probe on the packed objective is a fast-mode-shaped search with real
cutoffs rather than the wide one full mode runs today.

**Most of the risk is already retired.** Patch 22 made full mode's cutoff
reachable at all. Patch 25 made the principal variation canonical independently
of move order, which is what a driver that re-searches at several windows would
otherwise threaten. Patch 29 established that a fail-soft bound returned on a
cutoff is acceptable there, and patch 31 gives every probe a cheaper node.

**What to watch.** The PV still has to come out of an exact-valued search, so the
driver converges to the value and then walks the line at the sentinel window,
which is what `walk_pv()` already does. And the probe count is the whole game:
seven probes that each cost a fifth of the current search is a win, seven that
each cost half is not. Measure the probe count before optimising anything about
it.

**This entry rated it five stars and that was wrong.** The write-up above is
kept because its arithmetic held up exactly -- the support count, the probe
count, all of it -- and because the reason the conclusion failed is the useful
part. See "Evaluated and rejected" for the measurements. The short version:
patches 22 and 23 had already captured most of what this offers, and a converged
run leaves a table of bounds that cannot answer the exact-value walk the PV and
the replay check require, so the recovery costs more than the convergence
saves.

The star rating was assigned from the size of the prize rather than from any
measurement, on a list whose whole discipline is the opposite. Full mode at 13
cards being the slowest thing left made it *look* like the largest remaining
lever; being the slowest thing left is not evidence that a particular lever
moves it.

### 35. ~~Suit-mixed move ordering~~ — ⭐⭐⭐ — **done, patch 35; from DDS §5**

The paper puts the best card of *each* suit at the head of the list, not one
card overall: *"If the hand-to-play is the trick-leading hand or is void in the
suit played by leading hand, the card with the highest weight for each present
suit will get a high additional bonus weight."* It gives the reason too, and the
reason is a hedge rather than a bet -- *"another aim is to have good mixture of
moves (i.e. not all cards from the same suit first) in case the heuristic is not
good for a particular set-up."*

This solver promotes exactly one card and then enumerates what is left by
`take_lowest`, which is suit-major ascending: every spade, then every heart, and
so on. That is precisely the shape §5 warns against.

**Two seats are unmined.** The opponents on lead take one promotion from 6b and
then fall back to suit-major. The cover partner is unordered entirely -- and 6c
was rejected because it *spent the cover unconditionally*, which a round-robin
enumeration does not do at all. A hedge and a bet fail differently, and the
measurement that killed 6c does not carry over to this.

**Cheap, and it fits the existing machinery.** The promotion mechanism already
lifts a card out of the mask; this lifts the lowest of each suit in rotation.
Note the constraint patch 30 sharpened: this runs inside the move loop on every
ordered node, so the thing to measure first is nodes per second, not nodes.

### 28c. ~~The `MODE_FULL` table cap, re-measured~~ — ⭐⭐⭐ — **done, patch 32**

Patch 28b raised the full-mode cap from 512 MiB to 1024 on the strength of full
mode not having saturated at 13 cards. Patch 30 changed the tree that was
measured on, and the ROADMAP's own rule applies: *a measurement is only valid
against the tree it was taken on.* Re-measured, three 13-card deals, seed 3,
full mode, boundary-only:

| cap | nodes | ms |
|---:|---:|---:|
| 128 MiB | 47,467,147 | 4,403 |
| **256 MiB** | **46,475,507** | **4,297** |
| 512 MiB | 46,363,634 | 4,613 |
| 1024 MiB (shipping) | 46,353,341 | 5,013 |

Saturation arrives at 256: the last 4x of memory buys 0.26% of nodes and costs
17% of wall time, which is the allocation and the cache footprint being paid for
entries that are never read. The same shape at 12 cards, where the curve is flat
from 64 MiB and 1024 MiB is 47% slower than 128.

**Landed at 256, and re-measured once more first, which mattered.** The table
above was taken after patch 30; patch 31 then removed another fifth of the nodes
and moved the curve again. The shipped number comes from seven 13-card deals
across three seeds rather than the three above — see the patch 32 section in Done
for the numbers and for the full curve on the seed that binds.

The fast-mode half of this item was measured at the same time and **declined**;
see "Evaluated and rejected". The short version is that `resize()` never returns
capacity, so a thread that runs full mode is already holding more than the fast
cap and lowering it allocates nothing.

### 31. ~~Winning-rank backup and masked table matching~~ — ⭐⭐⭐⭐⭐ — **31a done, patch 36; 31b refuted; from DDS §6.1-6.3 and [Ginsberg]**

**Split in two by what happened to it.** 31a -- the backup machinery and the
population measurement -- landed. 31b -- the masked table the backup was for --
was built and refuted by counterexample. The full write-up with both
counterexamples, the `need` histogram and the canonical-key design is in
"Evaluated and rejected"; what follows is the short version and the part worth
keeping in mind.

**What landed.** During the search, record which cards won a trick *by rank* --
the heart A that beat three hearts, but not the spade A that won a trick nobody
could follow. Back them up: union across siblings, the cutting move alone at a
cutoff, exactly as `MergeAllMovesData` and `MergeCutoffMovesData` do. Read off
`need`, the truncation level an entry would require. `--rank-stats` prints the
histogram; the search is byte-identical with it off.

**What did not, and the one-sentence reason.** *A card's rank can matter without
that card ever winning a trick by rank.* The criterion in §6.1 is an
under-approximation, and the counterexample is small enough to hold in the head:
give the opponent ♥KQJT against ♥KQJ2 with the same distribution and the same
top three heart slots, and the ♥T changes the value while never taking a trick.

**What the paper's retrieval problem turned out to be worth solving anyway.**
§6.3's masked linear scan exists because a probing position does not know which
mask to look itself up under. It does not need to: canonicalise the don't-care
region -- sort its owner bits -- and the masked key is exactly hashable, so the
probe stays one hash and one bucket instead of a scan over 125 entries. That
part of the design is sound and is written down in the rejection entry, because
the next person to want a coarser key should not have to rediscover it. It is
the *criterion* that failed, not the retrieval.

**And the population would not have paid.** At 13 cards, 57.8% of live owner
slots still need pinning, and the entries coarse enough to truncate sit near the
leaves: `need <= 3` covers 96.5% of entries at 8 cards remaining and 22.8% at
16. Masks are coarse where subtrees are cheap. This is the same shape items 15,
28 and 34 found, and it is now the fourth independent time that a lever with a
large theoretical class has come back concentrated in the shallow end.

### 36. ~~Later tricks: the forced trump tricks~~ — ⭐⭐⭐ — **done, patch 39; from DDS §4**

Patch 31 answers a node from the tricks that are left, and reads **no cards at
all** to do it: a trick is worth `per_nil` to the bidder, `per_partner` to the
cover and nothing to either opponent, so a subtree with `t` tricks left is worth
`per_nil*n + per_partner*p` over `n + p <= t`. Linear over a simplex, extremes
at its vertices. That is its virtue and its ceiling — it has to allow every one
of the `t` tricks to fall wherever the simplex permits, and on most positions
some of them provably cannot move at all.

**The predicate.** `top_spade_run()` returns the hand holding the highest
outstanding spade and how many of the top spades it holds consecutively from the
top. That hand wins at least that many of the remaining tricks **in every line
of play**:

* every card is played eventually, so each `si` is played at some trick `Ti`;
* one hand plays one card per trick, so `T1..Tk` are **k distinct tricks**;
* spades is trump, so a trick carrying a spade is won by the highest spade on it;
* the only spades above `si` are `s1..s(i-1)`, which are in this same hand and
  therefore not on trick `Ti`.

**"In every line" is the load-bearing phrase, not "can force".** The simplex
ranges over every leaf without assuming anybody plays well, so what it can
consume is a floor that holds down every branch, good and bad alike. A bound on
optimal play would be the wrong shape and would be unsound here.

**Why one hand, anchored at the top.** Both halves are doing work:

* **Split fails.** `A` and `K` in the two opponents' hands is *not* two tricks —
  both may be void in the led suit and both may ruff the same trick, collapsing
  the pair into one. A side-wide count is therefore conditional on play.
* **Un-anchored fails.** A hand holding `KQ` under an outstanding `A` is
  promised nothing; the ace plays over it.

**This is not item 32 reopened, and the difference is exactly what makes it
sound.** Item 32 had to prove that a *named seat* takes a trick, from a *pooled*
count, and no such count reached it — its counterexample is an opponent
compelled to play a high spade and rescue the nil. A rescuing spade still wins
the trick for the hand that played it, which is all this counts. Confining the
claim to a single hand steps around the concentration failure rather than into
it, and 32's 96.2%-precision variant is what that failure looks like when it is
not stepped around.

**Three cases, one scan.** Which constraint the run adds depends on whose hand
it is. Each is still a linear function over a triangle, so each is still three
vertex evaluations and a min and a max:

| top run held by | constraint | vertices `(n,p)` |
|---|---|---|
| an opponent | `n + p <= t - k` | (0,0), (t−k,0), (0,t−k) |
| the nil bidder | `n >= k` | (k,0), (k,t−k), (t,0) |
| the cover partner | `p >= k` | (0,k), (0,t), (t−k,k) |

The opponent case alone — the one DDS §4 states — was worth **−1.2% at 13
cards**, which is thin. The other two came free with a scan already being paid
for and took it to −3.6% on the same workload, then −16.7% once the sample
widened. **The nil-bidder case overlaps `nil_must_take_a_trick`**, which pins
`n >= 1`; this pins `n >= k`, so it is strictly stronger at `k >= 2` and the
same claim at `k = 1`.

Placed **after** the untightened test rather than before it, which is a cost
decision: a node the cheap bound already answers wants nothing more computed,
and it answers 7–11% of boundaries at 12 and 13 cards.

**`MODE_FULL` only, and inert in fast mode twice over** — the reach bound it
rides on is gated to full mode, and a count of the *opponents'* tricks does not
bound a value that is the nil bidder's own trick count. Verified byte-identical
against pristine HEAD on all three 13-card seeds.

#### Measurements

Nodes, `MODE_FULL`, interleaved on one binary against `--no-later-tricks`:

| workload | with | control | |
|---|---:|---:|---:|
| corpus, 560 | 281,287 | 325,975 | −13.71% |
| 9c ×20, seed 1 | 7,556,472 | 7,772,036 | −2.77% |
| 11c ×6, seed 3 | 89,134,281 | 89,118,687 | **+0.02%** |
| 12c ×6, seed 3 | 35,437,172 | 35,940,769 | −1.40% |
| **13c ×8, seed 3** | 1,162,611,881 | 1,396,170,131 | **−16.73%** |
| 13c ×8, seed 42 | 1,705,618,332 | 1,710,273,021 | −0.27% |
| 13c ×3, seed 11 | 232,501,536 | 234,515,537 | −0.86% |

Per-deal on the two widened 13-card samples, sorted by share of the control
tree — the column that matters is the top row of each, because that deal *is*
the workload:

| seed 3 | control nodes | share | Δ | | seed 42 | control nodes | share | Δ |
|---|---:|---:|---:|---|---|---:|---:|---:|
| r13-0007 | 1,114,309,779 | 79.8% | **−19.69%** | | r13-0003 | 1,216,607,188 | 71.1% | −0.38% |
| r13-0006 | 83,913,782 | 6.0% | −0.91% | | r13-0005 | 158,457,944 | 9.3% | −0.14% |
| r13-0005 | 78,394,930 | 5.6% | −1.96% | | r13-0004 | 96,628,902 | 5.6% | **+0.83%** |
| r13-0000 | 66,221,283 | 4.7% | −2.74% | | r13-0007 | 96,313,898 | 5.6% | −0.06% |
| r13-0001 | 31,745,623 | 2.3% | −30.05% | | r13-0006 | 61,614,345 | 3.6% | **+1.38%** |
| r13-0002 | 8,484,140 | 0.6% | −4.33% | | r13-0002 | 52,134,565 | 3.0% | −0.22% |
| r13-0004 | 7,148,117 | 0.5% | −0.05% | | r13-0001 | 26,602,674 | 1.6% | −3.11% |
| r13-0003 | 5,952,477 | 0.4% | −2.76% | | r13-0000 | 1,913,505 | 0.1% | −24.35% |

Wall, seed 3 ×8: **150,140 ms against 181,512 ms, 1.21x**, on throughput of
7,743,489 nodes/sec against 7,691,863 — **flat, marginally in the bound's
favour**, so wall tracks nodes (−17.3% against −16.7%) instead of giving part of
it back. That pair is one paired run rather than interleaved medians, because
each arm is 2.5 minutes; the `×3` workloads did get interleaved reps — seed 3
faster in 5 of 5, seed 11 in 3 of 3, seed 42 in 3 of 5.

#### The two negatives, recorded

**11 cards is a wash, and the population measurement predicted the opposite.**
A floor sweep before any code was written said 11 cards had the *largest*
new-firing population of any size — **4.97% of trick-boundary nodes against
3.08% at 13** — and it returned +0.02%. Same shape as several entries in
"Evaluated and rejected": **population is a weak predictor of savings**, because
the nodes a bound newly answers are not drawn uniformly from the nodes that cost
anything. Anyone reading a population number in this file should discount it
accordingly.

**Seed 42 gets essentially nothing**, and two of its eight deals get *worse*
(+0.83%, +1.38%). The three-deal samples this item was first measured on were
misleading in both directions: they showed −11.01% on seed 3 (against −16.73%
widened) and −1.74% on seed 42 (against −0.27% widened). **Three deals is not a
sample at 13 cards** — one deal routinely owns 70-80% of the tree, and which
deal that is decides the headline.

There is no account of what separates a −19.69% deal from a −0.38% one. The
variance is **deal-to-deal, not size-to-size**: the −24.35% deal on seed 42 is
0.1% of that tree and the −19.69% deal on seed 3 is 79.8% of its own, so it is
not that the bound only pays on cheap positions. Finding the discriminator is
open, and would be worth more than the patch was.

### 41. ~~The cutoff bound from a partial table match~~ — ⭐⭐⭐ — **done, patch 41; from [Plaat et al.]**

`tt.hpp` carried the admission for as long as the table has had bounds in it: a
match that does not settle the window *"is counted as `partial` and reported as
a miss, so that `hits` keeps meaning nodes answered from the table"*. Reported
as a miss and then discarded — and it is not a miss. `BOUND_LOWER` at x says the
value is at least x, `BOUND_UPPER` at x says it is at most x, and both are facts
about the POSITION, true whatever window happens to be asking.

**Not item 5, twice over.** That item wanted the stored MOVE off a partial entry
for move ordering, and it was closed twice — patch 12 because `partial` was
identically zero, patch 26 because once patch 22 gave it a population the move
turned out to be a worse hint than what it displaces. Neither result says
anything about the stored BOUND, which is not a hint at all but a proof.

**What it does.** Only one of the two bounds can end a node — beta at a
maximiser, alpha at a minimiser — so the entry is compared against that bound
alone and, when tighter, becomes the node's cutoff threshold. `alpha` and `beta`
are untouched, so children are searched under exactly the window the caller
gave and nothing about their stored entries changes.

**That restriction is the item.** Tightening the window itself, which is what
the textbook form does, was built first and is a **loss** — +5.5% at 11 cards
after the classification fix, +20.3% before it. See "Evaluated and rejected"
for the sweep, which is monotone across both the direction and the depth axis,
and for the mechanism: a tighter window makes descendants record one-sided
bounds where they would have recorded `BOUND_EXACT`, and an exact entry answers
every window while a bound answers almost none.

**Why the early cutoff is sound.** Take a maximiser whose entry pins `V <= y`
and which stops at the first `best >= y`. The move that produced `best` was
searched under the untouched window, so if `best` sits above alpha it came back
exact and `V >= best`; with `V <= y <= best` that forces `V = best`. The value is
squeezed exact by the same fact that shortened the search, so `walk_pv` and
`canonical_move_for` still compare true values and MODE_FULL's principal
variation is unmoved. Symmetrically at a minimiser. A partial also can never
close the window — failing to answer means the bound sits strictly inside it —
so there is no degenerate case to handle.

**Measured**, both arms from one binary, `--no-tt-narrow` as the control:

| workload | off | on | nodes |
|---|---:|---:|---:|
| corpus 560, full | 281,287 | 279,941 | −0.48% |
| corpus 560, fast | 39,701 | 39,701 | **identical** |
| random 9c x20, seed 1 | 7,556,472 | 7,477,320 | −1.05% |
| random 11c x6, seed 3 | 89,134,281 | 88,491,025 | −0.72% |
| random 12c x6, seed 3 | 35,437,172 | 35,059,198 | −1.07% |
| random 13c x3, seed 3 | 94,727,951 | 93,581,425 | **−1.21%** |
| random 13c x3, seed 11 | 232,501,536 | 231,122,364 | −0.59% |
| random 13c x3, seed 42 | 79,246,073 | 78,767,161 | −0.60% |

Nothing gets worse and the gain is largest at 13 cards, which is the right shape
— though it is **not provably monotone** and the entry should not be read as
claiming so: a node that cuts earlier stores a different entry, and the table
couples workloads that the flag otherwise separates.

**Wall time is a weaker result than nodes and this says so.** Interleaved
medians on two 13-card seeds: 3390.8 ms against 3406.7 (seed 3) and 2807.0
against 2827.4 (seed 42) — 0.5% and 0.7%, faster in 3 of 5 reps on each. Per
patch 40's lesson this was measured with the arm toggled at runtime on ONE
binary; a cross-binary comparison could not have resolved a change this size.
Throughput is flat because the change adds no per-node work: `cut_at` is
loop-invariant (loop narrowing moves the OTHER bound) and hoists out of the move
loop, which is where the old `maximizing ? beta : alpha` was being re-evaluated.

`MODE_FAST` is inert by arithmetic rather than by a gate — its window is `[0, 1]`
at every node and every value it stores is a bound at one end or the other, so
every match settles its window. The test suite pins that as an equality, not an
inequality.

### 32. An adversarial nil-set proof — ⭐⭐⭐ → ⭐⭐ — **population measured, patch 38; proof NOT built, and the reason is worth reading**

`nil_must_take_a_trick` proves the nil bidder wins a trick *whatever all four
players do*. Fast mode does not need that much: its window is `[0, 1]`, a return
of 1 is a lower bound, and the opponents are the maximisers, so they may be
assumed hostile rather than cooperative. DDS §4's `LaterTricks` is the same
question asked the right way round.

**The population is real.** At 7 cards, over positions where the nil bidder holds
a spade: the proof fires today on 31.5%, and of the 68.5% where it stays silent,
**54.0% are genuinely forced and an adversarial spade count spots them.** That is
a large addressable population and it is why this item was worth measuring
rather than guessing at.

**And no static spade count reaches it soundly.** Three variants, against the
solver as ground truth (`nil_fails` in MODE_FAST *is* "the opponents can force a
trick", so the truth is exact rather than sampled):

| variant | precision | population |
|---|---|---|
| ignore the partner's spades | 77.6% | 54.0% |
| require the partner void in spades | **96.2%** | 10.5% |
| let each partner spade overtake the highest nil spade below it | 89.5% | 39.7% |

A proof returns a lower bound that cuts the search off. 96.2% is not a proof;
it is a bug that passes testing. **That middle row is the most dangerous number
on this page** -- the most conservative variant, one false alarm in twenty-six,
and it would have survived the corpus.

**The counterexample, which is the mirror of this item's own insight.**
`N:KQ75.4.9.3 .J96.J.762 J3.5.K76.4 4.T82.2.Q5`, West bids nil holding a bare
♠4, East is void in spades, and the nil holds. The forcing story says an
opponent leads ♠3 and the ♠4 is stranded with no lower card to duck with. What
it misses is that **the other opponent must follow suit too, and North holding
♠KQ75 is compelled to play a card that beats the ♠4 and rescues the nil.** The
item's own example is that covers concentrated in one hand can be spent only one
per trick, which hurts the nil bidder; this is the same fact pointing the other
way, an opponent with only high spades forced to save the nil it is trying to
break. A pooled count cannot see either, and seeing one without the other is
worse than seeing neither.

**What a sound version would have to do**, and why that is no longer ⭐⭐⭐: model
concentration in both directions, and then model tempo -- whether the opponents
can win enough tricks to lead spades the required number of times before the
hand runs out. Entry analysis is a different and much larger item than a
counting rule, and item 9's void map is a prerequisite rather than a neighbour.

**Shipped:** `--nilset-stats`, the three-way count at trick boundaries inside a
real search, and `tools/nilset_population.py`, which is what produced the table
above. Anyone reopening this should start from the false alarm, not from §4.

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
1 ✅ → 2 ✅ → 3 ✅ → 4 ✅ → 5 ⊘ → 7 ✅ → 6a ✅ → 6b ✅ → 6c ⊘ → 6d ✅ → 15 ⊘ → 21 ✅ → 22 ✅ → 23 ✅ → 24 ✅ → 22b ✅ → 25 ✅ → 5 ⊘⊘ → 27 ✅ → 28 ⊘ → 28b ✅ → 29 ✅ → 30 ✅ → 33 ✅ → 28c ✅ → 34 ⊘ → 35 ✅ → 31a ✅ → 31b ⊘ → 32 ⊘ → 36 ✅ → 41 ✅ → 42 ⊘ → 29b (single-suit) → 9 ⊘ → 10 ⊘ → measure → 11..14
```

**Item 29b is next, and the case for it is now measured rather than argued.** A
node-population sweep at 11 and 13 cards (fast and full) says **31-33% of
branching nodes exhaust their move list without cutting**, and those all-nodes
emit **54-56% of every child search in the tree** -- ~2.9-3.1 moves each against
~1.15 for a cut node. Ordering cannot touch them by definition. The same sweep
says **87-91% of cutoffs already land on the first move tried**, which caps
items 9 and 10 and every other ordering idea low enough that they are struck
from the sequence above: whatever is left for them lives in the 9-13% of
cutoffs that do not cut immediately, a move or two apiece. **The remaining
headroom is in bounds that convert an all-node into a cut node, not in ordering
that reaches a cut sooner.** Item 36 is the first withdrawal against that
reading and it paid; 29b is the large one. In fast mode an exhausted maximiser
node *is* "nil survives this subtree" -- exactly what `nil_cannot_be_forced`
proves when it fires, which it does on 2.9% of boundary nodes. **The gap between
2.9% and ~31% is 29b's target population.**

34 was built and refuted, and 31 has followed it. Item 31 was taken out of order, ahead
of 29b, and the ordering argument that put it second was right for the wrong
reason -- it said 31's first honest step was a population measurement, and the
population measurement is indeed what closed it, but a counterexample closed it
first and cost less. **The lesson 34 taught was to get a number before building;
31 adds a second: get a counterexample before getting a number.** The two
positions that refuted 31b differ in one card and could have been found by a
randomised differential in an afternoon, against the several sessions the full
canonical-key build took. A soundness lever earns a property test before it
earns an implementation.

That test is worth naming precisely, because it is what the corpus could not do.
Restricting coarse entries to exact nodes passed all 560 oracle-pinned
positions, and passed 900 random deals at two truncation levels, and was *still*
unsound -- it showed up only at the most aggressive truncation, twice in 700
deals. **A corpus pass is evidence about the corpus.** Where a change claims two
positions have the same value, the thing to run is a generator that hunts for a
pair that do not.

*What the old text said, kept for the record.* It followed because full mode at 13 cards is the slowest thing left and
because patch 31 is its prerequisite in fact if not in form: a cheaper node makes
a repeated-probe driver affordable, and the support count that makes bisection
tractable at all is now written down.

Item 31 was sequenced after 29b rather than before it because 29b is a bounded
piece of work with a measured target and 31 was a table redesign whose first
honest step is a population measurement. It got taken first anyway, and is now
closed -- 31a landed, 31b refuted. Item 32 sits lower still: its population is
unmeasured, and the shape of this file's last several results is that unmeasured
populations are usually small.

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

`--tt-all-plies` appends `+ttallplies` in both modes, and it is the one suffix
on this list that earns itself twice over: the arm it marks moves node counts in
*both* directions depending on the deal, so a history that merged the two groups
would not read as a regression or an improvement — it would read as noise, which
is worse.
