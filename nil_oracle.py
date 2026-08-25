#!/usr/bin/env python3
"""
spades_oracle.py -- a deliberately naive brute-force double-dummy oracle for
Spades card play, written to validate a separate (fast) solver.

WHAT IT COMPUTES
----------------
Given a four-hand layout, a leader, a spades-broken flag, and a *designated*
player, play out the hand under a LEXICOGRAPHIC objective:

    PRIMARY    the designated player's trick count.
               North and South MINIMIZE it, East and West MAXIMIZE it.

    SECONDARY  each pair's own trick count, used only to break ties in the
               primary.  The direction is a parameter:
                   secondary="max"   each pair takes as many as it can
                   secondary="min"   each pair takes as few as it can

The primary is NOT ordinary trick maximization.  A side will happily throw away
a trick of its own if doing so forces a trick onto the designated player; the
secondary only chooses among lines that are already equally good for the
primary.

Both components are strictly opposed -- the two pairs' trick counts sum to a
constant, so N/S taking more is identical to E/W taking fewer -- which is why a
single flag can set a coherent direction for both sides at once, and why plain
minimax over the packed pair is still well defined.

nil_already_set=True drops the primary objective entirely.  Use it once the nil
has actually been broken in the real game: there is nothing left to protect or
to attack, and only the secondary objective matters.

The sides are fixed by seat parity, not by the designated player's seat.  If
the designated player is North, then North itself plays to minimize its own
trick count.

DELIBERATELY ABSENT
-------------------
No alpha-beta.  No transposition table (see --memo for an opt-in exception,
described in solve()).  No move ordering, no quick-trick shortcuts, no
rank-equivalence collapsing.  The point of this file is that it is structurally
unlike, and independent of, the solver it is checking.

TIE-BREAKING
------------
Candidate moves are always enumerated in the canonical card order
(suit index, then rank), where suits are ordered S < H < D < C and ranks
2 < ... < A.  A move replaces the incumbent best only on a STRICT improvement,
so among equal-valued moves the canonically lowest card wins.  The principal
variation is therefore reproducible across runs and platforms.

THE FORCED SPADE LEAD
---------------------
The stated rule is "spades break when a spade is played on a trick where the
player was void in the led suit."  Read literally, a *forced spade lead* (a
player holding nothing but spades leads one while spades are unbroken) does NOT
break spades, because the leader is not void in the led suit.

This module used to implement that reading, with the other convention behind a
flag, and warned that picking the wrong one produces a one-card PV divergence
deep in a hand.  Both readings are gone: playing a spade breaks spades, always.
That is how the game is scored where this solver is used, it removes the only
place two defensible answers existed, and it means the oracle and the C++
solver can no longer be configured to disagree.
"""

from __future__ import annotations

import argparse
import random
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, NamedTuple, Sequence, Tuple

# ---------------------------------------------------------------------------
# Cards, suits, seats
# ---------------------------------------------------------------------------

SUIT_CHARS = "SHDC"           # index 0..3; PBN order: spades, hearts, diamonds, clubs
SPADES = 0
RANK_CHARS = "23456789TJQKA"  # rank value == index + 2, i.e. 2..14
SEAT_CHARS = "NESW"           # index 0..3, clockwise


class Card(NamedTuple):
    """A card.  Natural tuple ordering IS the canonical tie-break order."""

    suit: int  # 0..3, index into SUIT_CHARS
    rank: int  # 2..14

    def __str__(self) -> str:
        return SUIT_CHARS[self.suit] + RANK_CHARS[self.rank - 2]


FULL_DECK: Tuple[Card, ...] = tuple(
    Card(s, r) for s in range(4) for r in range(2, 15)
)

Play = Tuple[int, Card]  # (seat, card)


def card_from_str(text: str) -> Card:
    """Parse 'SA', 'HT', 'D10', 'c7' (suit letter first)."""
    t = text.strip().upper().replace("10", "T")
    if len(t) != 2:
        raise ValueError(f"bad card {text!r}")
    suit = SUIT_CHARS.find(t[0])
    rank = RANK_CHARS.find(t[1])
    if suit < 0 or rank < 0:
        raise ValueError(f"bad card {text!r}")
    return Card(suit, rank + 2)


def seat_from_str(text: str) -> int:
    seat = SEAT_CHARS.find(text.strip().upper()[:1])
    if seat < 0:
        raise ValueError(f"bad seat {text!r} (expected one of N, E, S, W)")
    return seat


def cards_str(cards: Sequence[Card]) -> str:
    return " ".join(str(c) for c in cards)


# ---------------------------------------------------------------------------
# The four rules, each isolated in one small function
# ---------------------------------------------------------------------------


def legal_moves(
    hand: Tuple[Card, ...],
    trick: Tuple[Card, ...],
    spades_broken: bool,
) -> Tuple[Card, ...]:
    """Legal plays for `hand`, given the cards already on the current trick.

    `hand` must be sorted in canonical order; the result preserves that order,
    which is what makes the PV tie-break deterministic.
    """
    if not trick:
        # Leading.  May not lead spades until broken, unless spades are all
        # that remain.
        non_spades = tuple(c for c in hand if c.suit != SPADES)
        if not spades_broken and non_spades:
            return non_spades
        return hand

    led = trick[0].suit
    follow = tuple(c for c in hand if c.suit == led)
    return follow if follow else hand


def spades_broken_after(
    spades_broken: bool,
    trick: Tuple[Card, ...],
    card: Card,
) -> bool:
    """Update the broken flag after `card` is added to `trick`."""
    # Playing a spade breaks spades.  A spade led while they are unbroken can
    # only be a forced lead, since legal_moves permits a voluntary one only once
    # they are broken; a spade played to a non-spade lead is a ruff or discard.
    # `trick` is unused and kept so callers read naturally at the call site.
    del trick
    return spades_broken or card.suit == SPADES


