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

SEAT ROLES
----------
The command line describes the deal the way the solver's does, with --seats: one
role per seat, running clockwise from the seat the PBN names.  See ROLE_* below.
It is a spelling of the two arguments it replaced -- the designated seat, and
whether its nil is already broken -- and NOT a change to what is computed.  In
particular the coalitions are still fixed by parity, so this file answers the
nil question exactly when the nil sits North or South; tools/crosscheck.py
rotates each deal so that it does.  Making the coalitions follow the roles would
make the oracle agree with the solver for all four seats, and it is deliberately
not done here: this file is the ground truth the solver is checked against, and
its search is not moved by a representation change.

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

# What each seat is doing.  Wire values, shared with the C++ solver's SeatRole
# and with the corpus format's `seats` column.
ROLE_NIL = 0        # a nil bidder that has not yet taken a trick
ROLE_NIL_SET = 1    # a nil bidder whose nil is already broken
ROLE_COVER = 2      # the partner covering it
ROLE_OPPONENT = 3   # a seat on a side with no nil bid
ROLE_NAMES = ("nil", "nil-set", "cover", "opponent")


def roles_from_nil(nil_seat: int, already_set: bool = False) -> List[int]:
    """The arrangement a designated seat and an already-set flag describe."""
    roles = [ROLE_OPPONENT] * 4
    roles[nil_seat % 4] = ROLE_NIL_SET if already_set else ROLE_NIL
    roles[(nil_seat + 2) % 4] = ROLE_COVER
    return roles


def nil_seat_of(roles: Sequence[int]) -> int:
    for seat, role in enumerate(roles):
        if role in (ROLE_NIL, ROLE_NIL_SET):
            return seat
    return -1


def nil_already_set_of(roles: Sequence[int]) -> bool:
    return ROLE_NIL_SET in tuple(roles)


# The arrangements this file can answer, and the name each goes by.
SHAPE_SINGLE_NIL = "single-nil"      # one nil, its partner covering, two opponents
SHAPE_PARTNER_NILS = "partner-nils"  # both members of one pair bid, two opponents
SHAPE_OPPOSING_NILS = "opposing-nils"  # one bid per pair, each with a partner


def role_shape(roles: Sequence[int]) -> str:
    """Name the arrangement, or raise saying why it is not one we answer.

    Raising is the whole point of this function: an arrangement nobody has
    implemented must come back as a refusal rather than as an answer to some
    adjacent question.  The two shapes below are what exists today.
    """
    if len(roles) != 4:
        raise ValueError(f"expected four seat roles, got {len(roles)}")
    for seat, role in enumerate(roles):
        if role not in (ROLE_NIL, ROLE_NIL_SET, ROLE_COVER, ROLE_OPPONENT):
            raise ValueError(
                f"seat {SEAT_CHARS[seat]} has role {role}; expected 0 (nil), "
                "1 (nil-set), 2 (cover) or 3 (opponent)"
            )
    nils = [s for s, r in enumerate(roles) if r in (ROLE_NIL, ROLE_NIL_SET)]
    covers = [s for s, r in enumerate(roles) if r == ROLE_COVER]
    opponents = [s for s, r in enumerate(roles) if r == ROLE_OPPONENT]
    described = describe_roles(roles)

    if not nils:
        raise ValueError(f"no seat bid nil ({described}); one seat must have role 0 or 1")

    if len(nils) == 1:
        if not covers:
            raise ValueError(
                f"no cover partner for {SEAT_CHARS[nils[0]]}'s nil ({described}); "
                "one seat must have role 2"
            )
        if len(covers) > 1:
            raise ValueError(
                f"more than one cover partner ({described}); exactly one seat may have role 2"
            )
        if covers[0] != (nils[0] + 2) % 4:
            raise ValueError(
                "the cover partner must sit across from the nil bidder: "
                f"{SEAT_CHARS[nils[0]]} bid nil, so the cover is "
                f"{SEAT_CHARS[(nils[0] + 2) % 4]} and not {SEAT_CHARS[covers[0]]} ({described})"
            )
        return SHAPE_SINGLE_NIL

    if len(nils) == 2 and (nils[0] + 2) % 4 != nils[1]:
        # ONE BID PER PAIR.  The other two seats are each a bidder's partner, and
        # the ROLE ON THAT PARTNER is not a statement about teams -- both teams
        # obviously have a nil -- but about what that side does when it cannot
        # have both halves of what it wants.  ROLE_COVER saves its own bid at the
        # cost of letting the opponent's live; ROLE_OPPONENT sets the opponent's
        # at the cost of its own.  See side_rank.
        for seat in nils:
            if roles[seat] == ROLE_NIL_SET:
                raise ValueError(
                    f"an already-broken nil on opposing sides is not supported "
                    f"yet ({described})"
                )
        for seat in range(4):
            if seat in nils:
                continue
            if roles[seat] not in (ROLE_COVER, ROLE_OPPONENT):
                raise ValueError(
                    f"seat {SEAT_CHARS[seat]} partners a nil bidder, so its role "
                    f"must be 2 (save our own first) or 3 (set theirs first), "
                    f"not {roles[seat]} ({described})"
                )
        return SHAPE_OPPOSING_NILS

    if len(nils) == 2:
        if covers:
            raise ValueError(
                f"a pair that both bid nil has nobody left to cover ({described}); "
                "the other two seats must both have role 3"
            )
        if len(opponents) != 2:
            raise ValueError(
                f"expected two opponents against a pair of nils ({described})"
            )
        return SHAPE_PARTNER_NILS

    raise ValueError(
        f"{len(nils)} nils ({described}); more than two is not supported yet"
    )


def validate_roles(roles: Sequence[int]) -> None:
    """Raise unless this is a shape this file can answer.  See role_shape."""
    role_shape(roles)


def parse_roles(text: str, anchor: int) -> List[int]:
    """Read '0 3 2 3' -- clockwise from `anchor` -- into absolute seat order.

    Whitespace and commas separate; four digits run together are four values.
    """
    digits = [ch for ch in text if not ch.isspace() and ch != ","]
    for ch in digits:
        if not ch.isdigit():
            raise ValueError(
                f"bad seat role character {ch!r} in {text!r}; expected four values "
                "from 0 (nil), 1 (nil-set), 2 (cover), 3 (opponent)"
            )
    if len(digits) != 4:
        raise ValueError(f"expected exactly four seat roles, got {len(digits)} in {text!r}")
    roles = [ROLE_OPPONENT] * 4
    for offset, ch in enumerate(digits):
        roles[(anchor + offset) % 4] = int(ch)
    return roles


def roles_to_str(roles: Sequence[int], anchor: int) -> str:
    """The inverse: '0 3 2 3', clockwise from `anchor`."""
    return " ".join(str(roles[(anchor + off) % 4]) for off in range(4))


def describe_roles(roles: Sequence[int]) -> str:
    """Absolute and unambiguous: 'N=nil E=opponent S=cover W=opponent'."""
    return " ".join(
        f"{SEAT_CHARS[s]}={ROLE_NAMES[r] if 0 <= r < 4 else '?'}"
        for s, r in enumerate(roles)
    )


