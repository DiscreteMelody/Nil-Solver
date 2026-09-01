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
| ✅ | ~~Two corrections and a test deal, from a reader's question~~ | patch 85 | **Both found by T asking two questions about the same deal, and both were things the measurements could not have caught.** (1) **Item 82's prose overreached.** It said deal 5 *is not a nil problem any more, it is a double-dummy trick count wearing a nil problem's clothes*. T asked whether that ignores branches where both bids get set, and gave the case: **a cover that wins every trick robs both bids of one, so both SURVIVE** -- rank 2, better than the answer for whoever reaches it, and the search must consider it. A node's rank is settled only once its MASK IS FULL, and deal 5 is 8.62% intact, 52.35% one-down, 39.04% both-down: **61% of it is still a nil problem** and only the last 39% is the pure trick count item 82 measures. The measurement was always scoped to that 39% and was right; the prose was not. **A wrong sentence sitting next to correct numbers is the kind that survives review.** (2) **`nil_tricks` counts tricks won by ANY bidder**, which is right for a two-bid deal, and the CLI labelled it `Tricks for N`, so a reader saw *Tricks for N 6* and reasonably read it as N winning six. N won two; E won four. Now `Tricks for N+E  combined 6 of 13`. Reported by a user comparing two role assignments of one deal -- exactly the comparison the label made impossible. (3) **`tests/corpus/opposed13_settled.txt`**: deal 5's cards with the bids on S and W instead of N and E, where S's KQ of spades under W's AJT9 kills both before a card is played. Census **0.00% intact, 7.39% one down, 92.61% both down**. So the 4.7x gap between the two assignments -- 292,597,961 against 62,331,424 -- **is not the solver being worse at one**, they are different questions. And the EASY one still costs 62 million nodes with the nil question free, because 92.6% of it is a trick count with every bound off: the cleanest test case in the repo for item 82, whose own ceiling here is 46.78% needing one trick. 29/29; opposed 402,422,529 and every banked count unmoved |
| ✅ | ~~The both-down region is a pure trick count: ceiling measured (item 82)~~ | patch 84 | **The answer to *what prunes when both bids must die* is: stop pruning on nils and prune on tricks.** The ranks a node can still reach are those of the masks containing its own, and against a *both die* answer **all four masks either straddle the band or sit exactly on it** -- so no arithmetic on the mask refutes anything. Against a *both live* answer two of the four sit entirely below and vanish on arrival, which is deal 6's 86.62% and item 79's whole yield. **Deal 5's 1.013x from item 78c is that table, not a weak probe.** The consequence: with the rank pinned at the root by items 77 and 78, every node inherits it and the entire remaining job of a 292-million-node search is the far side's TRICK COUNT -- a double-dummy trick count wearing a nil problem's clothes, solved with every trick bound switched off. **How strong would a bound have to be?** Asked before re-expressing QuickTricks or LaterTricks, because that is most of the work of shipping them; the answer falls out of the window and the tricks left with no bound written. The region is 12.37% of nodes at a trick boundary, and **71.88% of it needs at most two tricks proven, 48.05% exactly one** -- an ace, a top trump, a ruff, the first thing DDS section 3 computes. Only 0.11% is hopeless. Deal 5 alone is the same or better: 15.91% of nodes, 49.56% needing one trick. **Against item 81, rejected one patch earlier, the contrast is the argument**: 81 had a 19.08% population and a 10.8% firing rate because its proofs missed the common middle case, where here the DEMAND is a whole rank lower. **Still unmeasured, and 81 is why that is said plainly**: this bounds how strong a proof must be, NOT how often one fires, and 81 died in exactly that gap. Measurement arm shipped under `--opposed-stats`; nothing spent, nothing moved: 29/29, opposed 402,422,529, banked counts unmoved |
| ⊘ | ~~Recover the single-nil machinery in the one-down region~~ | patch 83 | **REJECTED on ceiling. Population 19.08% of nodes, ceiling 1.24%.** The argument is sound and still is: with exactly one bid live the position IS a single-nil position, the dead bid cannot come back, and a proof pinning the survivor's fate collapses item 79's reachable set from two values to one -- tightening its bound by a whole `k*k`. Item 76 made the same argument for the BOTH-down region and it held. **It dies in the middle step**: only 10.8% of eligible nodes get a proof at all. `nil_must_take_a_trick` needs the live bidder to hold spades and be short of covers; `nil_cannot_be_forced` needs it to hold none; **a live bidder holding spades WITH adequate covers gets neither**, and that is the common case in this region. They are cheap SUFFICIENT conditions, not a decision procedure. Newly answered if the rank were pinned: 4,393,491 nodes, **1.24%**, and worst on the deal that matters -- deal 5 **1.09%** against deal 3's 2.05% and deal 6's 1.70%, with deal 5 being 73% of the remaining tree. The arm would be charged across a fifth of the search to answer one node in a hundred where it counts. **`Population is not the same as firing rate` for the third time in this file**, and this time the population looked good and the firing rate killed it -- the reverse of item 79, whose mediocre aggregate hid an excellent per-deal split. Neither was guessable from the other. **What would revive it**: a cheap proof covering the middle case. `duck_depth` (item C0) already computes that holding's shape and ships with nothing consuming it; if it can decide the survivor's fate rather than describe the holding, re-measure the FIRING RATE, not the population, which is now known to be 19.08%. The measurement shipped under `--opposed-stats` so the claim is re-checkable, not merely recorded. Nothing moved: 29/29, opposed 402,422,529, and 39,701 / 278,059 / 49,084 / 163,393,676 / 4,833,200 unmoved |
| ✅ | ~~The conjunction probe wired into the presolve (item 78c)~~ | patch 82 | **1.024x overall, and THE PREDICTION IN 78b WAS BACKWARDS -- that is the finding.** The third probe fires only when the first two DISAGREE, so the six deals where they agree measure **1.000x, not approximately**; the probe never runs. Deal 3: 15,161,073 -> 9,316,457, **1.627x net and 3.14x gross**. Deal 5: 296,509,340 -> 292,597,961, 1.013x. **78b predicted the opposite**, reasoning from size: the probe costs 1.2% of deal 5 and 20.1% of deal 3. Both figures were right and the conclusion was still wrong, because **tree size is not what decides it -- the RANK the band closes to is.** Deal 3's conjunction is TRUE and closes on rank 0, an extreme, where item 79's mask bound refutes every node under a broken bid on arrival. Deal 5's is FALSE and closes on *both bids die*, **the one rank where a closed band buys nothing**, because from there the reachable sets straddle the answer from both sides. Item 79's own entry derived that -- *R=1 gives no arithmetic refutation* -- and 78b then failed to apply it. **The band being closed is not the point; closing it somewhere USEFUL is.** The corollary kills the gate this item was meant to design: **which rank it closes to is exactly what the probe is paid to discover**, so a gate would have to predict its own answer -- and no gate is needed, since both mixed deals come out net positive anyway. **Patch 77's only losing deal is now its best**: deal 3 was 0.87x when the presolve shipped and is 13,166,148 -> 9,316,457 against the pre-77 base. Wall 4 of 4 at 0.949 / 0.953 / 0.997 / 0.908, a wide spread and one nearly-flat rep, which is what a 1.024x node change on a workload dominated by one barely-moved deal should look like. The rank is pinned without casing out the partner leans: enumerate the four, drop those outside the existing bound, strike off the one the probe refuted, close only if exactly one survives. 29/29; all 8 deals identical on value, trick counts, `nils_set` and PV; `opposing_crosscheck` re-run with the gate forced to zero, 96/96, 96/96, 48/48; banked counts unmoved |
| ✅ | ~~The conjunction probe in C++, measured but not yet wired in (item 78b)~~ | patch 81 | **Built, cross-checked against two oracle routes, and MEASURED BEFORE BEING WIRED IN -- the measurement says it pays on one of the two deals it was built for and probably not the other.** A question about a shape rather than a mode: `conjunction_seat` names the attacking side's bidder and `solve()` refuses it anywhere but `SHAPE_OPPOSING_NILS`. **The indicator is RANK 3 and nothing else**, under both partner leans, so it reads two bidder seats and no role; charged as a delta the way the rank already is, telescoping to the answer. **Two gates had to be excluded BY NAME rather than read off the weights**: the probe's weights are `(1, 0, 0)` exactly like MODE_FAST's and mean something else, so `value_is_nil_tricks` would have wired two sound proofs to a value they say nothing about -- `disable_single_nil_machinery`'s failure mode arriving through the back door -- and `gains_nonnegative` is false because the delta is **-1** as well as +1. **The defending side's goal is a DISJUNCTION**, its bid alive OR the attacker's dead, so it may dump a trick on the attacker's bidder and abandon its own; a search that let it only protect would report the attacker winning lines it cannot win, confidently and unrefutably by any corpus. **Checked against the oracle's independent boolean search AND the outcome rank of its exhaustive utility search**: 320 probes at 3 cards and 256 at 4 across all eight strictly opposed role sets, all three agreeing every time, 80 and 58 forceable so the run is not all one answer. **THE FLOOR MEASUREMENT IS THE DELIVERABLE.** Only the two MIXED deals ever ask -- the other six are settled by 77's plain probes. Deal 5: 3,476,421 against 296,509,340, **1.2%**. Deal 3: 3,040,879 against 15,161,073, **20.1%** -- on a deal that is already a 0.87x loss from patch 77 and whose item-79 ceiling is 2.79% against deal 5's 14.78%, so it has less to win as well as more to pay. **The probe wants a gate, and the honest gate is not obvious**: hand size does not separate these two deals and nor does anything 77 already knows. Nothing is wired in, so nothing moved: 29/29, 39,701 / 278,059 / 49,084 / 163,393,676, multinil 4,833,200 and opposed 412,178,524 all unmoved |
| ✅ | ~~The conjunction probe, in the oracle (item 78a)~~ | patch 80 | **Ground truth first, as items 58 and 67 did, and it turned out to be derivable as well as searchable.** The probe: *can one side force the other's bid down while keeping its own?* -- the third question item 77's presolve needs, which closes the two mixed cases where 77 can only supply a one-sided bound and where item 79's ceiling is **14.78% and 2.79% against the 86.62% a closed band gets**. The two items MULTIPLY: 78 closes the band, and 79's whole yield is a function of whether it is closed. **THE EQUIVALENCE is the finding**: the conjunction is exactly outcome RANK 3 for the attacking side, and that same outcome is rank 0 -- the unique worst -- for the other side **under both partner leans**, so a defender maximising its own utility escapes it whenever it can and backward induction on the utility pair lands on rank 3 precisely when the attacker can force it. **It therefore holds on all three shapes, not just the strictly opposed one**, so the probe is already defined for the two arrangements the C++ refuses. Shipped anyway as an INDEPENDENT boolean AND-OR search, because the point of having two is that they are written separately: 1,920 comparisons across all sixteen opposing arrangements at 3 and 4 cards, **zero mismatches**, 488 of 1,920 forceable -- and the population is asserted too, since a check that only ever sees False passes while measuring nothing. `solve_conjunction()`, `ConjunctionSolution`, `--conjunction <seat>` with a compact form for 78b's crosscheck. **The hazard 78b must not walk into**: the DEFENDING side's goal is a DISJUNCTION -- its bid lives OR theirs dies -- so it may dump a trick on the attacker's bidder and abandon its own bid. Constrain it to protect and the search reports the attacker winning lines it cannot win, confidently and unrefutably by any corpus. All 164 oracle checks pass; 28/28; no C++ touched and no node count moved |
| ✅ | ~~The principal-variation walk's window travels with the line~~ | patch 79 | **82,348,545 PV-walk nodes down to 14,751,323, 5.6x, with every reported node count bit-identical; 1.19x of wall time, 4 of 4 reps.** The whole item is wall time -- the walk's nodes are snapshotted out of `search_nodes`, which is why a 22.6% cost sat unnoticed from patch 77 until patch 78's own instrumentation split it out. **The shift the walk never had**: `search_impl` hands children `alpha - gained, beta - gained` because a child's value is measured from the child, and `walk_pv` re-enters at a POSITION rather than descending through `value_after`, so it asked every step the question the ROOT was asked. Loose windows forgave it; a rank band does not, because the root's value carries a `k*k` of rank a sub-position past a broken bid does not. Patch 77 answered by walking under the sentinels -- correct and blunt, since nothing the walk probes then settles the wider window. **THE CONTROL ARM TRIED TO SHIP A BROKEN SOLVER**: the obvious `--no-pv-shift` leaves the band and stops shifting it, which is not the previous behaviour but *the bug patch 77 found* -- the replay check fires on deal 5, deals abort, and the arm reported 100,508,111 nodes against 412,178,524. **A flag whose OFF position is unsound measures nothing.** OFF is now patch 77's actual walk, so both arms answer and one re-searches; it is also a retroactive proof that the sentinels were needed rather than a tweak, because the tweak IS the bug. Single nil gains too, less: `large.txt` 18.67s to 18.02s. **Also fixed: a silent no-op shipped in patch 78** -- the `pv_walk_nodes` row never reached `--opposed-stats` because the edit adding it used a string replace with no assertion, matched nothing and reported success. The counter was live; only the display was missing, and a measurement that stops being printed looks exactly like a measurement of zero. 28/28; all three corpora under `--check-pv --check-moves`; 8 deals byte-identical to HEAD with the PV; 39,701 / 278,059 / 49,084 / 163,393,676 and multinil 4,833,200 unmoved |
| ✅ | ~~The reachable-rank bound for one bid on each side~~ | patch 78 | **1.20x in nodes and 1.21x on the clock on top of patch 77, 4 of 4 reps; 1.57x from the pre-77 base.** The `target_bounds` this shape lost at patch 68 with the comment *Correct first*. The rank is a step function of the broken-bid mask and a bid never un-breaks, so the ranks a node can still reach are those of the masks containing its own -- four of them, no cards read -- and the subtree's value range follows. **Population and ceiling measured before building**: 24.86% of nodes answerable in aggregate, but **86.62% on deal 6 with far-down and both-down each firing at 100.00%**, against 14.78% on deal 5 and 2.79% on deal 3. **The ceiling is a function of whether patch 77 closed a TWO-SIDED band, not of the cards**, which is also why the near-bid-down row reads 0.41% and is not worth chasing: that state can still reach ranks either side of the answer and is unanswerable by construction. **THE FIRST SPELLING COST 13.8% OF THROUGHPUT** -- sixty-four iterations per node to fire on a quarter of them, measuring 1.20x on nodes and 0.971 on the clock, which is item 44's failure and C1's. **Unlike either, there was a cheaper spelling sitting right there**: the rank term depends on the mask alone and there are four masks, so it is four pairs of numbers settled in `configure()` plus one comparison skipping the two states that never fire. Node counts came back **bit-identical** and throughput went 6.71M to 8.18M nodes/sec. The lesson is not *measure throughput*, which this file has three times over -- it is that **a per-node gate should be a table lookup before it is abandoned.** Nothing is stored in the TT, and the bound is inert along the PV by arithmetic rather than by a gate. **Also found, by this patch's own instrumentation: patch 77 spends ~88M nodes, about 20% of the opposed search, recovering the principal variation, and none of it is in any reported node count** -- it walks under the sentinels while the search ran under a band, so the table does not settle and the walk re-searches. Item 77's *costs nothing that is measured* is true and misleading; item 80 is the fix. Behind `--no-opposed-reach`; `--opposed-stats` ships the measurement arm. 28/28; 39,701 / 278,059 / 49,084 / 163,393,676 and multinil 4,833,200 all unmoved |
| ✅ | ~~A presolve-seeded root window for one bid on each side~~ | patch 77 | **1.31x in nodes and 1.23x on the clock over the eight contested 13-card deals, 4 of 4 reps.** Item 23's presolve was gated on `nil_count(roles) == 1` since it was written; the opposed shape got no root window at all and searched between sentinels. It now runs TWO fast probes, one per bidder, each an ordinary single-nil question the solver has always answered. **Each answer is a GUARANTEE, which is why they compose**: it holds against every strategy the other side has, including ones that sacrifice the other bid, so it survives being asked inside a deal where the attacker has a bid of its own. A guarantee about one bid confines the outcome to two of the four, and two of four bounds the outcome RANK -- at the top when the minimising side owns it, at the bottom when the maximiser does. **Both bids safe means both live, both breakable means both die, and neither needs a third search**; the two mixed cases get a one-sided bound only, which is item 23's shape and is why deal 3 LOSES 0.87x. Range 0.87x to 2.65x, probes 7.5% of the armed total, throughput -6.4% because the nodes the band removes are the cheap ones under a broken bid. **THE BUG THIS FOUND is worth more than the entry.** `walk_pv` re-searches each PV step under the ROOT's window, unshifted by what the line has banked -- sound against the sentinels and against item 23's loose beta, and NOT sound against a rank band, because a sub-position past a broken bid does not carry the `k*k` the root paid for and fails high on every step. Caught by the replay check on deal 5, search -70 against replay -322. **It hid on 7 of 8 deals**: when both bids live the rank never moves along its own PV, so the unshifted window contains every step by accident. A bug that hides on seven eighths of the corpus is a bug that ships. **The random-deal sample is much weaker and says so honestly** -- 0.99x to 1.30x, and a wash on the clock at 11c seed 3 -- because a hand drawn at random usually gives a bidder an ace and the search never has to work. That is patch 69's lesson for the third time. **`opposing_crosscheck` re-run with `PRESOLVE_MIN_TRICKS` forced to zero**, since the shipped gate of 8 means the 3-5 card suite cannot reach the new code: 96/96, 96/96, 48/48 across all eight strictly opposed role sets. Also shipped: `Solution::presolve_nodes` so the cost reads off the run that measures the benefit, and **`nil_bench --deals`**, because `opposed13.txt` has been in the repo since patch 76 with nothing able to read it. NOT wired into `solve_moves`, and the comment there says why. 28/28; 39,701 / 278,059 / 49,084 / 163,393,676 and multinil 4,833,200 all unmoved |
| ✅ | ~~The static cutoff in positions where every bid is already down~~ | patch 76 | **-1.29% of nodes on the opposed corpus, sound, and it is the SMALL half of a much more useful finding.** `gains_nonnegative` guards the end-of-trick static cutoff and is refused outright for the opposed shape, on the grounds that its primary weight multiplies outcome RANK and rank falls when a side loses its own bid. True, and it goes no further than the first position where NO NIL IS LEFT TO BREAK: there the rank is fixed, what remains is the trick term, and that only rises. So the refusal is a fact about the WEIGHTS where it should be a fact about the POSITION, and it is now tested per node against `st.nils_broken == ctx.nil_mask`. All three opposed weights are already non-negative (`primary = k*k`, `secondary = +k`, `tertiary = 0`); the blanket `!ctx.opposing` was the only thing switching it off. **Every one of the eight opposed deals improves** -- -0.27% to -4.55%, 654,842,428 -> 646,389,160 -- and every answer, all 560 corpus values, all 19 `large.txt` rows and 39,701 / 278,059 / 49,084 are unmoved. Behind `--no-settled-gains` / `NIL_FLAG_NO_SETTLED_GAINS`. **THE PREDICTION WAS WRONG AND THAT IS THE VALUABLE PART.** It was built because 43% of an opposed tree sits in the all-broken state, so a cutoff restored there looked like a large win. It is worth 1.29%. A cutoff being LEGAL in 43% of the tree says nothing about how often it FIRES: the static test needs one trick's gain to carry the line to beta, and in an all-broken state the primary contribution is already banked, so the per-trick gain is small against the window and the test almost never trips. **Population is not the same as firing rate, and this project has now been caught by that twice** -- once here and once on move-ordering item C1, where a rule that ran on 8-15% of nodes changed the move on 0.2-2.9% of them. Measure the firing rate, not the eligible population. **Also banked: `tests/corpus/opposed13.txt`**, the eight deals patch 69's timings were taken on. Patch 69 recorded the timings and NOT the deals; they survived only because a session transcript happened to contain the generator, and were recovered from it here. A measurement is only valid against the tree it was taken on, and that tree was very nearly lost |
| ⏸ | ~~Move ordering C5: the cover partner ducking the nil bidder short~~ | patch 75 | **PARKED, not rejected -- the only item in the move-ordering block with two-sided signal, and the first that is not decided by a single deal.** On lead, play the cheapest card the nil bidder can duck beneath in its SHORTEST suit: the nil bidder is safe on the trick by construction, and the suit shortens toward the void that makes every later lead a free discard. It manufactures the void C3 cashes into. **It won three of six workloads on EVERY rep -- -4.06%, -4.34% and -12.03% of nodes -- and lost two of six on every rep (+2.37%, +6.01%). Nothing came out neutral.** **Throughput is flat**, +1.3% to -1.2% with no consistent sign, so this is not C1's failure; and every workload moves by at least 2.4%, so it is not N1's or C2's either. The 13-card aggregate of +0.09% is the least informative number in the set: seed 11 is two thirds of that workload by nodes and hides two 4% wins. Unlike every earlier item here, SEVERAL deals move per workload and the sign varies WITHIN a workload -- at seed 11, `r13-0002` +50.6%, `r13-0004` -5.1%, `r13-0006` +1.5%. That is a real effect with an unidentified condition attached, not noise. **Population: 0.81-1.36% of nodes, the most consistent in the block**; fires on 59-72% of cover-lead nodes and changes the move on about half. **Named next experiment: the TIE-BREAK.** The nil bidder's shortest suit is tied on ~40% of firings and ties currently fall to rotation order -- spades first, the same accident C2 measured at 12% of nodes. A rule whose sign flips between workloads with an arbitrary choice on two fifths of its firings has an obvious first suspect. Shipped behind an OPT-IN `--cover-duck-short` / `NIL_FLAG_COVER_DUCK_SHORT` with the arm OFF, so the default build is unchanged and the next iteration is a tie-break rather than a rewrite. 28/28; 39,701 / 278,059 / 49,084 unmoved |
| ⊘ | ~~Move ordering C3: the cover partner cashing into the nil bidder's void~~ | patch 74 | **the old four-tier lead rule is now four items, C3 to C6, and this is tier one measured alone.** Split because four tiers sharing a fallback chain are four separate bets, and one number would have recorded four arms as a single result. The rule: on lead, holding a card nobody can beat in a suit the NIL BIDDER IS VOID IN, lead it -- this side wins the trick and the nil bidder must discard, which is a free throw of its most dangerous card elsewhere. **Population: 0.07-0.74% of nodes at 13 cards**, firing on 18k-221k lead nodes of which roughly three quarters are ruff-proof. **Nodes: 36,287,899 -> 37,104,695 across the three 13-card seeds, +2.25%** -- +3.46% on seed 11, +1.05% on seed 3, -0.29% on seed 42. `r13-0002` alone goes 1,293,640 -> 2,281,815 and is the whole of seed 11. **Wall clock, four interleaved paired reps: one of six workloads clean**, and it is an 11-card one; the three 13-card seeds are 2/4, 0/4, 1/4. **Not a failure of mechanism.** 11 cards seed 42 is a real -9.56% on nodes, four reps of four -- when this rule helps it helps more than anything else in the block. The failure is VARIANCE: the same rule costs 3.86% at 11 cards seed 3, and under 1% of nodes the result is decided by the largest deal. The rule promotes a HIGH card where the incumbent promotes a low one, which is the same qualitative change C2 stumbled into with the ruff and which was worth 12% there -- so the direction is not obviously wrong, the selection is. Nothing in the population counters separates the shapes that disagree: firing rate, ruff-proof share and lead-node share are all within a factor of two across seeds that differ by 13 points of nodes. **C4, C5 and C6 are specified and unbuilt**; C6 reuses the `duck_depth` suit comparison C2 measured at +0.08%. 28/28; 39,701 / 278,059 / 49,084 unmoved |
| ⊘ | ~~Move ordering C2: the cover partner, discarding~~ | patch 73 | **rejected, and the measurement is worth more than the heuristic was.** The rule: void in the led suit, throw from the suit where the nil bidder can duck the fewest tricks under this hand. **As specified it costs +12.01% of nodes across the three 13-card seeds** (36,287,899 -> 40,645,401; +10.16%, +7.78%, +22.03%). **Held to its own stated scope it does NOTHING: +0.08%.** The gap between those two columns is the finding. The incumbent order at these nodes is not a heuristic -- it is `take_next_suit` starting from `suit_cursor = 3`, which reaches suit 0 first, and **suit 0 is SPADES**. So at every void node where the cover partner holds a trump, today's first move is a **RUFF**, on **80-95%** of these nodes, and any C2 promotion displaces it. **Nothing decided that**: it falls out of the bit layout in `cards.hpp` meeting the cursor initialiser in `order_moves`. Renumber the suits and it vanishes silently -- answers stay correct, the benchmark gets slower. **The duck-depth suit choice, measured alone on the 0.09-0.17% of nodes with no ruff to displace, moves node counts by less than a tenth of a percent on every seed.** No wall clock was run on that arm; there was nothing for it to measure. **First evidence about whether C0 is USEFUL as opposed to correct**: on its one consumer, ranking suits by duck depth is indistinguishable from ranking them by suit number. C3 tier 4 proposes the same comparison and should be expected to behave the same way. This patch leaves the behaviour alone and PINS it -- a comment recording the measured cost, and unit tests asserting the rotation reaches spades first and leaves the cursor there, so a future `take_next_suit` breaks a test rather than a benchmark. **The accident is the largest single effect this block has measured, larger than N1, C1 and C2 together, and it was already in the code.** 28/28; 39,701 / 278,059 / 49,084 unmoved |
| ⊘ | ~~Move ordering C1: the cover partner, following suit~~ | patch 72 | **the largest population of any item in the move-ordering block, and it still lost -- on THROUGHPUT, which is the failure this block was written to catch.** The cover partner has had no rule since 6c was rejected, so it searches canonical ascending. C1 gave it one: when the nil bidder is winning the trick or would win it, search the CHEAPEST card that covers it; otherwise promote nothing, because ascending already leads with the cheapest card. **Population, measured before implementing: 0.72-2.86% of nodes at 13 cards** -- 2.5 to 9 times N1's, which is why this one was worth building where N1's number alone would have closed it. The seat follows suit on 8-15% of all nodes and the nil bidder is safe on about five of six of them. **Nodes: 36,287,899 -> 35,891,801 across the three 13-card seeds, -1.09%** -- but +7.78% on seed 3, and +20.25% at 12 cards seed 42. As with N1 the aggregate is one deal: `r12-0002` goes 4,256,094 -> 6,835,111. **Wall clock, four interleaved paired reps: ZERO of six workloads a clean win**, and throughput is down 1.0-1.8%. Seed 11 decides it -- 1.54% of nodes off, 1.8% of throughput back, reps split 1/4. **The cost was attacked before the verdict was recorded**: the first version scanned the trick for the nil bidder's card, the second computed its index as `(nil_seat - leader) & 3`. Node counts came back BIT-IDENTICAL, so same tree and same heuristic, and throughput did not move. The cost is the branch, `trick_best_card` and the led-suit mask charged to 8-15% of nodes to change the move on 0.2-2.9% of them; no cheaper spelling closes that ratio. **Kept**: `cheapest_cover_above` in `bounds.hpp`, the second half of C0 and the query C3 tier 3 still wants, property-tested over the same exhaustive sweep. Nothing reads it; binaries byte-identical to HEAD. 28/28; 39,701 / 278,059 / 49,084 unmoved |
| ✅ | ~~Move ordering C0: duck depth under the cover hand~~ | patch 71 | **a primitive with no consumer, shipped alone on purpose, and verified over its ENTIRE input space.** `duck_depth(nil, cover, suit)` in `bounds.hpp`: in one suit, the largest set of pairs matching each of the nil bidder's cards to a **distinct** strictly higher card of its partner's. Distinct is the whole content -- `A` opposite `987` is one duck, not three -- and four worked examples in MOVE_ORDERING.md do not pin that, so it is stated in the header and pinned in the tests. **Not the same question as `cover_deficit_depth`, and the same combinatorics**: that one asks where a holding first runs short against everything OUTSTANDING and answers in leads; this asks how much cover a NAMED HAND supplies and answers in tricks. Hall's deficiency form `m - max_j(j - above_j)` is the bridge, and the property test asserts the agreement so the two cannot drift apart. **Verification is the deliverable, because there is no consumer**: no corpus row and no node count would notice if this were wrong. **All 3^13 ways to deal one suit between the two hands, in each of four suits -- 6,377,292 holdings, exhaustive rather than sampled**, with three independent computations agreeing on every one: the shipped greedy descent, an unconstrained maximum bipartite matching that assumes nothing about the structure, and Hall's deficiency. Plus, on every holding: the bound against both suit lengths, monotonicity in each hand separately, the zero condition, and **invariance under an order-preserving relabelling of the ranks** -- that last because the table stores RELATIVE ranks. **The test was itself mutation-checked**: seven deliberate breakages, six caught; the survivor is the void early-out, documented as an optimisation rather than a case, so surviving is correct and confirms the comment. **Free when unused, provably**: `nil_bench` and `nil_cli` build **byte-identical to HEAD**, which is a stronger claim than the usual unmoved node counts. `tools/duck_depth_property.cpp`, ctest #2, ~7 s; 28/28; 39,701 / 278,059 / 49,084 unmoved and the three 13-card seeds unmoved. **Left out deliberately**: C1 and C3 tier 3 also want *the cheapest cover card the nil can duck beneath*, which is a different query -- folding it in would have made C0 two primitives measured as one |
| ⊘ | ~~Move ordering N1: the nil bidder's forced covers~~ | patch 70 | **the recorded objection was wrong, the condition is exactly true, and the heuristic still loses -- on NODES, which is new for this list.** 6a promotes the highest card that loses *as the trick stands* and leaves cards above the trick's best card alone, because a cover is voluntary and a double-dummy opponent declines to make one. N1's answer: a seat that must follow suit and holds nothing in the led suit low enough to duck has no choice, and **forced is not declined**. **The condition is exact** -- brute force over 400,000 random positions, every legal continuation by the seats still to play enumerated: **8,082 cards promoted above the trick's best card, zero able to win the trick.** **The population, measured before implementing**: the rule changes the promoted card on **0.27-0.33% of nodes at 13 cards** (1.5-1.9% of the nodes where 6a runs). It is small for a structural reason worth keeping -- **48% of 6a's nodes are the nil bidder playing FOURTH**, where *every remaining seat is forced* is vacuously true and completely wrong, 18% have a void seat still to play that may discard instead, and even where the gate opens only 7.5% of nodes change card. **Nodes, one binary, `--no-forced-covers` the only difference: 36,287,899 -> 36,877,301 across the three 13-card seeds, +1.62%** -- seed 11 +3.55%, seed 42 -2.34%, seed 3 -0.73%; 11 cards seed 3 is +5.23%. **Every workload's whole movement is ONE deal**: seven of eight deals at seed 11 move by under 0.001% and `r13-0004` alone (+13.0%) is the entire result. Two deals up, two down, on 0.3% of nodes -- a coin flip, with nothing to tune. **Wall time, four interleaved paired reps per workload**: three of six fail *every rep a win*, including the largest 13-card seed (best ratio 1.030), and 11c seed 3 loses 4 reps of 4 at 1.070-1.086. **The part worth reading**: throughput is FLAT, +1.8% to -1.7%, because the rule is gated on there being a card above the trick's best card at all -- one mask against a set already in hand. Every previously rejected ordering item won on nodes and gave it back on throughput; this one paid nothing per node and lost anyway, so a cheaper implementation of the same rule cannot rescue it. **And it is exactly what this file's own banked ceiling predicts**: 87-91% of cutoffs already land on the first move tried, so any ordering rule is drawing from what is left of the other 9-13%. Nothing shipped; fixed points unmoved at 39,701 / 278,059 / 49,084 |
| 📋 | Where the opposed solver actually stands at 13 cards | patch 69 | **41 seconds worst case, and the first sample said half a second.** Six deals built by pushing every honour onto the two PARTNER seats came back at 0.14-0.53s -- and all six had `nils_set=0`, because both bids were trivially safe and the search never had to work. Loosening the generator so the bids are CONTESTED gives 0.39 / 1.36 / 3.51 / 3.72 / 5.07 / 8.95 / 9.40 / **41.49s**, median around 5s. The cost is not in the card count, it is in whether the outcome is in doubt, and a generator that decides the bids for you measures nothing. **Sequencing**: optimise before adding the unopposed pair. The opposed shape is past the few-seconds bar on ordinary contested deals, and the unopposed pair will be SLOWER than this rather than faster -- it admits shallow pruning only where this one has full alpha-beta, so building it on an unoptimised foundation produces something unusable and makes it impossible to attribute the cost. **What will NOT fix it**: the patch-66 reach bound was worth 1.22x on twin nil and the whole single-nil bound family is 1.31x on top of a table. 1.3x against 41 seconds is 32 seconds. Find where the time goes before porting the bound that worked last time |
| ✅ | ~~One nil on each side: the C++ solver, opposed shapes~~ | patch 68 | **functional, deliberately unoptimised, and it caught an assumption that had been true for sixty-seven patches.** Only the STRICTLY OPPOSED arrangements are accepted -- partners leaning opposite ways, `0 0 3 2` and its seven rotations -- because those are the ones whose rankings are exact reverses and so admit a single scalar. Both partners leaning the same way is refused by name: *the two sides share an interest and the deal is not a strictly opposed two-team game*. **The value is written from the side opposite `ctx.nil_seat`**: its outcome rank, which the near side's mirrors exactly because the two sum to a constant, plus its own tricks. **The rank is charged as a DELTA** -- when a bid dies the rank moves, and the move is worth `rank(after) - rank(before)`; summed along a line those telescope to `rank(final) - rank(none broken)`, so the accumulated value is off by a constant that no comparison within a position can see. **THE BUG THIS FOUND**: `gains_nonnegative` is derived from the weights, and had been a sound derivation everywhere because the primary always multiplied a COUNT that only goes up. Here it multiplies a change in RANK, which FALLS when the side the value is written from loses its own bid -- a negative gain from a non-negative weight, which broke the static cutoff the flag guards. The cross-check caught it as four wrong game values out of forty-eight. A second, quieter one: `replay_pv` split the table with `on_nil_side`, which identifies a side only while ONE side has a bid; with a bid on each it lumped three seats together. Now split by parity, which is identical on every older shape and correct on this one. **Verified**: 144 deals at 3, 4 and 5 cards across all eight opposed role sets, comparing the UTILITY PAIR rather than the cards -- the solver's own line, replayed under the oracle's rules, reaches the oracle's game value every time. `tools/opposing_crosscheck.py` is ctest #27. Everything is gated off for the shape including the patch-66 trapezoid, which is a claim about one side's bids and its own tricks. 27/27, single-nil unmoved |
| ✅ | ~~One nil on each side: the oracle~~ | patch 67 | **the specification turns out to describe two different games, and only one of them is an ordinary one.** Roles: a bid on each side, and the role on a bidder's PARTNER says what that side does when it cannot both save its own and set the other's -- `2` saves ours first, `3` sets theirs first. Ranking the four outcomes 3..0 per side and adding the two ranks is the test for strict opposition, and it is a property of the ROLES alone: **`0 3 2 0` sums to a constant 3, so it is strictly opposed and ordinary minimax applies. `0 2 2 0` sums 4/3/3/2 and `0 3 3 0` sums 2/3/3/4, so they are NOT** -- both sides would rather have both bids live (or both dead) than trade, which is a shared interest no single scalar can express. That is precisely the situation the Sturtevant and Korf paper in this repo is about, and it means one third of this feature is a two-team search and two thirds is not. The oracle sidesteps it by being exhaustive: `_search_opposing` carries a utility PAIR and each side maximises its own component, which is backward induction and needs no pruning to be correct. Broken-nil mask in the state and the memo key, as with twin nil. Selftests pin the ranking, the strict-opposition property per shape, weight separation, memo agreement on all three shapes, and that flipping a partner's role never moves that side to an outcome its own ranking calls worse. **What this means for the C++**: `0 3 2 0` can reuse everything -- one scalar, alpha-beta, the table, the re-derived reach bound. `0 2 2 0` and `0 3 3 0` cannot, and by Korf's condition (maxsum 4 < 2*maxp 6) admit SHALLOW pruning only. Do the opposed shape first; it is the cheap two thirds of the value |
| ✅ | ~~The reach bound, re-derived for two nils~~ | patch 66 | **1.22x in nodes and 1.22x on the clock, which is the whole of item 62 in one bound.** Patch 64 measured the arms separately and found `target_bounds` worth 1.21x while everything else sat at or below 1.01x, so this is the only piece that was worth building. **The single-nil derivation does not port and the method does.** Its primary weights a trick COUNT, giving `value = per_nil * n + per_partner * p` over a simplex; the two-nil primary counts BIDS, which is a step function of who wins what. But the reachable region is still a convex polygon and the value is still linear over it, so the extremes are still at the vertices. Only the polygon changes: `0 <= d <= D` for D = min(bids still standing, tricks left), and **`d <= u <= t`, because a bid dies exactly when its own seat wins a trick and no two bids can die on the same one**. That is a trapezoid, not a simplex -- four vertex evaluations instead of three, still reading no cards. The `u >= d` edge is what makes it worth having: without it the bound would have to allow d bids down AND the pair taking nothing for them, which no line can do. **Measured**: 1.164x / 1.295x / 1.241x / 1.223x in nodes at 4/5/6/13 cards, and throughput falls only 1.2% (9.61M to 9.49M nodes/sec) so the node win survives to the clock -- four paired interleaved reps on one binary at 13 cards gave 1.32x, 1.16x, 1.16x, 1.23x, every rep a win. **Worst-case 13-card twin nil is now 269 ms, down from 748.** `later_tricks` stays off: its three cases are constraints on *the nil bidder* and *the cover partner*, and there is no cover. Correctness: all 152 corpus rows with `--check-pv`, 192 audit positions across four rotations and trick depths 0-3, 26/26, single-nil unmoved |
| ✅ | ~~Twin-nil corpus covering every state, and a latent oracle bug~~ | patch 65 | **the corpus tested one shape out of four and never a mid-trick position.** Patches 61 and 63 added two whole shapes with unit-test coverage only, and every row in `multinil.txt` was `0 3 0 3` from a clean lead. Rows now cycle through all four states -- `0 3 0 3` both live, `1 3 0 3` and `0 3 1 3` one down on either partner so a seat-parity slip shows, `3 1 3 1` both down -- and through trick depths 0 to 3, because three nodes in four are mid-trick and a hand asks *what do I play NOW* far more often than it asks from a clean lead. 152 rows: 76 mid-trick, four shapes, **nothing unanswered**. The six 13-card rows were `?` and pure timing fodder; the solver now answers them in tens of milliseconds, so they are **pinned from the solver** -- a weaker claim than an oracle row, saying the answer has not CHANGED rather than that it is right, but that is the difference between a regression anchor and a stopwatch. **The regeneration found a real bug**: patch 63 removed the oracle's refusal of `3 1 3 1` but left `minimizing_parity=nil_seats[0] % 2` reading the LIVE seats, which is empty when both bids are down -- an IndexError on a shape that patch declared supported. The C++ took its parity from the whole pair and was fine; the oracle was simply never asked, because nothing generated a row for it. Coalitions come from the pair, live or not: a busted bidder still plays for its side's trick total. **Verified across all four rotations, trick depths 0-3 and both tie-break directions: 192 positions against the oracle, no mismatches.** 26/26, single-nil unmoved. Corpus stays `max`-only by standing instruction |
| 📋 | Two measurements, no code | patch 64 | **(a) `3 0 3 1` is NOT interchangeable with `3 0 3 2`, and the reason is not the busted seat.** A busted bidder does behave exactly as a cover: it takes tricks freely, they count for the pair, it shields the live bid. What differs is the LIVE nil's primary granularity. Single-nil weights the bidder's trick COUNT, so a doomed nil has tricks piled onto it; twin-nil counts BIDS DOWN, so once the bid is gone the opponents are indifferent to further tricks on that seat. Measured over 300 deals: **the verdict agrees 150/150 in both tie-break directions**, and the trick totals agree **58/58 whenever the live nil MAKES** -- but only 39/92 (max) and 87/92 (min) when it FAILS. Substituting one for the other is safe for the verdict always, and safe for the counts only while the bid is alive. **(b) Item 62, broken down per arm** at 13 cards on `large.txt`, single nil, each disabled alone: `target_bounds` **1.21x**, `later_tricks` 1.13x (which rides on it), `static_bounds` **1.01x**, `tt_narrow` 1.01x, `presolve` **1.00x**. Combined 1.31x. **The cheap port is worth 1% and the valuable one is the hard one**: `bounds.hpp`'s proofs, which patch 58 called the hard part, buy almost nothing at 13 cards, while the reach bound worth 1.21x is precisely the one whose triangle arithmetic the step-function primary invalidates. Anyone reviving item 62 should start at `target_bounds` and skip the rest |
| ✅ | ~~Both twin bids already down~~ | patch 63 | **it was refused, and refusing it was an inconsistency rather than a safeguard.** `--seats 3 1 3 1`: both bids broken, hand still being played, tricks still worth points. Patch 61 turned it away as *there is no nil left to play for* -- but a SINGLE nil already set has degenerated to exactly this since the beginning, so the twin path was refusing what the single path had always answered. **It degenerates rather than failing**: with no live bid there is no primary level, and what remains is the secondary alone -- each pair taking or shedding as many tricks as it can, which is an ordinary double-dummy question. `objective_weights` now returns primary 0 for the shape, mirroring what an already-set single nil returns, so the weights say so rather than merely behaving that way against an empty live mask. **Verified by equivalence**: a dead bidder and a cover partner both play freely, so `3 1 3 1` and `3 1 3 2` are the same game for that pair -- **240 deals, both tie-break directions, agreeing on the pair's tricks AND the opponents' every time**, and the max/min split lands on the identical 20/21/19 distribution. 26/26, single-nil unmoved. **Worth knowing about `--secondary min`**: it does NOT simply mean the side takes fewer. Under `min` the OPPONENTS are trying to force tricks onto that side, and forcing is often easier than taking, because a hand must follow suit and can be endplayed. Measured across 60 deals the side ends up with MORE tricks under `min` than under `max` about as often as fewer |
| ✅ | ~~Mid-hand re-solve consistency, tested~~ | patch 62 | **the property a table actually depends on, and nothing was checking it.** Solve a two-bid deal, play along its own PV until one bid breaks, hand the REMAINING position back with that seat marked `ROLE_NIL_SET`, and require the same verdict on the surviving bid and the same pair trick total over what is left. **It holds, on 183 deals at 4, 5, 6, 10 and 13 cards, with no mismatches.** The reason is structural rather than lucky: the primary weight is charged on a bidder's FIRST trick and never again, so from the moment a bid is down the rest of the subtree is already being scored on the surviving bid and the pair's total -- which is precisely what `ROLE_NIL_SET` asks for. The weights differ between the two calls, `K*K` with K = tricks remaining + 1, so 196 against 121 at 13 and 10 cards; but `K*t < K*K` at every size, so only the SCALE moves and the lexicographic order is identical. `tools/subgame_consistency.py`, wired into ctest at 5 cards. **What it does not claim** is that the same CARD is chosen among equally-good ones: ties break to the canonically lowest in both calls, but a windowed search can reach a node under a different window, which is why the corpus header has always called the PV informational |
| ✅ | ~~A live twin nil beside one already broken~~ | patch 61 | **the commonest twin-nil state in real play, and it was refused.** `--seats 1 3 0 3`: partner's bid died at trick three, yours is still standing, re-solve from here. Patch 58 turned it away as unimplemented; it is not a shape, it is a FACT about the deal. **The rule is that only LIVE bids carry primary weight** -- a bid cannot go down twice -- so `ctx.nil_mask` narrows from every bidder to every live bidder, and the dead seat keeps only its half of the secondary level. That makes it play exactly as a cover partner does: freely, because there is nothing left to protect. The search finds the right line unprompted, funnelling every unavoidable trick through the seat already lost to keep the live bid standing. **Reporting unified rather than special-cased**: `Tally` splits into `live_nils_broken` (what the search charges, what a re-packing check compares against) and `nils_set` (that plus whatever the caller declared down, which is what gets reported). `out.nils_set = tally.nils_set` is now one expression for every shape, replacing a `nil_already_set() ? 1 :` special case in two places. **Verified**: 120 random deals across both rotations against the oracle, zero mismatches; 25/25; single-nil unmoved. Both bids down is still refused -- there is then no primary level at all |
| ✅ | ~~Test and benchmark coverage for two nils~~ | patch 60 | **the shape had shipped twice with no test of its own, and adding one found a bug in the first hour.** Nothing in ctest, nothing in `tests/`, nothing in `scripts/` touched the partner-nils path -- `multinil.txt` was deliberately unwired in patch 55 because the solver could not read it, and nobody wired it back after patch 58 could. **Three ctest entries**: `corpus_multinil` (all 150 oracle rows plus the six 13-card deals, `--check-pv`, 1.5s), `corpus_multinil_quick` for the inner loop, and `corpus_multinil_no_tt` -- the control arm for patch 59's key, because a missing mask bit does not crash, it returns a value from a line where different bids were already down. **A unit-test block** covering shape acceptance and the four refusals next door, weight separation at 2-13 tricks, table-on against table-off on three deals, fast mode refused, and the CONCENTRATION property that distinguishes the two levels: two unavoidable tricks funnelled through one seat kill one bid, split kill two, and the trick COUNT is identical either way so only the primary tells them apart. **A third benchmark leg** in `run-bench.sh` and `.cmd`, separate rather than averaged in, because a change that helps one nil can do nothing for two. **The bug**: `solve_moves` had its own per-move consistency check still re-packing with the single-nil formula. Patch 58 forked the one in `solve()` and missed this one, so it had been reporting a number nothing computed ever since -- silently, because a verifier that is not forked alongside the thing it verifies does not fail loudly, it stops verifying. Also fixed: fast mode with two bidders returned `NIL_ERR_INTERNAL` across the ABI instead of `NIL_ERR_UNSUPPORTED`, losing exactly the typo-versus-feature distinction that code exists for. 25/25, single-nil unmoved |
| ✅ | ~~The transposition table for two nils~~ | patch 59 | **the shape is usable at thirteen cards, and the table is why.** Two changes, both small. The key carries the broken-nil mask as four raw seat bits, and the shape gets its own `TAG_MULTI_NIL` -- the key says which POSITION an entry is about and the tag says which QUESTION, so a two-nil value can never be read by a one-nil search at the same cards. **The packing is OPTIONAL and that is the whole trick**: under one bid the objective is additive, the mask is not part of the position, and packing it would shift every field after it and change which positions fit -- moving node counts a dozen measurements are banked against. Four bits spent only when carried means the single-nil key is bit-identical, and 39,701 / 278,059 / 49,084 confirm it. **A/B on the same 150 rows**, table off against on: **2.01x / 3.32x / 5.42x** fewer nodes at 4/5/6 cards, growing with card count exactly as the oracle's distinct-entry count predicted it would matter. All 150 still match the oracle with the table on, `--check-pv` included. **FIRST THIRTEEN-CARD NUMBERS FOR THE SHAPE**: all six `m13-` deals solve, 13.3M nodes total, **worst case 748 ms** and median around 100 ms, at 10.4M nodes/sec. That is with every bound still gated off, so it is a ceiling on how bad item 62 can be, not a floor |
| ✅ | ~~Two nils on one side: the C++ solver~~ | patch 58 | **correct first, fast never -- and the measurement says the shape was never the problem.** `validate_seat_roles` grows a `seat_shape()` mirroring the oracle's `role_shape()`; `State` carries a broken-nil mask; `score_trick()` forks the trick accounting so the primary weight is charged once per bidder on its FIRST trick rather than on every trick it takes; `objective_weights` forks to `K*K * (bids down)`. **All 150 oracle rows match**, and single-nil is unmoved at 39,701 / 278,059 / 49,084 with 22/22. **The shape is nearly free and the gating is not**: same deals, both questions, all machinery off on both sides, two nils costs 1.05x / 1.03x / 1.09x at 4/5/6 cards. On the same deals the machinery the shape gives up is worth **3.0x / 4.9x / 7.6x** -- and it GROWS with card count, which is the number that matters for 13. **The internal consistency check caught the one real bug**: it re-packed the value with the single-nil formula and reported 60 against the search's correct 35, on a position where the search had already agreed with the oracle. A check that is not forked alongside the thing it checks silently stops checking. **MODE_FAST refuses the shape** rather than answering it |
| ✅ | ~~One corpus schema for both shapes, and a proposal withdrawn~~ | patch 57 | **the format I proposed was wrong, and measuring it said so.** The plan was four PER-SEAT trick counts as the universal answer column, on the reasoning that `nil_tricks` names "the nil bidder" and that has two answers when a pair both bid. **An answer column may only hold what the OBJECTIVE pins**, and per-seat counts are not pinned: nothing in either objective constrains how the opponents split tricks between themselves, or how a pair that has already lost both bids splits its own. Re-searching all 140 solved two-nil rows preferring the LAST equally-good card instead of the first -- which is exactly what a move-ordering change does -- **moved the value on 0 rows and moved the per-seat counts on 43**. Recording them would have made the corpus fail the first time move ordering changed, which is the thing its header has always warned about for the PV. **What shipped instead is one new column**, `nils_set`, and a uniform reading of the two that were already there: `nil_tricks` is now *tricks taken by seats holding a nil bid* -- the bidder's own count with one bidder, pinned by the primary; the pair's total with two, pinned by the secondary, where it equals `side_tricks` because there is no cover to add. One definition, both shapes, always determined. All three corpora now share a schema, so `corpus_view.py` and the C++ loader read `multinil.txt` without a special case, and `nil_bench` checks the recorded count rather than inferring a boolean from a trick count. **Unmoved**: 39,701 fast / 278,059 full / 49,084, all 560 values matching, 22/22. `make_multinil_corpus.py` regenerates the converted rows byte-for-byte |
| ✅ | ~~`nil_fails` widened from a flag to a count~~ | patch 56 | **a boolean with two answers, asked of a question that has three.** `nil_fails` was 0 or 1: was THE nil broken. A pair that both bid has three answers, so the field is now `nils_set`, an integer count of how many bids are down. **A widening, not a break**: with a single nil the count is 0 or 1, numerically what the flag held, so `if (nils_set)` reads exactly as `if (nil_fails)` did and no caller's logic inverts. A bid the caller declared broken with `ROLE_NIL_SET` counts toward it, since the question is how many are down rather than how many the search knocked down. **The single-nil search is untouched and the numbers say so** -- 39,701 fast / 278,059 full / 49,084 on `large.txt` all unmoved, 22/22, `--mode both` agrees on every one of the 560. The change is at the REPORTING boundary only: `MODE_FAST` still searches the nil bidder's trick count over a `[0, 1]` window and the count is read off at the end, because moving the fast-mode value to a 0/1/2 scale would move the fail-soft returns and orphan every banked count. Renamed through `Solution`, `MoveScore`, `nil_result`, `nil_move`, both CLIs' compact output, `nil_oracle.py` (where single-nil `Solution` grows a `nils_set` property so both result types answer in the same units) and the Python tools. The ABI entry point `nil_fails()` becomes `nil_count_set()`, since a function named for a predicate that returns a count is a trap. C# got the rename ONLY -- a renamed P/Invoke target fails at runtime rather than at compile time, so leaving it would have been a landmine; the real C# work is still pending |
| ✅ | ~~Two nils on one side, in the oracle~~ | patch 55 | **the ground truth goes first, and the measurement is half the deliverable.** `nil_oracle.py` answers `--seats 0 3 0 3`: one pair bid two nils, the other pair is trying to set both. Objective is lexicographic as specified -- **primary** how many of the two nils are set (opponents maximize, the pair minimizes), **secondary** the pair's own trick count under `--secondary`. **This could not reuse the existing search, and the reason is structural.** The single-nil objective is ADDITIVE: every completed trick contributes a fixed amount, so `_search` accumulates as it unwinds and needs no memory. *How many nils got set* is a step function -- a nil bidder's FIRST trick costs the whole primary level and every later one costs nothing -- so the value of a subtree depends on which nils were already broken on the way in. `_search_multi` carries that as a two-bit mask in its state and in its memo key, the same way `spades_broken` already travels. **The two levels are coupled in a way the single-nil pair is not**: every trick the pair takes is taken BY a bidder, so having lost one bid the pair funnels everything through the seat already broken and keeps the other alive -- which is what a spades player does, and what a naive *minimize our own tricks* objective gets wrong. Pinned by selftest. **Coalitions here come from the roles, not from parity** -- new code, no ground truth to preserve, so it does the correct thing; the two agree whenever the nils sit N/S. `replay_pv` is now a reading of a new role-agnostic `replay_pv_by_seat`, so the two verifiers cannot disagree; the 560-row corpus and `crosscheck.py` confirm nothing moved. **New corpus** `tests/corpus/multinil.txt`: 150 oracle-computed rows at 4-6 cards plus 6 constructed 13-card deals with `?` answers for local timing, all 150 re-verified independently by PV replay. **Not wired into ctest** -- the C++ solver still refuses the shape, by design. **Three measurements, and the first one is a negative result**: see below |
| ✅ | ~~A role per seat, replacing the nil seat and the already-set flag~~ | patch 54 | **a representation change, and deliberately nothing else.** The objective was described by two scalars -- which seat bid nil, and whether that nil was already broken -- and two scalars can only ever describe ONE nil. Real spades puts two on the table often enough that it matters, and the optimal line changes when it happens, because a seat that is both defending its own nil and attacking another's has an objective neither scalar can express. Replaced by `SeatRoles`: one role per seat, `0` a nil bidder with no trick yet, `1` a nil already broken, `2` the partner covering it, `3` a seat on a side with no nil bid. **The search is untouched.** `configure()` derives `ctx.nil_seat` from the array and everything below it is byte-identical, which is the point: every measurement this file banks stays valid. **Absolute inside, anchored on the wire.** `SeatRoles::role` is indexed by absolute seat, the same as `hands[]` and `leader`, so nothing in the solver thinks about rotation; the TEXT form runs clockwise from the seat the PBN names, exactly as that string's hands do, and the anchor is an argument to `parse_seat_roles` rather than an assumption. `W:...` with `--seats 0 3 2 3` puts the nil on West. **What is refused, and how.** `validate_seat_roles` accepts exactly the arrangements the two scalars could describe and separates the two ways of failing: a malformed array is `NIL_ERR_ILLEGAL_POSITION`, a well-formed one holding two nils is `NIL_ERR_UNSUPPORTED` and says *multiple nils are not supported yet*, so a caller can tell a typo from a feature that has not landed. **Changed everywhere at once**: `solve`, `solve_moves`, `replay_pv`, `objective_weights`, `SearchOptions::nil_already_set` (gone), `Solution::nil_seat` (now `roles` with an accessor), the C ABI's `nil_seat` argument (now `const int32_t* seats`), `NIL_FLAG_NIL_ALREADY_SET` (deleted, bit `0x20u` **burned not recycled**, as `0x2u` was in patch 45), the corpus's `nil` and `nilset` columns (one `seats` column), both CLIs, the C# bindings, `nil_oracle.py`'s argument surface and all seven Python tools. **Verified answer-neutral three ways**: 39,701 fast / 278,059 full / 49,084 on `large.txt` all unmoved with 560 oracle values matching, 22/22 tests, and a differential harness running **144 random deals** at 2-7 cards across both modes, both tie-break directions, live and already-set nils and all four seats through the OLD binary on the old command line and the new one on the new -- **byte-identical output on every deal**. The corpus was rewritten mechanically rather than re-solved, because the two old columns determine the roles exactly. **Two bugs found on the way**, both recorded below |
| ✅ | ~~Both corpus generators have been crashing since patch 45~~ | patch 53 | **neither corpus could be regenerated, and nobody noticed because neither needed to be.** Patch 45 dropped the `forced` column and removed its ARGUMENT from both row writers while leaving the format strings at their old width. Every argument after `broken` shifted left by one, `trick_text` landed on the `%d` that used to take `forced`, and both tools died with `TypeError: %d format: a real number is required, not str`. `make_corpus.py` had 13 placeholders for 12 arguments; `make_large_corpus.py` had 13 for 12. Fixed to 11 and 12 columns respectively, matching the headers each already writes, with `nil_tricks` and `side_tricks` as `%s` in the large writer because a timed-only row records `?` for both. **The stale header in `tests/corpus/positions.txt` was the same bug's shadow**: it still named a `forced` column its rows have not carried since patch 45, precisely because the tool that would have rewritten it could not run. **Item 45's generator half is fixed here too, because the crash was hiding it.** With the writers working, `make_large_corpus.py` immediately emitted a 10-card row holding all thirteen spades with `broken=1`, which `validate()` refuses — the same coin-flip bug patch 47 fixed in `nil_bench`. Both `make_large_corpus.py` and `crosscheck.py` now draw the coin and mask it, which leaves the deal stream in place and only ever clears a flag that could not have been set; the partial trick may still set it afterwards, correctly, since a spade played to the current trick has left a hand. Verified end to end: both tools regenerate, and the output replays through `nil_bench` under `--check-pv` and `--check-moves` — `--verify` (oracle), `--pin` (solver), `--timed` and `--hardest` paths all exercised. 22/22, and the committed corpora are untouched (39,701 / 278,059 / 163,393,676) |
| ✅ | ~~Every trace of the card gate, and the trick-spade over-rejection~~ | patch 52 | **the solver now declines a position for one reason only: it is illegal.** Patch 51 removed the gate but kept its ABI vestiges; nothing links against this yet, so they go too. **Deleted**: `NIL_FLAG_FORCE_LARGE` (bit `0x8u` is now free), `NIL_CARD_LIMIT`, `NIL_ERR_TOO_MANY_CARDS`, `--force` from `nil_cli`, and the `--force` argument from `corpus_view.py`, `invariants.py` and `make_large_corpus.py`. **The error codes are renumbered**: `-4` was `NIL_ERR_TOO_MANY_CARDS`, so `NIL_ERR_BUFFER_TOO_SMALL` is now `-4`, `NIL_ERR_INTERNAL` `-5` and `NIL_ERR_UNSUPPORTED` `-6`. **There was never a fast-mode gate below the API layer**, and `nil_already_set` has no size restriction either — both already worked at thirteen and were verified so. **Also fixed: `validate()` counted spades on the CURRENT TRICK toward the thirteen-still-in-play test**, which rejected a reachable position — lead a diamond, have it ruffed, and twelve spades sit in hands with the thirteenth face up, spades broken by that ruff and the count still reading thirteen. A spade on the trick has left a hand, so it no longer counts. Found by generating 3,000 twelve-card layouts with a partial trick: **9 hit it, and all 9 are accepted now.** The change is purely permissive — it only ever lowers the count — so no previously-legal position became illegal, and no banked count moved (39,701 fast, 278,059 full, 163,393,676 on `large.txt`, 22/22, 560 values under `--check-pv` and `--check-moves`). `nil_oracle.py` keeps its own seven-card `--force`: that really is an exhaustive search with no pruning, and its guard is not dead code |
| ✅ | ~~The nine-card gate on full mode~~ | patch 51 | **a guard rail that outlived what it guarded against.** `nil_cli` and `nil_solve` both refused more than **nine** cards per hand unless passed `--force` / `NIL_FLAG_FORCE_LARGE`, on the stated grounds that *"the full search is exhaustive (the transposition table collapses repeated positions but prunes nothing) and will take a very long time"*. That sentence stopped being true at patch 22 and has been getting less true every patch since — target bounds, later tricks, the presolve, the static bounds and quick tricks all cut. Measured over 24 random 13-card full-mode solves: **median 4.15 s, p90 19.6 s, max 22.7 s, none over 30 s**. A real hand that prompted this — `N:Q532..A854.K8643 6.KT86.973.JT752 AKJT9.AQJ3.T2.Q9 874.97542.KQJ6.A`, leader E, nil S — is 71,135,608 nodes in **7.2 s**, and `--moves` on it, which scores every legal card, is 9.6 s rather than thirteen times that, because the table is shared across the per-card searches. The gate is removed from both interfaces. **Nothing is deleted from the ABI**: `NIL_FLAG_FORCE_LARGE` is accepted and ignored (retained so the bit is not reassigned under an old caller), `NIL_ERR_TOO_MANY_CARDS` keeps its value so the numbering below it does not shift, and `NIL_CARD_LIMIT` becomes 13 and is redocumented as what a hand can hold rather than what will be attempted — an oversized hand is still refused, by `validate()`, as `NIL_ERR_ILLEGAL_POSITION`. `--force` stays a valid CLI argument because `corpus_view.py`, `invariants.py` and `make_large_corpus.py` all pass it. No search behaviour changed and no banked count moved |
| ⊘ | Section 3's ceiling: the cover partner's can-cash count (item 43b) | patch 50 | **measured and rejected; the guard works and the population does not.** Patch 49 left this half out on a soundness hazard -- the claim `p >= b` is about a NAMED HAND, and a nil bidder holding nothing but spades is forced to ruff its own partner's winner. Both halves of that are now settled. **The hazard is real**: a counterexample hunter computing the game value where the nil side maximises the cover's tricks and the opponents minimise finds the unguarded count over-claiming **8-12 times per 400 deals** at three cards. **The guard closes it** -- cap b at the nil bidder's cards in the suit plus its cards that are neither spades nor of that suit -- with **zero counterexamples in ~2,700 deals** at three, four and five cards, costing only 0.3-8.3% of the cuts. **And it still fails on population**: it opens its gate on 14.50% of boundaries to cut 1.27%, an 8.8% hit rate, against the shipped half's 7.21% and 2.02% for 28% -- half the yield at twice the cost, and the shipped half was already a wall-time wash. **The asymmetry is structural**: item 43's bound is capped at ZERO, so `beta > 0` refutes it in one comparison; this one is capped at `per_partner * t` (−182 at thirteen cards), which alpha clears virtually always, so there is no cheap precondition to be had. Measured: −0.79% nodes on the corpus, −0.04% on `large.txt`, −0.12% at 13c seed 3 and **+0.27% at 13c seed 42** -- it makes that seed BIGGER, deterministically, most likely by item 41's mechanism of trading exact table entries for one-sided bounds. Wall +2.9% and +3.4%. Correctness was never in question: all 560 corpus values and all 19 `large.txt` rows match under `--check-pv` with the arm on |
| ✅ | ~~QuickTricks: the opponents' can-cash floor~~ | patch 49 | DDS §3, spent in the one direction that is sound here. A can-cash count is a claim about one STRATEGY, so it bounds a node only from the side that owns it -- the opponents maximise, so theirs says the nil side splits at most `t - c` and the value cannot fall below the worst corner of that. Spent against beta, at a node where they are on lead, and nowhere else. **−0.14% to −1.16% nodes across the three 13-card seeds, −0.66% on the corpus, −0.46% on `large.txt`**, `MODE_FAST` byte-identical. **What made it shippable was one line of ordering**: the bound is strongest at `c = t`, which leaves the nil side nothing to split and floors the value at zero, so `beta > 0` refutes it in a SINGLE COMPARISON before a popcount is spent. Putting that ahead of the four-popcount longest-suit test flipped 12 cards from slower in 4 of 5 interleaved reps to faster in 3 of 5, with the node count provably unmoved (278,059 either way). Its gate then opens on **7.14% of boundaries against item 44's 71%**, and that ratio is the whole reason one ships and the other does not. **Wall time is much weaker than nodes and this entry says so**: interleaved medians, one binary, arms toggled at runtime -- 12c 484.3 -> 477.5 ms (3 of 5 faster), 13c seed 42 3598.1 -> 3584.7 (3 of 5), 13c seed 3 1055.5 -> 1074.6 (2 of 5), 11c 1280.4 -> 1293.5 (2 of 5). A wash, slightly positive. The cover partner's mirror image is NOT taken and bounds.hpp says why. 400 random deals x 3 arms in both tie-break directions agree on value, split and PV while 299 differ in node count; all 560 corpus values under `--check-pv` and `--check-moves`, all 19 `large.txt` rows |
| ⏸ | The forced-trump floor for all four hands (item 44) | patch 49 | **built, gated, measured, SHIPPED OFF.** Every hand's forced floor in one walk, combining into `n >= kn`, `p >= kp`, `n + p <= t - ko` -- three constraints where `top_spade_run` gives one. Sound: an exhaustive-playout property test over 18,800 checks at 3, 4 and 5 cards finds no counterexample. **−1.28% nodes at 13 cards and −8.3% of throughput, so net slower.** A gate on three masked popcounts -- floors cannot exceed spade counts, and the triangle only shrinks as floors grow -- **bought essentially nothing (8.4% -> 8.3%), because it opens on 71% of boundaries**. Carried behind `--spade-matrix` / `NIL_FLAG_SPADE_MATRIX` rather than deleted: the node saving is real and it is **nearly disjoint from item 43**, which the corpus shows almost exactly additively -- 6,299 nodes saved alone plus 1,836 alone against 8,127 saved together. Both arms on is −2.44% at 13c seed 3. What it needs is a tighter gate or a cheaper walk |
| ⊘ | QuickTricks, DDS §3 (item 43) | patch 48 | **measured, not built, and the measurement is the deliverable.** §3 counts what the side on lead CAN CASH, which bounds a node only from the side holding the option -- an upper bound when the minimising nil side holds it, a lower bound when the maximising opponents do. Both were evaluated against each node's own window at the boundaries the reach bound leaves open, and neither was applied. **The result that matters is that §3 and §4 are nearly DISJOINT**: the can-cash count cuts 1.57-4.07% of still-open boundaries and **97-98% of those cuts are at boundaries where the forced floor does not cut**, so the two add rather than overlap -- 2.90% to 8.11% together against 1.34-4.12% for the forced floor alone. Against item 44's calibration (4.12% would-cut bought −1.28% of nodes) that projects to roughly −2.5% at best, and the per-suit walk is dearer than the trump-only one it would sit beside. **A cheap necessary condition halves it and no more**: per_partner is negative, so the opponents' bound is at most zero and a positive beta refutes it in one comparison -- 48.90% of boundaries still let the walk run. Shipped: `forced_spade_tricks()` and `cashable_tricks()` in `bounds.hpp`, and `--quick-tricks-stats`, **free when off** and verified so (39,701 fast, 279,895 full, 164,156,179 on `large.txt`, 22/22). A soundness gap found on the way is recorded in the item and would have to be closed before any of it is spent |
| ✅ | ~~The random benchmark generator draws an impossible spades-broken flag~~ | patch 47 | **a measurement bug, and it has been eating the 13-card leg since patch 45.** `random_position()` set `pos.spades_broken` on a coin flip; patch 45's `validate()` rejects that flag while all thirteen spades are in play, which a 13-card deal ALWAYS is. The failing half never ran, and the summary line went on dividing by the requested count -- so `--cards 13 --count 8` reported **a third to a half of the work it claimed**: seed 3 869,633 against a real 1,996,445 (2.30x), seed 11 7,919,000 against 23,858,179 (3.01x), seed 42 4,759,514 against 10,433,275 (2.19x), fast mode. **12 cards loses 1-3 deals in 8 and 11 cards loses one on seed 42**, because 44 dealt cards can hold every spade. The coin is drawn either way and then masked, so the DEAL STREAM DOES NOT MOVE and every previously-passing deal reproduces byte-identical -- verified at 6, 9 and 11 cards on two seeds, and the corpus (39,701 fast / 279,895 full) and `large.txt` (164,156,179) are untouched because neither uses random deals. **Patch 45's verdict survives the repair**: rebuilt at `410a30c` and re-run on a full 8-deal sample, the three 13-card seeds go 62,013,835 -> 36,287,899, **−41.5% against the −44% that entry banked**. What does not survive is every PER-SEED 13-card figure taken since patch 45; those are on a truncated sample and must not seed the next A/B |
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