def _beats(candidate: Card, incumbent: Card) -> bool:
    """Does `candidate` beat the current best card on the trick?

    `incumbent` is always either the led card or a spade, so a card of any
    third suit is a discard and cannot win.
    """
    if candidate.suit == SPADES and incumbent.suit != SPADES:
        return True
    if candidate.suit != incumbent.suit:
        return False
    return candidate.rank > incumbent.rank


def trick_winner(leader: int, cards: Sequence[Card]) -> int:
    """Seat that wins a complete trick led by `leader` (cards in play order)."""
    best = 0
    for i in range(1, len(cards)):
        if _beats(cards[i], cards[best]):
            best = i
    return (leader + best) % 4


# ---------------------------------------------------------------------------
# Positions
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Position:
    """A position at any point in a hand.

    hands          : indexed by absolute seat (N=0, E=1, S=2, W=3), each tuple
                     sorted in canonical order.  Cards already played to the
                     current trick must NOT appear here.
    leader         : seat that led the current trick.
    spades_broken  : as of this position.
    current_trick  : cards already played to the in-progress trick, in play
                     order starting from `leader`.  Empty for a trick boundary.
    """

    hands: Tuple[Tuple[Card, ...], ...]
    leader: int = 0
    spades_broken: bool = False
    current_trick: Tuple[Card, ...] = ()

    # -- construction helpers ------------------------------------------------

    @staticmethod
    def build(
        hands: Sequence[Sequence[Card]],
        leader: int = 0,
        spades_broken: bool = False,
        current_trick: Sequence[Card] = (),
    ) -> "Position":
        pos = Position(
            hands=tuple(tuple(sorted(h)) for h in hands),
            leader=leader,
            spades_broken=spades_broken,
            current_trick=tuple(current_trick),
        )
        pos.validate()
        return pos

    # -- invariants ----------------------------------------------------------

    def validate(self) -> None:
        if len(self.hands) != 4:
            raise ValueError("need exactly four hands")
        if not 0 <= self.leader < 4:
            raise ValueError("leader out of range")
        if len(self.current_trick) > 3:
            raise ValueError("current_trick must hold 0-3 cards")

        seen: Dict[Card, str] = {}
        for seat, hand in enumerate(self.hands):
            for c in hand:
                if c in seen:
                    raise ValueError(
                        f"duplicate card {c} in {SEAT_CHARS[seat]} and {seen[c]}"
                    )
                seen[c] = SEAT_CHARS[seat]
        for c in self.current_trick:
            if c in seen:
                raise ValueError(f"card {c} is both on the trick and in {seen[c]}")
            seen[c] = "trick"

        # Everyone held the same number of cards at the start of this trick.
        played = len(self.current_trick)
        to_play = (self.leader + played) % 4
        base = len(self.hands[to_play])
        for offset in range(4):
            seat = (self.leader + offset) % 4
            want = base - 1 if offset < played else base
            if len(self.hands[seat]) != want:
                raise ValueError(
                    "inconsistent hand sizes: "
                    + ", ".join(
                        f"{SEAT_CHARS[s]}={len(self.hands[s])}" for s in range(4)
                    )
                    + f" with {played} card(s) already on the trick"
                )

        # A position with cards on the trick must be self-consistent with the
        # follow-suit rule; we do not re-derive history, but we do check that
        # the pre-played cards could have been legal given what is left.
        if played:
            led = self.current_trick[0].suit
            for offset in range(1, played):
                seat = (self.leader + offset) % 4
                card = self.current_trick[offset]
                if card.suit != led and any(c.suit == led for c in self.hands[seat]):
                    raise ValueError(
                        f"{SEAT_CHARS[seat]} played {card} off-suit while still "
                        f"holding {SUIT_CHARS[led]}"
                    )

    @property
    def cards_per_hand(self) -> int:
        return max(len(h) for h in self.hands)

    @property
    def tricks_remaining(self) -> int:
        return self.cards_per_hand

    @property
    def to_play(self) -> int:
        return (self.leader + len(self.current_trick)) % 4

    def to_pbn(self, first_seat: int = 0) -> str:
        return deal_to_pbn(self.hands, first_seat)


# ---------------------------------------------------------------------------
# PBN parsing / writing
# ---------------------------------------------------------------------------


def parse_hand(text: str) -> Tuple[Card, ...]:
    """Parse one PBN hand, e.g. 'AQT643.T.QJ864.8' or 'K752.QJ8762.A95.'.

    Suits are given in the order spades.hearts.diamonds.clubs.  A void suit is
    an empty string (or a lone '-').  A whole hand of '-' (PBN's "not given")
    is treated as empty, which is useful for shortened mid-play deals.
    """
    text = text.strip()
    if text in ("", "-"):
        return ()
    parts = text.replace("10", "T").split(".")
    if len(parts) != 4:
        raise ValueError(
            f"hand {text!r} has {len(parts)} suit group(s); expected 4 "
            "(spades.hearts.diamonds.clubs)"
        )
    cards: List[Card] = []
    for suit, part in enumerate(parts):
        for ch in part.strip():
            if ch == "-":
                continue  # some writers use '-' for a void
            idx = RANK_CHARS.find(ch.upper())
            if idx < 0:
                raise ValueError(f"bad rank {ch!r} in hand {text!r}")
            cards.append(Card(suit, idx + 2))
    return tuple(sorted(cards))