def pbn_anchor(text: str) -> int:
    """The seat a PBN string is named for; its hands, and its roles, run
    clockwise from there."""
    t = text.strip()
    if len(t) < 2 or t[1] != ":":
        raise ValueError("PBN deal must start with a seat letter and a colon, e.g. 'N:'")
    return seat_from_str(t[0])


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


# ---------------------------------------------------------------------------
# One nil on each side
#
# WHY THIS IS NOT ONE SCALAR
# --------------------------
# Every objective before this one had two sides that wanted opposite things, so
# a single number served: one side pushed it up and the other pushed it down.
# With a bid on each side that stops being true in general.
#
# Rank the four outcomes for a side, best first.  Both sides agree on the top
# and the bottom -- my bid making while theirs fails is best, the reverse is
# worst.  What they need not agree on is the MIDDLE, and the role on a bidder's
# partner is what says which way that side leans:
#
#   ROLE_COVER    save our own bid first:  both make  >  both fail
#   ROLE_OPPONENT set theirs first:        both fail  >  both make
#
# Score those 3, 2, 1, 0 and add the ranks of the two sides:
#
#   0 3 2 0  mixed        3 3 3 3   CONSTANT -- strictly opposed, so the game is
#                                   zero-sum and ordinary minimax applies
#   0 2 2 0  protective   4 3 3 2   not constant: both sides would rather have
#   0 3 3 0  aggressive   2 3 3 4   both bids live (or both dead) than trade
#
# So one of the three shapes is an ordinary two-team game and the other two are
# not.  In the two that are not, the sides share an interest, and there is no
# scalar for one to minimise -- which is the situation Sturtevant and Korf's
# paper in this repo is about.  This file sidesteps the question by being
# exhaustive: it carries a utility PER SIDE and each side maximises its own
# component, which is backward induction and needs no pruning to be correct.
# What the C++ solver can then prune is a separate question, and the answer
# differs between the shapes.
# ---------------------------------------------------------------------------


def side_rank(mine_makes: bool, theirs_makes: bool, partner_role: int) -> int:
    """How good this outcome is for a side, 3 best to 0 worst."""
    if mine_makes and not theirs_makes:
        return 3
    if theirs_makes and not mine_makes:
        return 0
    both_make = mine_makes and theirs_makes
    if partner_role == ROLE_COVER:
        return 2 if both_make else 1
    return 1 if both_make else 2


def opposing_weights(tricks_remaining: int, secondary: str = "max") -> Tuple[int, int]:
    """(rank weight, own-trick weight) for one side's utility.

    Each side MAXIMISES its own utility, so the trick weight is positive when
    tricks are wanted and negative when they are shed.  The rank weight is K*K
    with K = tricks remaining + 1; one step of rank is worth K*K and the trick
    term can never exceed K*tricks_remaining, which is smaller.
    """
    if secondary not in ("max", "min"):
        raise ValueError("secondary must be 'max' or 'min'")
    k = tricks_remaining + 1
    return k * k, (k if secondary == "max" else -k)


@dataclass
class _OppCtx:
    nil_of_side: Tuple[int, int]      # bidder seat for side 0 and side 1
    partner_role: Tuple[int, int]     # that bidder's partner's role
    rank_weight: int
    trick_weight: int
    memo: Optional[Dict] = None
    nodes: int = 0


def _terminal_utility(broken_nils: int, ctx: _OppCtx) -> Tuple[int, int]:
    """Both sides' rank contribution, once every card is played."""
    makes = [not (broken_nils & (1 << seat)) for seat in ctx.nil_of_side]
    return (
        ctx.rank_weight * side_rank(makes[0], makes[1], ctx.partner_role[0]),
        ctx.rank_weight * side_rank(makes[1], makes[0], ctx.partner_role[1]),
    )


def _search_opposing(
    hands: Tuple[Tuple[Card, ...], ...],
    leader: int,
    trick: Tuple[Card, ...],
    spades_broken: bool,
    broken_nils: int,
    ctx: _OppCtx,
) -> Tuple[Tuple[int, int], Tuple[Play, ...]]:
    """Backward induction on a utility PAIR.  Returns ((u_side0, u_side1), PV).

    The seat to play belongs to one side, and it chooses the move that maximises
    THAT side's component.  With strictly opposed shapes this coincides with
    minimax; with the other two it is what minimax cannot express.
    """
    ctx.nodes += 1
    if not any(hands):
        return _terminal_utility(broken_nils, ctx), ()

    key = None
    if ctx.memo is not None:
        key = (hands, leader, trick, spades_broken, broken_nils)
        cached = ctx.memo.get(key)
        if cached is not None:
            return cached

    seat = (leader + len(trick)) % 4
    side = seat % 2

    best: Optional[Tuple[int, int]] = None
    best_pv: Tuple[Play, ...] = ()

    for card in legal_moves(hands[seat], trick, spades_broken):
        next_hands = tuple(
            tuple(c for c in hand if c != card) if s == seat else hand
            for s, hand in enumerate(hands)
        )
        next_broken = spades_broken_after(spades_broken, trick, card)
        played = trick + (card,)

        if len(played) == 4:
            winner = trick_winner(leader, played)
            next_nils = broken_nils
            for s in ctx.nil_of_side:
                if winner == s:
                    next_nils |= 1 << s
            sub_value, sub_pv = _search_opposing(
                next_hands, winner, (), next_broken, next_nils, ctx
            )
            gained = [0, 0]
            gained[winner % 2] = ctx.trick_weight
            value = (sub_value[0] + gained[0], sub_value[1] + gained[1])
        else:
            value, sub_pv = _search_opposing(
                next_hands, leader, played, next_broken, broken_nils, ctx
            )

        # Strict improvement only, so ties keep the canonically lowest card.
        if best is None or value[side] > best[side]:
            best = value
            best_pv = ((seat, card),) + sub_pv

    assert best is not None, "a non-terminal position must have a legal move"
    result = (best, best_pv)
    if ctx.memo is not None:
        ctx.memo[key] = result
    return result