**Forced covers for the nil bidder's shed (move ordering item N1).** Built,
property-tested, measured on nodes and on the clock, rejected. The first
ordering item on this list to fail on NODES rather than on throughput, which is
why it is written up at length.

*What it claimed.* 6a promotes "the highest card that loses as the trick
stands", and deliberately never considers a card ABOVE the card currently
winning, on the grounds recorded in its own comment: such a card loses only if a
later seat covers it, and a later OPPONENT declines to cover, because letting the
nil bidder win is the whole of what the opponent wants. N1's answer is that the
objection is about a VOLUNTARY cover. A seat that must follow suit and holds
nothing in the led suit low enough to duck under has no choice in the matter.

*The condition, stated exactly.* A card `c` above the current best is safe iff
every seat yet to play holds at least one card of the led suit and all of that
seat's led-suit cards beat `c`. Equivalently: `c` is below the lowest led-suit
card any remaining seat holds. Two ways to decline, and both matter more than
they look. A VOID later seat may discard, and a discard beats nothing — it may
also hold a trump, but holding one is not playing one, and an opponent given the
choice takes the discard. And a nil bidder playing FOURTH has no seat behind it
at all, where "every remaining seat covers" is vacuously true and a card above
the best simply wins the trick.

*The condition is exactly true, and that was tested before it was measured.*
Brute force over 400,000 random positions, enumerating every legal continuation
by the seats still to play: **8,082 cards promoted above the trick's best card,
zero able to win the trick under any line.** The objection is overturned. What
follows is not about soundness.