def parse_pbn(text: str) -> Tuple[Tuple[Card, ...], ...]:
    """Parse a PBN deal string into hands indexed by absolute seat (N,E,S,W).

    Example: 'N:8.K5.KT2.KQT9762 AQT643.T.QJ864.8 J9.A943.73.AJ543 K752.QJ8762.A95.'
    Hands run clockwise from the named seat.  Shortened hands are accepted.
    """
    text = text.strip()
    if len(text) < 2 or text[1] != ":":
        raise ValueError("PBN deal must start with a seat letter and a colon, e.g. 'N:'")
    first = seat_from_str(text[0])
    groups = text[2:].split()
    if len(groups) != 4:
        raise ValueError(f"expected 4 hands, got {len(groups)}")

    hands: List[Tuple[Card, ...]] = [()] * 4
    for offset, group in enumerate(groups):
        hands[(first + offset) % 4] = parse_hand(group)
    return tuple(hands)


def deal_to_pbn(hands: Sequence[Sequence[Card]], first_seat: int = 0) -> str:
    groups = []
    for offset in range(4):
        hand = hands[(first_seat + offset) % 4]
        by_suit = [
            "".join(
                RANK_CHARS[c.rank - 2]
                for c in sorted(hand, reverse=True)
                if c.suit == suit
            )
            for suit in range(4)
        ]
        groups.append(".".join(by_suit))
    return SEAT_CHARS[first_seat] + ":" + " ".join(groups)


# ---------------------------------------------------------------------------
# The search
# ---------------------------------------------------------------------------


@dataclass
class _Ctx:
    designated: int
    primary_weight: int          # K*K, or 0 when the nil is already set
    secondary_weight: int        # +/-K: N/S want their side's tricks, or want rid of them
    tertiary_weight: int         # 1 when the cover's share is what counts, else 0
    memo: Optional[Dict] = None
    nodes: int = 0


def objective_weights(
    tricks_remaining: int,
    secondary: str = "max",
    nil_already_set: bool = False,
) -> Tuple[int, int, int]:
    """Return (primary, secondary, tertiary) weights for the objective.

    The search returns ONE integer that N/S minimize and E/W maximize:

        value = primary   * (tricks the designated player takes)
              + secondary * (tricks N/S take, both partners together)
              + tertiary  * (tricks the designated player takes)

    with K = tricks_remaining + 1, primary = K*K and secondary = +/-K, so each
    level is strictly larger than anything the levels below it can contribute
    and the three are compared lexicographically.

    LEVEL 1, primary.  The designated player's trick count.  Zero when the nil
    is already set, which switches the whole level off.

    LEVEL 2, secondary.  The N/S pair's trick count.  Negative when they want
    tricks (minimizing the value maximizes them), positive when they want rid of
    them.

    LEVEL 3, tertiary.  The designated player's trick count again, always in the
    direction "N/S would rather their partner took it".  This is what separates
    three tricks to the nil bidder and one to its partner from one and three: to
    the partner's bid only the partner's tricks count, so among lines where the
    pair takes the same total, the pair prefers the nil bidder to take fewer.

    Level 3 is inert whenever level 1 is on, because level 1 has already pinned
    the designated player's count -- so it changes nothing for a live nil.  It
    is zero in the "min" direction, because bags accrue to the pair whoever won
    the trick, which makes the two partners' tricks genuinely interchangeable
    there.  The one case it bites is a nil that is already set while the pair is
    still trying to take tricks.

    A caveat about that case.  With the nil set, "the pair maximizes the
    partner's tricks" and "the opponents maximize their own" are not strictly
    opposed: both sides would rather the nil bidder took nothing, so the split
    between the two partners is not a tug of war, it is slack that only one side
    cares about.  Level 3 sits BELOW the pair's total on purpose, so the
    opponents' objective stays exactly "take as many as we can" and the split is
    resolved against the pair -- making the reported partner count the one the
    pair can guarantee rather than the one it might get if the opponents helped.
    """
    if secondary not in ("max", "min"):
        raise ValueError("secondary must be 'max' or 'min'")
    k = tricks_remaining + 1
    primary = 0 if nil_already_set else k * k
    secondary_weight = -k if secondary == "max" else k
    tertiary = 1 if secondary == "max" else 0
    return primary, secondary_weight, tertiary


def _search(
    hands: Tuple[Tuple[Card, ...], ...],
    leader: int,
    trick: Tuple[Card, ...],
    spades_broken: bool,
    ctx: _Ctx,
) -> Tuple[int, Tuple[Play, ...]]:
    """Return (objective value from here on, PV from here).

    The seat to play is (leader + len(trick)) % 4.  Seats N and S minimize the
    returned value; seats E and W maximize it.  See objective_weights for what
    the value means.
    """
    ctx.nodes += 1

    if not any(hands):
        return 0, ()

    key = None
    if ctx.memo is not None:
        key = (hands, leader, trick, spades_broken)
        cached = ctx.memo.get(key)
        if cached is not None:
            return cached

    seat = (leader + len(trick)) % 4
    maximizing = (seat % 2) == 1  # E and W

    best_value: Optional[int] = None
    best_pv: Tuple[Play, ...] = ()

    for card in legal_moves(hands[seat], trick, spades_broken):
        next_hands = tuple(
            tuple(c for c in hand if c != card) if s == seat else hand
            for s, hand in enumerate(hands)
        )
        next_broken = spades_broken_after(
            spades_broken, trick, card
        )
        played = trick + (card,)

        if len(played) == 4:
            winner = trick_winner(leader, played)
            gained = 0
            if winner == ctx.designated:
                gained += ctx.primary_weight + ctx.tertiary_weight
            if winner % 2 == ctx.designated % 2:   # the N/S pair, whichever it is
                gained += ctx.secondary_weight
            sub_value, sub_pv = _search(next_hands, winner, (), next_broken, ctx)
            value = gained + sub_value
        else:
            value, sub_pv = _search(next_hands, leader, played, next_broken, ctx)

        # Strict improvement only, so ties keep the canonically lowest card.
        better = (
            best_value is None
            or (value > best_value if maximizing else value < best_value)
        )
        if better:
            best_value = value
            best_pv = ((seat, card),) + sub_pv

    assert best_value is not None, "a non-terminal position must have a legal move"
    result = (best_value, best_pv)
    if ctx.memo is not None:
        ctx.memo[key] = result
    return result