def _search_conjunction(
    hands: Tuple[Tuple[Card, ...], ...],
    leader: int,
    trick: Tuple[Card, ...],
    spades_broken: bool,
    broken_nils: int,
    ctx: "_ConjCtx",
) -> bool:
    """Can the attacking side force ITS bid to survive while the other's dies?

    A BOOLEAN AND-OR SEARCH, written independently of `_search_opposing` on
    purpose.  The two answer the same question by different routes -- this one
    over a two-valued objective, that one over a utility pair with a trick
    tie-break underneath -- and `selftest` requires them to agree on every
    fixture.  Two algorithms agreeing is worth more than one algorithm passing.

    THE DEFENDING SIDE'S GOAL IS A DISJUNCTION, and that is the whole hazard in
    this item.  It wins by EITHER keeping its own bid alive OR breaking the
    attacker's, so it may deliberately dump a trick on the attacker's bidder and
    abandon its own bid to do it.  Nothing here constrains it to protect: it
    simply minimises the conjunction, which lets both routes through.  A search
    that let the defender only protect would report the attacker succeeding on
    lines it cannot actually win, and no corpus would catch it.
    """
    ctx.nodes += 1

    attacker_bid = ctx.nil_of_side[ctx.attacker]
    defender_bid = ctx.nil_of_side[1 - ctx.attacker]

    # The attacker's own bid is gone, so the conjunction can never hold again.
    # Sound because a bid never un-breaks.
    if broken_nils & (1 << attacker_bid):
        return False

    if not any(hands):
        return bool(broken_nils & (1 << defender_bid))

    key = None
    if ctx.memo is not None:
        key = (hands, leader, trick, spades_broken, broken_nils)
        cached = ctx.memo.get(key)
        if cached is not None:
            return cached

    seat = (leader + len(trick)) % 4
    maximising = seat % 2 == ctx.attacker

    result = not maximising  # AND for the defender, OR for the attacker
    for card in legal_moves(hands[seat], trick, spades_broken):
        next_hands = tuple(
            tuple(c for c in hand if c != card) if s == seat else hand
            for s, hand in enumerate(hands)
        )
        next_broken_spades = spades_broken_after(spades_broken, trick, card)
        played = trick + (card,)

        if len(played) == 4:
            winner = trick_winner(leader, played)
            next_nils = broken_nils
            for bid in ctx.nil_of_side:
                if winner == bid:
                    next_nils |= 1 << bid
            value = _search_conjunction(
                next_hands, winner, (), next_broken_spades, next_nils, ctx
            )
        else:
            value = _search_conjunction(
                next_hands, leader, played, next_broken_spades, broken_nils, ctx
            )

        # First answer either way ends the node, which is what makes this cheap
        # and is the same shape MODE_FAST's [0, 1] window gives the C++ search.
        if value == maximising:
            result = value
            break

    if ctx.memo is not None:
        ctx.memo[key] = result
    return result


@dataclass
class _ConjCtx:
    nil_of_side: Tuple[int, int]
    attacker: int
    memo: Optional[Dict] = None
    nodes: int = 0


@dataclass
class ConjunctionSolution:
    """Can one side break the other's bid while keeping its own?"""

    roles: List[int]
    attacker: int                 # side 0 (N/S) or 1 (E/W)
    attacker_bid: int             # that side's bidder seat
    defender_bid: int
    can_force: bool
    nodes: int
    position: Position


def solve_conjunction(
    position: Position,
    roles: Sequence[int],
    attacker: int,
    use_memo: bool = True,
) -> ConjunctionSolution:
    """Ground truth for the third probe the C++ presolve wants.

    `solve_opposing_nils` settles two of the four (bid safe / bid breakable)
    combinations outright and leaves the other two with a one-sided bound.  What
    closes them is this: *can this side force the other's bid down WHILE KEEPING
    ITS OWN?*

    WHY IT IS ALSO DERIVABLE FROM THE UTILITY SEARCH, which is what makes the
    agreement check possible.  The conjunction is exactly outcome RANK 3 for the
    attacking side -- its bid alive, theirs dead -- and rank 3 is the unique best
    outcome on that side's ladder while being the unique worst, rank 0, on the
    other's.  Under EVERY partner lean.  So a defender maximising its own utility
    escapes rank 0 whenever it can and an attacker steers to rank 3 whenever it
    can, and backward induction on the pair lands on rank 3 exactly when the
    attacker can force it.  That holds on all three shapes, not only the strictly
    opposed one, so this probe is already defined for the two the C++ refuses.
    """
    position.validate()
    shape = role_shape(roles)
    if shape != SHAPE_OPPOSING_NILS:
        raise ValueError(f"solve_conjunction needs a bid on each side, got {shape}")
    if attacker not in (0, 1):
        raise ValueError("attacker must be side 0 (N/S) or 1 (E/W)")

    nil_of_side = []
    for side in (0, 1):
        nil_of_side.append(
            next(x for x in (side, side + 2) if roles[x] in (ROLE_NIL, ROLE_NIL_SET))
        )

    ctx = _ConjCtx(
        nil_of_side=tuple(nil_of_side),
        attacker=attacker,
        memo={} if use_memo else None,
    )
    can_force = _search_conjunction(
        position.hands,
        position.leader,
        position.current_trick,
        position.spades_broken,
        0,
        ctx,
    )
    return ConjunctionSolution(
        roles=list(roles),
        attacker=attacker,
        attacker_bid=nil_of_side[attacker],
        defender_bid=nil_of_side[1 - attacker],
        can_force=can_force,
        nodes=ctx.nodes,
        position=position,
    )


@dataclass
class OpposingNilSolution:
    """The answer to a deal with one nil on each side."""

    roles: List[int]
    seat_tricks: List[int]
    nil_of_side: List[int]        # bidder seat for side 0 (N/S) and side 1 (E/W)
    nil_makes: List[bool]         # did each side's bid survive
    side_tricks: List[int]        # tricks taken by side 0 and side 1
    utility: List[int]            # each side's own scalar, which it maximised
    ranks: List[int]              # each side's outcome rank, 3 best to 0 worst
    strictly_opposed: bool        # do the ranks sum to a constant on this shape
    pv: List[Play]
    nodes: int
    position: Position
    secondary: str = "max"


def solve_opposing_nils(
    position: Position,
    roles: Sequence[int],
    use_memo: bool = False,
    secondary: str = "max",
) -> OpposingNilSolution:
    """Exhaustive backward induction for one nil on each side."""
    position.validate()
    shape = role_shape(roles)
    if shape != SHAPE_OPPOSING_NILS:
        raise ValueError(f"solve_opposing_nils needs a bid on each side, got {shape}")

    nil_of_side = []
    partner_role = []
    for side in (0, 1):
        seat = next(x for x in (side, side + 2) if roles[x] in (ROLE_NIL, ROLE_NIL_SET))
        nil_of_side.append(seat)
        partner_role.append(roles[(seat + 2) % 4])

    rank_weight, trick_weight = opposing_weights(position.tricks_remaining, secondary)
    ctx = _OppCtx(
        nil_of_side=tuple(nil_of_side),
        partner_role=tuple(partner_role),
        rank_weight=rank_weight,
        trick_weight=trick_weight,
        memo={} if use_memo else None,
    )
    utility, pv = _search_opposing(
        position.hands,
        position.leader,
        position.current_trick,
        position.spades_broken,
        0,
        ctx,
    )

    # Self-check: replay independently and re-encode.  A solver that lies is
    # worse than no solver, and here there are two numbers to get wrong.
    seat_tricks = replay_pv_by_seat(position, list(pv))
    makes = [seat_tricks[seat] == 0 for seat in nil_of_side]
    side_tricks = [seat_tricks[0] + seat_tricks[2], seat_tricks[1] + seat_tricks[3]]
    ranks = [
        side_rank(makes[0], makes[1], partner_role[0]),
        side_rank(makes[1], makes[0], partner_role[1]),
    ]
    replayed = tuple(
        rank_weight * ranks[i] + trick_weight * side_tricks[i] for i in (0, 1)
    )
    if replayed != tuple(utility):
        raise AssertionError(
            f"internal inconsistency: search says {utility}, replaying the PV "
            f"gives {replayed} (ranks={ranks}, side tricks={side_tricks})"
        )

    # Do the two rankings sum to a constant?  If they do the deal is an ordinary
    # two-team game; if not, the sides share an interest and no single scalar
    # describes it.  A property of the ROLES, not of the cards.
    sums = {
        side_rank(a, b, partner_role[0]) + side_rank(b, a, partner_role[1])
        for a in (True, False)
        for b in (True, False)
    }

    return OpposingNilSolution(
        roles=list(roles),
        seat_tricks=seat_tricks,
        nil_of_side=nil_of_side,
        nil_makes=makes,
        side_tricks=side_tricks,
        utility=list(utility),
        ranks=ranks,
        strictly_opposed=len(sums) == 1,
        pv=list(pv),
        nodes=ctx.nodes,
        position=position,
        secondary=secondary,
    )


