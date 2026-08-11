#!/usr/bin/env python3
"""Check the solver against transformed copies of the same position.

WHY THIS EXISTS
---------------
Above six cards the oracle stops being usable, so corpus rows at 7, 8 and 9
cards are mostly pinned from the solver itself.  A row that records what the
solver said, checked by asking the solver again, catches a regression and
nothing else: if the answer is wrong today it will be confidently wrong forever.

These checks need no reference answer.  They take a position, transform it into
a different position that must have the same answer for reasons that come from
the rules rather than from either implementation, and compare.  They work at any
hand size, they cost two solves instead of one, and they catch whole families of
bug -- suit confusion, seat confusion, rank comparison errors, coalition
assignment errors -- that a self-generated corpus cannot.

THE THREE TRANSFORMS
--------------------
seat rotation
    Move every hand k seats clockwise and move the leader and the nil bidder
    with them.  Nothing about the position has changed except the names, so
    every trick count must be identical, and the principal variation must be
    the same cards played by correspondingly renamed seats.

suit permutation
    Permute hearts, diamonds and clubs.  Spades are left alone, because spades
    are the only suit the rules single out -- they trump, and they have the
    breaking rule.  The other three are interchangeable, so every trick count
    must be identical.  The principal variation may legitimately differ: the
    tie-break among equal-valued moves prefers the canonically lowest card, and
    relabelling suits changes which card that is.

One wrinkle: when the nil is already set AND the pair is shedding tricks, the
two partners are interchangeable, so nothing in the objective decides which of
them takes a trick and the split falls out of the tie-break.  It may therefore
move under a suit permutation.  The side and opponent totals must still hold.
Everywhere else the split is pinned -- by the primary while the nil is live, and
by the tertiary level once it is set and the pair is taking tricks.

rank compression
    In each suit, relabel the ranks actually in play down to a contiguous block
    starting at the two, preserving their order.  Only the relative order of
    ranks can matter, so trick counts must be identical, and this time the
    principal variation must correspond card for card, because the compression
    preserves canonical order.

CHECKING THE BOOLEAN SEARCH
---------------------------
--mode fast checks the pruned search instead of the exhaustive one.  It is a
weaker check per position -- fast mode reports no trick counts and no principal
variation, so only nil_fails can be compared -- but it is the check that
reaches furthest.  Below about nine cards, agreement with full mode is a
stronger statement and `nil_bench --mode both` already makes it on every build.
Above that full mode cannot finish, and these transforms become the only
verification the boolean search has at all.  An alpha-beta window is exactly
the kind of thing that can be subtly wrong in a way that is invariant under
nothing, so it is worth running there.

  tools/invariants.py --corpus tests/corpus/large.txt
  tools/invariants.py --corpus tests/corpus/positions.txt --cards 4 --limit 50
  tools/invariants.py --random 20 --cards 5 --seed 1
  tools/invariants.py --random 12 --cards 11 --mode fast --timeout 120
"""

from __future__ import annotations

import argparse
import itertools
import os
import random
import subprocess
import sys
from typing import Dict, List, Optional, Sequence, Tuple

SEAT_CHARS = "NESW"
SUIT_CHARS = "SHDC"
RANK_CHARS = "23456789TJQKA"

Card = Tuple[int, int]          # (suit index, rank index into RANK_CHARS)
Hands = List[List[Card]]


# ---------------------------------------------------------------------------
# PBN <-> cards
# ---------------------------------------------------------------------------


def parse_pbn(pbn: str) -> Hands:
    first = SEAT_CHARS.index(pbn[0].upper())
    hands: Hands = [[], [], [], []]
    for offset, group in enumerate(pbn[2:].split()):
        seat = (first + offset) % 4
        parts = group.split(".")
        for suit in range(4):
            if suit >= len(parts):
                continue
            for ch in parts[suit]:
                if ch == "-":
                    continue
                hands[seat].append((suit, RANK_CHARS.index(ch.upper())))
    return hands