*The population, measured before implementing, on the shipped tree.* Counters
only — every node count came back equal to its banked baseline, which is what
says the tree measured is the real one.

| workload | nodes | 6a nodes | N1 promotes differently | of 6a nodes | of all nodes |
|---|---:|---:|---:|---:|---:|
| 13c, 8 deals, seed 3 | 1,996,445 | 355,244 | 5,296 | 1.49% | **0.265%** |
| 13c, 8 deals, seed 11 | 23,858,179 | 4,097,819 | 79,550 | 1.94% | **0.333%** |
| 13c, 8 deals, seed 42 | 10,433,275 | 1,873,054 | 33,056 | 1.77% | **0.317%** |
| 11c, seed 3 | 14,104,592 | 2,226,611 | 63,804 | 2.87% | 0.452% |
| corpus, 560, fast | 39,701 | 6,031 | 256 | 4.25% | 0.645% |

**Why it is small is the reusable part.** Of the 4,097,819 nodes where 6a runs at
seed 11: **48.1% are the nil bidder playing fourth**, 18.0% have a void seat
still to play, 8.0% already have a ruff on the trick where 6a promotes
everything anyway, and 25.9% open the gate. **Of the ones that open the gate,
only 7.5% change card** — the threshold must clear the card currently winning,
and the nil bidder must hold something between the two. Anyone pricing a
following-suit heuristic for any seat should start from that split: half of
these nodes are fourth hand.