class Tally(NamedTuple):
    """Who took how many tricks along a line."""

    designated: int      # tricks the designated (nil) player takes
    designated_side: int # tricks the designated player and its partner take
    opponents: int       # tricks the other pair takes


@dataclass
class Solution:
    tricks: int              # designated player's tricks; the primary objective
    side_tricks: int         # designated player + partner
    opponent_tricks: int
    value: int               # the raw lexicographic scalar the search minimized
    pv: List[Play]
    nodes: int
    designated: int
    position: Position
    secondary: str = "max"
    nil_already_set: bool = False


def solve(
    position: Position,
    designated: int,
    use_memo: bool = False,
    secondary: str = "max",
    nil_already_set: bool = False,
) -> Solution:
    """Exhaustive minimax over a lexicographic objective.

    Primary (unless nil_already_set): the designated player's trick count.
    N/S minimize it, E/W maximize it, exactly as before.

    Secondary, used only to break ties in the primary:
        secondary="max"  each pair takes as many tricks as it can
        secondary="min"  each pair takes as few tricks as it can

    "Each pair" is the honest phrasing: because the two pairs' trick counts sum
    to a constant, N/S maximizing their own is identical to E/W minimizing N/S,
    so one flag sets a coherent direction for both sides at once.

    nil_already_set=True drops the primary objective.  Use it once the nil has
    actually been broken in the real game: there is nothing left to protect or
    to attack, and only the secondary objective matters.

    use_memo=False by default, matching the "no transposition table" brief.
    Setting it True caches _search results keyed on the FULL state (all four
    hands, leader, cards on the current trick, broken flag).  Because _search
    is a pure function of exactly that state, the cache changes neither the
    value nor the PV -- it is memoization of a pure function, not a search
    enhancement.  It is here only because 7-card hands are otherwise slow in
    Python.  Leave it off if you want the search to be maximally dumb.
    """
    position.validate()
    if not 0 <= designated < 4:
        raise ValueError("designated out of range")

    primary_weight, secondary_weight, tertiary_weight = objective_weights(
        position.tricks_remaining, secondary, nil_already_set
    )
    ctx = _Ctx(
        designated=designated,
        primary_weight=primary_weight,
        secondary_weight=secondary_weight,
        tertiary_weight=tertiary_weight,
        memo={} if use_memo else None,
    )
    value, pv = _search(
        position.hands,
        position.leader,
        position.current_trick,
        position.spades_broken,
        ctx,
    )

    # Self-check: an oracle that lies is worse than no oracle.  Replaying the PV
    # recovers the trick counts independently, and re-encoding them must land
    # back on the value the search reported.
    tally = replay_pv(position, list(pv), designated)
    replayed = (
        (primary_weight + tertiary_weight) * tally.designated
        + secondary_weight * tally.designated_side
    )
    if replayed != value:
        raise AssertionError(
            f"internal inconsistency: search says {value}, replaying the PV gives {replayed} "
            f"(designated={tally.designated}, side={tally.designated_side})"
        )

    return Solution(
        tricks=tally.designated,
        side_tricks=tally.designated_side,
        opponent_tricks=tally.opponents,
        value=value,
        pv=list(pv),
        nodes=ctx.nodes,
        designated=designated,
        position=position,
        secondary=secondary,
        nil_already_set=nil_already_set,
    )


def replay_pv(
    position: Position,
    pv: Sequence[Play],
    designated: int,
) -> Tally:
    """Independently replay a PV, checking every play for legality.

    Returns a Tally of who took what.  Raises on any illegal or out-of-turn
    play, or if the PV does not exhaust every hand.  This is the verifier for
    the search, and it is also useful for checking a PV produced by the solver
    under test.
    """
    hands = [list(h) for h in position.hands]
    leader = position.leader
    broken = position.spades_broken
    trick = list(position.current_trick)
    designated_tricks = 0
    side_tricks = 0
    opponent_tricks = 0

    for ply, (seat, card) in enumerate(pv):
        expected = (leader + len(trick)) % 4
        if seat != expected:
            raise ValueError(
                f"ply {ply}: {SEAT_CHARS[seat]} played out of turn "
                f"(expected {SEAT_CHARS[expected]})"
            )
        if card not in hands[seat]:
            raise ValueError(f"ply {ply}: {SEAT_CHARS[seat]} does not hold {card}")
        allowed = legal_moves(tuple(sorted(hands[seat])), tuple(trick), broken)
        if card not in allowed:
            raise ValueError(
                f"ply {ply}: {SEAT_CHARS[seat]} played {card}, which is illegal; "
                f"legal: {cards_str(allowed)}"
            )
        hands[seat].remove(card)
        broken = spades_broken_after(
            broken, tuple(trick), card
        )
        trick.append(card)
        if len(trick) == 4:
            winner = trick_winner(leader, trick)
            if winner == designated:
                designated_tricks += 1
            if winner % 2 == designated % 2:
                side_tricks += 1
            else:
                opponent_tricks += 1
            leader = winner
            trick = []

    if trick:
        raise ValueError("PV ends mid-trick")
    if any(hands):
        raise ValueError("PV does not play out every card")
    return Tally(designated_tricks, side_tricks, opponent_tricks)