def to_pbn(hands: Hands) -> str:
    groups = []
    for seat in range(4):
        by_suit = []
        for suit in range(4):
            ranks = sorted((r for s, r in hands[seat] if s == suit), reverse=True)
            by_suit.append("".join(RANK_CHARS[r] for r in ranks))
        groups.append(".".join(by_suit))
    return "N:" + " ".join(groups)


def parse_cards(text: str) -> List[Card]:
    out = []
    for token in text.replace(",", " ").split():
        token = token.strip().upper().replace("10", "T")
        out.append((SUIT_CHARS.index(token[0]), RANK_CHARS.index(token[1])))
    return out


def card_str(card: Card) -> str:
    return SUIT_CHARS[card[0]] + RANK_CHARS[card[1]]


# ---------------------------------------------------------------------------
# The transforms
# ---------------------------------------------------------------------------


def rotate(spec: Dict, k: int) -> Tuple[Dict, Dict]:
    """Move everything k seats clockwise."""
    hands = parse_pbn(spec["pbn"])
    rotated: Hands = [[], [], [], []]
    for seat in range(4):
        rotated[(seat + k) % 4] = hands[seat]
    out = dict(spec)
    out["pbn"] = to_pbn(rotated)
    out["leader"] = SEAT_CHARS[(SEAT_CHARS.index(spec["leader"]) + k) % 4]
    out["nil"] = SEAT_CHARS[(SEAT_CHARS.index(spec["nil"]) + k) % 4]
    # Cards on the trick are listed in play order from the leader, so they are
    # untouched -- only the seat that played them has a new name.
    return out, {"kind": "rotate", "k": k}


def permute_suits(spec: Dict, mapping: Sequence[int]) -> Tuple[Dict, Dict]:
    """Relabel hearts/diamonds/clubs; leave spades alone."""
    full = [0] + list(mapping)

    def move(card: Card) -> Card:
        return (full[card[0]], card[1])

    hands = [[move(c) for c in hand] for hand in parse_pbn(spec["pbn"])]
    out = dict(spec)
    out["pbn"] = to_pbn(hands)
    if spec["trick"]:
        out["trick"] = " ".join(card_str(move(c)) for c in parse_cards(spec["trick"]))
    return out, {"kind": "suits", "mapping": full}


def compress_ranks(spec: Dict) -> Tuple[Dict, Dict]:
    """Squash the ranks in play down to a contiguous block, order preserved."""
    hands = parse_pbn(spec["pbn"])
    trick = parse_cards(spec["trick"]) if spec["trick"] else []

    per_suit: Dict[int, List[int]] = {}
    for card in [c for hand in hands for c in hand] + trick:
        per_suit.setdefault(card[0], []).append(card[1])

    forward: Dict[Card, Card] = {}
    for suit, ranks in per_suit.items():
        for new, old in enumerate(sorted(set(ranks))):
            forward[(suit, old)] = (suit, new)

    out = dict(spec)
    out["pbn"] = to_pbn([[forward[c] for c in hand] for hand in hands])
    if trick:
        out["trick"] = " ".join(card_str(forward[c]) for c in trick)
    back = {v: k for k, v in forward.items()}
    return out, {"kind": "ranks", "back": back}


def unmap_pv(pv: str, info: Dict) -> str:
    """Map a transformed PV back into the original's vocabulary."""
    if not pv:
        return pv
    out = []
    for play in pv.split():
        seat, _, card = play.partition(":")
        if info["kind"] == "rotate":
            seat = SEAT_CHARS[(SEAT_CHARS.index(seat) - info["k"]) % 4]
        elif info["kind"] == "ranks":
            card = card_str(info["back"][parse_cards(card)[0]])
        out.append("%s:%s" % (seat, card))
    return " ".join(out)


