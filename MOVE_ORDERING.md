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
spending the cover card unconditionally.

Nothing anywhere targets the secondary objective. Every rule above aims at
resolving the nil question fast.

---

## Proposed heuristics

Written as precisely as I can from the design discussion. Each needs a
population measurement before it is built — see the protocol at the end.

### N1. Nil bidder, following: include forced covers

**Today** 6a promotes the highest card that loses *as the trick stands*. A card
above the current best is never considered.

**Proposed** promote the highest card that either ducks the current trick **or**
that every remaining player is *forced* to cover.

**Why the recorded objection does not apply.** 6a's comment rejects the
speculative shed because "a later OPPONENT will decline to cover, letting the nil
bidder win." That is about a *voluntary* cover. A player who must follow suit and
holds only cards above the nil's card has no choice. Forced is not declined.

**The exact condition** for a card `c` above the current best to be safe: every
seat yet to play must hold at least one card in the led suit, and *all* of their
led-suit cards must beat `c`. A void later seat breaks it — it may discard
off-suit, which does not beat `c`, and the nil wins.

**Cost** this is not a bit scan against one card any more; it reads the remaining
seats' holdings in the led suit. Cheap in absolute terms, but 6a runs on a large
fraction of nodes, so throughput is the risk, not soundness.

### N2. Nil bidder, leading

Still nothing, and still open. Leading was built and measured with the rest of
6d and did nothing. Not on this list unless something new motivates it.

---

### C0. The shared primitive: duck depth under the cover hand

Three of the four cover rules need the same number: **in a given suit, how many
tricks can the nil duck underneath the cover hand?** Build and test this once,
before any rule that uses it.

Example: cover holds `JT87`, nil holds `954`. The nil can get under the cover
three times. Cover holds `KQ4`, nil holds `J96`: twice.

This is related to `cover_deficit_depth` but is not the same question — that one
measures a holding against the *outstanding* cards, this one measures two
specific holdings against each other.

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

1. **N1** — extends existing code, smallest diff, and has a specific recorded
   objection to overturn. A clean first result either way.
2. **C0** — a prerequisite, not a heuristic. Build and property-test it alone.
   Nothing downstream is trustworthy if this is wrong.
3. **C1** — the largest unserved node population in the table above.
4. **C2** — same machinery as C1, different case.
5. **C3** — most complex, smallest population, and four tiers means four things
   that could each be the reason it does or does not work.
6. **O1** — opponents following is a large population too, but the rule is the
   least specified of the set; worth doing after the cover rules have shown
   whether this style of heuristic pays here at all.

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

---

## Rejected, and why — do not re-investigate without new information

- **6c, cover partner ordering (original form)** — spent the cover card
  unconditionally.
- **Rotating the whole tail in suit mixing** — 77.4M nodes against 77.9M over
  three 13-card seeds, given back on throughput. Interleaved wall time 0.96x to
  1.03x against a flat 1.03x for the incumbent.
- **Nil bidder on lead (part of 6d)** — measured, did nothing.
- **Demoting certain winners** — already free: they are the high cards of the
  suit and the canonical order is ascending, so they are at the back already.
