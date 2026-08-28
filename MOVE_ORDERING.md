# Move ordering

A working document for the move-ordering effort. Everything here is either
already in `src/nil/search.cpp` or a proposal that has not been measured yet;
nothing below is a claim about what works.

Ordering never changes what a node is worth, only how fast the search finds it.
That makes every item here a pure speed question, and it means **a heuristic
with a good story and no measurement is worth nothing**.

---

## How ordering works today

**One card is lifted to the front.** Not a sort. Everything else keeps the
canonical ascending order it always had. That is deliberate: the loop costs a
`take_lowest` per move, and both ordering optimisations this project has
rejected lost on *throughput*, not on nodes.

On top of that sits **suit mixing** (item 35, DDS §5): one card from each present
suit taken in rotation, then the canonical tail. Only where the seat has a free
choice of suit — leading, or void in the suit led. The tail is deliberately not
mixed; rotating it saved 0.7% of nodes and gave them back on throughput.

The shared primitive is **`cover_deficit_depth`** in `bounds.hpp`: how many of a
holding's own cards sit below the point where it runs short of covers, i.e. how
many times the suit must be led before the hand is stranded. Small is dangerous.
A card nobody can beat scores 0 whatever its rank; an ace behind three small ones
scores 3.

### What each role has

| Seat | Leading | Following | Discarding |
|---|---|---|---|
| Nil bidder | *nothing* | **6a**: highest card that still loses | **6d**: highest still-losing card, from the suit closest to running out of covers |
| Cover partner | *nothing* | *nothing* | *nothing* |
| Opponents | **6b**: lowest card of the suit where the nil is closest to running out of covers | *nothing* | *nothing* |

The cover partner has **no heuristic at all**. That was item 6c, rejected for
spending the cover card unconditionally. C1 to C3 are the proposal to give it
one; `duck_depth` (item C0, patch 71) is the measure all three read, and it is
built and tested but unread.

Nothing anywhere targets the secondary objective. Every rule above aims at
resolving the nil question fast.

---

## Proposed heuristics

Written as precisely as I can from the design discussion. Each needs a
population measurement before it is built — see the protocol at the end.

### ~~N1. Nil bidder, following: include forced covers~~

⊘ **Built, property-tested, measured and rejected, patch 70.** The forced-cover
condition is exactly true and the promotion is worth nothing: **+1.62% nodes**
across the three 13-card seeds. Full measurements in the rejected list at the
bottom of this file and in ROADMAP.md.

### N2. Nil bidder, leading

Still nothing, and still open. Leading was built and measured with the rest of
6d and did nothing. Not on this list unless something new motivates it.

---

### ~~C0. The shared primitive: duck depth under the cover hand~~

✅ **Built and property-tested, patch 71.** `duck_depth(nil, cover, suit)` in
`bounds.hpp`. Nothing reads it yet, which is the point: it ships alone so that
C1, C2 and C3 each measure one heuristic rather than a heuristic plus a
primitive.

Three of the four cover rules need the same number: **in a given suit, how many
tricks can the nil duck underneath the cover hand?**

Example: cover holds `JT87`, nil holds `954`. The nil can get under the cover
three times. Cover holds `KQ4`, nil holds `J96`: twice.

**The definition, stated exactly**, because four documented examples do not pin
one: it is the size of the largest set of pairs in which each of the nil
bidder's cards is matched to a **distinct** strictly higher card of the cover
partner's. *Distinct* is the whole content — one high cover card shelters one
nil card, not every card beneath it. `A` opposite `987` is one duck, not three.

This is related to `cover_deficit_depth` but is not the same question — that one
measures a holding against the *outstanding* cards, this one measures two
specific holdings against each other. They are the same combinatorics read two
ways, and Hall's theorem is the bridge: the deficiency form says the largest
matching is `m - max_j(j - above_j)`, so a holding this call matches in full is
exactly one the other reports as covered. The property test asserts that
agreement so the two cannot drift.

**What it does not claim.** Not that these tricks will happen — the suit may
never be led that often, an opponent may win over both hands, and the cover
partner may have better uses for the card. It measures *supply*, in the same
spirit as `cover_deficit_depth`, and ruffs are outside it because the question is
asked per suit.

**How it was verified**, since it has no consumer and so no corpus row and no
node count that would notice a mistake:

- **Exhaustive, not sampled.** All 3^13 = 1,594,323 ways to deal one suit
  between the two hands, in each of the four suits — **6,377,292 holdings**,
  which is every question the function can be asked about one suit.
- **Three independent computations agree on all of them**: the shipped greedy
  descent, an unconstrained maximum bipartite matching that assumes nothing
  about the problem's structure, and Hall's deficiency formula.