# ---------------------------------------------------------------------------
# Two nils on the same side
#
# WHY THIS IS A SEPARATE SEARCH
# -----------------------------
# The single-nil objective is ADDITIVE: every completed trick contributes a
# fixed amount to the value, so _search can accumulate as it unwinds and needs
# no memory of what came before.  "How many nils got set" is not additive.  It
# is a step function -- the FIRST trick a nil bidder takes costs its side the
# whole primary level and every later one costs nothing more -- so the value of
# a subtree depends on which nils have already been broken on the way in.
#
# That path state therefore has to travel with the search and go into the memo
# key, exactly as `spades_broken` does.  It is two bits wide and it only ever
# fills up, so it collapses quickly: once both nils are gone the remaining
# search is pure secondary.
#
# THE OBJECTIVE
#
#     value = primary * (number of nils set) + secondary * (nil side's tricks)
#
# with primary = K*K and secondary = -/+K for K = tricks_remaining + 1, so the
# two levels compare lexicographically and neither can overturn the other.  The
# NIL SIDE MINIMIZES and the opponents maximize:
#
#   opponents:  two nils set  >  one nil set  >  their own tricks per `secondary`
#   nil pair:   two nils made >  one made     >  their own tricks per `secondary`
#
# The two levels are coupled in a way the single-nil objective is not.  Every
# trick the nil pair takes is taken BY a nil bidder, so a nil side that wants
# tricks can only get them by breaking one of its own bids.  With secondary
# "max" that makes the ordering do real work: having lost the first nil, the
# pair funnels everything through the seat already broken and keeps the other
# alive, which is what a spades player does and what a naive "minimize our own
# tricks" objective would get wrong.
#
# COALITIONS COME FROM THE ROLES HERE, NOT FROM PARITY.  The single-nil search
# above fixes N/S as the minimizing side whatever the roles say -- see item 55
# in ROADMAP.md -- and this is new code with no ground truth to preserve, so it
# does the correct thing instead: the pair holding the nils minimizes.  Both
# agree whenever the nils sit North and South.
# ---------------------------------------------------------------------------


@dataclass
class _MultiCtx:
    nil_seats: Tuple[int, ...]   # the two partners that bid
    minimizing_parity: int       # seats with this parity minimize the value
    primary_weight: int          # K*K, charged once per nil the first time it takes
    secondary_weight: int        # -/+K on the nil side's own tricks
    memo: Optional[Dict] = None
    nodes: int = 0


def multi_objective_weights(
    tricks_remaining: int,
    secondary: str = "max",
) -> Tuple[int, int]:
    """Return (primary, secondary) weights for the two-nil objective.

    primary is K*K and secondary is -/+K with K = tricks_remaining + 1.  The
    secondary level can contribute at most K*tricks_remaining in absolute value,
    which is strictly less than K*K, so a difference in the number of nils set
    can never be outweighed by any difference in trick counts.
    """
    if secondary not in ("max", "min"):
        raise ValueError("secondary must be 'max' or 'min'")
    k = tricks_remaining + 1
    return k * k, (-k if secondary == "max" else k)


def _search_multi(
    hands: Tuple[Tuple[Card, ...], ...],
    leader: int,
    trick: Tuple[Card, ...],
    spades_broken: bool,
    broken_nils: int,
    ctx: _MultiCtx,
) -> Tuple[int, Tuple[Play, ...]]:
    """Return (objective value from here on, PV from here).

    `broken_nils` is a bitmask over seats of which nil bidders have ALREADY
    taken a trick on the way to this node.  It is part of the state because the
    primary level is charged on a nil bidder's first trick and on no other.
    """
    ctx.nodes += 1

    if not any(hands):
        return 0, ()

    key = None
    if ctx.memo is not None:
        key = (hands, leader, trick, spades_broken, broken_nils)
        cached = ctx.memo.get(key)
        if cached is not None:
            return cached

    seat = (leader + len(trick)) % 4
    maximizing = (seat % 2) != ctx.minimizing_parity

    best_value: Optional[int] = None
    best_pv: Tuple[Play, ...] = ()

    for card in legal_moves(hands[seat], trick, spades_broken):
        next_hands = tuple(
            tuple(c for c in hand if c != card) if s == seat else hand
            for s, hand in enumerate(hands)
        )
        next_broken = spades_broken_after(spades_broken, trick, card)
        played = trick + (card,)

        if len(played) == 4:
            winner = trick_winner(leader, played)
            gained = 0
            next_nils = broken_nils
            if winner in ctx.nil_seats and not (broken_nils & (1 << winner)):
                # The trick that kills this bid.  Charged once, here.
                gained += ctx.primary_weight
                next_nils = broken_nils | (1 << winner)
            if winner % 2 == ctx.minimizing_parity:
                gained += ctx.secondary_weight
            sub_value, sub_pv = _search_multi(
                next_hands, winner, (), next_broken, next_nils, ctx
            )
            value = gained + sub_value
        else:
            value, sub_pv = _search_multi(
                next_hands, leader, played, next_broken, broken_nils, ctx
            )

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


@dataclass
class MultiNilSolution:
    """The answer to a deal where one pair bid two nils."""

    roles: List[int]
    seat_tricks: List[int]        # tricks each seat took, indexed by seat
    nils_set: int                 # 0, 1 or 2
    nil_seats: List[int]
    nil_side_tricks: int
    opponent_tricks: int
    value: int                    # the raw lexicographic scalar the nil side minimized
    pv: List[Play]
    nodes: int
    position: Position
    secondary: str = "max"

    def nil_fails(self, seat: int) -> bool:
        return self.seat_tricks[seat] > 0