*Nodes, one binary, `--no-forced-covers` the only difference.* 13 cards first:

| seed | off | on | change |
|---|---:|---:|---:|
| 3 | 1,996,445 | 1,981,863 | −0.73% |
| 11 | 23,858,179 | 24,706,139 | **+3.55%** |
| 42 | 10,433,275 | 10,189,299 | −2.34% |
| **total** | **36,287,899** | **36,877,301** | **+1.62%** |

11 cards seed 3 is +5.23%, seed 42 +0.10%; 12 cards +0.55% and −0.04%; the
corpus −0.08% and `large.txt` −0.06%.

**Every workload's entire movement is one deal**, and that is the finding rather
than a caveat. Seven of the eight deals at seed 11 move by less than 0.001%;
`r13-0004` goes 6,535,990 → 7,383,874 and is the whole +3.55%. `r11-0004` is all
of 11c seed 3, `r13-0002` all of 13c seed 42 (−9.7%), `r13-0004` all of 13c seed
3. Two deals up, two down. **On a population of 0.3% of nodes there is no signal
to tune**, only which deal happened to be biggest.

*Wall clock, four interleaved paired reps per workload, arm toggled at runtime.*

| workload | nodes | reps won | best ratio | throughput |
|---|---:|---:|---:|---:|
| 13c, seed 3 | −0.73% | 3/4 | 0.987 | +0.5% |
| 13c, seed 42 | −2.34% | 3/4 | 0.962 | +1.5% |
| 13c, seed 11 | +3.55% | 2/4 | **1.030** | +0.5% |
| 11c, seed 3 | +5.23% | **0/4** | **1.071** | −1.7% |
| 11c, seed 42 | +0.10% | 1/4 | 1.002 | −0.1% |
| corpus, fast | −0.08% | 3/4 | 0.982 | +1.8% |