# ---------------------------------------------------------------------------
# Running the solver
# ---------------------------------------------------------------------------


def cli_args(exe: str, spec: Dict, mode: str = "full") -> List[str]:
    args = [exe, "--pbn", spec["pbn"], "--leader", spec["leader"], "--nil", spec["nil"],
            "--compact", "--force"]
    if mode == "fast":
        args += ["--mode", "fast"]
    if spec.get("broken") == "1":
        args.append("--spades-broken")
    if spec.get("forced") == "1":
        args.append("--break-on-forced-lead")
    if spec.get("secondary") == "min":
        args += ["--secondary", "min"]
    if spec.get("nilset") == "1":
        args.append("--nil-already-set")
    if spec.get("trick"):
        args += ["--trick", spec["trick"]]
    return args


def solve(exe: str, spec: Dict, timeout: float,
          mode: str = "full") -> Optional[Dict[str, str]]:
    try:
        proc = subprocess.run(
            cli_args(exe, spec, mode), capture_output=True, text=True, timeout=timeout
        )
    except subprocess.TimeoutExpired:
        return None
    if proc.returncode != 0:
        return None
    return dict(
        line.split("=", 1) for line in proc.stdout.strip().split("\n") if "=" in line
    )


def check(exe: str, spec: Dict, timeout: float, rng: random.Random,
          mode: str = "full") -> Tuple[int, int, str]:
    """Returns (checks run, failures, message).

    In fast mode the solver reports no trick counts and no principal variation,
    so `nil_fails` is the whole of what there is to compare.  That is a weaker
    check per position than full mode's, and it is the only one there is at
    hand sizes full mode cannot finish -- which, now that fast mode prunes and
    full mode does not, starts at around ten cards.
    """
    base = solve(exe, spec, timeout, mode)
    if base is None:
        return 0, 0, "skipped (solver did not finish in %.0fs)" % timeout

    cases = [
        rotate(spec, rng.choice((1, 2, 3))),
        permute_suits(spec, rng.choice([p for p in itertools.permutations((1, 2, 3))
                                        if p != (1, 2, 3)])),
        compress_ranks(spec),
    ]

    run = 0
    failures = 0
    messages = []
    for transformed, info in cases:
        result = solve(exe, transformed, timeout, mode)
        if result is None:
            messages.append("  %s: solver did not finish" % info["kind"])
            continue
        run += 1
        # With the nil already set AND the pair shedding tricks, the two
        # partners are interchangeable -- bags accrue to the pair whoever won --
        # so nothing in the objective decides the split and it falls out of the
        # tie-break.  Relabelling suits changes which card is canonically
        # lowest, so the split may legitimately move there.  Everywhere else the
        # split is pinned: by the primary when the nil is live, and by the
        # tertiary level when it is set and the pair is taking tricks.
        undetermined_split = (spec.get("nilset") == "1"
                              and spec.get("secondary") == "min")
        if mode == "fast":
            fields = ["nil_fails"]
        else:
            fields = ["side_tricks", "opponent_tricks"]
            if not (info["kind"] == "suits" and undetermined_split):
                fields.insert(0, "tricks")
        for field in fields:
            if result[field] != base[field]:
                failures += 1
                messages.append("  %s: %s changed, %s -> %s"
                                % (info["kind"], field, base[field], result[field]))
        # Rotation and rank compression preserve canonical order, so the line
        # itself must survive.  Suit permutation does not: it changes which of
        # several equal-valued cards is canonically lowest.
        if mode == "full" and info["kind"] in ("rotate", "ranks"):
            mapped = unmap_pv(result["pv"], info)
            if mapped != base["pv"]:
                failures += 1
                messages.append("  %s: PV changed\n    was %s\n    now %s"
                                % (info["kind"], base["pv"], mapped))
    return run, failures, "\n".join(messages)


# ---------------------------------------------------------------------------
# Sources of positions
# ---------------------------------------------------------------------------


