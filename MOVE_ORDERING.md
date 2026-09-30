# Move ordering

A working document for the move-ordering effort. Everything here is either
already in `src/nil/search.cpp` or a proposal that has not been measured yet;
nothing below is a claim about what works.

Ordering never changes what a node is worth, only how fast the search finds it.
That makes every item here a pure speed question, and it means **a heuristic
with a good story and no measurement is worth nothing**.

---

## How ordering works today

**Two mechanisms, chosen per node by who is to play** (since the Sept 2026
ordering study below):

- **A seat with no live bid of its own** — the cover partner, every opponent,
  and any seat once its own bid is broken — gets a **whole scored list**:
  `trick_order_moves` in `search.cpp`, the double-dummy engine's own DDS-style
  rules with a live bidder's winning card read the right way round for each
  side. An opponent on lead against exactly one live bid puts **6b**'s attacking
  lead in front of that list. Both modes. `--no-trick-order` switches it off.
  In full mode a **killer move** rides on it: the move that last cut a scored
  node at the same depth goes SECOND in the list (`--no-killer-order`).
- **A seat holding a live bid** keeps the old mechanism: **one card lifted to
  the front** (6a following, 6d discarding), then suit mixing, then the
  canonical ascending tail. Not a sort; the loop costs a `take_lowest` per move.

A node with one legal move left after the equivalence collapse orders nothing
(free, node-identical, and common deep in the tree).

**Suit mixing** (item 35, DDS §5) now only runs where the old mechanism does:
one card from each present suit taken in rotation, then the canonical tail,
only where the seat has a free choice of suit. The tail is deliberately not
mixed; rotating it saved 0.7% of nodes and gave them back on throughput.

The shared primitive is **`cover_deficit_depth`** in `bounds.hpp`: how many of a
holding's own cards sit below the point where it runs short of covers, i.e. how
many times the suit must be led before the hand is stranded. Small is dangerous.
A card nobody can beat scores 0 whatever its rank; an ace behind three small ones
scores 3.

### What each role has