11 cards seed 3 is not noise: four reps of four lost, ratios 1.086 / 1.072 /
1.071 / 1.070.

*Why it failed, mechanically.* **Throughput is flat**, which is the diagnostic.
The rule is gated on there being a card above the trick's best card at all —
one mask against a set already in hand — so the up-to-three-seat lookahead never
reaches a node it cannot help. Every previously rejected ordering item on this
list won on nodes and gave it back on throughput; this one paid almost nothing
per node and lost anyway. **The failure is in the heuristic, not in its cost**,
and a cheaper implementation of the same rule cannot rescue it.

**It is also what this file's own ceiling already predicted.** The node-population
sweep under item 29b found **87–91% of cutoffs already landing on the first move
tried**. A rule that changes the first move on 0.3% of nodes is drawing on what
is left of the other 9–13%, and at that size the tail behaviour of one deal is
larger than the effect being measured. That sweep struck items 9 and 10 from the
suggested sequence; N1 is the first measurement taken *after* it that confirms
the reading on a heuristic someone actually built.

*One refinement, measured and dropped with it.* A void later seat holding nothing
but trumps is also forced, since it must ruff. It adds 1–2% to the differing
population (79,550 → 80,474 at seed 11) and nothing else.

*A thing worth knowing for the next ordering item: the fixed points move, and
one of them moves for a non-obvious reason.* Had N1 shipped, the corpus fast
count would be 39,669 and `large.txt` 49,054 — expected, since ordering fires on
the corpus. **The full-mode count would move too, 278,059 → 277,914**, even
though full mode does not reorder: that total includes the `MODE_FAST` presolve
that bounds its root window, and the presolve orders. `--no-presolve` confirms
the movement is entirely there. For an ordering patch the durable node anchor is
the arm switched OFF, which must reproduce 39,701 / 278,059 / 49,084 exactly.

*What would revive it.* Nothing about the condition. A workload where the nil
bidder is materially less often fourth to the trick would raise the ceiling, and
neither the corpus nor the random generator produces one.

**Spending the FAILING branch of the presolve boolean (item 23b).** Item 23 runs
a `MODE_FAST` presolve and spends the answer only when the nil is SAFE, where it
closes beta onto `max_value_if_nil_safe`. The other branch looked like free
money and is not.

*The population is enormous.* At 13 cards on seed 3, **seven of eight deals are
nil-fails, and the fast presolve settles them in 1 to 4 nodes** -- the static
proof fires immediately. On all seven `root_beta` stays at `WINDOW_MAX`, so
**99.6% of the full-mode maximise workload (380M of 385.8M nodes) sits behind a
presolve that is silent by construction.**

*It buys nothing.* `nil_fails` means the bidder takes at least one trick, so the
value is above the whole nil-safe band and `root_alpha` can be raised to it.
Measured: **385,799,941 -> 385,944,937, +0.04%.** The reason is the asymmetry
between the two branches rather than anything about the population. On the safe
branch beta closes ONTO a threshold that sits just above the true value; on the
failing branch alpha lands at the BOTTOM of a range five hundred wide, and the
root's own first child raises it past there within one move.

*A trap found on the way, and it is the reusable part.* An earlier arm of this
measured **−22.5%** and was an artifact: `walk_pv` and `canonical_move_for`
inherit the root window, and an alpha derived from "the bidder takes a trick
somewhere in this deal" is FALSE about the residual position after it already
has. Two principal variations failed the replay check (`search says 465,
replaying the PV gives 479`). Beta happens to stay valid down the PV and alpha
does not, so item 23's shape is safe and its mirror image is not. **Anything
that narrows alpha at the root has to hand the PV walk the sentinels.**

*What would revive it.* Nothing about this branch. The bound is weak because it
is anchored at the wrong end, not because it is spent badly.

**The threshold ladder: pinning the primary exactly (item 23c).** The natural
next move after 23b -- climb `MODE_FAST`-shaped searches at targets 1, 2, 3...
until one answers no, and `nil_tricks` is pinned exactly. The packed value is
then confined to `t + 1` values instead of `(t + 1)(t + 2) / 2`, which is 14
instead of 105 at thirteen cards. Each rung costs on the order of a fast search,
which is ~1/190 of the full one.

**The ceiling was measured before anything was built, and it is −13.8%.** The
root window was pinned by hand to the band where `nil_tricks` equals its true
value -- the answer handed over free, no rungs paid for:

| deal | n | control | pinned | ratio |
|---|---:|---:|---:|---:|
| r13-0000 | 2 | 14,165,782 | 4,521,184 | **0.319x** |
| r13-0001 | 3 | 21,897,593 | 17,722,086 | 0.809x |
| r13-0002 | 3 | 8,013,509 | 6,233,489 | 0.778x |
| r13-0003 | 0 | 5,765,966 | 5,765,966 | 1.000x |
| r13-0004 | 1 | 4,300,783 | 4,362,314 | 1.014x |
| r13-0005 | 1 | 64,877,696 | 64,815,062 | **0.999x** |
| r13-0006 | 3 | 82,611,444 | 79,335,184 | 0.960x |
| r13-0007 | 2 | 184,167,168 | 149,973,234 | **0.814x** |
| total | | 385,799,941 | 332,728,519 | **0.862x** |

**The two deals that own the workload return 0.999x and 0.814x**, and together
they are 65% of it. A −13.8% ceiling that has to pay for its own rungs is not a
patch, and this is the MTD(f) floor measurement again: the mechanism does
exactly what it says and the headroom is not there. Same lesson, third time --
**get the ceiling before building the machine.**

*What would revive it.* A rung that costs materially less than a fast search --
the static proofs generalised to a target of k rather than 1 would settle rungs
2 and 3 without searching, the way `nil_must_take_a_trick` settles rung 1 in
one node. Even then the ceiling caps the whole item at 13.8%.

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

### 43. ~~QuickTricks~~ — ⭐⭐⭐⭐ → ⭐⭐⭐ — **done, patch 49; population measured in patch 48, and §3 turned out to be complementary to §4 rather than a replacement for it**

**The one primary cutoff in the paper this solver has never had.** §2 landed as
patch 31 (−22.4% at 13 cards), §4 as patch 39 (−16.7%), and §3 is absent from
the code and, until now, from this file.

*The population is already measured and it is the whole point.* `--nilset-stats`
on 13-card full-mode maximise: **the forced-trick proof fires on 3 of 51,231
eligible boundaries — 0.01%**, while the adversarial ceiling identifies 70.4% of
them as genuinely forced. `nil_must_take_a_trick` is a SINGLE-SUIT predicate --
the spade run -- and `top_spade_run` feeding patch 31's simplex is the same
card-set seen from another angle. **§3 counts sure tricks across all four suits,
with entries, which is what makes `n >= k` reachable for `k >= 2`.**

*The hook already exists.* The simplex vertices for "the nil bidder is forced to
take k" are written and commented in `search.cpp` at the `owner == ctx.nil_seat`
branch, alongside the cover-partner and opponent cases. What is missing is a
predicate that returns a k bigger than 1 more than three times in fifty thousand.

*The thing to be careful about, and it is not what §3 is careful about.* DDS
counts tricks the side on lead **can cash**, which is a lower bound on what a
maximiser can achieve and is exactly what a trick-maximising search wants. The
bounds here range over the whole simplex **without assuming anybody plays well**,
so what they can consume is a floor that holds down EVERY line, good and bad
alike. §3's cashing count is the wrong shape and adapting it is the work.
Read `top_spade_run`'s comment in `bounds.hpp` for the standard the predicate has
to meet, and item 44 below for the closed form that already meets it.

*Two smaller pieces of §3 in the same direction.* The **post-lead check** (Kuijf's
addition -- re-run the sure-trick test after the leading card is played, where
the winner is far more constrained), and the **opponents' sure tricks** as a cap
on `n + p`, which is the opponent case of item 44 generalised off trump.

---

## What patch 48 measured

`cashable_tricks()` in `bounds.hpp` is §3's count: per suit, the run of top
outstanding cards the hand holds, capped in a side suit by the shorter
opponent's length in it, because an opponent void in the suit and holding a
spade ruffs. It reports `best` (the largest single suit, unconditionally sound
-- cash one suit and stop) and `sum` (every suit added, optimistic, because
cashing one suit can force a void opponent to discard from another and shorten
the guard the next suit was counted against). The two bracket the truth instead
of pretending to be it.

**Where a can-cash count may be spent, and it is not where §4's is.** The reach
bound ranges over the simplex without assuming anybody plays well, so it
consumes floors that hold down every line. A can-cash count is a statement about
ONE strategy, so it bounds the node from one side only:

  * **opponents on lead** (they maximise): under a cashing strategy they take at
    least `c`, so `n + p <= t - c` for every reply, and
    `V >= per_partner * (t - c)`. Fires against beta.
  * **cover partner on lead** (the nil side minimises): under a cashing strategy
    `p >= b` for every reply, so `V <= per_nil * (t - b) + per_partner * b`.
    Fires against alpha.

The nil bidder's own cash count is worth nothing -- an upper bound is already at
`n = t`, which is the case it would be describing.

**The measurement**, taken at the boundaries the untightened simplex AND the
incumbent later-tricks tightening both left open, so every figure is a fraction
of what is still unanswered:

| workload | boundaries open | forced floor cuts | can-cash cuts (sound) | of which forced does NOT cut | either |
|---|---:|---:|---:|---:|---:|
| 13c x4, seed 3, full max | 16,769,137 | 4.12% | 4.07% | **3.99%** | **8.11%** |
| 13c x4, seed 42, full max | 55,135,192 | 1.34% | 1.57% | **1.55%** | **2.90%** |
| 11c x6, seed 3, full max | 26,697,207 | 1.41% | 3.47% | **3.45%** | **4.87%** |

**That fourth column is the finding.** 97-98% of the can-cash cuts land where the
forced floor does not, on all three workloads. §3 is not a better §4 and it is
not a subset of it -- the two mechanisms are nearly disjoint and roughly the same
size, so together they answer about twice what §4 answers alone. That is what
the paper's structure implies (they are separate cutoffs run in sequence) and it
had not been checked here.

**Why it still is not built.** Item 44 calibrates the exchange rate: a 4.12%
would-cut rate bought **−1.28% of nodes** at 13 cards, and cost 8.4% of
throughput. Doubling the fire rate projects to roughly −2.5%, against a
predicate that walks four suits instead of one trump suit. The cheap gate helps
and does not rescue it: per_partner is negative so the opponents' bound is at
most zero and a positive beta refutes it in a single comparison, but **48.90% of
boundaries still let the walk run** (13c seed 3). 4.07% of cuts out of 48.90% of
walks is an 8% hit rate on the expensive part.

*A soundness gap, and it has to be closed before any of this is spent.* The
cover-partner bound claims `p >= b` -- that the COVER takes those tricks. If the
nil bidder is void in the cashing suit and holds nothing but spades it is forced
to ruff its own partner's winner, and the trick moves from `p` to `n`. The claim
is about a named hand rather than about the side, so unlike the opponents' case
this does not survive the substitution. It is narrow but real, and the measured
4.07% is therefore an upper estimate of what a sound version would cut. Any
implementation needs the guard; the population above should be re-measured with
it in place before the numbers are trusted to two decimals.

*What would revive it, concretely.* A tighter gate than the one measured. The
cover bound needs `b >= (per_nil * t - alpha) / (per_nil - per_partner)`, so the
minimum useful `b` is one division away and the walk can be skipped outright
whenever the cover holds fewer cards than that. That was not measured and is the
first thing to try. **Nothing here is a soundness problem with §3 itself** --
the population is real, the disjointness is real, and the arithmetic is cost.

---

### 44. The forced-trump floor for all four hands — ⭐⭐ — **built, gated, measured, SHIPPED OFF in patch 49; parked on cost, not on soundness and not on nodes**

`top_spade_run` names one hand and one number. The same argument carried one
step further gives a floor for **every** hand: with `o_i` the number of spades
outside a hand ranked above that hand's i-th spade from the top,

    forced(H) = max over i of (i - o_i)

and H wins at least that many tricks down every line. *Why:* H's top i spades are
played on i distinct tricks, one is lost only to a HIGHER spade landing on it, a
higher spade in H cannot (one card per trick), so the spoiler is one of the `o_i`
spades outside H above `r_i`, and each is played once so each spoils at most one.
**DDS §4's rules 2 and 3 are the i <= 2 cases of this formula** -- rule 3, "the
second highest trump plus at least one trump more behind the hand with the
highest trump", is exactly i = 2, o = 1. At o_i = 0 it reproduces
`top_spade_run`, so it is never weaker for the hand that one spoke about, and it
speaks about the other three as well.

**Summing across two hands is sound here**, which the `top_spade_run` comment
denies for a side-wide count. What is unusable there is pooling a RUN across two
hands, where s1 and s2 can land on one trick and collapse. Nothing is pooled
here: each floor is proved on its own hand, and two hands cannot win the same
trick, so the two opponents' floors ADD. The formula prices the collapse in by
itself -- give s1 to West and s2 to East and East scores 1 - 1 = 0.

That gives three constraints where there was one: `n >= kn`, `p >= kp`,
`n + p <= t - ko`. Still a triangle, still three vertices.

*Soundness first, per the lesson from 31b.* A counterexample hunter that
enumerates **every legal play sequence** of a small deal and records the true
minimum tricks each seat wins, then checks the floor against it, spades treated
as already broken because that is the largest line set and so the smallest
minimum. **18,800 checks at 3, 4 and 5 cards; zero counterexamples; ~5% of
checks strictly beat `top_spade_run`.** The incumbent was checked alongside and
also never failed.

*And then the measurements, which are why it is parked:*

| workload | control | arm | nodes | throughput |
|---|---:|---:|---:|---:|
| corpus 560, fast | 39,701 | 39,701 | **byte-identical** | — |
| corpus 560, full | 279,895 | 273,596 | −2.25% | — |
| `large.txt`, 19 rows | 164,156,179 | 153,846,448 | **−6.28%** | — |
| random 13c x8, seed 3, full max | 385,799,941 | 380,858,610 | **−1.28%** | **8.30M -> 7.60M/s** |

22/22 tests, all 560 corpus values under `--check-pv` and `--check-moves`, all 19
`large.txt` values, fast mode byte-identical as predicted.

**−1.28% of nodes against −8.4% of throughput is net SLOWER in wall time** at 13
cards (46.5 s -> 50.1 s). The floors are one or two tricks against `t = 13`, so
they barely narrow the simplex, while the predicate walks every outstanding spade
with an owner search inside it on 38.6% of nodes. It is patch 39's shape -- gains
where hands are short -- except the per-node price is no longer free, and **the
standing preference on this project is for 13-card gains even at the cost of
small hands.** On that criterion, as written, it fails.

*What would revive it.* The predicate has to get near free. Two specific moves,
neither tried: drop the owner-search inner loop for a per-seat popcount against
an above-mask, and compute `ko` first so `per_nil * room <= alpha` can cut before
`kn` and `kp` are ever touched. **The node result is real and the soundness is
established** -- this is a cost problem and nothing else, which is a much better
place to be parked than 31b or 32.

**Patch 48 changed what this item is worth by measuring item 43 beside it.** The
two mechanisms are nearly disjoint -- 97-98% of §3's can-cash cuts land at
boundaries where this forced floor does not cut -- so they should be built and
measured TOGETHER or not at all. Either alone is a ~1.3% node saving fighting an
8% throughput bill; together the fire rate roughly doubles against one shared
walk down the position, and one walk amortised over two bounds is a different
arithmetic from two walks over one bound each. `forced_spade_tricks()` shipped in
patch 48 as measurement machinery, so both predicates are already in
`bounds.hpp` and the next attempt starts from working code rather than from this
entry.

## What patch 49 shipped, and what it did not

**The two arms were built into one block and measured apart, and that is the
only reason the right one shipped.** Built together they read as −2.44% of nodes
and net slower, which would have been recorded as a single failure. Isolated:

| arm at 13c seed 3, 8 deals | nodes | throughput |
|---|---:|---:|
| control (both off) | 385,799,941 | 10.82M/s |
| **item 43 only** (can-cash) | 381,343,003 (**−1.16%**) | 10.81M/s (**flat**) |
| item 44 only (forced floor) | 380,858,610 (−1.28%) | 9.92M/s (**−8.3%**) |
| both | 376,385,611 (−2.44%) | 9.75M/s (−10.3%) |

Nearly the same node saving; wildly different bills. Item 6's rule -- *heuristics
measured together are heuristics not measured at all* -- earns its keep again.

**The gate ratio is the whole result.** Both arms are a cheap test in front of an
expensive walk, and what separates them is how often the test lets the walk run:

| arm | gate opens | cuts |
|---|---:|---:|
| item 44, forced floor | **71.21%** | 18.90% |
| item 43, can-cash | **7.14%** | 2.01% |

Item 43's gate is one comparison and it is exact rather than heuristic: the
bound is strongest at `c = t`, which floors the value at zero, so `beta > 0`
refutes it outright. Item 44's gate is three popcounts and an arithmetic bound
that is simply loose -- a hand's forced floor cannot exceed its spade count, and
that ceiling is nowhere near tight enough to close 71% down to something a walk
can be paid for out of.