def solve_partner_nils(
    position: Position,
    roles: Sequence[int],
    use_memo: bool = False,
    secondary: str = "max",
) -> MultiNilSolution:
    """Exhaustive minimax for a pair that both bid nil.

    Primary: how many of the two nils get set.  The opponents maximize it, the
    pair minimizes it.  Secondary, breaking ties only: the pair's own trick
    count, wanted under secondary="max" and shed under "min".

    See the block comment above for why this cannot reuse _search, and
    use_memo carries the same caveat it does there -- it caches a pure function
    of the full state (now including which nils are already broken), so it
    changes neither the value nor the PV.
    """
    position.validate()
    shape = role_shape(roles)
    if shape != SHAPE_PARTNER_NILS:
        raise ValueError(f"solve_partner_nils needs two partners bidding, got {shape}")

    # ONLY LIVE BIDS ARE PLAYED FOR.  A bid the caller marked ROLE_NIL_SET is
    # already down and cannot go down twice, so it carries no primary weight and
    # the mask never tracks it.  What that seat DOES keep is its half of the
    # secondary level: its tricks still count for the pair, so it plays exactly
    # as a cover partner does -- freely, because there is nothing left to
    # protect.  This is the state a real hand reaches the moment one of two nils
    # breaks, and re-solving from it is the point.
    live_seats = tuple(s for s, r in enumerate(roles) if r == ROLE_NIL)
    already_down = sum(1 for r in roles if r == ROLE_NIL_SET)
    # With every bid already down there is no primary level at all, and what is
    # left is the secondary alone: each pair takes or sheds as many tricks as it
    # can, which is an ordinary double-dummy question.  The mask is simply empty
    # and nothing is ever charged against it.
    nil_seats = live_seats
    # The COALITION is the whole pair, live bids or not: a busted bidder still
    # plays for its side's trick total.  Taking the parity from `live_seats`
    # instead crashes the moment both bids are down, which is a legal shape --
    # and was, until the corpus grew rows for it, a shape nothing exercised.
    pair_seats = tuple(s for s, r in enumerate(roles) if r in (ROLE_NIL, ROLE_NIL_SET))
    primary_weight, secondary_weight = multi_objective_weights(
        position.tricks_remaining, secondary
    )
    ctx = _MultiCtx(
        nil_seats=nil_seats,
        minimizing_parity=pair_seats[0] % 2,
        primary_weight=primary_weight,
        secondary_weight=secondary_weight,
        memo={} if use_memo else None,
    )
    value, pv = _search_multi(
        position.hands,
        position.leader,
        position.current_trick,
        position.spades_broken,
        0,
        ctx,
    )

    # Self-check: an oracle that lies is worse than no oracle.  Replaying the PV
    # recovers the per-seat counts independently, and re-encoding them must land
    # back on the value the search reported.
    seat_tricks = replay_pv_by_seat(position, list(pv))
    live_broken = sum(1 for seat in nil_seats if seat_tricks[seat] > 0)
    # The value the search optimised only counts LIVE bids going down, so that
    # is what has to be re-encoded -- but the reported count is how many bids
    # are down in total, which includes the ones the caller declared.
    nils_set = live_broken + already_down
    # Every seat on the pair, live or not: the secondary level is the PAIR's
    # tricks, and a broken bidder's tricks still count for it.
    nil_side = sum(seat_tricks[seat] for seat in pair_seats)
    replayed = primary_weight * live_broken + secondary_weight * nil_side
    if replayed != value:
        raise AssertionError(
            f"internal inconsistency: search says {value}, replaying the PV gives "
            f"{replayed} (live bids down={live_broken}, nil_side={nil_side})"
        )

    return MultiNilSolution(
        roles=list(roles),
        seat_tricks=seat_tricks,
        nils_set=nils_set,
        nil_seats=list(pair_seats),
        nil_side_tricks=nil_side,
        opponent_tricks=sum(seat_tricks) - nil_side,
        value=value,
        pv=list(pv),
        nodes=ctx.nodes,
        position=position,
        secondary=secondary,
    )