# ---------------------------------------------------------------------------
# Random fixtures
# ---------------------------------------------------------------------------


@dataclass
class Fixture:
    position: Position
    designated: int
    seed: int

    def to_pbn(self) -> str:
        return self.position.to_pbn()


def random_fixture(
    seed: int,
    cards_per_hand: int = 5,
    leader: Optional[int] = None,
    designated: Optional[int] = None,
    spades_broken: Optional[bool] = None,
) -> Fixture:
    """Deal `cards_per_hand` cards to each seat from one deck, no duplicates.

    Intended range is 4-7 cards; smaller values are allowed because they are
    handy for tests.  Everything not pinned by an argument is drawn from the
    seeded RNG, so a seed fully determines the fixture.
    """
    if not 1 <= cards_per_hand <= 13:
        raise ValueError("cards_per_hand must be 1..13 (4..7 is the intended range)")

    rng = random.Random(seed)
    dealt = rng.sample(list(FULL_DECK), 4 * cards_per_hand)
    hands = [
        tuple(sorted(dealt[i * cards_per_hand : (i + 1) * cards_per_hand]))
        for i in range(4)
    ]

    if leader is None:
        leader = rng.randrange(4)
    if designated is None:
        designated = rng.randrange(4)
    if spades_broken is None:
        spades_broken = bool(rng.getrandbits(1))

    return Fixture(
        position=Position.build(hands, leader=leader, spades_broken=spades_broken),
        designated=designated,
        seed=seed,
    )


# ---------------------------------------------------------------------------
# Output formatting
# ---------------------------------------------------------------------------