**The near-additivity is measured, not assumed.** On the corpus, item 43 alone
saves 1,836 nodes and item 44 alone saves 6,299; together they save 8,127 against
the 8,135 that adding them predicts. Patch 48 predicted this from the
would-cut overlap (97-98% disjoint) and the node counts confirm it, which is why
item 44 is carried behind a flag rather than deleted: **anything that makes its
walk affordable gets its −1.28% on top of item 43's, not instead of it.**

*Two things to try, neither attempted.* A tighter ceiling on `forced(H)` than the
spade count -- `forced(H) <= a - o_1`, where `a` is the hand's spade count and
`o_1` the number of spades above its highest, is one popcount and strictly
tighter. And computing `ko` before `kn` and `kp`, so the opponent constraint
alone can close the window before the other two floors are touched.

*Also not attempted, and it is the larger of the two.* §3's other half -- the
cover partner's can-cash count as an UPPER bound -- stays out on the soundness
gap patch 48 found: the claim is about a named hand rather than a side, and a nil
bidder holding nothing but spades is forced to ruff its own partner's winner.
The guard is cheap to state (the nil bidder holds at least `b` cards in the
cashed suit, or holds no spades at all) and the question is what it leaves of
the population. That is a measurement, not a build, and it is the next thing to
do on this item.

### 43b. The cover partner's can-cash count as a ceiling — ⭐⭐⭐ → ⊘ — **measured and REJECTED; the guard works, the population does not**

Section 3's other half, and the piece patch 49 deliberately left out. The cover
partner on lead cashes b tricks, so `p >= b`, so the value is capped at
`max{per_nil*n + per_partner*p : p >= b, n + p <= t}`. Spent against alpha.

**The hazard patch 49 named is real, and now it is demonstrated rather than
argued.** A counterexample hunter computed the game value where the nil side
(cover plus nil bidder, one player in a two-team game) MAXIMISES the cover
hand's trick count and the opponents minimise it -- the right quantity, because
"can cash" is a claim about a best strategy and not about every line. Against
that, the unguarded count over-claims **8 to 12 times per 400 deals** at three
cards and twice per 200 at four. The failure is the predicted one: the nil
bidder runs out of the cashed suit, holds nothing but spades, and is forced to
ruff its own partner's winner.