| Seat | Leading | Following | Discarding |
|---|---|---|---|
| Live nil bidder | *nothing* | **6a**: highest card that still loses | **6d**: highest still-losing card, from the suit closest to running out of covers |
| Cover partner of a live bid | trick score (cash a top card; never lead toward the nil's top card) | trick score (overtake a winning nil) | trick score |
| Opponents of a live bid | **6b** attacking lead first, then the trick score | trick score (leave a winning nil alone unless its partner can still overtake) | trick score |
| No live bid anywhere near | trick score | trick score | trick score |

With `--no-trick-order` the pre-study table applies: the cover partner has no
rule, opponents have 6b on lead and, in full mode only, `win_order`'s cheapest
winning card when a non-bidding opponent holds the trick.

---

## The solver-driven study (Sept 2026)

The question was not "which rule sounds right" but "which move does the exact
solver say cuts, at the nodes the real search visits", and every candidate was
then required to make the real solver faster. The lab tools (a batch front end
on `nil_solver_core`, an instrumented build, the generator and the analysis
scripts) are not in the branch, per the project's preference; the numbers are.

### Method

1. **Where the time goes.** An instrumented build counted, per node class
   (role of the seat to play, cards already on the trick, tricks left, which
   rule fired), how often a node cut, whether its FIRST move cut, and how many
   subtree nodes the failing moves before the cut cost ("waste"). Workload: the
   40 `bench_random13.py --seed 1` deals under per-card full scoring, the bot's
   call.
2. **Which move should have come first.** One expanded node in ~10,000 was
   sampled (one in ~3,000 at 8+ tricks, reweighted), 55,000 in all, recording
   the hands, the trick, the broken-bid mask and the window. Offline, each legal
   move of each sampled node was searched under that node's own window with a
   cold table, giving for every move whether it CUTS and what its subtree COSTS.
   Any candidate ordering can then be scored exactly on every sample: cost =
   the subtree sizes of the moves it tries up to and including the first one
   that cuts, against an oracle cost = the cheapest cutting move.
3. **Which move is best, statically.** A reproducible generator
   (fixed seeds) reached 3,660 positions by random or greedy legal play from
   uniform, nil-ish and seven kinds of pathological deal (high cards in one
   hand, two-suiters, voids, the nil holding top spades, the nil holding only
   low cards, spade-heavy hands, a weak cover), 4 to 13 cards, 0 to 3 cards on
   the trick, all four seat shapes. Every legal card of every position was
   scored by the solver's own per-card full call; a further 3,600 positions,
   400 per deal style, were labelled for the adversarial breakdown.
4. **Does it make the solver faster.** One binary, the arm toggled at run
   time, positions interleaved, and the answers of both arms compared row for
   row on every run.

### What the data said

**The heuristics were built for the wrong question.** Every rule in the table
above was designed and measured on MODE_FAST's boolean `[0, 1]` window. The
per-card call scores each row by MTD(f) null windows on the packed value --
nil outcome first, then the pair's tricks. Classifying each sampled node's
window against the value bands: **15% ask the nil question, 85% ask a trick
question** ("keeping the nil, can the pair still take x?" or "breaking it, can
the opponents still hold them to y?").

**Outside the nil bidder, the old order was no better than chance**:

| node (seed 1, per-card) | first move cuts, before | a random first move | after |
|---|---:|---:|---:|
| opponent on lead | 70.2% | 69% | 78.0% |
| cover on lead | 77.5% | 80% | 85.7% |
| opponent, third hand | 82.3% | 80% | 94.3% |
| opponent, fourth hand | 92.4% | 91% | 98.1% |
| nil bidder (6a/6d), any | 96.7-97.1% | 81-90% | 96.9-97.4% |

and statically, on the 3,660 labelled positions, the old order put an
exactly-best card first on **57%** of the positions where the choice matters
(all moves equal on 59% of them), against **51%** for a random card and
**77%** for the new order. Cover leads were worst of all: 27% before, 52%
random, 66% after. The nil bidder's shed is the one rule far above chance --
94-99% of its cut nodes cut on the first move -- and it is kept unchanged.

**The replacement that won is the double-dummy engine's own ordering, read
through the nil**: cash top cards, lead low toward partner's top card, win
cheaply when a win is sure, play low otherwise, do not overtake a partner who
has it. Two nil-specific corrections carried most of the rest, and both were
found in the sampled failures, not guessed:

- **A live bidder never chooses to overtake, and overtaking it is a gift to the
  other side.** So "is this winner sure?" and "does partner have the trick?"
  are judged against the seats still to play EXCLUDING a live bidder, and an
  opponent treats a winning nil as a partner's winning card: it plays low and
  leaves it there. -8.4% of nodes on 112 13-card deals, 21 better and 2 worse
  on seed 1.
- **...unless the nil's partner is still to play and can overtake it**, because
  then leaving it does not keep it there. The sampled failures of the previous
  rule were dominated by exactly this (e.g. `W:D3 N:D8`, East holds `K954`,
  South `T2` still to play: the low diamond lets South overtake; only `DK`
  cuts). -2.4% on every workload measured, none worse.

**A killer move helps -- in second place, in full mode.** The project had
never built one (ROADMAP item 10 expected the legal move set to churn too much).
Measured on top of the trick score: the last move that cut a scored node at
the same depth, tried FIRST, costs 68% more nodes on twelve 13-card deals -- a
sibling's refutation is a worse guess than the node's own score -- while tried
SECOND it saves 6.9% of nodes and 5.8% of wall time over 147 13-card deals, 55
better and 9 worse, the same sign on every workload. In MODE_FAST it is +1.8%
and stays off there; extending it to the live bidder's own nodes was +1.5%.

**And 6b survives, as a first move rather than as the order.** 6b followed by
the canonical order scored at chance. The trick score alone lost up to 43% on
the pathological deal where the nil must be broken to score at all. 6b's card
in front of the trick score was -6.9% of 13-card nodes on top of it (52 of
147 deals better by 2% or more, 27 worse), and -11% in MODE_FAST.

### What was tried and rejected, with the reason

| candidate | result | why it is out |
|---|---|---|
| Table move first at a partial hit (item 5, re-opened: MTD(f) rows make partial hits common) | -0.45% of nodes before the trick score, +0.6% after it | the scored order already puts that card first; confirms patch 26 |
| Trick score only at trick windows (window-gated) | the ungated score is 2.3% cheaper, 4 of 112 deals worse | the score also wins at nil windows |
| Trick score at leads only / follows only | full mode +3.4% (leads only); fast mode +2.7% and +29% | both halves carry weight, in both modes |
| The nil bidder by the trick score when the window says the nil is already expected to fail | geo-mean -13%, but seed 4's monster deal +56% | a tail risk on exactly the deals that matter |
| Opponents keep their low cards (play the highest loser, or highest winner, in a suit the nil holds) | offline -4% of cold cost; in the solver +7.7% | offline local cost is not the tree |
| 6b's attack only where the window needs the nil broken | pathological deals +26% | the attack also shortens trick questions |
| C5 (cover ducks the nil short) first, then the score | +9% / +13% on two seeds | stopped early |
| Killer move FIRST (rather than second) | +68% on twelve 13-card deals | a sibling's refutation beats the node's own score far less often than it is tried |
| Killer move at the live bidder's nodes too, or in MODE_FAST | +1.5% / +1.8% | 6a/6d's first move is already right 95-99% of the time |
| Ruff risk that ignores a live bidder's ruff | +/-1% | no signal |
| Cover lead: penalise a card the nil must play over; bonus for the nil's void | first-move rate up 3-5 points, cost UP | first-move accuracy is not the metric; a cheap refutation beats an expensive cut |

### What did NOT need changing

**The double-dummy engine's ordering is already close to optimal.** On 2,577
sampled engine nodes, its order costs 3.67M cold nodes against an oracle
3.33M (91%); leads first-cut 87% but the failing leads are cheap (lead cost
95% of oracle). Nothing to buy there.

### Cost of each rule

Measured on 3,000 nodes sampled from real searches, each rule timed alone in a
loop over them (harness overhead included, about 3-5 ns):

| rule | ns per call |
|---|---:|
| trick score (`trick_order_moves`) | 40 (leads 42, following or void 37) |
| 6a nil shed | 15 |
| 6d nil discard | 49 |
| 6b attacking lead | 33 |
| `win_order` cheapest win | 5 |
| suit mixing + canonical walk | 6 |
| killer lookup | one table read and a short shift, at the nodes the score runs |

The score is several times dearer than the one-card promotions it replaces --
about 30 ns more per non-bidder node with a real choice -- and it is still a
net win of about a quarter of the wall time, because it removes a third of the
nodes. The following-suit case, most of the nodes, is built from three masks
rather than scored card by card.

### Results

Per-card full scoring (`solve_moves`, the bot's call), one binary,
`--no-trick-order` (the tree before this study, exactly) against the default
(trick score, 6b first, killer second), each position run twice per arm with
the arms interleaved, best time kept; the double-dummy engine's table cleared
before every solve so neither arm inherits the other's. Linux, GCC 13 Release,
one core. **Every per-card row, trick count, bid mask and line was identical in
both arms on every position.**

| workload | positions | nodes | wall time | geo-mean nodes | better / worse (>2%) | under 1 s |
|---|---:|---:|---:|---:|---:|---:|
| 13 cards, `bench_random13 --seed 1` (`0 3 2 3`) | 40 | 325.7M -> 199.6M (**-38.7%**) | 37.0 -> 25.2 s (**-32.0%**) | 0.81 | 26 / 5 | 33 -> 34 |
| 13 cards, seed 3 | 24 | 319.2M -> 196.6M (-38.4%) | 39.7 -> 27.5 s (-30.8%) | 0.73 | 16 / 5 | 16 -> 17 |
| 13 cards, seed 4 | 24 | 501.7M -> 347.8M (-30.7%) | 69.9 -> 48.9 s (-30.0%) | 0.76 | 20 / 4 | 17 -> 18 |
| 13 cards, one bid per side (`0 0 2 3`) | 12 | 372.9M -> 238.3M (-36.1%) | 42.3 -> 31.0 s (-26.7%) | 0.58 | 9 / 2 | 4 -> 7 |
| 13 cards, partner nils (`0 3 0 3`) | 12 | 299.6M -> 237.3M (-20.8%) | 40.7 -> 34.8 s (-14.4%) | 0.80 | 11 / 1 | 6 -> 7 |
| 13 cards, pathological deals (7 kinds) | 35 | 425.3M -> 326.4M (-23.3%) | 47.9 -> 40.2 s (-15.9%) | 0.68 | 21 / 7 | 27 -> 29 |
| 13 cards, `opposed13.txt` (`0 0 3 2`) | 8 | 168.2M -> 134.8M (-19.8%) | 16.8 -> 14.4 s (-14.2%) | 0.68 | 7 / 1 | 3 -> 4 |
| **all 155 13-card deals** | 155 | 2,412.6M -> 1,680.9M (**-30.3%**) | 294.2 -> 222.0 s (**-24.5%**) | **0.73** | 110 / 25 | **106 -> 116** |
| 12-13 cards, mid-trick positions | 16 | 95.4M -> 64.7M (-32.1%) | 11.9 -> 8.8 s (-25.9%) | 0.65 | 12 / 3 | 12 -> 13 |
| 8-11 cards, mid-trick positions | 60 | 9.11M -> 6.55M (-28.1%) | 0.95 -> 0.74 s (-22.0%) | 0.76 | 41 / 12 | all |
| 4-7 cards, every shape | 400 | 1.16M -> 1.10M (-5.8%) | 0.115 -> 0.114 s (-0.8%) | 0.97 | 189 / 96 | all |

Of that, the killer is worth -6.9% of nodes and -5.8% of wall time measured on
its own (above); the rest is the trick score with 6b in front.

The median 13-card deal goes 0.58 s -> 0.37 s. The slowest deals before
(indices from zero): seed 4 #5 50.7 -> 33.0 s, partner #8 25.6 -> 21.7 s,
pathological #1 19.1 -> 18.5 s, one-bid-per-side #3 18.0 -> 8.9 s, seed 1 #27
14.2 -> 6.7 s, one-bid-per-side #1 11.4 -> **14.6 s**, seed 3 #10
10.3 -> 5.5 s, `opposed13` row 5 9.7 -> 8.4 s. **The losers, in full**: one
of the eight slowest gets slower (the one in bold, +28% of wall time, nodes
x1.15); the largest relative loss is a 0.18 s pathological deal that becomes
0.52 s (nodes x2.8); then one-bid-per-side #10 1.2 -> 2.1 s, `opposed13` row 8
1.3 -> 1.6 s, and seed 1 #19 0.77 -> 1.01 s, which crosses the one-second line
the other way.

**MODE_FAST** (`nil_fails` and every presolve), T's 400-deal
`nil13_verdict.txt`, same protocol: 377.1M -> 247.9M nodes (**-34.3%**),
33.4 -> 24.8 s (-25.8%), geo-mean 0.59, 192 better / 73 worse; every verdict
identical. (The killer is off in this mode, so this is the score and 6b alone.)

**Single-answer solves** (`nil_bench`, the banked baselines, nodes):
`opposed13` 166.6M -> 122.0M, `opposed13_real` 86.6M -> 56.8M, `large.txt`
full 14.7M -> 7.95M (its three 13-card rows 14.6M -> 7.85M, c13-0000 alone
13.1M -> 6.3M), `multinil.txt` 2.43M -> 2.33M. Three rows move up and are
listed in `tools/check_baselines.py` with their causes: `positions.txt` full
+3.0% (4-6 cards), `large.txt` fast +26.6% (one 9-card row, under 3 ms either
way), `opposed13_settled` +1.1%.

**Table and engine, seed 1** (the 40 deals above, both arms, counters from
the solve): nodes of the general search, the double-dummy engine's apart,
253.0M -> 136.0M (**-46%**); engine nodes 72.7M -> 63.6M (-12.5%) over
13.65M -> 8.38M hand-offs; table probes 77.9M -> 36.8M, hits 64.8M -> 29.0M.
The hit rate falls, 83.1% -> 78.7%: the lines the old order wandered into
transposed into one another, and those are the lines that are gone.
Evictions 75,647 -> 2,663; partial hits 2.4% -> 2.6% of probes.

**What the removed nodes were** (instrumented build, the trick score and 6b;
the killer was measured after this run). The general search expanded 164.6M
nodes before and 97.3M after (-41%); the share of cut
nodes whose first move cut rose from 93.2% to 95.5% -- a third of the misses
gone -- at every depth (92.6 -> 94.5% at 2-4 tricks left, 93.3 -> 95.8% at 5-7,
93.2 -> 95.7% at 8-10, 93.0 -> 95.0% at 11-13).

**Where the rest of the time is**, for whoever picks this up next, on the same
run after the change: 81% of nodes are the MTD(f) rows and 19% the
canonical re-derivation of each row's line; the double-dummy engine is about a
third of wall time. See "Not done" below.

### Not done

- **The line recovery** (19% of nodes). Asking each canonical candidate the
  one-sided null-window question instead of `(v-1, v+1)` -- a maximiser's
  children are all worth at most `v` -- is exact and no position of 172 measured
  got more than 2% worse, but it saves only 0.3% at 13 cards; left out of this
  patch.
- **The first row's MTD(f) guess.** Rows after the first start from the best
  row so far and settle in two probes; the first starts at 0 and can creep up
  the trick count one probe at a time (10+ probes). Those creeping probes turned
  out to be cheap -- they are far from the value -- so the measured ceiling is
  small; not built.
- **Opponent leads** are still the weakest class (78% first-move cut). Every
  static refinement tried there (suit length, the nil's void, partner's ruff,
  ruff threats that ignore the nil) was neutral or worse in the solver.
- Windows/MSVC timings; the shed (`--secondary min`) direction beyond the
  corpus and unit differentials.

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

### ~~C1. Cover partner, following suit~~

⊘ **Built, measured and rejected, patch 72.** Nodes roughly flat at 13 cards
(**−1.09%** aggregate, but **+7.78%** on seed 3) and throughput down **1.0–1.8%**.
Zero of six workloads a clean win. Full write-up at the bottom of this file.

- **Nil is safe for this trick** — either already under another card, or an
  upcoming player is forced to play something the nil can duck: play the
  **lowest** card.
- **Nil is winning** — highest card so far, and no forced cover coming: play the
  **cheapest card the nil can duck beneath**.

Following is roughly three nodes in four for this seat, so this is the largest
cover population by some way.

### ~~C2. Cover partner, discarding~~

⊘ **Built, measured and rejected, patch 73, and it found something.** As
specified it costs **+12.01% of nodes** across the three 13-card seeds. Held to
its own stated scope it does **nothing at all** — +0.08%. Full write-up at the
bottom of this file; the short version is that the rule spends its whole effect
displacing a ruff nobody knew was being tried first.

Discard from the **worst suit**: the suit where the nil can duck the fewest
tricks under the cover hand.

Example: on a diamond lead, cover holds `KQ` clubs and `54` hearts, nil holds
`A2` clubs and `Q32` hearts. Discard the low club — the nil can duck under the
cover once in clubs, twice in hearts.

Uses C0.

### ~~C3. Cover partner on lead: cash a winner into the nil's void~~

⊘ **Built, measured and rejected, patch 74.** **+2.25% of nodes** across the
three 13-card seeds; one of six workloads a clean win. Full write-up at the
bottom of this file.

On lead, holding a card nobody can beat in a suit the nil is **void** in: lead
it. The trick is won by this side and the nil must discard, which is a free
throw of its most dangerous card elsewhere.

### C4. Cover partner on lead: cash a winner in the nil's shortest suit

Same idea one step weaker — no void yet, but the nil is close to one, and
cashing there both wins the trick and shortens the suit toward a void.

### ~~C5. Cover partner on lead: the cheapest duckable card in the nil's shortest suit~~

⏸ **Built, measured and PARKED, patch 75.** Shipped behind an opt-in
`--cover-duck-short` with the arm **off**, which is unusual here and deliberate:
it is the only item in this block with two-sided signal. It won three of six
workloads on **every rep**, by 4.1%, 4.3% and 12.0% of nodes, and lost two of six
on every rep. Nothing came out neutral. Full write-up at the bottom of this file.

Lead the **cheapest card in the nil's shortest suit that the nil can duck**.
Nil's shortest is `96` hearts, cover holds `T732`: lead the `7`. Uses
`cheapest_cover_above` from C0.

### C6. Cover partner on lead: the top of the safest suit

Lead the top of the suit where the nil can duck the **most** tricks under the
cover hand. Uses `duck_depth` from C0.

Example: choosing between `KQ4` clubs and `JT87` diamonds with the nil holding
`J96` clubs and `954` diamonds, lead the `J` of diamonds — three ducks available
there against two in clubs.

**Read C2's entry before building this one.** C6 is the exact comparison C2 made
— rank the suits by `duck_depth` — and C2 measured it at +0.08% of nodes, which
is nothing. Lead nodes are 2.3–2.8% of all nodes, a smaller slice than the
discard nodes C2 had. Something would have to argue that lead choice is
different in kind before this is worth the build.

**These four were one item.** They are split because they are four separate
bets sharing a fallback chain, and measuring them as one number would have
recorded four arms as a single result. If any of them wins alone, the chained
combination is a fifth independent variable and needs its own measurement.

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
3. ~~**C1**~~ ⊘ *done, patch 72 — rejected.* It had the largest unserved node
   population in the table and the largest measured population of any item on
   this page. It still lost.
4. ~~**C2**~~ ⊘ *done, patch 73 — rejected.* Same machinery as C1, different
   case, and the first item on this page whose measurement was worth more than
   its heuristic.
5. ~~**C3**~~ ⊘ *done, patch 74 — rejected.* Split from the old four-tier item
   into C3–C6, because four tiers meant four things that could each be the
   reason it did or did not work. C3 is tier one; C4–C6 are unbuilt.
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
- **C1, cover partner following suit** — *won on nodes, lost on throughput*, the
  failure mode this page was written to catch. Full entry below.
- **C2, cover partner discarding** — *the heuristic does nothing and the rule as
  written displaces a ruff worth 12% of nodes.* Full entry below.
- **C3, cover partner cashing into the nil's void** — *the mechanism is real and
  the ordering still costs nodes.* Full entry below.

Not rejected, parked:

- **C5, cover partner ducking the nil short** — *the only two-sided result in the
  block.* Full entry below.
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

---

## C1 in full — the measurements, patch 72

### The population, measured before implementing

Counters on the shipped tree, promotion unchanged; every node count came back
equal to its banked baseline.

The question for C1 is sharper than it was for N1. The cover partner has no rule
at all, so it searches canonical ascending — lowest first. C1's first bullet
*also* says play the lowest. So the rule can only differ where the nil bidder is
winning **and** the partner's lowest card does not already cover it.

| workload | nodes | cover following | nil exposed | C1 differs | of cover nodes | of all nodes |
|---|---:|---:|---:|---:|---:|---:|
| **13c, seed 3** | 1,996,445 | 270,193 (13.5%) | 46,304 | 14,307 | 5.3% | **0.717%** |
| **13c, seed 11** | 23,858,179 | 2,380,910 (10.0%) | 492,350 | 286,032 | 12.0% | **1.199%** |
| **13c, seed 42** | 10,433,275 | 1,541,693 (14.8%) | 415,387 | 298,698 | 19.4% | **2.863%** |
| 12c, seed 3 | 2,360,615 | 288,548 (12.2%) | 70,785 | 12,401 | 4.3% | 0.525% |
| 11c, seed 3 | 14,104,592 | 1,662,185 (11.8%) | 138,786 | 33,460 | 2.0% | 0.237% |
| 11c, seed 42 | 6,537,526 | 660,579 (10.1%) | 72,204 | 24,054 | 3.6% | 0.368% |
| corpus, 560 | 39,701 | 3,175 (8.0%) | — | 52 | 1.6% | 0.131% |

**This is the largest population any item on this page has had** — 2.5 to 9
times N1's, and the reason C1 was worth building where N1's number alone would
have closed it. The nil bidder is safe on about five of six cover-following
nodes, which is why the rule is free there: it promotes nothing and the
canonical order is already correct.

### Nodes — one binary, `--no-cover-follow` the only difference

| seed | off | on | change |
|---|---:|---:|---:|
| 13c, 3 | 1,996,445 | 2,151,854 | **+7.78%** |
| 13c, 11 | 23,858,179 | 23,491,097 | −1.54% |
| 13c, 42 | 10,433,275 | 10,248,850 | −1.77% |
| **13-card total** | **36,287,899** | **35,891,801** | **−1.09%** |
| 12c, 3 | 2,360,615 | 2,514,674 | +6.53% |
| 12c, 42 | 12,158,582 | 14,620,969 | **+20.25%** |
| 11c, 3 | 14,104,592 | 13,982,207 | −0.87% |
| 11c, 42 | 6,537,526 | 6,565,291 | +0.42% |
| corpus | 39,701 | 39,631 | −0.18% |
| `large.txt` | 49,084 | 48,739 | −0.70% |

As with N1, the aggregate is one or two deals. `r13-0003` alone is all of seed
3's +7.78%; `r12-0002` goes 4,256,094 → 6,835,111, +60.6%, and is all of 12c
seed 42. Seed 42 at 13 cards is the exception and moves four deals, all small.

### Wall clock — four interleaved paired reps, arm toggled at runtime

| workload | nodes | reps won | throughput |
|---|---:|---:|---:|
| **13c, seed 3** | +7.78% | 2/4 | +1.5% |
| **13c, seed 11** | −1.54% | 1/4 | **−1.8%** |
| **13c, seed 42** | −1.77% | 3/4 | **−1.5%** |
| 11c, seed 3 | −0.87% | 1/4 | −1.0% |
| 11c, seed 42 | +0.42% | 0/4 | −1.4% |
| corpus | −0.18% | 2/4 | −0.6% |

**Zero of six clean.** Seed 11 is the case that decides it: the rule takes 1.54%
of the nodes off and gives 1.8% of the throughput back, so the two cancel and
the reps split 1/4.

### Why it failed, and it is not the same reason N1 failed

**C1 is the canonical failure this page was written to catch.** N1 lost on nodes
with flat throughput. C1 wins on nodes at 13 cards and loses on throughput —
exactly the shape of every ordering item rejected before this block existed.

The per-node cost was attacked once before the verdict was recorded. The first
implementation scanned the trick to find the nil bidder's card; the second
computes its index as `(nil_seat - leader) & 3`, which is a subtraction and a
mask. **Node counts came back bit-identical**, confirming same tree and same
heuristic, and the throughput did not recover — seed 11 went −1.8% to −1.8%. So
the cost is not the scan. It is the branch, `trick_best_card`, and the led-suit
mask, charged to the **8–15% of all nodes** where this seat follows suit, in
order to change the move on 0.2–2.9% of them. That ratio is the whole story, and
no cheaper spelling of the same rule closes it.

*What would revive it.* A version that costs nothing on the five-in-six nodes
where the nil bidder is already safe. The current gate cannot know that without
computing `trick_best_card` and locating the nil bidder's card, which is most of
the work. If some caller upstream already has both facts to hand, C1 becomes
nearly free and is worth re-measuring; nothing in `order_moves` has them today.

*Kept from this patch.* `cheapest_cover_above` in `bounds.hpp`, the second half
of C0 — the query C1 used and C3 tier 3 still wants. Property-tested alongside
`duck_depth` over the same exhaustive sweep. Nothing reads it; the binaries are
byte-identical to HEAD.

---

## C2 in full — the measurements, patch 73

### The population, and the thing it turned up

Counters on the shipped tree; every node count came back equal to its baseline.

| workload | nodes | cover discarding | real choice of suit | C2 picks a different suit | of choice nodes | of all nodes |
|---|---:|---:|---:|---:|---:|---:|
| **13c, seed 3** | 1,996,445 | 47,733 (2.4%) | 45,300 | 13,296 | 29.4% | 0.666% |
| **13c, seed 11** | 23,858,179 | 1,164,467 (4.9%) | 1,093,114 | 841,268 | **77.0%** | 3.526% |
| **13c, seed 42** | 10,433,275 | 432,469 (4.2%) | 366,180 | 112,677 | 30.8% | 1.080% |

A 77% disagreement rate should have been the first warning. The incumbent order
at these nodes is not a heuristic — it is `take_next_suit` starting from
`suit_cursor = 3`, which reaches suit 0 first, and **suit 0 is spades**. So
whenever the cover partner is void in the led suit and holds a trump, today's
first move is a **ruff**, and C2 displaces it.

| workload | a ruff is available | no ruff, a real discard choice | C2 differs there |
|---|---:|---:|---:|
| 13c, seed 3 | 36,040 (80%) | 9,260 | 3,328 — **0.167% of nodes** |
| 13c, seed 11 | 1,042,982 (95%) | 50,132 | 35,262 — **0.148% of nodes** |
| 13c, seed 42 | 333,067 (91%) | 33,113 | 9,098 — **0.087% of nodes** |

So the rule as specified is two changes wearing one name: a suit choice among
discards, on 0.09–0.17% of nodes, and a decision to stop ruffing first, on
almost all the rest.

### Nodes, and the control that separates them

Both arms on one binary, `--no-cover-discard` the only difference.

| seed | off | C2 as specified | C2 with the ruff left first |
|---|---:|---:|---:|
| 3 | 1,996,445 | 2,199,219 (**+10.16%**) | 1,998,353 (+0.10%) |
| 11 | 23,858,179 | 25,714,523 (**+7.78%**) | 23,878,111 (+0.08%) |
| 42 | 10,433,275 | 12,731,659 (**+22.03%**) | 10,439,807 (+0.06%) |
| **total** | **36,287,899** | **40,645,401 (+12.01%)** | **36,316,271 (+0.08%)** |

**The duck heuristic contributes nothing.** Held to the nodes where there is no
ruff to displace — its own stated scope, a discard choice between discards — it
moves the node count by less than a tenth of a percent on every seed. No wall
clock was run on it, because a rule that changes 0.09–0.17% of nodes and moves
none of them has nothing for the clock to measure.

The other column is the finding. C2 is rejected on both.

### What this says about C0

`duck_depth` is correct — property-tested over its whole input space — and this
is the first evidence about whether it is *useful*, which is a different
question. On the one consumer built so far, ranking suits by it is
indistinguishable from ranking them by suit number. C3 tier 4 proposes the same
comparison for lead choice and should be expected to behave the same way unless
something argues otherwise.

### The accident, now pinned

The ruff-first ordering at void nodes is worth roughly 12% of nodes at 13 cards
and **nothing decided it**: it falls out of spades being suit 0 in `cards.hpp`
meeting `int suit_cursor = 3` in `order_moves`. Renumber the suits, or start the
cursor elsewhere, and it goes away silently — the answers stay correct and the
benchmark gets slower.

This patch leaves the behaviour alone and pins the mechanism: a comment at the
cursor initialiser recording the measured cost, and unit tests asserting that
the rotation reaches spades first and leaves the cursor there. A future
`take_next_suit` that breaks it now fails a test instead of a benchmark.

**It is also the largest single effect this whole block has measured** — larger
than N1, C1 and C2 put together, and it was already in the code.

---

## C3 in full — the measurements, patch 74

Tier one of the old four-tier lead rule, now its own item.

### The population

| workload | nodes | cover on lead | rule fires | ruff-proof | promotes differently | of lead nodes | of all nodes |
|---|---:|---:|---:|---:|---:|---:|---:|
| **13c, seed 3** | 1,996,445 | 55,651 (2.8%) | 18,270 | 17,256 | 1,394 | 2.5% | **0.070%** |
| **13c, seed 11** | 23,858,179 | 544,698 (2.3%) | 220,685 | 162,343 | 176,794 | 32.5% | **0.741%** |
| **13c, seed 42** | 10,433,275 | 252,503 (2.4%) | 80,562 | 60,372 | 50,662 | 20.1% | **0.486%** |

Roughly three quarters of the firings are ruff-proof — no opponent void in the
suit with a trump left — so the winner mostly is one.

The spread is the thing to notice: 0.07% on seed 3 against 0.74% on seed 11. On
seed 3 the rule fires 18,270 times and changes the move only 1,394 of them,
because the incumbent's lowest-card-of-the-lowest-suit already lands on the same
card when the cover holds a singleton there.

### Nodes — one binary, `--no-cover-cash-void` the only difference

| seed | off | on | change |
|---|---:|---:|---:|
| 13c, 3 | 1,996,445 | 2,017,422 | +1.05% |
| 13c, 11 | 23,858,179 | 24,684,143 | **+3.46%** |
| 13c, 42 | 10,433,275 | 10,403,130 | −0.29% |
| **13-card total** | **36,287,899** | **37,104,695** | **+2.25%** |
| 11c, 3 | 14,104,592 | 14,649,198 | +3.86% |
| 11c, 42 | 6,537,526 | 5,912,542 | **−9.56%** |
| corpus | 39,701 | 39,904 | +0.51% |

One deal again: `r13-0002` goes 1,293,640 → 2,281,815, +76.4%, and is the whole
of seed 11.

### Wall clock — four interleaved paired reps

| workload | nodes | reps won | throughput |
|---|---:|---:|---:|
| **13c, seed 3** | +1.05% | 2/4 | +1.1% |
| **13c, seed 11** | +3.46% | **0/4** | −1.3% |
| **13c, seed 42** | −0.29% | 1/4 | −0.7% |
| 11c, seed 3 | +3.86% | 0/4 | −0.1% |
| 11c, seed 42 | −9.56% | **4/4** | −1.3% |
| corpus | +0.51% | 3/4 | +1.3% |

**One of six clean**, and it is an 11-card workload. The three 13-card seeds are
2/4, 0/4 and 1/4.

### Why it failed

Not for lack of mechanism. 11 cards seed 42 is a real 9.56% node win, four reps
of four — when the rule helps it helps a lot, which is more than N1 or C2's duck
heuristic ever managed. The problem is variance: the same rule costs 3.46% at 13
cards seed 11 and 3.86% at 11 cards seed 3, and on a population under 1% of
nodes the outcome is decided by whichever deal happens to be largest.

**The rule promotes a HIGH card where the incumbent promotes a low one**, which
is the same qualitative change C2 stumbled into with the ruff — and there it was
worth 12%. So the direction is not obviously wrong; the selection is. Cashing
into a void wins the trick and buys the nil bidder a discard, but it also strips
the cover partner of a control card, and the search finds out which mattered
only several plies down.

*What would revive it.* A condition that separates the seed-42 shape from the
seed-11 shape. Nothing in the population counters does: firing rate, ruff-proof
share and lead-node share are all within a factor of two across the seeds that
disagree by 13 percentage points of nodes.

*Not measured.* C4, C5 and C6. C6 in particular reuses the `duck_depth` suit
comparison that C2 measured at +0.08%.

---

## C5 in full — the measurements, patch 75

### The population

The most consistent in the block, and the largest apart from C1's.

| workload | nodes | cover on lead | rule fires | promotes differently | of lead nodes | of all nodes |
|---|---:|---:|---:|---:|---:|---:|
| **13c, seed 3** | 1,996,445 | 55,651 (2.8%) | 32,919 | 16,163 | 29.0% | **0.810%** |
| **13c, seed 11** | 23,858,179 | 544,698 (2.3%) | 385,198 | 262,824 | 48.3% | **1.102%** |
| **13c, seed 42** | 10,433,275 | 252,503 (2.4%) | 182,748 | 141,960 | 56.2% | **1.361%** |

It fires on 59–72% of cover-lead nodes and changes the move on about half of
them. Where it declines: the cover partner cannot lead the nil bidder's shortest
suit (about a fifth of these nodes), or holds nothing above the nil bidder's
lowest card there (about a fifteenth).

**The shortest suit is tied between two or more suits on roughly 40% of them**,
and those ties are currently broken by the enumeration's own rotation order.
That is the loosest joint in the rule and the first place to look next.

### Nodes and wall clock — four interleaved paired reps, one binary

| workload | nodes | reps won | rep ratios | throughput |
|---|---:|---:|---|---:|
| **13c, seed 3** | **−4.06%** | **4/4** | 0.988 / 0.949 / 0.951 / 0.963 | +0.1% |
| **13c, seed 11** | +2.37% | **0/4** | 1.029 / 1.052 / 1.072 / 1.048 | −0.6% |
| **13c, seed 42** | **−4.34%** | **4/4** | 0.961 / 0.956 / 0.979 / 0.973 | −1.2% |
| 11c, seed 3 | +6.01% | **0/4** | 1.033 / 1.047 / 1.047 / 1.064 | +0.6% |
| 11c, seed 42 | **−12.03%** | **4/4** | 0.881 / 0.912 / 0.893 / 0.877 | +0.3% |
| corpus | +1.27% | 3/4 | — | +1.3% |

13-card aggregate is +0.09%, which is the least informative number here: seed 11
is two thirds of the workload by nodes, so it decides an average that hides two
4% wins.

### Why this one is parked rather than rejected

**Throughput is flat** — +1.3% to −1.2%, no consistent sign — so this is not
C1's failure. And it is not N1's or C2's either, where the rule moved nothing:
here every workload moves by at least 2.4%, and three of them move by 4% or
more. **Nothing came out neutral, which is the finding.**

The other four items in this block produced results decided by one deal on an
otherwise unmoved field. C5 moves several deals per workload — at seed 11,
`r13-0002` is +50.6% but `r13-0004` is −5.1% and `r13-0006` is +1.5%, so the
rule is broadly active and the sign varies within a single workload as well as
between them.

That is a rule doing something real with a condition attached that nobody has
identified yet. It fails the bar and it is not noise, so it is kept behind an
opt-in flag rather than deleted.

### The next experiment, named

**The tie-break.** The nil bidder's shortest suit is tied on ~40% of firings and
those ties currently go to rotation order — which is to say, to spades first,
the same accident C2 found was worth 12% of nodes. A rule whose sign flips
between workloads, with an arbitrary choice on two fifths of its firings, has an
obvious first suspect.

Three tie-breaks worth measuring, each alone: the suit where the cover partner's
covering card is cheapest; the suit where `duck_depth` is highest, so the cover
can keep doing this; and the longest cover holding, for the same reason.

*What was not measured.* Whether the win comes from the *cheapest* half of the
rule or the *shortest suit* half. Leading the cheapest duckable card in a suit
chosen by rotation order would separate them, and it is one flag.