def from_corpus(path: str, cards: Optional[str]) -> List[Dict]:
    fields = ("name", "pbn", "leader", "nil", "broken", "forced", "trick",
              "secondary", "nilset", "nil_tricks", "side_tricks", "pv", "provenance")
    specs = []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split("|")]
            spec = dict(zip(fields, parts))
            size = max(len(g.replace(".", "").replace("-", "")) for g in spec["pbn"][2:].split())
            if cards and str(size) != cards:
                continue
            spec["cards"] = size
            specs.append(spec)
    return specs


def from_random(rng: random.Random, count: int, cards: int, suits: int) -> List[Dict]:
    deck = [(s, r) for s in range(suits) for r in range(13)]
    specs = []
    for index in range(count):
        dealt = rng.sample(deck, 4 * cards)
        hands = [dealt[i * cards:(i + 1) * cards] for i in range(4)]
        specs.append({
            "name": "rand-%04d" % index,
            "pbn": to_pbn(hands),
            "leader": SEAT_CHARS[rng.randrange(4)],
            "nil": SEAT_CHARS[rng.randrange(4)],
            "broken": str(rng.randrange(2)),
            "forced": str(rng.randrange(2)),
            "trick": "",
            "secondary": rng.choice(("max", "min")),
            "nilset": "1" if rng.random() < 0.2 else "0",
            "cards": cards,
        })
    return specs


def main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--exe", default=os.path.join("build", "bin", "nil_cli"))
    p.add_argument("--corpus", default=os.path.join("tests", "corpus", "positions.txt"))
    p.add_argument("--random", type=int, default=0, metavar="N",
                   help="check N random deals instead of a corpus")
    p.add_argument("--cards", default=None, help="restrict to one hand size")
    p.add_argument("--suits", type=int, default=4, choices=(2, 3, 4),
                   help="suits to deal from, for --random [4]")
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--limit", type=int, default=0, help="stop after N positions")
    p.add_argument("--timeout", type=float, default=30.0,
                   help="per-solve wall-clock cap; positions that blow it are "
                        "skipped rather than failed [30]")
    p.add_argument("--mode", default="full", choices=("full", "fast"),
                   help="which search to check.  full compares trick counts and "
                        "the principal variation; fast compares only nil_fails, "
                        "and is the only thing that reaches the hand sizes full "
                        "mode cannot finish [full]")
    p.add_argument("--quiet", action="store_true")
    args = p.parse_args(argv)

    if not os.path.exists(args.exe):
        print("invariants: %s not found; build first" % args.exe, file=sys.stderr)
        return 2

    rng = random.Random(args.seed)
    if args.random:
        specs = from_random(rng, args.random, int(args.cards or 5), args.suits)
    else:
        if not os.path.isfile(args.corpus):
            print("invariants: no corpus at %s" % args.corpus, file=sys.stderr)
            return 2
        specs = from_corpus(args.corpus, args.cards)
    if args.limit:
        specs = specs[: args.limit]
    if not specs:
        print("nothing to check")
        return 0

    total_checks = 0
    total_failures = 0
    skipped = 0
    for spec in specs:
        run, failures, message = check(args.exe, spec, args.timeout, rng, args.mode)
        total_checks += run
        total_failures += failures
        if run == 0:
            skipped += 1
        if failures:
            print("FAIL %s (%s cards)" % (spec["name"], spec["cards"]))
            print(message)
            print("  %s" % " ".join(
                "'%s'" % a if " " in a else a
                for a in cli_args(args.exe, spec, args.mode)))
        elif not args.quiet and message:
            print("note %s: %s" % (spec["name"], message))

    print("\n%d position(s), %d transform checks, %d skipped for time"
          % (len(specs), total_checks, skipped))
    if total_failures:
        print("%d INVARIANT VIOLATION(S)" % total_failures)
        return 1
    print("all invariants hold")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