def solve_seats(
    position: Position,
    roles: Sequence[int],
    use_memo: bool = False,
    secondary: str = "max",
):
    """Solve whatever arrangement `roles` describes, or raise saying it cannot.

    Returns a Solution for a single nil and a MultiNilSolution for a pair that
    both bid.  The two are different objectives rather than one generalised
    one -- see multi_objective_weights -- so they are different result types.
    """
    shape = role_shape(roles)
    if shape == SHAPE_OPPOSING_NILS:
        return solve_opposing_nils(position, roles, use_memo=use_memo, secondary=secondary)
    if shape == SHAPE_SINGLE_NIL:
        return solve(
            position,
            nil_seat_of(roles),
            use_memo=use_memo,
            secondary=secondary,
            nil_already_set=nil_already_set_of(roles),
        )
    return solve_partner_nils(position, roles, use_memo=use_memo, secondary=secondary)


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

    @property
    def nils_set(self) -> int:
        """How many bids are broken: 0 or 1 here, 0..2 for a pair that both bid.

        The count rather than a flag, so that both result types answer the same
        question in the same units.  A bid the caller declared already broken
        counts toward it, since the question is how many are down and not how
        many the search knocked down.
        """
        return 1 if (self.nil_already_set or self.tricks > 0) else 0


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

    A thin reading of replay_pv_by_seat, which does the actual work.  Every
    count here is derived from the per-seat one, so the two verifiers cannot
    disagree.
    """
    seat_tricks = replay_pv_by_seat(position, pv)
    return Tally(
        seat_tricks[designated],
        seat_tricks[designated] + seat_tricks[(designated + 2) % 4],
        seat_tricks[(designated + 1) % 4] + seat_tricks[(designated + 3) % 4],
    )


def replay_pv_by_seat(position: Position, pv: Sequence[Play]) -> List[int]:
    """Replay a PV and return the tricks each SEAT took, indexed by seat.

    The role-agnostic verifier.  Who is nil and who is covering does not enter
    into it -- a trick is won by a seat, and every objective this file supports
    is a reading of these four numbers.  Raises on any illegal or out-of-turn
    play, or if the PV does not exhaust every hand.
    """
    hands = [list(h) for h in position.hands]
    leader = position.leader
    broken = position.spades_broken
    trick = list(position.current_trick)
    seat_tricks = [0, 0, 0, 0]

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
            seat_tricks[winner] += 1
            leader = winner
            trick = []

    if trick:
        raise ValueError("PV ends mid-trick")
    if any(hands):
        raise ValueError("PV does not play out every card")
    return seat_tricks


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


def format_opposing_solution(solution: OpposingNilSolution, compact: bool = False) -> str:
    pos = solution.position
    names = [SEAT_CHARS[x] for x in solution.nil_of_side]
    if compact:
        return (
            f"shape={SHAPE_OPPOSING_NILS}\n"
            f"strictly_opposed={1 if solution.strictly_opposed else 0}\n"
            f"nil_makes={' '.join('1' if m else '0' for m in solution.nil_makes)}\n"
            f"ranks={' '.join(str(r) for r in solution.ranks)}\n"
            f"side_tricks={' '.join(str(t) for t in solution.side_tricks)}\n"
            f"seat_tricks={' '.join(str(n) for n in solution.seat_tricks)}\n"
            f"pv={' '.join(f'{SEAT_CHARS[a]}:{c}' for a, c in solution.pv)}\n"
        )
    takes = "takes" if solution.secondary == "max" else "sheds"
    out = [
        f"PBN            {pos.to_pbn()}",
        format_hands(pos),
        f"Leader         {SEAT_CHARS[pos.leader]}",
        f"Seats          {describe_roles(solution.roles)}",
        f"Objective      each side ranks the four outcomes its own way, then {takes}",
        f"               tricks.  The role on a bidder's PARTNER decides only the",
        f"               middle: 2 saves our own bid first, 3 sets theirs first",
        f"Game           "
        + (
            "strictly opposed -- the two rankings sum to a constant, so this is "
            "an ordinary two-team game"
            if solution.strictly_opposed
            else "NOT strictly opposed -- both sides rank the middle two outcomes "
            "the same way, so no single scalar describes it"
        ),
        f"Spades broken  {'yes' if pos.spades_broken else 'no'}",
    ]
    if pos.current_trick:
        out.append(f"On the trick   {cards_str(pos.current_trick)}")
    for i in (0, 1):
        verdict = "MAKES" if solution.nil_makes[i] else "FAILS"
        out.append(
            f"  {names[i]}'s nil      {verdict}   (rank {solution.ranks[i]} of 3, "
            f"side took {solution.side_tricks[i]})"
        )
    out += [
        "Tricks by seat " + " ".join(
            f"{SEAT_CHARS[i]}={n}" for i, n in enumerate(solution.seat_tricks)
        ),
        f"Nodes          {solution.nodes}",
        f"Utilities      N/S={solution.utility[0]}  E/W={solution.utility[1]}",
        "",
        "Principal variation",
        " ".join(f"{SEAT_CHARS[a]}:{c}" for a, c in solution.pv),
    ]
    return "\n".join(out)


def format_multi_solution(solution: MultiNilSolution, compact: bool = False) -> str:
    pos = solution.position
    per_seat = " ".join(
        f"{SEAT_CHARS[i]}={n}" for i, n in enumerate(solution.seat_tricks)
    )
    if compact:
        return (
            f"shape={SHAPE_PARTNER_NILS}\n"
            f"nils_set={solution.nils_set}\n"
            f"seat_tricks={' '.join(str(n) for n in solution.seat_tricks)}\n"
            f"nil_side_tricks={solution.nil_side_tricks}\n"
            f"opponent_tricks={solution.opponent_tricks}\n"
            f"pv={' '.join(f'{SEAT_CHARS[a]}:{c}' for a, c in solution.pv)}\n"
        )
    takes = (
        "the pair takes as many as it can"
        if solution.secondary == "max"
        else "the pair takes as few as it can"
    )
    direction = "then " + takes
    nil_names = "/".join(SEAT_CHARS[x] for x in solution.nil_seats)
    out = [
        f"PBN            {pos.to_pbn()}",
        format_hands(pos),
        f"Leader         {SEAT_CHARS[pos.leader]}",
        f"Seats          {describe_roles(solution.roles)}",
        f"               ({nil_names} minimize nils set, "
        f"the other pair maximizes it)",
        f"Objective      "
        + (
            f"both bids already down, so {takes}"
            if all(r != ROLE_NIL for r in solution.roles)
            else f"how many of the two nils are set, {direction}"
        ),
        f"Spades broken  {'yes' if pos.spades_broken else 'no'}",
    ]
    if pos.current_trick:
        out.append(
            f"On the trick   {cards_str(pos.current_trick)}"
            "   (already played, in order from the leader)"
        )
    out += [
        f"Nils set       {solution.nils_set} of 2",
    ]
    for seat in solution.nil_seats:
        verdict = "FAILS" if solution.nil_fails(seat) else "MAKES"
        out.append(
            f"  {SEAT_CHARS[seat]}            {verdict}  "
            f"({solution.seat_tricks[seat]} trick(s))"
        )
    out += [
        f"Tricks by seat {per_seat}",
        f"Side tricks    nil pair={solution.nil_side_tricks}  "
        f"opponents={solution.opponent_tricks}",
        f"Nodes          {solution.nodes}",
        f"Value          {solution.value}",
        "",
        "Principal variation",
        " ".join(f"{SEAT_CHARS[a]}:{c}" for a, c in solution.pv),
    ]
    return "\n".join(out)


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
            f"nils_set={solution.nils_set}\n"
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
        f"Seats          {describe_roles(roles_from_nil(solution.designated, solution.nil_already_set))}"
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
    # ---------------------------------------------------------------- two nils
    if verbose:
        print("Two nils on the same side")

    # Shape recognition, and the refusals that guard it.
    check("single nil is recognised", role_shape([0, 3, 2, 3]), SHAPE_SINGLE_NIL)
    check("partner nils are recognised", role_shape([0, 3, 0, 3]), SHAPE_PARTNER_NILS)
    for bad, why in (
        ([0, 0, 1, 3], "an already-broken nil opposite a live one"),
        ([0, 3, 0, 2], "a cover with nobody to cover"),
        ([0, 0, 0, 3], "three nils"),
    ):
        try:
            role_shape(bad)
            check(f"refuses {why}", "accepted", "raised")
        except ValueError:
            check(f"refuses {why}", "raised", "raised")

    # The weights must separate the levels: no run of tricks can outweigh one
    # more nil going down.
    for t in (2, 3, 5, 9, 13):
        pw, sw = multi_objective_weights(t, "max")
        check(f"weights separate at {t} tricks", abs(sw) * t < pw, True)
    check("max wants the pair's tricks", multi_objective_weights(4, "max")[1] < 0, True)
    check("min wants rid of them", multi_objective_weights(4, "min")[1] > 0, True)

    # A hand-checkable ending.  One trick left, N and S both bid, and the only
    # question is who takes it.  W is on lead holding a card that cannot win.
    tiny = Position(
        hands=(
            (Card(1, 2),),
            (Card(1, 3),),
            (Card(1, 4),),
            (Card(1, 5),),
        ),
        leader=3,
        spades_broken=True,
    )
    tiny_sol = solve_partner_nils(tiny, [0, 3, 0, 3])
    check("W's five wins, so no nil is set", tiny_sol.nils_set, 0)
    check("and the pair takes nothing", tiny_sol.nil_side_tricks, 0)

    # The same ending with the high card in a nil hand: it must take the trick,
    # so exactly one bid dies and nothing can prevent it.
    forced = Position(
        hands=(
            (Card(1, 5),),
            (Card(1, 3),),
            (Card(1, 4),),
            (Card(1, 2),),
        ),
        leader=3,
        spades_broken=True,
    )
    forced_sol = solve_partner_nils(forced, [0, 3, 0, 3])
    check("N is forced to win", forced_sol.nils_set, 1)
    check("and it is N that dies", forced_sol.seat_tricks[0], 1)
    check("S survives", forced_sol.seat_tricks[2], 0)

    # CONCENTRATION.  Two tricks the pair cannot avoid taking.  Splitting them
    # kills both bids; funnelling both through one seat kills one.  The pair
    # must prefer the funnel even though the trick counts are identical.
    funnel = Position(
        hands=(
            (Card(1, 14), Card(2, 14)),
            (Card(1, 2), Card(2, 2)),
            (Card(1, 3), Card(2, 3)),
            (Card(1, 4), Card(2, 4)),
        ),
        leader=0,
        spades_broken=True,
    )
    funnel_sol = solve_partner_nils(funnel, [0, 3, 0, 3])
    check("both tricks land on the pair", funnel_sol.nil_side_tricks, 2)
    check("but only one bid dies", funnel_sol.nils_set, 1)
    check("because N took both", funnel_sol.seat_tricks[0], 2)

    # Memoization is memoization: same value, same line.
    for seed in (11, 29, 47):
        fx = random_fixture(seed=seed, cards_per_hand=4, leader=0, designated=0)
        plain = solve_partner_nils(fx.position, [0, 3, 0, 3])
        memoed = solve_partner_nils(fx.position, [0, 3, 0, 3], use_memo=True)
        check(f"seed {seed}: memo agrees on value", memoed.value, plain.value)
        check(f"seed {seed}: memo agrees on PV", memoed.pv, plain.pv)

    # A nil the pair can hold TOGETHER can be held ALONE.  If the two-nil game
    # ends with nothing set, the pair had a strategy guaranteeing it, and that
    # same strategy is available in the single-nil game -- where the partner is
    # under no constraint at all, so it can only do better.  The converse does
    # not hold, and that gap is the whole content of the multi-nil question.
    for seed in (3, 8, 21, 34, 55):
        fx = random_fixture(seed=seed, cards_per_hand=4, leader=0, designated=0)
        both = solve_partner_nils(fx.position, [0, 3, 0, 3], use_memo=True)
        if both.nils_set == 0:
            for seat in (0, 2):
                alone = solve(fx.position, seat, use_memo=True)
                check(
                    f"seed {seed}: {SEAT_CHARS[seat]} safe together implies safe alone",
                    alone.tricks,
                    0,
                )

    # Permuting the three non-spade suits is a relabelling, not a different
    # deal, so neither level of the objective may move.
    for seed in (7, 19):
        fx = random_fixture(seed=seed, cards_per_hand=4, leader=0, designated=0)
        base = solve_partner_nils(fx.position, [0, 3, 0, 3], use_memo=True)
        swapped = Position(
            hands=tuple(
                tuple(
                    Card({1: 2, 2: 1}.get(c.suit, c.suit), c.rank)
                    for c in hand
                )
                for hand in fx.position.hands
            ),
            leader=fx.position.leader,
            spades_broken=fx.position.spades_broken,
        )
        other = solve_partner_nils(swapped, [0, 3, 0, 3], use_memo=True)
        check(f"seed {seed}: suit swap holds nils set", other.nils_set, base.nils_set)
        check(
            f"seed {seed}: suit swap holds the pair's tricks",
            other.nil_side_tricks,
            base.nil_side_tricks,
        )

    # ------------------------------------------------------ one nil per side
    if verbose:
        print("One nil on each side")

    check("mixed roles are recognised", role_shape([0, 3, 2, 0]), SHAPE_OPPOSING_NILS)
    check("two protective partners too", role_shape([0, 2, 2, 0]), SHAPE_OPPOSING_NILS)
    check("two aggressive partners too", role_shape([0, 3, 3, 0]), SHAPE_OPPOSING_NILS)
    for bad, why in (
        ([0, 0, 0, 3], "a third bid"),
        ([0, 1, 2, 0], "a partner that is itself a bid"),
    ):
        try:
            role_shape(bad)
            check(f"refuses {why}", "accepted", "raised")
        except ValueError:
            check(f"refuses {why}", "raised", "raised")

    # THE RANKING, straight from the specification.  Both sides agree on the
    # best and worst outcome; the partner's role decides only the middle.
    check("mine makes, theirs fails is best", side_rank(True, False, ROLE_COVER), 3)
    check("and best however I lean", side_rank(True, False, ROLE_OPPONENT), 3)
    check("mine fails, theirs makes is worst", side_rank(False, True, ROLE_COVER), 0)
    check("and worst however I lean", side_rank(False, True, ROLE_OPPONENT), 0)
    check("protective prefers both making", side_rank(True, True, ROLE_COVER) >
          side_rank(False, False, ROLE_COVER), True)
    check("aggressive prefers both failing", side_rank(False, False, ROLE_OPPONENT) >
          side_rank(True, True, ROLE_OPPONENT), True)

    # WHICH SHAPES ARE ORDINARY TWO-TEAM GAMES.  The ranks summing to a constant
    # is exactly the condition, and it is a property of the roles alone.  Only
    # the mixed shape has it; that is why it can be solved by minimax and the
    # other two cannot.
    def rank_sums(roles):
        return {
            side_rank(a, b, roles[2]) + side_rank(b, a, roles[1])
            for a in (True, False)
            for b in (True, False)
        }

    check("mixed is strictly opposed", len(rank_sums([0, 3, 2, 0])), 1)
    check("mirrored mixed is too", len(rank_sums([0, 2, 3, 0])), 1)
    check("both protective is NOT", len(rank_sums([0, 2, 2, 0])) > 1, True)
    check("both aggressive is NOT", len(rank_sums([0, 3, 3, 0])) > 1, True)

    # Weights separate the levels: no run of tricks outweighs one step of rank.
    for t in (2, 3, 5, 9, 13):
        rw, tw = opposing_weights(t, "max")
        check(f"rank outranks tricks at {t}", abs(tw) * t < rw, True)

    # A hand-checkable ending: one trick, W on lead with the only high card, so
    # neither bidder can be made to win it and both bids survive.
    # East leads the only high card, and East partners a bidder rather than
    # being one, so the trick lands on a seat with no bid to lose.
    both_live = Position(
        hands=((Card(1, 2),), (Card(1, 5),), (Card(1, 3),), (Card(1, 4),)),
        leader=1,
        spades_broken=True,
    )
    for seats in ([0, 3, 2, 0], [0, 2, 2, 0], [0, 3, 3, 0]):
        sol = solve_opposing_nils(both_live, seats)
        check(f"{seats}: both bids survive", sol.nil_makes, [True, True])

    # Memoization is memoization, on every shape.
    for seats in ([0, 3, 2, 0], [0, 2, 2, 0], [0, 3, 3, 0]):
        for seed in (13, 41):
            fx = random_fixture(seed=seed, cards_per_hand=4, leader=0, designated=0)
            plain = solve_opposing_nils(fx.position, seats)
            memoed = solve_opposing_nils(fx.position, seats, use_memo=True)
            check(f"{seats} seed {seed}: memo agrees on utility", memoed.utility,
                  plain.utility)
            check(f"{seats} seed {seed}: memo agrees on PV", memoed.pv, plain.pv)

    # THE SPECIFICATION'S OWN CLAIM, tested where it bites.  Take deals whose
    # mixed-shape answer already trades one bid for the other, and check that
    # flipping a partner's role moves that side's outcome the way the role says.
    swung = 0
    for seed in range(60):
        fx = random_fixture(seed=1000 + seed, cards_per_hand=4, leader=0, designated=0)
        protective = solve_opposing_nils(fx.position, [0, 3, 2, 0], use_memo=True)
        aggressive = solve_opposing_nils(fx.position, [0, 3, 3, 0], use_memo=True)
        # Side 0's partner went from "save ours" to "set theirs".  It can never
        # end up in a state its own ranking calls worse than what the other
        # setting reached, judged by ITS OWN ranking.
        own = lambda sol, role: side_rank(sol.nil_makes[0], sol.nil_makes[1], role)
        check_silent = own(aggressive, ROLE_OPPONENT) >= own(protective, ROLE_OPPONENT)
        if not check_silent:
            check(f"seed {seed}: flipping the role cannot hurt that side", False, True)
        if protective.nil_makes != aggressive.nil_makes:
            swung += 1
    check("the role actually changes some outcomes", swung > 0, True)


    # ------------------------------------------- the conjunction probe (78)
    if verbose:
        print("Can one side break the other's bid while keeping its own")

    # TWO ALGORITHMS, ONE QUESTION.  `_search_conjunction` is a boolean AND-OR
    # search; `_search_opposing` is backward induction on a utility pair with a
    # trick tie-break underneath.  The conjunction is exactly outcome rank 3 for
    # the attacking side, so the two must agree on every fixture -- and they are
    # written independently, so agreeing is evidence rather than a tautology.
    #
    # RUN ON ALL SIXTEEN ARRANGEMENTS, including the eight the C++ refuses.  The
    # equivalence does not need strict opposition: rank 3 for one side is rank 0
    # for the other under every partner lean, so the defender escapes it whenever
    # it can whichever way it leans.
    conj_shapes = []
    for first in range(4):
        for lean_a, lean_b in ((2, 3), (3, 2), (2, 2), (3, 3)):
            roles = [None] * 4
            roles[first] = ROLE_NIL
            roles[(first + 1) % 4] = ROLE_NIL
            roles[(first + 2) % 4] = lean_a
            roles[(first + 3) % 4] = lean_b
            if role_shape(roles) == SHAPE_OPPOSING_NILS:
                conj_shapes.append(tuple(roles))
    conj_shapes = sorted(set(conj_shapes))
    check("every opposing arrangement is covered", len(conj_shapes), 16)

    mismatches = 0
    forced = 0
    compared = 0
    for seed in range(6):
        fixture = random_fixture(cards_per_hand=3, seed=seed)
        for roles in conj_shapes:
            opposing = solve_opposing_nils(fixture.position, list(roles), use_memo=True)
            for side in (0, 1):
                conj = solve_conjunction(fixture.position, list(roles), side, use_memo=True)
                compared += 1
                forced += 1 if conj.can_force else 0
                if conj.can_force != (opposing.ranks[side] == 3):
                    mismatches += 1
    check("boolean search agrees with the utility search", mismatches, 0)
    # A check that only ever sees False would pass while measuring nothing, so
    # the population is asserted too: the conjunction must be forceable
    # sometimes and not others.
    check("...on a population that is not all one answer",
          0 < forced < compared, True)

    # The attacker's own bid being down is a terminal FALSE, and it has to be:
    # a bid never un-breaks, so the conjunction can never come back.
    check("a broken attacker bid is unforceable",
          _search_conjunction((), 0, (), False, 1 << 0,
                              _ConjCtx(nil_of_side=(0, 1), attacker=0)),
          False)
    check("...and the mirror for the other side",
          _search_conjunction((), 0, (), False, 1 << 1,
                              _ConjCtx(nil_of_side=(0, 1), attacker=1)),
          False)
    check("both bids down is still False for either attacker",
          _search_conjunction((), 0, (), False, 0b11,
                              _ConjCtx(nil_of_side=(0, 1), attacker=0)),
          False)
    check("only the defender's bid down, on an empty deal, is True",
          _search_conjunction((), 0, (), False, 1 << 1,
                              _ConjCtx(nil_of_side=(0, 1), attacker=0)),
          True)

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
            "  %(prog)s --random --seed 42 --cards 5 --leader N --seats 3 3 0 2\n"
            "  %(prog)s --pbn 'N:A...2 K...3 Q...4 J...5' --leader N --seats 0 3 2 3\n"
            "  %(prog)s --random --seed 42 --cards 5 --compact   # diff-friendly\n"
            "  %(prog)s --pbn '...' --seats 0 3 2 3 --secondary min\n"
            "  %(prog)s --pbn '...' --seats 1 3 2 3   # the nil is already set\n"
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
    p.add_argument(
        "--seats",
        nargs="+",
        default=None,
        metavar="R",
        help="what each seat is doing, clockwise from the seat --pbn names: "
        "0 = nil bidder with no trick yet, 1 = nil already broken, 2 = the "
        "partner covering it, 3 = a seat on a side with no nil.  Exactly one "
        "nil and its partner covering [0 3 2 3]",
    )
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
    p.add_argument(
        "--conjunction",
        default=None,
        metavar="SEAT",
        help="with a bid on each side, answer only whether the side holding SEAT "
             "can force ITS bid to survive while the other's dies (item 78's "
             "probe). SEAT is N/E/S/W and must be one of the two bidders",
    )
    p.add_argument("--selftest", action="store_true", help="run the built-in checks")
    args = p.parse_args(argv)

    if args.selftest:
        return 1 if selftest() else 0

    leader = seat_from_str(args.leader) if args.leader else None
    seats_text = " ".join(args.seats) if args.seats else "0 3 2 3"

    try:
        if args.random or not args.pbn:
            # A generated deal is written from North, so that is the anchor the
            # roles are read against.
            roles = parse_roles(seats_text, 0)
            validate_roles(roles)
            fx = random_fixture(
                seed=args.seed,
                cards_per_hand=args.cards,
                leader=leader if leader is not None else 0,
                designated=nil_seat_of(roles),
                spades_broken=args.spades_broken,
            )
            hands = fx.position.hands
            leader = fx.position.leader
        else:
            hands = parse_pbn(args.pbn)
            roles = parse_roles(seats_text, pbn_anchor(args.pbn))
            validate_roles(roles)
            leader = 0 if leader is None else leader
        designated = nil_seat_of(roles)

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

    if args.conjunction is not None:
        # ITEM 78'S PROBE, ASKED ON ITS OWN.  Answered by the boolean search
        # rather than read off `solve_opposing_nils`, because the point of having
        # it is that the two are independent; `selftest` is where they are made
        # to agree.
        letter = args.conjunction.strip().upper()[:1]
        if letter not in SEAT_CHARS:
            print(f"error: bad --conjunction seat '{args.conjunction}'", file=sys.stderr)
            return 2
        seat = SEAT_CHARS.index(letter)
        if role_shape(roles) != SHAPE_OPPOSING_NILS:
            print("error: --conjunction needs a bid on each side", file=sys.stderr)
            return 2
        if roles[seat] not in (ROLE_NIL, ROLE_NIL_SET):
            print(f"error: {letter} did not bid nil; --conjunction names the "
                  f"ATTACKING side's bidder", file=sys.stderr)
            return 2
        conj = solve_conjunction(position, roles, seat % 2, use_memo=args.memo)
        att = SEAT_CHARS[conj.attacker_bid]
        dfn = SEAT_CHARS[conj.defender_bid]
        if args.compact:
            print(f"conjunction={1 if conj.can_force else 0}")
            print(f"attacker={att}")
            print(f"defender={dfn}")
            print(f"nodes={conj.nodes}")
        else:
            verdict = "CAN" if conj.can_force else "CANNOT"
            print(f"{att}'s side {verdict} force {att}'s bid to survive while "
                  f"{dfn}'s dies  ({conj.nodes} nodes)")
        return 0

    solution = solve_seats(
        position,
        roles,
        use_memo=args.memo,
        secondary=args.secondary,
    )
    if isinstance(solution, OpposingNilSolution):
        print(format_opposing_solution(solution, compact=args.compact))
        return 0
    if isinstance(solution, MultiNilSolution):
        print(format_multi_solution(solution, compact=args.compact))
        return 0
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