- **Invariants on every holding**: bounded by both suit lengths; monotone in each
  hand separately; zero exactly when no cover card outranks the nil's lowest; and
  **invariant under an order-preserving relabelling of the ranks**, which matters
  because the transposition table stores relative ranks.
- **The test was mutation-checked.** Seven deliberate breakages of `duck_depth`;
  six were caught. The survivor is the void early-out, which is documented as an
  optimisation rather than a case — the walk returns 0 without it — so surviving
  is the correct result and it confirms the comment.
- **Free when unused, provably.** `nil_bench` and `nil_cli` build **byte-identical
  to HEAD** with the primitive added. Not "node counts unmoved"; the same bytes.

`tools/duck_depth_property.cpp`, wired as ctest `duck_depth_property`, ~7 s.

**One thing deliberately left out, and it is a scoping question for C1.** C1 and
C3 tier 3 both want a second, smaller query — *the cheapest cover card the nil
can duck beneath* (nil `96`, cover `T732`: the `7`). That is not duck depth and
building it here would have made C0 two primitives measured as one. It is two
lines wherever it lands; the question is whether it belongs in `bounds.hpp`
beside this one or inside C1.

**The walk is the plain thirteen-rank one**, not a version bounded by the two
holdings. Deliberate: nothing calls it, so a tighter loop could not be measured,
and this page is emphatic that ordering work here lives or dies on throughput.
Tighten it in C1, against a consumer.

### C1. Cover partner, following suit

- **Nil is safe for this trick** — either already under another card, or an
  upcoming player is forced to play something the nil can duck: play the
  **lowest** card.
- **Nil is winning** — highest card so far, and no forced cover coming: play the
  **cheapest card the nil can duck beneath**.

Following is roughly three nodes in four for this seat, so this is the largest
cover population by some way.

### C2. Cover partner, discarding

Discard from the **worst suit**: the suit where the nil can duck the fewest
tricks under the cover hand.

Example: on a diamond lead, cover holds `KQ` clubs and `54` hearts, nil holds
`A2` clubs and `Q32` hearts. Discard the low club — the nil can duck under the
cover once in clubs, twice in hearts.

Uses C0.

### C3. Cover partner, leading

A four-tier rule, in order:

1. Cash a winner in a suit the **nil is void in**.
2. Otherwise cash a winner in the **nil's shortest suit**.
3. Otherwise lead the **cheapest card in the nil's shortest suit that the nil can
   duck**. (Nil's shortest is `96` hearts, cover holds `T732`: lead the `7`.)
4. Otherwise lead the **top of the safest suit** — the suit where the nil can duck
   the most tricks under the cover hand.

Example for tier 4: choosing between `KQ4` clubs and `JT87` diamonds with the nil
holding `J96` clubs and `954` diamonds, lead the `J` of diamonds — three ducks
available there against two in clubs.

Uses C0. The most complex of the four, and lead nodes are the smallest slice.

---

### O1. Opponents, following suit

When the opponent cannot get under the nil, or the nil is not winning and this
opponent is last to play: play its **highest** card in the suit.

More opponent rules to come; this is the one that is clearly specified.

---

## Suggested order, and why

The order is about *what can be measured cheaply and attributed cleanly*, not
about which sounds most promising.

1. ~~**N1**~~ ⊘ *done, patch 70 — rejected.* It was first because it extended
   existing code and had a recorded objection to overturn. It overturned the
   objection and lost anyway; see the rejected list.
2. ~~**C0**~~ ✅ *done, patch 71.* A prerequisite, not a heuristic; built and
   property-tested alone, with no consumer and no A/B.
3. **C1** — the largest unserved node population in the table above.
4. **C2** — same machinery as C1, different case.
5. **C3** — most complex, smallest population, and four tiers means four things
   that could each be the reason it does or does not work.
6. **O1** — opponents following is a large population too, but the rule is the
   least specified of the set; worth doing after the cover rules have shown
   whether this style of heuristic pays here at all.

**Read N1's result before starting C1.** It is the first item on this list to
fail on NODES rather than on throughput, and the reason generalises: ROADMAP's
node-population sweep found that **87–91% of cutoffs already land on the first
move tried**, so what is left for any ordering heuristic lives in the 9–13% of
cutoffs that do not — a move or two apiece. N1 promoted a strictly better card
on the nodes it touched and still came out behind. The cover rules have a larger
population than N1 did, which is the case for trying them; they do not have a
larger ceiling.

**Do these on the SINGLE-NIL solver first.** That is where the heuristics are
phrased, where a mature 13-card baseline exists, and where a 560-row corpus and a
19-row large corpus will catch a mistake. The multi-nil shapes have two bidders
and, in the twin case, no cover partner at all, so every rule needs a separate
mapping — and mapping an unmeasured heuristic is guesswork on top of guesswork.

---

## Measurement protocol

Non-negotiable, because ordering is the area where this project has most often
been wrong.

**Arms in isolation.** Every heuristic ships behind its own `--no-X` flag and is
measured alone before being combined. A preliminary combined measurement has
previously masked an individual failure.

**High card counts are what matter.** A change that helps at 11–13 cards and
costs a little at 4–6 is a *good* change. The reverse is not. Report 13-card
numbers first and treat small-hand regressions as acceptable unless they are
large.

**Nodes are necessary, wall time is decisive.** Every rejected ordering item so
far lost on throughput while winning on nodes. Report both, always, and expect
the node win to be the easy half.

**Wall time requires interleaved paired reps on one binary**, with the arm
toggled at runtime. Container clock drift makes sequential timing unreliable.
Four paired reps minimum; every rep should be a win, not just the mean.

**A measurement is only valid against the tree it was taken on.** Any
tree-changing patch invalidates banked results. Re-measure before adjusting
constants.

**Population before implementation.** For each item, first count how often the
rule would promote a *different* card from the one canonical order already puts
first. If that is a small fraction of nodes, the ceiling is small and the item
can be closed without writing it.

**Fixed points that must not move**, unless the change is deliberate and the new
values are re-verified against the oracle: 39,701 fast / 278,059 full on
`tests/corpus/positions.txt`, 49,084 on `large.txt`.

**But note what that means for an ordering item specifically, because it is not
what it means elsewhere on this project.** Every heuristic on this page fires on
the corpus, so *any* of them that ships moves the fast number by construction —
N1 would have made it 39,669 and `large.txt` 49,054. **And it moves the FULL
number too**, which is the part that surprises: full mode does not reorder, but
`278,059` includes the nodes of the `MODE_FAST` presolve that bounds its root
window, and the presolve does. N1 would have made it 277,914, with
`--no-presolve` confirming the movement is entirely there.

So for these six items the fixed points are **answer** anchors, not node
anchors: all 560 oracle values and all 19 `large.txt` rows must still match, and
the node counts are the thing being changed. The node anchor that does survive
an ordering patch is the arm switched OFF — `--no-X` must reproduce 39,701 /
278,059 / 49,084 exactly, which is what says the arm is inert when off rather
than merely small.

---

## Rejected, and why — do not re-investigate without new information

- **N1, forced covers for the nil bidder's shed** — *the objection was wrong and
  the heuristic still loses.* Full entry below.
- **6c, cover partner ordering (original form)** — spent the cover card
  unconditionally.
- **Rotating the whole tail in suit mixing** — 77.4M nodes against 77.9M over
  three 13-card seeds, given back on throughput. Interleaved wall time 0.96x to
  1.03x against a flat 1.03x for the incumbent.
- **Nil bidder on lead (part of 6d)** — measured, did nothing.
- **Demoting certain winners** — already free: they are the high cards of the
  suit and the canonical order is ascending, so they are at the back already.

---

## N1 in full — the measurements, patch 70

Kept here rather than only in ROADMAP.md because the next five items on this
page are the same shape and will be judged the same way.

### The population, measured before implementing

Instrumentation only: the counters ran on the shipped tree without changing what
was promoted, and every node count came back equal to its banked baseline, which
is what says the measured tree is the real one.

| workload | nodes | nodes where 6a runs | N1 promotes a different card | of 6a nodes | of all nodes |
|---|---:|---:|---:|---:|---:|
| **13c, 8 deals, seed 3** | 1,996,445 | 355,244 (17.8%) | 5,296 | 1.49% | **0.265%** |
| **13c, 8 deals, seed 11** | 23,858,179 | 4,097,819 (17.2%) | 79,550 | 1.94% | **0.333%** |
| **13c, 8 deals, seed 42** | 10,433,275 | 1,873,054 (18.0%) | 33,056 | 1.77% | **0.317%** |
| 12c, seed 3 | 2,360,615 | 388,783 (16.5%) | 20,483 | 5.27% | 0.868% |
| 12c, seed 42 | 12,158,582 | 1,526,997 (12.6%) | 35,763 | 2.34% | 0.294% |
| 11c, seed 3 | 14,104,592 | 2,226,611 (15.8%) | 63,804 | 2.87% | 0.452% |
| 11c, seed 42 | 6,537,526 | 1,027,748 (15.7%) | 23,948 | 2.33% | 0.366% |
| corpus, 560, fast | 39,701 | 6,031 (15.2%) | 256 | 4.25% | 0.645% |

**Where the population goes**, at 13 cards seed 11, of the 4,097,819 nodes where
6a runs:

| | nodes | share |
|---|---:|---:|
| a ruff is already on the trick — 6a already promotes everything | 327,837 | 8.0% |
| **the nil bidder plays fourth — no seat behind it to be forced** | 1,971,826 | **48.1%** |
| some later seat is void in the led suit — it may discard instead | 735,653 | 18.0% |
| every later seat follows suit — the gate opens | 1,062,503 | 25.9% |

Half of 6a's nodes are fourth hand, where "every remaining seat is forced to
cover" is vacuously true and completely wrong. And **even where the gate opens,
only 7.5% of those nodes change card**: the threshold must clear the card
currently winning the trick, and the nil bidder must actually hold something
between the two.

One thing the breakdown got right that the write-up did not predict: in the
majority of the differing population **6a had declined outright** — every legal
card wins the trick as it stands — so N1 is mostly supplying an ordering where
there was none rather than overriding one. 77,013 of 79,550 at seed 11, 4,156 of
5,296 at seed 3, 16,722 of 33,056 at seed 42.

### The condition is exactly true

Brute force over 400,000 random positions: every card N1 promotes strictly above
the card winning the trick, checked against **every** legal continuation by the
seats still to play. **8,082 promotions above the current best, zero cases where
the nil bidder could win the trick.** Forced is not declined, as the item said.

A refinement was measured and dropped with it: a void later seat holding nothing
but trumps is *also* forced, since it must ruff. It adds 1–2% to the differing
population (79,550 → 80,474 at seed 11) and nothing to the argument.

### Nodes — one binary, `--no-forced-covers` the only difference

**13 cards first.**

| seed | off | on | change |
|---|---:|---:|---:|
| 3 | 1,996,445 | 1,981,863 | −0.73% |
| 11 | 23,858,179 | 24,706,139 | **+3.55%** |
| 42 | 10,433,275 | 10,189,299 | −2.34% |
| **total** | **36,287,899** | **36,877,301** | **+1.62%** |

| workload | off | on | change |
|---|---:|---:|---:|
| 12c, seed 3 | 2,360,615 | 2,373,662 | +0.55% |
| 12c, seed 42 | 12,158,582 | 12,154,240 | −0.04% |
| 11c, seed 3 | 14,104,592 | 14,842,715 | **+5.23%** |
| 11c, seed 42 | 6,537,526 | 6,544,323 | +0.10% |
| corpus, fast | 39,701 | 39,669 | −0.08% |
| `large.txt` | 49,084 | 49,054 | −0.06% |

**Every workload's entire movement is one deal.** Seven of the eight deals at
seed 11 move by less than 0.001%; `r13-0004` goes 6,535,990 → 7,383,874, +13.0%,
and that one deal is the whole +3.55%. Same shape everywhere: `r11-0004` is all
of 11c seed 3 (+5.4%), `r13-0002` all of 13c seed 42 (−9.7%), `r13-0004` all of
13c seed 3 (−1.3%). Two deals up, two down, on a population of 0.3% of nodes.
**There is no signal here to tune** — the aggregate is decided by whichever
single deal happens to be biggest.

### Wall clock — four interleaved paired reps, one binary, arm toggled at runtime

| workload | nodes | reps won | best ratio | throughput |
|---|---:|---:|---:|---:|
| **13c, seed 3** | −0.73% | 3/4 | 0.987 | +0.5% |
| **13c, seed 42** | −2.34% | 3/4 | 0.962 | +1.5% |
| **13c, seed 11** | +3.55% | 2/4 | **1.030** | +0.5% |
| 11c, seed 3 | +5.23% | **0/4** | **1.071** | −1.7% |
| 11c, seed 42 | +0.10% | 1/4 | 1.002 | −0.1% |
| corpus, fast | −0.08% | 3/4 | 0.982 | +1.8% |

Three of six workloads fail *every rep a win*, including the largest 13-card
seed. 11 cards seed 3 is not noise: 4 reps of 4 lost, ratios 1.086 / 1.072 /
1.071 / 1.070, and a −1.7% throughput cost on top of the node loss.

### Why this one is worth reading even though it failed

**It is the first ordering item on this project to fail on nodes.** Throughput is
flat — between +1.8% and −1.7% — because the rule is gated on there *being* a
card above the trick's best card, which is one mask against a set already in
hand, so the lookahead is never charged to the nodes it cannot help. Every
previously rejected ordering item won on nodes and gave it back on throughput.
This one paid almost nothing per node and still lost, which means the failure is
in the heuristic rather than in its cost, and a cheaper implementation of the
same rule would not rescue it.

**And it is what the banked ceiling predicts.** ROADMAP's node-population sweep
found 87–91% of cutoffs already landing on the first move tried. A rule that
changes the first move on 0.3% of nodes is drawing from what is left of the
other 9–13%, and at that size the tail behaviour of individual deals is larger
than the effect. 6a's own history says the same thing from the other direction:
it moved seed 11 by +201% on one deal while winning 24.6% overall.

*What would revive it.* Nothing about the condition, which is exact. A workload
where the nil bidder is materially more often *not* fourth to the trick would
raise the ceiling, and nothing in the corpus or the random generator produces
one.