def _tricks_from_pv(position: Position, pv: Sequence[Play]):
    """Yield (trick_no, leader, [(seat, card, from_pv)], winner)."""
    plays: List[Tuple[int, Card, bool]] = [
        ((position.leader + i) % 4, c, False)
        for i, c in enumerate(position.current_trick)
    ]
    plays += [(seat, card, True) for seat, card in pv]

    leader = position.leader
    for i in range(0, len(plays), 4):
        chunk = plays[i : i + 4]
        cards = [c for (_, c, _) in chunk]
        winner = trick_winner(leader, cards) if len(cards) == 4 else None
        yield (i // 4 + 1, leader, chunk, winner)
        if winner is None:
            break
        leader = winner


def format_pv(solution: Solution) -> str:
    """One line per trick -- line-oriented so `diff` localizes a divergence."""
    lines = []
    running = 0
    for trick_no, leader, chunk, winner in _tricks_from_pv(
        solution.position, solution.pv
    ):
        cells = []
        for seat, card, from_pv in chunk:
            mark = " " if from_pv else "*"
            cells.append(f"{SEAT_CHARS[seat]}:{str(card):<3}{mark}")
        if winner == solution.designated:
            running += 1
        tag = SEAT_CHARS[winner] if winner is not None else "?"
        star = " <-- designated" if winner == solution.designated else ""
        lines.append(
            f"  T{trick_no}  {' '.join(cells)}  won by {tag}"
            f"   [{SEAT_CHARS[solution.designated]}={running}]{star}"
        )
    return "\n".join(lines)


def format_pv_compact(solution: Solution) -> str:
    """Bare play sequence, e.g. 'W:H4 N:HK E:H2 S:H9 N:S3 ...'."""
    return " ".join(f"{SEAT_CHARS[s]}:{c}" for s, c in solution.pv)


def format_hands(position: Position) -> str:
    lines = []
    for seat in range(4):
        by_suit = [
            "".join(
                RANK_CHARS[c.rank - 2]
                for c in sorted(position.hands[seat], reverse=True)
                if c.suit == suit
            )
            or "-"
            for suit in range(4)
        ]
        cells = " ".join(f"{SUIT_CHARS[s]}:{by_suit[s]}" for s in range(4))
        lines.append(f"  {SEAT_CHARS[seat]}  {cells}")
    return "\n".join(lines)


def format_solution(solution: Solution, compact: bool = False) -> str:
    pos = solution.position
    if compact:
        return (
            f"tricks={solution.tricks}\n"
            f"side_tricks={solution.side_tricks}\n"
            f"opponent_tricks={solution.opponent_tricks}\n"
            f"pv={format_pv_compact(solution)}\n"
        )
    objective = (
        "secondary only (nil already set)"
        if solution.nil_already_set
        else f"{SEAT_CHARS[solution.designated]}'s tricks, then"
    )
    direction = "each pair takes as many as it can" if solution.secondary == "max" else (
        "each pair takes as few as it can"
    )
    out = [
        f"PBN            {pos.to_pbn()}",
        format_hands(pos),
        f"Leader         {SEAT_CHARS[pos.leader]}",
        f"Designated     {SEAT_CHARS[solution.designated]}"
        f"  (N/S minimize, E/W maximize)",
        f"Objective      {objective} {direction}",
        f"Spades broken  {'yes' if pos.spades_broken else 'no'}",
    ]
    if pos.current_trick:
        out.append(f"On the trick   {cards_str(pos.current_trick)}  (marked * below)")
    ns = "NS" if solution.designated % 2 == 0 else "EW"
    ew = "EW" if ns == "NS" else "NS"
    out += [
        f"Tricks for {SEAT_CHARS[solution.designated]}   {solution.tricks}"
        f" of {pos.tricks_remaining}",
        f"Side tricks    {ns}={solution.side_tricks}  {ew}={solution.opponent_tricks}",
        f"Nodes          {solution.nodes:,}",
        "Principal variation:",
        format_pv(solution),
        "Compact PV:",
        "  " + format_pv_compact(solution),
    ]
    return "\n".join(out)


# ---------------------------------------------------------------------------
# Self-tests
# ---------------------------------------------------------------------------


def selftest(verbose: bool = True) -> int:
    """Small hand-verifiable checks.  Returns the number of failures."""
    failures = 0

    def check(name: str, got, want) -> None:
        nonlocal failures
        ok = got == want
        if not ok:
            failures += 1
        if verbose or not ok:
            status = "ok  " if ok else "FAIL"
            print(f"  [{status}] {name}")
            if not ok:
                print(f"         got  {got!r}")
                print(f"         want {want!r}")

    H = lambda *names: tuple(sorted(card_from_str(n) for n in names))

    print("Rule units")
    # Leading with spades unbroken: spades are excluded while anything else remains.
    check(
        "unbroken lead excludes spades",
        legal_moves(H("SA", "H3", "C9"), (), False),
        H("H3", "C9"),
    )
    # ...unless spades are all that remain.
    check(
        "unbroken lead forced when hand is all spades",
        legal_moves(H("SA", "S3"), (), False),
        H("SA", "S3"),
    )
    check(
        "broken lead allows spades",
        legal_moves(H("SA", "H3"), (), True),
        H("SA", "H3"),
    )
    # Following.
    check(
        "must follow suit",
        legal_moves(H("SA", "H3", "H9"), (card_from_str("H2"),), True),
        H("H3", "H9"),
    )
    check(
        "void may play anything",
        legal_moves(H("SA", "C3"), (card_from_str("H2"),), True),
        H("SA", "C3"),
    )
    # Breaking.
    check(
        "ruff breaks spades",
        spades_broken_after(False, (card_from_str("H2"),), card_from_str("S3")),
        True,
    )
    check(
        "discarding a non-spade does not break",
        spades_broken_after(False, (card_from_str("H2"),), card_from_str("C3")),
        False,
    )
    check(
        "a forced spade lead breaks spades",
        spades_broken_after(False, (), card_from_str("S3")),
        True,
    )
    # Trick winner.
    check(
        "highest of led suit wins",
        trick_winner(0, [card_from_str(x) for x in ("D2", "DA", "D5", "D7")]),
        1,
    )
    check(
        "any spade beats any non-spade",
        trick_winner(0, [card_from_str(x) for x in ("DA", "S2", "DK", "DQ")]),
        1,
    )
    check(
        "highest spade wins",
        trick_winner(2, [card_from_str(x) for x in ("S2", "SA", "DK", "S9")]),
        3,
    )
    check(
        "third-suit discard cannot win",
        trick_winner(0, [card_from_str(x) for x in ("D2", "CA", "D3", "HA")]),
        2,
    )

    print("Search")
    # One card each, all diamonds: E's ace wins, full stop.
    pos = Position.build(parse_pbn("N:..2. ..A. ..5. ..7."), leader=0, spades_broken=True)
    check("single trick, designated E", solve(pos, 1).tricks, 1)
    check("single trick, designated N", solve(pos, 0).tricks, 0)
    check("single trick, designated S", solve(pos, 2).tricks, 0)
    check(
        "single trick PV",
        format_pv_compact(solve(pos, 1)),
        "N:D2 E:DA S:D5 W:D7",
    )

    # Designated holds the bare ace of the only suit in play: exactly one trick,
    # regardless of anyone's intentions.
    ace = Position.build(
        parse_pbn("N:.A2.. .K3.. .54.. .Q6.."), leader=0, spades_broken=True
    )
    for d in range(4):
        want = 1 if d == 0 else None
        if want is not None:
            check(f"bare ace guarantees 1 (designated {SEAT_CHARS[d]})", solve(ace, d).tricks, want)

    # Squander test.  All hearts, two cards each:
    #   N: HK H2   E: HA H3   S: H5 H4   W: HQ H6      designated N, N leads.
    # N holds the lowest heart in play, so N can win at most with HK => <= 1.
    # E and W each hold exactly one card that beats HK.  They have two tricks
    # and can dump both high cards on the trick where N plays H2, leaving HK
    # to win the other one => >= 1.  Exact answer: 1.
    # A solver that let the SECONDARY objective outrank the primary would grab
    # the king with the ace and report 0 here.
    squander = Position.build(
        parse_pbn("N:.K2.. .A3.. .54.. .Q6.."), leader=0, spades_broken=True
    )
    check("squander: E/W duck to hand N a trick", solve(squander, 0).tricks, 1)
    # And the mirror: with N/S minimizing, the same layout designating E.
    check("squander mirror is a different problem", solve(squander, 1).tricks >= 0, True)

    # Spade-break restriction actually constrains the opening lead.
    #   N holds SA and one club; spades unbroken, so N must lead the club.
    break_pos = Position.build(
        parse_pbn("N:A...2 K...3 Q...4 J...5"), leader=0, spades_broken=False
    )
    check(
        "unbroken opening lead is the club",
        solve(break_pos, 0).pv[0][1],
        card_from_str("C2"),
    )
    check(
        "broken opening lead may be the spade",
        solve(
            Position.build(
                parse_pbn("N:A...2 K...3 Q...4 J...5"), leader=0, spades_broken=True
            ),
            0,
        ).pv[0][1],
        card_from_str("SA"),
    )

    print("Lexicographic secondary objective")
    check("weights: max", objective_weights(4, "max", False), (25, -5, 1))
    check("weights: min", objective_weights(4, "min", False), (25, 5, 0))
    check("weights: nil already set drops the primary",
          objective_weights(4, "max", True), (0, -5, 1))
    check("weights: each level outranks the ones below it",
          objective_weights(4, "max", False)[0] > abs(objective_weights(4, "max", False)[1]) * 4
          + objective_weights(4, "max", False)[2] * 4, True)

    # Three tricks to the nil bidder and one to its partner is NOT the same as
    # one and three: only the partner's tricks count towards the partner's bid.
    # With the nil live, level 1 has already pinned the nil bidder's count, so
    # the distinction cannot arise.  With the nil set it can, and level 3 is
    # what resolves it -- below the pair's total, so the opponents still simply
    # take what they can.
    # N/S can take two tricks here whatever they do; the question is who holds
    # them.  Optimizing the pair total alone leaves the nil bidder with one of
    # the two, which is worth nothing to the partner's bid.  Level 3 moves both
    # onto the partner without costing the pair its total.
    split = Position.build(
        parse_pbn("N:9.42.J. 5.Q.9.A A6.6..6 ..AT.Q2"), leader=1, spades_broken=True
    )
    settled = solve(split, 0, secondary="max", nil_already_set=True)
    check("nil set: the pair still takes everything it can", settled.side_tricks, 2)
    check("nil set: and its partner ends up holding it", settled.tricks, 0)

    # Level 3 must never buy a better split at the cost of the pair's total.
    for seed in range(6):
        f = random_fixture(seed=300 + seed, cards_per_hand=4)
        live = solve(f.position, f.designated, use_memo=True, secondary="max")
        gone = solve(f.position, f.designated, use_memo=True, secondary="max",
                     nil_already_set=True)
        check(f"seed {300+seed}: setting the nil never costs the pair tricks",
              gone.side_tricks >= live.side_tricks, True)
        # And in the "min" direction the two partners stay interchangeable, so
        # the pair total is all there is.
        shed = solve(f.position, f.designated, use_memo=True, secondary="min",
                     nil_already_set=True)
        check(f"seed {300+seed}: shedding is a pair total, not a split",
              shed.side_tricks + shed.opponent_tricks, f.position.tricks_remaining)

    # Two cards each, N is nil and safe either way, so the primary is a tie and
    # the secondary decides.  S holds HA H3: cashing the ace wins tricks for
    # N/S, ducking with the three sheds them.
    #   N: H2 C2   E: H5 C5   S: HA H3   W: H6 C6      leader E
    cover = Position.build(
        parse_pbn("N:.2..2 .5..5 .A3.. .6..6"), leader=1, spades_broken=True
    )
    grab = solve(cover, 0, secondary="max")
    shed = solve(cover, 0, secondary="min")
    check("secondary does not disturb the primary (max)", grab.tricks, 0)
    check("secondary does not disturb the primary (min)", shed.tricks, 0)
    check("secondary max: N/S take what they can", grab.side_tricks, 1)
    check("secondary min: N/S shed what they can", shed.side_tricks, 0)
    check("the two directions really do differ", grab.pv != shed.pv, True)

    # Protecting the nil is not free.  Here N/S can hold N to zero, but only by
    # giving up a trick they could otherwise win: once the nil is already set
    # and there is nothing left to protect, the same layout yields them all
    # three tricks -- one of which N itself takes.
    costly = Position.build(
        parse_pbn("N:7..6.3 6.J.2. J3.7.. 9..3.9"), leader=0, spades_broken=True
    )
    protect = solve(costly, 0, secondary="max")
    ignore = solve(costly, 0, secondary="max", nil_already_set=True)
    check("nil is protected", protect.tricks, 0)
    check("protecting it costs a trick", protect.side_tricks, 2)
    check("nil already set: primary is off", ignore.tricks, 1)
    check("nil already set: N/S now take everything", ignore.side_tricks, 3)

    # Tallies must be consistent no matter which knobs are set.
    for secondary in ("max", "min"):
        for already in (False, True):
            sol = solve(costly, 0, secondary=secondary, nil_already_set=already)
            label = f"{secondary}/{'set' if already else 'live'}"
            check(f"{label}: sides sum to the tricks played",
                  sol.side_tricks + sol.opponent_tricks, costly.tricks_remaining)
            check(f"{label}: designated is part of its own side",
                  sol.tricks <= sol.side_tricks, True)

    # THE lexicographic property: a tie-break can never change the primary.
    # If this ever fails, the packing has overflowed and the secondary has
    # started outranking the nil.
    for seed in range(6):
        f = random_fixture(seed=200 + seed, cards_per_hand=4)
        high = solve(f.position, f.designated, use_memo=True, secondary="max")
        low = solve(f.position, f.designated, use_memo=True, secondary="min")
        check(f"seed {200+seed}: secondary never moves the primary",
              high.tricks, low.tricks)

    print("Parsing and fixtures")
    full = "N:8.K5.KT2.KQT9762 AQT643.T.QJ864.8 J9.A943.73.AJ543 K752.QJ8762.A95."
    hands = parse_pbn(full)
    check("full deal is 13 cards each", [len(h) for h in hands], [13, 13, 13, 13])
    check("full deal round-trips", deal_to_pbn(hands, 0), full)
    check(
        "hands rotate clockwise from the named seat",
        deal_to_pbn(parse_pbn("E:..2. ..3. ..4. ..5."), 0),
        "N:..5. ..2. ..3. ..4.",
    )
    check("lone dash is an empty hand", parse_hand("-"), ())
    check("'10' is accepted for the ten", parse_hand("10...")[0], card_from_str("ST"))

    fx = random_fixture(seed=7, cards_per_hand=5)
    check("fixture deals 5 each", [len(h) for h in fx.position.hands], [5, 5, 5, 5])
    check("fixture has no duplicates", len({c for h in fx.position.hands for c in h}), 20)
    check("fixture round-trips through PBN", parse_pbn(fx.to_pbn()), fx.position.hands)
    check(
        "fixture is reproducible",
        random_fixture(seed=7, cards_per_hand=5).position,
        fx.position,
    )

    print("End-to-end invariants")
    for seed in range(4):
        f = random_fixture(seed=100 + seed, cards_per_hand=4)
        sol = solve(f.position, f.designated)
        check(f"seed {100+seed}: PV plays every card", len(sol.pv), 16)
        check(f"seed {100+seed}: value in range", 0 <= sol.tricks <= 4, True)
        # replay_pv already ran inside solve(); this pins determinism.
        again = solve(f.position, f.designated)
        check(f"seed {100+seed}: run is deterministic", again.pv, sol.pv)
        memoized = solve(f.position, f.designated, use_memo=True)
        check(f"seed {100+seed}: memo agrees on value", memoized.tricks, sol.tricks)
        check(f"seed {100+seed}: memo agrees on PV", memoized.pv, sol.pv)

    print()
    print("FAILURES:" if failures else "All checks passed.", failures or "")
    return failures


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser(
        description="Brute-force double-dummy oracle for Spades card play.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "examples:\n"
            "  %(prog)s --selftest\n"
            "  %(prog)s --random --seed 42 --cards 5 --leader N --designated S\n"
            "  %(prog)s --pbn 'N:A...2 K...3 Q...4 J...5' --leader N --designated N\n"
            "  %(prog)s --random --seed 42 --cards 5 --compact   # diff-friendly\n"
            "  %(prog)s --pbn '...' --designated N --secondary min\n"
            "  %(prog)s --pbn '...' --designated N --nil-already-set\n"
        ),
    )
    src = p.add_mutually_exclusive_group()
    src.add_argument("--pbn", help="deal in PBN form, e.g. 'N:A2.K.. ...'")
    src.add_argument(
        "--random", action="store_true", help="generate a random fixture instead"
    )
    p.add_argument("--seed", type=int, default=0, help="RNG seed for --random")
    p.add_argument("--cards", type=int, default=5, help="cards per hand for --random (4-7)")
    p.add_argument("--leader", default=None, help="seat on lead (N/E/S/W)")
    p.add_argument("--designated", default=None, help="designated player (N/E/S/W)")
    p.add_argument("--spades-broken", action="store_true", help="start with spades broken")
    p.add_argument(
        "--trick",
        default="",
        help="cards already played to the current trick, in play order from the "
        "leader, e.g. 'H4 HK' (they must not appear in the hands)",
    )
    p.add_argument(
        "--secondary",
        choices=("max", "min"),
        default="max",
        help="tie-break direction: each pair takes as many tricks as it can "
        "(max, the default) or as few as it can (min)",
    )
    p.add_argument(
        "--nil-already-set",
        action="store_true",
        help="the nil has already been broken in the real game, so drop the "
        "primary objective and optimize only the secondary one",
    )
    p.add_argument(
        "--memo",
        action="store_true",
        help="memoize on full state; identical value and PV, just faster",
    )
    p.add_argument("--compact", action="store_true", help="print only value and PV")
    p.add_argument(
        "--force",
        action="store_true",
        help="allow more than 7 cards per hand (this will not finish)",
    )
    p.add_argument("--selftest", action="store_true", help="run the built-in checks")
    args = p.parse_args(argv)

    if args.selftest:
        return 1 if selftest() else 0

    leader = seat_from_str(args.leader) if args.leader else None
    designated = seat_from_str(args.designated) if args.designated else None

    try:
        if args.random or not args.pbn:
            fx = random_fixture(
                seed=args.seed,
                cards_per_hand=args.cards,
                leader=leader if leader is not None else 0,
                designated=designated if designated is not None else 0,
                spades_broken=args.spades_broken,
            )
            hands = fx.position.hands
            designated = fx.designated
            leader = fx.position.leader
        else:
            hands = parse_pbn(args.pbn)
            leader = 0 if leader is None else leader
            designated = 0 if designated is None else designated

        trick = tuple(
            card_from_str(t) for t in args.trick.replace(",", " ").split() if t
        )
        position = Position.build(
            hands, leader=leader, spades_broken=args.spades_broken, current_trick=trick
        )
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if position.cards_per_hand > 7 and not args.force:
        print(
            f"error: {position.cards_per_hand} cards per hand; this is an exhaustive "
            "search with no pruning and will not finish. Use --force to insist.",
            file=sys.stderr,
        )
        return 2

    solution = solve(
        position,
        designated,
        use_memo=args.memo,
        secondary=args.secondary,
        nil_already_set=args.nil_already_set,
    )
    print(format_solution(solution, compact=args.compact))
    return 0


if __name__ == "__main__":
    # With arguments, behave like a normal CLI.  With none, run the scratch
    # position below -- edit it and hit run.
    if len(sys.argv) > 1:
        raise SystemExit(main())

    # --- scratch position: edit and run with no arguments -----------------
    POSITION = Position.build(
        parse_pbn("N:K.A.A.K 32.2..A .K.3.32 A.3.K2."),
        leader=seat_from_str("N"),          # N=0 E=1 S=2 W=3
        spades_broken=False,
        current_trick=(),                   # e.g. (card_from_str("H4"),)
    )
    print(
        format_solution(
            solve(
                POSITION,
                designated=seat_from_str("E"),
                secondary="max",            # "max" or "min"
                nil_already_set=False,
            )
        )
    )
    raise SystemExit(0)