**The guard closes it completely.** Over b rounds the nil bidder plays b cards;
it follows suit while it can and must discard after that, and a discard is safe
exactly when it is not a spade, so b is capped at

    (nil bidder's cards in the suit) + (its cards that are neither spades nor of that suit)

with no guard needed for a spade lead or for a nil bidder holding no spades.
**Zero counterexamples in roughly 2,700 deals across three, four and five
cards**, and it costs almost nothing: it caps only 10-15 of ~630 positive claims
per run, and 0.3% to 8.3% of the cuts.

*So the soundness question is settled, and the item still fails.* The population
is the problem, at trick boundaries neither shipped arm settled:

| workload | boundaries left | cover on lead | gate opens | cuts, guard off | cuts, guard on |
|---|---:|---:|---:|---:|---:|
| 13c x4, seed 3 | 16,223,961 | 39.69% | 14.50% | 1.39% | **1.27%** |
| 13c x4, seed 42 | 54,488,387 | 39.96% | 14.19% | 0.55% | **0.55%** |
| 11c x6, seed 3 | 25,892,568 | 21.09% | 4.81% | 0.32% | **0.31%** |

Set beside the half that shipped: **item 43 opens its gate on 7.21% and cuts
2.02%, a 28% hit rate; this opens on 14.50% and cuts 1.27%, an 8.8% hit rate.**
Half the yield at twice the gate cost, and item 43 itself shipped as a wall-time
wash.

**The asymmetry is structural, not incidental, and it is worth keeping.** Item
43's bound is strongest at `c = t`, where it floors the value at ZERO -- a fixed
ceiling right in the middle of the window's range, so `beta > 0` refutes it in
one comparison and the walk almost never runs. This bound is strongest at
`b = t`, where it caps the value at `per_partner * t`, which is −182 at thirteen
cards. Alpha clears that virtually always, so there is no cheap precondition to
be had. **The two halves of section 3 are not symmetric in what they cost to
ask.**

*And the measured node result is worse than the population suggests:*

| workload | default | + cover ceiling | nodes |
|---|---:|---:|---:|
| corpus 560, full | 278,059 | 275,852 | −0.79% |
| `large.txt`, 19 rows | 163,393,676 | 163,321,448 | −0.04% |
| 13c x8, seed 3 | 381,343,003 | 380,879,588 | −0.12% |
| 13c x8, seed 42 | 338,508,564 | 339,429,146 | **+0.27%** |

**It makes seed 42 bigger.** Node counts are deterministic, so that is not
noise. The likely mechanism is item 41's: a cut returns a fail-soft BOUND where
the node would otherwise have produced an exact value, and this table's worth is
concentrated in its exact entries -- so a cheap cut high in the tree can cost
more downstream than it saves locally. Wall time follows: +2.9% on seed 3 and
+3.4% on seed 42. All 560 corpus values and all 19 `large.txt` rows still match
under `--check-pv`, so this is a performance verdict and not a correctness one.

*What would revive it.* Very little, and that is the point of recording it this
fully. The guard is not the obstacle -- it is nearly free and it is proven. The
obstacle is that the bound has no tight ceiling to gate on, so it must pay for a
four-suit walk on 14% of boundaries to buy under 1.3% of cuts, some of which
cost more than they save. A cheap upper bound on the cover's cashable count,
tighter than its longest suit, would be the thing to find; without one this does
not become affordable no matter how the rest is arranged.

### 45. ~~Two more places the impossible spades-broken flag is generated~~ — ⭐⭐⭐ — **generators done in patch 53; `invariants.py` still open**

Patch 47 fixed `nil_bench`. **The same bug is live in three Python tools**, and
one of them can bake a bad row into a committed file.

*`tools/invariants.py`* -- `"broken": str(rng.randrange(2))`. At 13 cards **4 of
8 generated specs are impossible and `nil_cli` rejects every one**. CMakeLists
ships this at 4 and 6 cards, where it cannot fire, and its own comment says *"run
it by hand at 11 or 12 cards when the search changes"* -- which is precisely the
workflow that walks into it.

*`tools/make_large_corpus.py`* and *`tools/crosscheck.py`* -- ~~same coin, drawn
before an optional partial trick is played out~~. **Fixed in patch 53**, which
had to: with the row writers repaired, `make_large_corpus.py` immediately
produced a 10-card row holding all thirteen spades with `broken=1` and the
corpus would not load. Both now draw the coin and mask it. The coupled
`validate()` bug described below is also gone, fixed in patch 52 -- a spade on
the current trick no longer counts toward the thirteen, so the partial trick may
legitimately set the flag after the mask.

**A coupled bug in `validate()` blocks the straightforward fix, and it is a
rules bug rather than a harness one.** The check counts spades on the CURRENT
TRICK towards `spades_seen`:

```
for (int i = 0; i < pos.trick_len; ++i)
    if (card_suit(pos.trick[i]) == SUIT_SPADES) ++spades_seen;
```

but a spade sitting on the current trick is exactly the evidence that spades ARE
broken -- it has left a hand. So a reachable position is rejected. Found by
generating 3,000 twelve-card cases with a partial trick; **8 of them are all-13-
spades-accounted-for with a spade on the trick and the flag legitimately set**,
and `nil_cli` refuses all 8:

```
pbn:   N:A97642.T2.AQ9.A 53.K.652.KQ9843 JT.AJ83.T743.5 KQ.Q765..JT762
trick: DJ S8            (a diamond led, ruffed -- spades are broken)
error: spades cannot be broken: all thirteen are still in play, so none has been played
```

The rule wants to be *"broken is impossible only if all thirteen spades are
still IN HANDS"*, i.e. the trick loop comes out. The converse still holds -- a
spade played to a completed trick is gone from the hands, so it cannot be that
all thirteen are in hands and spades are broken -- so dropping the loop does not
weaken the check.

**Sequenced together and after item 43**, because fixing the three generators
without fixing `validate()` would make them emit positions the library refuses,
and `validate()` is a library change that wants its own patch and its own
`large.txt` regeneration. `nil_bench` was separable and went first precisely
because `random_position()` never builds a current trick.

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

### 54. ~~A role per seat~~ — ⭐⭐⭐⭐⭐ — **done, patch 54; phase one of multi-nil**

Landed. See the Done table for what changed. Three things worth carrying
forward, because phase two has to keep faith with them.

**The anchor is a wire convention and nothing else.** Roles are absolute in
memory. The rotation happens in exactly two functions, `parse_seat_roles` and
`seat_roles_to_string`, and in the C# wrapper's `ToPbnOrder`. Every other piece
of code — the search, `validate_seat_roles`, the corpus loader after parsing —
sees `role[SEAT_NORTH]` and means North. Keep it that way; a roles array that is
anchored in some places and absolute in others is the bug this design exists to
prevent.

**`NIL_SEAT_NORTH` is literally `0`, so an un-migrated ABI call site compiles as
a null pointer rather than failing to build.** One survived the first pass and
was caught only by running the tests, coming back as `NIL_ERR_NULL_ARG`. The C
ABI test block now pins that case explicitly. Any future change to the `seats`
argument should assume the compiler will not help.

**What phase two actually needs.** The search still collapses the roles to a
single `ctx.nil_seat` and takes the coalitions from its parity. That is sound
under the shape `validate_seat_roles` accepts, because the cover IS the nil's
partner and so `{nil, cover}` is exactly that parity class. It stops being sound
the moment a second nil appears, and the places that assume one nil are:
`Ctx::nil_seat` and the weights derived from it, `objective_weights`'s single
primary level, `replay_pv`'s `nil_tricks` counter, and `bounds.hpp`, which
reasons throughout about "the nil bidder" and "the cover partner" as two named
hands. The objective itself is the hard part rather than the plumbing: with two
nils the primary is no longer a scalar one side minimises and the other
maximises, and the Sturtevant/Korf paper in this repo is about exactly that —
`maxⁿ` admits only shallow pruning, and a two-nil deal is closer to a four-way
game than to the two-team one alpha-beta wants. **Measure the population before
building anything**: how often two nils are actually bid, and how much the
optimal line moves when they are.

### 55. The oracle's coalitions still come from parity, not from the roles — ⭐⭐⭐

`nil_oracle.py` fixes the sides by seat parity: North and South always minimise
the designated player's tricks, East and West always maximise. That is the nil
question exactly when the nil sits North or South, and something else entirely
otherwise, which is why `crosscheck.py` rotates every deal so that it does.

With roles in hand the oracle could take the coalitions from them instead — the
nil side minimises, the opponents maximise — and it would then agree with the
solver for all four seats and the rotation could go. **Deliberately not done in
patch 54.** This file is the ground truth the solver is checked against, and
moving its search during a representation change would mean the corpus was
validated by one oracle and re-validated by another. It is a small change and it
wants its own patch, its own selftest pass, and a full corpus re-verification
showing no value moved.

Note that the oracle's own selftests pin `solve(pos, 1)` — designated East —
under the parity rule, so they will move when this does, and the new values have
to be justified by hand rather than re-pinned.

### 56. The C# `NilStatus` enum had drifted from the header — ⭐⭐⭐⭐ — **fixed in patch 54**

Recorded because the mechanism is worth remembering, not because it is open.

Patch 52 deleted `NIL_ERR_TOO_MANY_CARDS` and moved every code below `-4` up by
one, and the header says so in a comment. The C# mirror in `NilSolverNative.cs`
was never moved with it, so for two patches every code from `-4` down reported
under the wrong name: a native `BUFFER_TOO_SMALL` read as `TooManyCards`,
`INTERNAL` as `BufferTooSmall`, `UNSUPPORTED` as `Internal`, and C#'s
`Unsupported = -7` could not be produced at all — while `NilSolver.SolveWithLine`
documented returning it for a PV in fast mode.

It surfaced only because patch 54 makes `-6` a code a caller has a reason to
branch on: a roles array holding two nils. Fixed to match the header.

**The general lesson is that the C ABI has two mirrors and only one of them is
compiled here.** `src/api.cpp` now carries a `static_assert` tying the four
`NIL_ROLE_*` values to the core enum, which catches drift on that pair at build
time. Nothing catches drift between the header and the C# file, because no C#
toolchain runs in CI. Until one does, treat every edit to `nil_solver.h` as an
edit to `csharp/NilSolverNative.cs`.

### 57. `refresh_corpus.py` wrote answers by hard-coded column index — ⭐⭐⭐ — **fixed in patch 54**

Same family as patch 53's format-string shift, and it would have fired the same
way. The tool rewrote `parts[9]`, `parts[10]` and `parts[11]` with recomputed
`nil_tricks`, `side_tricks` and `pv`. The `seats` column replaced two columns
with one and moved all three left by one, so the tool would have written trick
counts into `secondary` and a principal variation into `side_tricks` — silently,
on a file whose whole job is to be the trusted answer.

Now derived from `FIELDS` by name. **Any tool that writes a corpus column by
number is one format change away from corrupting the corpus**; the remaining
readers all `zip(FIELDS, parts)`, which fails loudly instead.

### 58. ~~Two nils on one side: the oracle~~ — ⭐⭐⭐⭐⭐ — **done, patch 55**

Landed. What the measurements say, in the order they matter.

**The raw tree does not move at all, and that is a real finding rather than a
disappointing one.** Asked the same deal two ways, the oracle visits *exactly*
the same nodes:

```
   cards  deals       one nil       two nils   ratio
       3     12        11,941         11,941    1.00x
       4     12       774,631        774,631    1.00x
```

Not approximately — identically, on every deal. This oracle is exhaustive with
no pruning, so its node count is a function of the POSITION, of how many legal
play sequences exist, and not of the objective laid over it. **Whatever the
two-nil shape costs the C++ solver therefore comes entirely out of pruning, and
cannot be measured here.** Any estimate of the 13-card explosion has to wait for
the C++ implementation; the corpus exists so that measurement can be taken
against something.

**What does transfer is transposition pressure**, and it is mild:

```
   cards  deals   one nil    two nils   ratio
       3     12     5,697       6,197    1.09x
       4     12    94,701     106,467    1.12x
```

The broken-nil mask multiplies the key space by at most four, but the observed
cost is 9-12%, because the great majority of positions are only ever reached
under one mask value. That is the number to carry into the C++ port: the table
gets slightly less effective, not four times less. **It is a lower bound, not a
prediction** — the oracle's memo has no replacement policy and a real fixed-size
table under more distinct keys loses more than the key count alone suggests.

**The question is not reducible, which is why it needs its own search:**

```
   cards  deals   irreducible
       3    250   14  (5.6%)
       4    250   14  (5.6%)
```

That is deals where each bid is holdable ON ITS OWN but the pair cannot hold
both. If it were zero, two nils would be two independent single-nil questions
and could be answered by running the existing solver twice. **At one deal in
eighteen it is not**, and that gap is the whole content of the shape.

The one-directional half of that is a selftest: **if the two-nil game ends with
nothing set, each bid is provably holdable alone**, because the pair had a
strategy guaranteeing it and that same strategy is available in the single-nil
game where the partner is under no constraint at all. The converse is exactly
what fails 5.6% of the time.

### 59. Two nils on one side: the C++ solver — ⭐⭐⭐⭐⭐ — **next**

**MODE_FAST STAYS SINGLE-NIL, and should REFUSE the partner-nils shape rather
than answer it.** Fast mode asks one question -- *with perfect play, can the
specified player make nil?* -- and with two bidders there is no specified
player. Worse, whether one of them is safe is not even well defined on its own:
it depends on how the pair trades the two bids off against each other, which is
the full objective. So `NIL_FLAG_FAST_MODE` with two nils returns
`NIL_ERR_UNSUPPORTED`, the same way a PV in fast mode already does. That also
protects the `[0, 1]` window and every node count banked on it.

The oracle is the specification now; `tests/corpus/multinil.txt` is the target.

**What has to change, in the order the dependencies run.** `validate_seat_roles`
widens to the partner-nils shape, mirroring `role_shape` in the oracle. `Ctx`
gains the broken-nil mask and the state key gains it too — that is the piece
with a measured cost attached, 9-12% more distinct entries, and it should be
A/B'd on its own before anything else lands on top of it. `objective_weights`
forks: the multi-nil primary is `K*K` charged on a bidder's first trick, not a
weight on a trick count. `replay_pv` already counts per seat on the oracle side
and the C++ one should follow, because `Tally::nil_tricks` is a question with two
answers here.

**`bounds.hpp` is the hard part and should be assumed unsound until re-derived.**
Every bound in it reasons about "the nil bidder" and "the cover partner" as two
named hands with opposite jobs. With two bidders there is no cover, the nil
side's own tricks are the thing it is trying not to take, and the static
nil-safe/nil-set bounds do not carry over. **Do not wire any existing bound to
the new shape without an exhaustive property test first** — the corpus is 150
rows and a corpus pass is evidence about the corpus, not a correctness proof.

**Measure the 13-card explosion before optimising it.** The six `m13-` rows are
there for exactly that, and the honest first number is a node count with every
bound disabled, so the cost of the shape is separated from the cost of the
bounds not applying to it.

### 61. ~~The transposition table for two nils~~ — ⭐⭐⭐⭐⭐ — **done, patch 59**

The table is OFF for the shape, because two positions identical in cards but
reached under different broken-nil masks are not the same position and the key
does not carry the mask. Adding it is a two-bit widening of
`encode_state_key`.

The oracle priced the cost at **9-12%** more distinct entries, and warned that
was a lower bound. It measured the wrong direction to worry about: what the
table BUYS on this shape is 2.01x / 3.32x / 5.42x at 4/5/6 cards, and it grows.
The extra keys are real and they are irrelevant next to the hits.

Worth keeping for item 62: the gain grows with card count, so a bound measured
at four cards against the table-off baseline would have looked far better than
it will be worth once the table is on. **Every measurement in item 62 must be
taken with the table ON**, which is now the default for the shape.

### 62. ~~Re-deriving the bounds for two nils~~ — ⭐⭐ — **done, patch 66: the one bound worth having shipped at 1.22x**

**Re-measured with the table on, and it is worth far less than patch 58 said.**
That patch reported the single-nil machinery at 3.0x / 4.9x / 7.6x, against a
table-OFF baseline. Patch 59 gave the shape a table and took most of it back.
The ceiling for this item is what the bounds add ON TOP of a table, at the size
that matters:

```
single nil, 13 cards, everything on: 162,744,179 nodes
single nil, 13 cards, table only:    212,738,186 nodes   ->  1.31x
```

**1.31x**, and patch 64 breaks that down per arm: `target_bounds` 1.21x,
`later_tricks` 1.13x riding on it, and everything else at or below 1.01x --
`static_bounds` 1.01x, `tt_narrow` 1.01x, `presolve` 1.00x. So the whole item is
really one bound, and it is the one that needs a genuinely new derivation rather
than a port, on a shape whose worst case is already 748 ms. That is a poor
trade against anything else on this list. It stays open because 1.31x is not
nothing, and `nil_cannot_be_forced` remains the cheapest single piece -- the
predicate is still true and only its consumer is wrong -- but it is no longer
next.

**The lesson is about the measurement, not the bounds.** A ceiling taken against
a baseline missing another mechanism will overstate by whatever that mechanism
was worth. Patch 59 predicted this in its own note and it still needed
re-measuring to believe.

**Fork every self-check alongside every objective fork.** Twice now a re-packing
check has been left on the single-nil formula while the thing it checks moved --
`solve()` in patch 58, `solve_moves()` in patch 60 -- and neither failed loudly.
Before touching a bound, grep for every place `weights.primary` is multiplied by
a trick count and confirm it knows which objective it is on.

Where all the remaining value is. The all-off baseline in patch 58 is the
control arm, and the target is the 3.0x / 4.9x / 7.6x the single-nil solver
gets on the same deals.

Order by what the measurement says, not by what is easiest. `target_bounds` is
the big one and the hardest: it takes the extremes of `per_nil * n +
per_partner * p` at the vertices of a triangle, and the two-nil primary is a
step function of `n` and `p`, so vertex evaluation proves nothing. It needs a
genuinely new derivation over the three outcomes `nils_set in {0,1,2}` rather
than a port. `nil_cannot_be_forced` is the cheapest win: the PREDICATE is still
true, and only its consumer -- "the primary term vanishes" -- is wrong, so it
becomes "this bidder's own bid survives" and settles one of the two levels
rather than the whole value. **Exhaustive property test before wiring anything**;
150 rows is evidence about 150 rows.

### 77. ~~A presolve-seeded root window for one bid on each side~~ — ⭐⭐⭐⭐ — **done, patch 77**

Item 23's presolve, gated on `nil_count(roles) == 1` since it was written, opened
up for `SHAPE_OPPOSING_NILS`. **1.31x in nodes and 1.23x on the clock over the
eight contested 13-card deals, four reps of four.**

**What it does.** Two `MODE_FAST` probes, one per bidder, each built with
`seat_roles_from_nil` so it is a single-nil question this solver has always
answered: can THIS bid be broken, with both opponents unconstrained and free to
throw their own bid away doing it? Nothing new is searched. What is new is that
the pair of answers composes.

**Why it composes.** Each probe is a GUARANTEE — it holds against every strategy
the other side has, including ones that sacrifice the other bid — so it survives
being asked inside a deal where the attacker has a bid of its own to protect. A
guarantee about one bid confines the outcome to two of the four, and two of four
is a bound on the outcome rank. Which END it bounds is decided by who owns the
guarantee: the near side minimises the rank so its guarantees cap it, the far
side maximises so its guarantees floor it.

| near bid | far bid | rank | probes needed |
|---|---|---|---|
| safe | safe | both bids live | **2** |
| breakable | breakable | both bids die | **2** |
| safe | breakable | two of four | 3 — the third does not exist yet |
| breakable | safe | two of four | 3 — the third does not exist yet |

The rank converts to a value band because `primary` is `k*k` and the trick term
spans at most `k*t < k*k`, so each of the four ranks owns a band of the value
line and the bands do not touch. Pinning the rank pins the value to within one
band, and a window on that band refutes every line heading for a different one.

**Measured, `tests/corpus/opposed13.txt`.** Net includes the probes; gross is the
main search alone. The probes are 7.5% of the armed total.

| deal | off | on | net | gross | outcome |
|---:|---:|---:|---:|---:|---|
| 1 | 34,380,976 | 33,614,775 | 1.02x | 1.04x | both live |
| 2 | 3,060,792 | 2,378,827 | 1.29x | 2.09x | both live |
| 3 | 13,166,148 | 15,076,218 | **0.87x** | 1.02x | one down |
| 4 | 29,801,843 | 22,124,249 | 1.35x | 1.62x | both live |
| 5 | 377,597,716 | 312,420,336 | 1.21x | 1.27x | both down |
| 6 | 79,426,335 | 29,961,443 | **2.65x** | 3.27x | both live |
| 7 | 72,858,745 | 44,557,462 | 1.64x | 1.94x | both live |
| 8 | 36,096,605 | 32,013,941 | 1.13x | 1.23x | both live |
| **total** | **646,389,160** | **492,147,251** | **1.31x** | **1.42x** | |

Wall clock, one binary, `--no-presolve` the only difference, OFF-then-ON inside
each rep: **4 of 4 reps a win** at 0.814 / 0.808 / 0.814 / 0.815. Throughput
falls 6.4% (8.69M to 8.13M nodes/sec), which is why 1.31x in nodes is 1.23x on
the clock: the nodes the band removes are the cheap ones under a broken bid, so
what is left is dearer per node.

**Deal 3 is a real loss and it is the case the table predicts.** Mixed answers
give only a one-sided beta, which is exactly item 23's shape, and item 23
measures 1.00x on single nil. The probes cost 2.1M and the window saved 0.2M.
Closing that case needs a probe MODE_FAST cannot express — *can one side break
the other's bid WHILE KEEPING ITS OWN* — which is item 78.

**A wider and much weaker second sample.** Random opposed deals via
`--random --seats 0 0 3 2`: 11 cards 1.007x / 1.087x / 1.119x / 1.295x on seeds
3 / 11 / 7 / 42, 9 cards 0.986x and 1.098x on seeds 3 and 42. Wall clock on the
flattest of them (11c seed 3, +0.7% nodes) is **3 of 4 reps**, a wash. This is
not a contradiction, it is the patch-69 lesson again: **a hand drawn at random
usually gives a bidder an ace, so its bid is trivially breakable and the search
never has to work.** The arm is worth what it is worth on deals where the
outcome is in doubt, which is the only kind anyone bids nil on. `opposed13.txt`
is the workload to read.

**THE BUG THIS FOUND, and it is the part worth reading.** `walk_pv` re-searches
each step of the line under the window the ROOT was asked about, UNSHIFTED by
what the line has banked on the way down. That has always been sound because the
windows it was handed were loose: the sentinels, or item 23's beta, which caps a
value whose remaining part only gets less negative. **A rank band is not loose.**
The root's value carries `primary * (rank - rank0)` — a whole `k*k` — and a
sub-position reached AFTER that bid has broken does not: its value is the trick
term alone, which sits far above a band centred on a rank the root had to pay
for. Every such step fails high, `canonical_move_for` compares against a bound
rather than a value, and the walk leaves the line the search chose. Caught by the
replay check on deal 5: search -70, replay -322. **It hid on 7 of 8 deals**,
because when both bids live the rank never moves along its own PV and the
unshifted window contains every step by accident. The walk now runs under the
sentinels when the band was applied, which costs nothing measured — `search_nodes`
is snapshotted before it — and nothing on any other shape.

**NOT wired into `solve_moves`, deliberately.** A two-sided band means the
per-card re-search would have to trigger on rows falling off EITHER end rather
than just the low one, and a row whose rank differs from the root's falls off by
a whole `k*k` — which is most of the interesting rows. That is a second
correctness argument on a path with a different obligation. `--moves` on an
opposed deal keeps exactly the behaviour it has today.

**Verified.** All 8 deals agree arm-on and arm-off on the value, all three trick
counts, `nils_set` AND the full principal variation. Banked fixed points against
a pristine clone: 39,701 / 278,059 / 49,084 / 163,393,676 unmoved, and
`multinil.txt` full unmoved at 4,833,200. 28/28. And because the shipped
`PRESOLVE_MIN_TRICKS = 8` gate means `opposing_crosscheck` at 3-5 cards would
never touch the new code, it was re-run with the gate forced to zero: **96/96 at
3 cards, 96/96 at 4, 48/48 at 5, across all eight strictly opposed role sets.** A
test that cannot reach the change is not a test of it.

**Two tool changes came with it, both because the measurement needed them.**
`Solution::presolve_nodes`, reported through `nil_cli --compact`, so the arm's
cost reads off the same run that measures its benefit instead of being inferred
from a second one. And **`nil_bench --deals <file>`**: `opposed13.txt` has been
in the repo since patch 76 with nothing able to read it — it holds cards and no
answers, so `load_corpus` refuses it and every measurement taken on it was a
hand-rolled loop over `nil_cli`. It now runs through the standard harness, and
`ab_interleave.py` grows an `--workloads opposed` set so the wall-time protocol
covers a shape the default workloads cannot reach at all.

**Named next experiments, in order.** (a) Item 78, the conjunction probe, which
closes the mixed case deal 3 sits in. (b) The reachable-rank bound INSIDE the
tree: the rank at a node is a step function of `st.nils_broken` and the mask only
grows, so the reachable rank set is O(1) from the mask and a node whose set is
disjoint from the band is refutable without reading a card. This patch seeds the
window; that one spends it at every node. (c) `PRESOLVE_MIN_TRICKS` is 8 and 9
cards is the one size that measured a loss — the floor may want to be higher for
this shape, but it is a tuning knob and a separate variable.

### 78. ~~The conjunction probe: break theirs while keeping ours~~ — ⭐⭐⭐⭐ — ~~**78a patch 80, 78b patch 81, 78c patch 82**~~

**The third probe item 77 needs and `MODE_FAST` cannot express.** 77's two probes
settle two of the four combinations outright and leave the other two with a
one-sided bound, which is worth close to nothing: `opposed13.txt` deal 3 is a
0.87x LOSS for exactly this reason, and item 79's ceiling on the two mixed deals
is **14.78% and 2.79% against the 86.62% a closed band gets**. The question that
closes them: *can this side force the other's bid down WHILE KEEPING ITS OWN?*

**They multiply rather than add**, which is why this is worth more now than when
it was first proposed. 78 converts a one-sided bound into a closed band; 79's
whole yield is a function of whether the band is closed. Deal 5 is 72% of what is
left of the opposed tree and is one of the two.

#### 78a — the oracle, done in patch 80

Ground truth first, as items 58 and 67 did. **Two independent algorithms, made to
agree.** `_search_conjunction` is a boolean AND-OR search over a two-valued
objective. `_search_opposing` is backward induction on a utility pair with a
trick tie-break underneath. They answer the same question by different routes and
`selftest` requires agreement on every fixture.

**THE EQUIVALENCE, which is the useful part and was not obvious going in.** The
conjunction is exactly outcome **rank 3** for the attacking side — its bid alive,
theirs dead. Rank 3 is the unique best on that side's ladder and the same outcome
is rank **0**, the unique worst, on the other's, **under both partner leans**. So
a defender maximising its own utility escapes it whenever it can and an attacker
steers to it whenever it can, and backward induction on the pair lands on rank 3
precisely when the attacker can force it. **That holds on all three shapes, not
only the strictly opposed one**, so this probe is already defined for the two
arrangements the C++ refuses — which is a free head start on item 60.

**Measured**: 1,920 comparisons across all sixteen opposing arrangements at 3 and
4 cards, **zero mismatches**, with 488 of the 1,920 coming back forceable. The
population is asserted in `selftest` too, because a check that only ever sees
False passes while measuring nothing.

Also shipped: `solve_conjunction()`, `ConjunctionSolution`, and
`--conjunction <seat>` on the oracle CLI with a `--compact` form, so 78b's
crosscheck has something to shell out to. The CLI answers from the BOOLEAN search
rather than reading the rank off the utility search — the point of having two is
that they are independent, and `selftest` is the one place they are joined.

#### 78b — the C++ search, done in patch 81

**Built, cross-checked and MEASURED BEFORE BEING WIRED IN, and the measurement
says it pays on one of the two deals it was built for and probably not on the
other.**

**It is a question about a shape, not a mode.** `SearchOptions::conjunction_seat`
names the ATTACKING side's bidder; everything else about the deal still comes
from `seats`, and `solve()` refuses the probe on any shape but
`SHAPE_OPPOSING_NILS`. The value is the indicator, the window is `[0, 1]`, and
the search is the same AND-OR shape MODE_FAST is.

**The indicator is rank 3 and nothing else**, which is why it fitted here rather
than needing a new objective. `side_rank` gives 3 to *my bid alive, theirs dead*
under BOTH partner leans -- the leans only ever swap the two middle rungs -- so
the indicator reads two bidder seats and no role at all. Charged as a DELTA the
way the outcome rank already is, telescoping to `conj(final)`, and `conj` of an
empty mask is zero, so the accumulated value IS the answer rather than differing
by a constant.

**Two things had to be excluded by name rather than by reading the weights.**
The probe's weights are `(1, 0, 0)`, the same as MODE_FAST's, and they mean
something else: the value is an INDICATOR, not `ctx.nil_seat`'s trick count. Left
alone, `value_is_nil_tricks` would have wired two sound proofs to a value they
say nothing about -- the exact failure `disable_single_nil_machinery` exists to
prevent, arriving through the back door. And `gains_nonnegative` is false here
because the delta is **-1** as well as +1: the far bid dying after the near one
already has takes the indicator back down.

**The defending side's goal is a DISJUNCTION**, which is the hazard this whole
item is about. It wins by EITHER keeping its own bid alive OR breaking the
attacker's, so it may deliberately dump a trick on the attacker's bidder and
abandon its own bid. Nothing constrains it to protect: it minimises the
indicator, which lets both routes through. A search that let it only protect
would report the attacker succeeding on lines it cannot win -- confidently, and
unrefutably by any corpus.

**Checked against TWO oracle routes at once**, because one would not have been
enough: the oracle's independently-written boolean search, and the outcome rank
of its exhaustive utility search. **320 probes at 3 cards and 256 at 4, across
all eight strictly opposed role sets, all three answers agreeing every time**,
with 80 and 58 respectively coming back forceable so the run is not all one
answer. `tools/conjunction_crosscheck.py`, ctest #29.

#### The floor measurement, which is the deliverable

Per this file's own rule, the cost was measured before the probe was wired into
anything. **Only the two MIXED deals need it** -- the six where both bids are
safe are settled by item 77's two plain probes and never ask a third question.

| deal | case | total nodes | probe | cost |
|---:|---|---:|---:|---:|
| 3 | mixed | 15,161,073 | 3,040,879 | **20.1%** |
| 5 | mixed | 296,509,340 | 3,476,421 | **1.2%** |

**Deal 5 is where this pays and deal 3 probably is not.** 1.2% to close a band on
72% of what is left of the opposed tree is an obvious bet; 20.1% on a deal that
is *already* a 0.87x loss from patch 77 is not, and item 79's ceiling on deal 3
is 2.79% against deal 5's 14.78%, so deal 3 has less to win as well as more to
pay. **The probe is therefore likely to want a gate rather than to run
unconditionally**, and the honest version of that gate is not obvious: hand size
does not separate these two deals and neither does anything item 77 already
knows. That is the open question 78c inherits.

Also worth recording: the probe is **not** uniformly cheap. Deal 7's N-attacking
probe costs 11,526,620 nodes, four times deal 5's -- but deal 7 is a both-safe
deal and never asks, so it never pays. A gate that fires on the wrong deals would
cost more than the arm returns.

#### 78c — wired into the presolve, patch 82

**1.024x overall, which is not the story. THE STORY IS THAT THE PREDICTION IN
78b WAS BACKWARDS, and the reason is worth more than the number.**

The gate turned out not to need designing. The third probe fires when and only
when the first two DISAGREE -- exactly one bid safe -- and the six deals where
they agree measure **1.000x, not approximately**: the probe never runs and the
tree is untouched. Only deals 3 and 5 move.

| deal | off | on | net | gross | probe | conjunction | rank it closes to |
|---:|---:|---:|---:|---:|---:|---|---|
| 3 | 15,161,073 | 9,316,457 | **1.627x** | **3.14x** | 3,040,879 | TRUE | 0 — extreme |
| 5 | 296,509,340 | 292,597,961 | 1.013x | 1.027x | 3,476,421 | FALSE | 1 — both die |
| all 8 | 412,178,524 | 402,422,529 | 1.024x | | | | |

**78b predicted deal 5 would pay and deal 3 would not.** The reasoning was
size: deal 5 is 72% of the tree and the probe costs 1.2% of it, against deal 3
where the probe costs 20.1%. Both halves of that were right and the conclusion
was still wrong, because **the size of the tree is not what decides it. The RANK
the band closes to is.**

Deal 3's conjunction comes back TRUE, closing the band on rank 0 -- an extreme,
where every node under a broken bid sits on one side of the window and item 79's
mask bound refutes it on arrival. Deal 5's comes back FALSE, closing on *both
bids die*, which is **the one rank where a closed band buys nothing**: from that
outcome the reachable sets straddle the answer from both sides and nothing is
refutable by arithmetic. That was derived in item 79's entry -- *R=1 gives no
arithmetic refutation* -- and then not applied when predicting where 78c would
pay. **The band being closed is not the point; closing it somewhere USEFUL is.**

And the corollary kills the gate this item was supposed to design: **which rank
the band closes to is exactly what the probe is paid to discover.** A gate would
have to predict its own answer. What saves it is that no gate is needed -- both
mixed deals come out net positive, 1.627x and 1.013x, so a probe that costs 20%
of a small deal and 1.2% of a large one is worth running either way.

**Patch 77's only losing deal is now its best.** Deal 3 was 0.87x when the
presolve shipped, because a one-sided bound is item 23's shape and item 23 is
1.00x on single nil. Against the pre-77 base it is now 13,166,148 -> 9,316,457.

Wall clock, one binary, `--no-conjunction-presolve` the only difference: **4 of 4
reps** at 0.949 / 0.953 / 0.997 / 0.908. Rep 3 is nearly flat and the spread is
wide, which is what a 1.024x node change on a workload dominated by one
barely-moved deal should look like; the honest summary is that this is a large
win on one deal in eight and free on the rest.

**How the rank is pinned without casing out the leans.** A one-sided bound always
leaves exactly two of the four outcome ranks standing, and the conjunction is the
extreme one of that pair -- the asking side's best and the other's worst. So the
four ranks are enumerated, those outside the existing bound are dropped, the one
the probe refuted is struck off, and if exactly one survives the band closes on
it. If anything else survives the band is left as it was, which costs the
tightening and nothing else.

**Verified.** All 8 deals identical arm-on and arm-off on value, trick counts,
`nils_set` and the full PV. 29/29. Banked fixed points unmoved. And
`opposing_crosscheck` re-run with `PRESOLVE_MIN_TRICKS` forced to zero so the
third probe fires at sizes the shipped gate skips: **96/96 at 3 cards, 96/96 at
4, 48/48 at 5** across all eight strictly opposed role sets.

#### What is left on this shape

The opposed search stands at **646,389,160 -> 402,422,529** across items 77, 79,
80 and 78, and roughly 80 seconds to 40 on the eight deals. What remains is deal
5's shape rather than anything unmeasured: a deal whose answer is *both bids die*
gets nothing from a closed band, so the next real gain there has to come from
somewhere other than the root window -- most likely item 3's old target, the
one-down region, where a single live bid makes the position a single-nil one and
the machinery `disable_single_nil_machinery` switches off becomes sound again.

### 81. ~~Recover the single-nil machinery in the one-down region~~ — ⭐⭐⭐⭐ — **REJECTED on ceiling, patch 83**

**The population was 19.08% of nodes and the ceiling is 1.24%. Measured before
building, and the measurement said do not build.**

**The argument, which is still correct.** With exactly one bid still live the
position IS a single-nil position: the dead bid can never come back, so the only
thing that can still move the outcome rank is whether the survivor survives --
precisely the question `bounds.hpp` answers, and precisely the machinery
`disable_single_nil_machinery` switches off for this shape. Item 76 made that
argument for the BOTH-down region and it held. The one-down region was unclaimed
and is about a fifth of the tree. A proof firing pins the rank, which collapses
item 79's reachable set from two values to one and tightens its bound by a whole
`k*k`.

**Where it dies is the middle step.** Measured on `opposed13.txt` with everything
through patch 82 live:

| | count | rate |
|---|---:|---|
| one bid live, at a trick boundary | 67,554,315 | 19.08% of nodes |
| `nil_must_take_a_trick` fires | 5,680,519 | 8.41% of those |
| `nil_cannot_be_forced` fires | 1,593,594 | 2.36% of those |
| **newly answered if the rank were pinned** | **4,393,491** | **1.24% of nodes** |

**Only 10.8% of the eligible nodes get a proof at all.** The two are cheap
SUFFICIENT conditions, not a decision procedure, and they are complementary on
one test: `nil_must_take_a_trick` needs the live bidder to hold spades and be
short of covers, `nil_cannot_be_forced` needs it to hold none. **A live bidder
holding spades WITH adequate covers gets neither**, and that is the common middle
case in this region. The proofs are not weak by accident -- they are the cheap
ends of a question whose honest answer is a nil search.

**Uniform across deals, and weakest where it matters**: deal 5 **1.09%**, deal 3
2.05%, deal 6 1.70%. Deal 5 is 73% of the remaining opposed tree, so the arm
would be charged across a fifth of the search to answer one node in a hundred on
the deal that dominates it.

**This is `population is not the same as firing rate` for the third time in this
file**, after the killer study and item 79's own ceiling work. The difference is
that here the population looked good and the firing rate killed it, where item 79
had a mediocre aggregate hiding an excellent per-deal split. Neither could be
guessed from the other.

**What would revive it.** A cheap proof that covers the middle case -- a live
bidder holding spades with covers. `duck_depth` (item C0) already computes the
shape of that holding and is shipped as infrastructure with nothing consuming it;
if it can decide the survivor's fate rather than just describe the holding, the
firing rate is the number to re-measure, not the population, which is already
known to be 19.08%.

**What shipped anyway**: the measurement itself, under `--opposed-stats`, so the
claim is re-checkable rather than merely recorded. Free when off. No node count
moved: 29/29, 402,422,529 on the opposed deals, and 39,701 / 278,059 / 49,084 /
163,393,676 / 4,833,200 all unmoved.

### 82. The both-down region is a pure trick count — ⭐⭐⭐⭐⭐ — **ceiling measured, patch 84; build next**

**THE ANSWER TO "WHAT PRUNES WHEN BOTH BIDS MUST DIE" IS: STOP PRUNING ON NILS
AND PRUNE ON TRICKS.** The question is the right one and the arithmetic answers
it exactly.

**Why a both-die answer refutes nothing by rank.** The ranks a node can still
reach are those of the masks containing its own, and against an answer band the
four masks give:

| mask | reachable ranks | vs an answer of rank 2 (both live) | vs rank 1 (both die) |
|---|---|---|---|
| nothing broken | {0,1,2,3} | straddles | straddles |
| near bid down | {3,1} | **straddles** | **straddles** |
| far bid down | {0,1} | below -> refuted | **straddles** |
| both down | {1} | below -> refuted | **equals the band** |

When the answer is *both live*, two of the four sit entirely below it and go on
arrival -- that is deal 6's 86.62% and item 79's whole yield. When the answer is
*both die*, **every one of the four either straddles the band or sits exactly on
it**, and no arithmetic on the mask can refute anything. Deal 5's 1.013x from
item 78c is that table, not a weakness in the probe.

**So the rank is settled at the ROOT and contributes nothing more THERE.** Item
77's two probes plus item 78's third one pin it before the search starts.

**CORRECTED, patch 85.** The paragraph that stood here went on to say that deal
5 *is not a nil problem any more, it is a double-dummy trick count wearing a nil
problem's clothes*, and that is **wrong** -- caught by T, who asked whether
treating the region as trick maximisation would ignore branches where both bids
get set, and gave the case that settles it: a cover that wins every trick robs
both bids of one, so both SURVIVE. That outcome is rank 2, it is better than the
answer for whichever side reaches it, and the search must consider it.

The pinned rank is inherited by every node, but **a node's rank is only settled
once its mask is full**. Deal 5's census:

| region | share | is the rank settled there? |
|---|---:|---|
| both bids intact | 8.62% | no -- every outcome still reachable |
| one bid down | 52.35% | no -- the survivor's fate still moves it |
| both bids down | 39.04% | **yes** |

**So 61% of deal 5 is still a nil problem** and only the last 39% is the pure
trick count this item is about. Everything measured below is scoped to that 39%,
which was always right; the prose overreached and the measurement did not. The
gap between them is the kind that survives a review because the numbers check
out.

**How strong would a bound have to be?** Asked before re-expressing QuickTricks
or LaterTricks for this shape, because that is most of the work of shipping
them. A node's window admits a range of trick counts and a bound decides it only
by proving some side takes at least so many; that number falls out of the window
and the tricks left with no bound written. Measured on `opposed13.txt`:

| weakest sufficient claim | nodes | share of the region |
|---|---:|---:|
| already decided by arithmetic | 10,675,629 | 24.38% |
| **one trick** | **21,041,982** | **48.05%** |
| two tricks | 10,434,712 | 23.83% |
| three | 1,383,992 | 3.16% |
| four or more | 207,760 | 0.47% |
| nothing can help | 48,336 | 0.11% |

The region is 12.37% of all nodes at a trick boundary, and **71.88% of it needs
at most two tricks proven, with nearly half needing exactly one.**

**A DEAL WHERE THE REGION IS ALMOST THE WHOLE SEARCH, added at patch 85 as
`tests/corpus/opposed13_settled.txt`.** Deal 5's cards with the bids rotated onto
S and W instead of N and E: S holds KQ of spades under W's AJT9, so both bids are
dead before a card is played. Census 0.00% intact, 7.39% one down, **92.61% both
down**, against 8.62 / 52.35 / 39.04 for the same cards the other way round.

Two things follow. First, **the 4.7x gap between the two role assignments of one
deal -- 292,597,961 against 62,331,424 -- is not the solver being worse at one of
them**, it is that they are different questions: one has a nil problem occupying
61% of its tree and the other has none. Second, and more useful, **the easy one
still costs 62 million nodes with the nil question free**, because 92.6% of it is
a trick count with every bound switched off. That makes it the cleanest test case
in the repo for this item: anything that bounds the settled region should move it
more than any deal in `opposed13.txt`, and anything that does not move it is not
bounding the settled region. Its own ceiling is 35.87% of nodes at a boundary,
46.78% needing one trick and 30.77% needing two. One trick is
the weakest claim there is -- an ace, a top trump, a ruff -- and it is the first
thing DDS section 3 computes. On deal 5 alone the shape is the same or better:
15.91% of nodes, 49.56% needing one trick, 24.58% needing two.

**Set against item 81, which this file rejected one patch ago**, the contrast is
the whole argument: item 81 had a 19.08% population and a 10.8% firing rate
because its proofs missed the common middle case. Here the *demand* is 7x lower
-- one trick rather than a whole rank -- and the claim needed is the one every
double-dummy solver proves first.

**WHAT IS STILL UNMEASURED, and item 81 is the reason to say so plainly.** This
is a bound on how strong a proof must be, NOT a firing rate. It says a
one-trick claim would suffice on half the region; it does not say a one-trick
claim will be provable there. Item 81 died exactly in that gap. The honest next
step is a QuickTricks-shaped proof for the far side's trick count, measured with
`--opposed-stats` before it is spent, and the number to watch is how often it
FIRES, not how often it would suffice.

### 60. Nils on OPPOSING sides — ⭐⭐⭐

`0 3 0 3` was chosen first because it is the easy one: the two bidders are
partners, so the coalitions stay two-sided and ordinary minimax applies. Both
pairs bidding — `0 2 0 2`, or a bidder and a cover against a bidder and a cover
— does not have that property. Each side is simultaneously defending its own
bid and attacking the other's, and the objective stops being a single scalar one
side minimises and the other maximises.

**The Sturtevant and Korf paper in this repo is about precisely this.** Its
result is that `maxⁿ` admits only shallow pruning, and that a game whose sides
have genuinely independent objectives loses most of what alpha-beta buys. Before
building anything here, read it against this specific case and work out whether
the two-team structure survives — a deal where both pairs bid may still be
two-sided if the scoring makes the objectives strictly opposed, and if it does
not, the paper's asymptotic results say what to expect.

Not scheduled. Item 59 first, and its 13-card measurement will say a lot about
whether this one is affordable at all.

## Suggested sequence

```
1 ✅ → 2 ✅ → 3 ✅ → 4 ✅ → 5 ⊘ → 7 ✅ → 6a ✅ → 6b ✅ → 6c ⊘ → 6d ✅ → 15 ⊘ → 21 ✅ → 22 ✅ → 23 ✅ → 24 ✅ → 22b ✅ → 25 ✅ → 5 ⊘⊘ → 27 ✅ → 28 ⊘ → 28b ✅ → 29 ✅ → 30 ✅ → 33 ✅ → 28c ✅ → 34 ⊘ → 35 ✅ → 31a ✅ → 31b ⊘ → 32 ⊘ → 36 ✅ → 41 ✅ → 42 ⊘ → 47 ✅ → 43 ⊘→✅ → 43b ⊘ → 44 ⏸ → 54 ✅ → 58 ✅ → 56 ✅ → 57 ✅ → 59 ✅ → 61 ✅ → 62 ✅ → N1 ⊘ → C0 ✅ → C1 ⊘ → C2 ⊘ → C3 ⊘ → C4 → C5 ⏸ → C6 → O1 → 77 ✅ → 79 ✅ → 80 ✅ → 78a ✅ → 78b ✅ → 78c ✅ → 81 ⊘ → 82 ✅(ceiling) → 82b → 45 → 29b (single-suit) → 9 ⊘ → 10 ⊘ → measure → 11..14
```

**Three results off the move-ordering block, all negative, and the third one
found something the first two did not.** C2 established that an UNDOCUMENTED
ACCIDENT -- the move rotation trying a ruff first, because spades is suit 0 --
is worth about 12% of nodes at 13 cards, which is more than every heuristic in
this block put together. C3's old four-tier lead rule has since been split into C3-C6 so each tier is
measured as the separate bet it is; C3 is done and rejected, C4-C6 are unbuilt,
and C6 reuses the exact comparison C2 showed does nothing.

**Two of the first three results failed for OPPOSITE reasons.** N1 lost on nodes with throughput flat; C1
won on nodes at 13 cards and lost on throughput. Between them they bracket the
two ways an ordering item can fail here, and both are consistent with the
node-population sweep under item 29b: 87-91% of cutoffs already land on the
first move tried. C2 and C3 have smaller populations than C1 did.

**Where the opposed solver's time actually goes, measured at patch 76.** On the
42-second deal (`opposed13.txt` deal 5), against the SAME deal solved as a
single nil:

| arm | nodes |
|---|---:|
| single nil, same cards, bounds on | 26,954,201 |
| single nil, same cards, bounds off | 43,710,932 |
| **opposed, as shipped** | **378,616,612** |
| opposed, all six disabled bounds FORCED on (unsound) | 200,662,824 |

So the 14x gap is **1.89x missing bounds** and **7.44x the shape of the
problem**, and the shape dominates four to one. Forcing the six bounds on takes
42.1s to 23.3s and returns a WRONG answer (`tricks` 6 -> 2), so they are not
merely gated off, they are unsound with two bids and need re-deriving.

**The transposition table is not the problem.** Hit rates are 85.5% opposed
against 84.0-84.8% single-nil on the same deal; the four-state broken-nil mask
is not fragmenting anything. What is 8.2x larger is STORES -- distinct positions
reached.

**Why, in one line: breaking a nil ends the subtree in the single-nil solver and
does not end it here.** The single-nil search spends **100%** of its nodes with
the bid still intact, because the moment it breaks the bounds settle the
position. The opposed search spends **8.91%** there, 47.8% with one bid down and
**43.3% with both down**.

That reprices everything. All six disabled bounds prove things about *whether a
nil gets set*, and in 91% of the tree at least one outcome is already decided --
which is why forcing them on bought so little. The 43% with both bids down
contain no nil at all: they are ordinary two-team trick maximisation, which is
what [Haglund & Hein] sections 3 and 4 are bounds for and what this path has
none of. Both `later_tricks` and `quick_tricks` are written around a single
`ctx.nil_seat` and need re-deriving rather than re-enabling.

**C0 is done and is not evidence about anything.** It is a primitive with no
consumer: `duck_depth`, built and property-tested alone so that C1, C2 and C3
each measure one heuristic instead of a heuristic plus the number it reads. The
next result off this block is C1, and it is the first that can win or lose.

**The move-ordering block (N1, C0-C3, O1) is specified in `MOVE_ORDERING.md`,
and N1 is the first result off it.** It was placed first for being the smallest
diff with a recorded objection to overturn; it overturned the objection, proved
its condition exactly, and lost 1.62% of nodes at 13 cards. Read its entry under
*Evaluated and rejected* before starting C0 -- particularly the population split,
which says half of the nil bidder's following-suit nodes are FOURTH HAND, and
the note on which fixed points an ordering patch is allowed to move.

**Patch 47 comes before all of it, and it is not an optimisation.** The random
benchmark generator has been emitting an impossible spades-broken flag since
patch 45, so 13-card runs silently dropped a third to a half of their deals and
divided by the requested count anyway. Every per-seat 13-card figure banked
since patch 45 is on a truncated sample. The repaired baselines are at the
bottom of this file and **item 43's A/B must be taken against those**, not
against anything above.

**Item 43 (QuickTricks, DDS §3) is now next, ahead of 29b**, on a population
that item 32's own instrumentation already measured: the forced-trick proof
fires on **3 of 51,231** eligible boundaries in 13-card full mode, against 70.4%
of them being genuinely forced. That is the same 2.9%-versus-31% shape the
paragraph below describes for 29b, one order of magnitude worse, and against a
predicate whose consumer is already built and commented.

**Item 29b is next after it, and the case for it is measured rather than argued.** A
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

### Repaired 13-card baselines, patch 47

Everything above this heading that quotes a random 13-card figure was taken with
the generator emitting an impossible spades-broken flag, so between one and six
deals in eight never ran. These are the same seeds and the same deal stream with
the flag masked, 8 deals, 256 MiB, single core. **Compare against these.**

| workload | as HEAD reported | deals actually run | repaired, 8 deals |
|---|---:|---:|---:|
| 13c fast, seed 3 | 869,633 | 4 | **1,996,445** |
| 13c fast, seed 11 | 7,919,000 | 5 | **23,858,179** |
| 13c fast, seed 42 | 4,759,514 | 2 | **10,433,275** |
| 13c full max, seed 3 | 118,288,512 | 4 | **385,799,941** (44.8 s) |
| 13c full max, seed 11 | — | 5 | **689,596,598** (76.7 s) |
| 13c full max, seed 42 | — | 2 | **340,175,206** (33.7 s) |

Deals lost per run at HEAD, `--count 8`, by size and seed:

| cards | seed 1 | seed 3 | seed 11 | seed 42 |
|---|---:|---:|---:|---:|
| 11 | 0 | 0 | 0 | 1 |
| 12 | 2 | 2 | 1 | 3 |
| 13 | 6 | 4 | 3 | 6 |

12-card fast moves too -- seed 1 goes 8,137,206 -> 9,258,720, seed 42 goes
11,980,993 -> 12,158,582 -- and 11-card seed 42 moves by one deal
(6,537,357 -> 6,537,526). **Sizes at or below 11 on seeds 1, 3 and 11 are
byte-identical**, verified at 6, 9 and 11 cards, which is the check that the
deal stream did not move. The corpus and `large.txt` are untouched: neither
draws a random deal.

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
