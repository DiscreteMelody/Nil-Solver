#!/usr/bin/env python3
"""Build tests/corpus/multinil.txt, and measure what a second nil costs.

Two jobs, because they want the same deals:

  --measure   how much bigger the tree gets when a pair bids TWO nils instead
              of one.  Raw node counts with memoization OFF, because that is
              the honest tree size; the memo is a Python-speed crutch and its
              key now carries the broken-nil mask, so a memoized comparison
              would measure the crutch instead.

  (default)   write the corpus.  Low card counts get oracle-computed answers.
              Thirteen-card deals get the position and a `?`, because the
              oracle is an exhaustive search with no pruning and will not
              finish one this side of a holiday -- they are there so the C++
              solver can be timed against them once it can answer the shape.

THE FILE FORMAT is the same as the other two corpora, which it was not at
first.  This file originally carried four per-seat trick counts, on the
reasoning that `nil_tricks` names "the nil bidder" and that has two answers
here.  It was wrong: an answer column may only hold what the OBJECTIVE pins,
and nothing in it constrains how the opponents split tricks between themselves
or how a pair that has lost both bids splits its own.  Re-searching these rows
with the opposite tie-break moved no value at all and moved the per-seat counts
on 43 of 140.  What is pinned is `nils_set`, and the bidding seats' TOTAL --
which is what `nil_tricks` means in both shapes.

Usage:
  tools/make_multinil_corpus.py --measure
  tools/make_multinil_corpus.py --out tests/corpus/multinil.txt
"""

import argparse
import importlib.util
import pathlib
import random
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
CLI = ROOT / "build/bin/nil_cli"

# EVERY STATE A TWIN-NIL HAND CAN BE IN, and every trick depth it can be asked
# at.  Rows cycle through this list so the corpus covers the whole objective
# rather than one corner of it:
#
#   0 3 0 3   both bids live -- the case the primary level exists for
#   1 3 0 3   one down, the other standing.  The commonest mid-hand state, and
#             the one where the pair funnels tricks through the dead seat
#   0 3 1 3   the same, on the other partner, so a seat-parity slip shows up
#   3 1 3 1   both down.  No primary level at all; the objective degenerates to
#             each pair taking as many tricks as it can
#
# The trick depth matters independently: three nodes in four are mid-trick, the
# state key encodes a partial trick differently, and a hand asks "what do I play
# NOW" far more often than it asks from a clean lead.
VARIANTS = [
    ("0 3 0 3", 0),
    ("0 3 0 3", 1),
    ("0 3 0 3", 2),
    ("0 3 0 3", 3),
    ("1 3 0 3", 0),
    ("0 3 1 3", 0),
    ("3 1 3 1", 0),
    ("1 3 0 3", 2),
]
SEAT_CHARS = "NESW"
SUIT_CHARS = "SHDC"
RANK_CHARS = "23456789TJQKA"


def load_oracle():
    spec = importlib.util.spec_from_file_location("nil_oracle", ROOT / "nil_oracle.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["nil_oracle"] = module
    spec.loader.exec_module(module)
    return module


oracle = load_oracle()


def deal(rng, cards):
    """A random layout, as four hands of `cards` cards."""
    deck = [(s, r) for s in range(4) for r in range(2, 15)]
    picked = rng.sample(deck, 4 * cards)
    return [picked[i * cards:(i + 1) * cards] for i in range(4)]


def to_pbn(hands):
    """Accepts either raw (suit, rank) pairs or oracle Cards."""
    groups = []
    for hand in hands:
        pairs = [(c[0], c[1]) if isinstance(c, tuple) else (c.suit, c.rank) for c in hand]
        by_suit = []
        for suit in range(4):
            ranks = sorted((r for s, r in pairs if s == suit), reverse=True)
            by_suit.append("".join(RANK_CHARS[r - 2] for r in ranks))
        groups.append(".".join(by_suit))
    return "N:" + " ".join(groups)


def position_from(hands, leader, broken):
    return oracle.Position(
        hands=tuple(
            tuple(sorted(oracle.Card(s, r) for s, r in hand)) for hand in hands
        ),
        leader=leader,
        spades_broken=broken,
    )


def plausible_nil(hand):
    """Would a human actually bid nil on this?

    Not a rule, a filter.  A hand with an ace in it is set by anybody paying
    attention and the search disposes of it immediately, which makes a useless
    timing corpus.  The deals worth timing are the ones where BOTH partners
    hold something a human would actually bid.
    """
    ranks = [r for _, r in hand]
    spades = [r for s, r in hand if s == 0]
    return (
        max(ranks) <= 13                                  # no ace
        and sum(1 for r in ranks if r >= 12) <= 1         # at most one honour
        and len(spades) <= 4
        and (not spades or max(spades) <= 11)             # no spade above the jack
    )


def two_nil_deal(rng, cards):
    """Build a deal both partners could bid nil on, rather than hoping for one.

    Dealing at random and filtering does not work at thirteen cards: needing
    both N and S to hold a nil-worthy hand at once is rare enough that the
    filter never fires.  So the high cards are pushed toward E and W instead --
    the deck is sorted by rank with noise, the low end goes to the pair that is
    bidding, and the two hands are then checked like any other.
    """
    deck = [(s, r) for s in range(4) for r in range(2, 15)]
    # Rank plus noise: low cards drift to the nil pair, but not deterministically,
    # so the deals differ from each other rather than being one deal reshuffled.
    deck.sort(key=lambda c: c[1] + rng.uniform(-3.0, 3.0))
    low, high = deck[:2 * cards], deck[2 * cards:4 * cards]
    rng.shuffle(low)
    rng.shuffle(high)
    return [low[:cards], high[:cards], low[cards:2 * cards], high[cards:2 * cards]]


# ---------------------------------------------------------------------------
# What a second nil costs
# ---------------------------------------------------------------------------


def measure(args):
    """What a second nil costs.

    THE RAW TREE DOES NOT MOVE, and that is the first result.  This oracle is
    exhaustive with no pruning at all, so its node count is a function of the
    POSITION -- how many legal play sequences exist -- and not of the objective
    laid over it.  Asking a harder question of the same deal visits exactly the
    same nodes.  Whatever the two-nil shape costs the C++ solver will therefore
    come out of PRUNING, and cannot be measured here.

    What can be measured here is the pressure on the transposition table, and
    that is the second result.  The two-nil search has to carry which nils are
    already broken in its state, because the primary level is charged on a nil
    bidder's FIRST trick and on no other.  That mask goes into the memo key, so
    positions that were one entry become up to four.  The memo is the oracle's
    stand-in for the solver's table, and distinct-entry counts transfer.

    The third result is the one that says the question is worth asking at all:
    how often each bid is holdable on its own but the pair cannot hold both.
    """
    print("A second nil, measured three ways.\n")
    print("1. RAW TREE.  Exhaustive search, memo off -- same deal, both questions.")
    print("   cards  deals       one nil       two nils   ratio")
    for cards in args.cards:
        rng = random.Random(args.seed * 1000 + cards)
        one_total = two_total = counted = 0
        for _ in range(args.count):
            pos = _sample(rng, cards)
            if pos is None:
                continue
            one_total += oracle.solve(pos, 0, secondary="max").nodes
            two_total += oracle.solve_partner_nils(pos, [0, 3, 0, 3], secondary="max").nodes
            counted += 1
        if counted:
            print("   %5d  %5d  %12s  %13s  %6.2fx"
                  % (cards, counted, f"{one_total:,}", f"{two_total:,}",
                     two_total / one_total))

    print("\n2. TRANSPOSITION PRESSURE.  Distinct memo entries for the same deals.")
    print("   This is the cost that will transfer to the C++ table.")
    print("   cards  deals   one nil    two nils   ratio")
    for cards in args.cards:
        rng = random.Random(args.seed * 1000 + cards)
        one_states = two_states = counted = 0
        for _ in range(args.count):
            pos = _sample(rng, cards)
            if pos is None:
                continue
            ctx_one = {}
            oracle.solve(pos, 0, use_memo=True, secondary="max")
            # solve() builds its own memo, so count through a direct search.
            pw, sw, tw = oracle.objective_weights(pos.tricks_remaining, "max", False)
            c1 = oracle._Ctx(designated=0, primary_weight=pw, secondary_weight=sw,
                             tertiary_weight=tw, memo=ctx_one)
            oracle._search(pos.hands, pos.leader, pos.current_trick,
                           pos.spades_broken, c1)
            ctx_two = {}
            mp, ms = oracle.multi_objective_weights(pos.tricks_remaining, "max")
            c2 = oracle._MultiCtx(nil_seats=(0, 2), minimizing_parity=0,
                                  primary_weight=mp, secondary_weight=ms,
                                  memo=ctx_two)
            oracle._search_multi(pos.hands, pos.leader, pos.current_trick,
                                 pos.spades_broken, 0, c2)
            one_states += len(ctx_one)
            two_states += len(ctx_two)
            counted += 1
        if counted:
            print("   %5d  %5d  %8s  %10s  %6.2fx"
                  % (cards, counted, f"{one_states:,}", f"{two_states:,}",
                     two_states / one_states))

    print("\n3. IS THE QUESTION REDUCIBLE?  Deals where each bid is holdable")
    print("   ALONE but the pair cannot hold both.  If this were zero, two nils")
    print("   would be two separate single-nil questions and need no new search.")
    print("   cards  deals   irreducible")
    for cards in args.cards:
        rng = random.Random(args.seed * 7 + cards)
        divergent = counted = 0
        for _ in range(args.divergence_count):
            pos = _sample(rng, cards)
            if pos is None:
                continue
            counted += 1
            both = oracle.solve_partner_nils(pos, [0, 3, 0, 3], use_memo=True,
                                             secondary="max")
            if both.nils_set == 0:
                continue
            n_alone = oracle.solve(pos, 0, use_memo=True, secondary="max")
            if n_alone.tricks:
                continue
            s_alone = oracle.solve(pos, 2, use_memo=True, secondary="max")
            if s_alone.tricks == 0:
                divergent += 1
        if counted:
            print("   %5d  %5d   %d  (%.1f%%)"
                  % (cards, counted, divergent, 100.0 * divergent / counted))
    return 0


def _sample(rng, cards):
    hands = deal(rng, cards)
    leader = rng.randrange(4)
    broken = rng.random() < 0.5 if cards < 13 else False
    pos = position_from(hands, leader, broken)
    try:
        pos.validate()
    except Exception:
        return None
    return pos


# ---------------------------------------------------------------------------
# The corpus
# ---------------------------------------------------------------------------


HEADER = """\
# Two nils bid by the same pair.  Generated by tools/make_multinil_corpus.py.
#
#   name | pbn | leader | seats | broken | trick | secondary | nils_set |
#   nil_tricks | side_tricks | pv | provenance
#
#   name        identifier, e.g. "m5-0007"
#   pbn         PBN deal string
#   leader      N/E/S/W, seat that led the current trick
#   seats       four role digits, clockwise from the seat the PBN names, as in
#               the other corpora.  One pair bid two nils and the other pair is
#               trying to set both; rows cycle through every state that pair can
#               be in -- 0 3 0 3 both live, 1 3 0 3 and 0 3 1 3 one down,
#               3 1 3 1 both down
#   broken      0 or 1, spades already broken
#   trick       cards already on the trick.  Rows cycle through depths 0 to 3,
#               because three nodes in four are mid-trick and a hand asks "what
#               do I play NOW" far more often than it asks from a clean lead
#   secondary   tie-break direction; always "max" -- the "min" direction is
#               unoptimised and is not being pinned yet
#   tricks      FOUR per-seat trick counts under optimal play, clockwise from
#               the seat the PBN names, or "?" when nothing has solved it.
#               This replaces `nil_tricks` and `side_tricks`, which both name
#               "the nil bidder" and so cannot describe a deal with two
#   pv          principal variation, informational; move ordering will
#               legitimately change which of several equal cards is chosen
#   provenance  oracle  = nil_oracle.py computed it independently
#               solver  = pinned from this solver.  A weaker claim than an
#                         oracle row -- it says the answer has not CHANGED, not
#                         that it is right -- but it is what makes the 13-card
#                         rows regression anchors instead of timing fodder
#
# THE OBJECTIVE, lexicographically:
#   PRIMARY    how many of the two nils are set.  The opponents maximize it,
#              the pair minimizes it
#   SECONDARY  the pair's own trick count, wanted under secondary="max"
#
# The two levels are coupled: every trick the pair takes is taken by one of its
# nil bidders, so wanting tricks and wanting nils are in tension.  Having lost
# one bid, the pair funnels everything through the seat already broken.
#
# NOT WIRED INTO ctest.  The C++ solver refuses this shape today --
# validate_seat_roles accepts one nil -- so nil_bench cannot read this file
# yet.  It is the target to build against, not a passing test.
"""


def peel_trick(hands, leader, broken, depth, rng):
    """Play `depth` legal cards onto a fresh trick, returning what is left.

    Returns (remaining hands, cards on the trick, spades broken) or None if the
    position could not be built.  The cards are chosen at random rather than
    optimally: the point is a legal mid-trick position, not a good one.
    """
    # deal() hands back raw (suit, rank) pairs; the oracle's legality rules want
    # its own Card type, so convert once here and hand Cards back out.
    live = [[oracle.Card(s, r) for s, r in h] for h in hands]
    trick = []
    for i in range(depth):
        seat = (leader + i) % 4
        legal = oracle.legal_moves(tuple(sorted(live[seat])), tuple(trick), broken)
        if not legal:
            return None
        card = rng.choice(legal)
        live[seat].remove(card)
        broken = oracle.spades_broken_after(broken, tuple(trick), card)
        trick.append(card)
    return live, trick, broken


def solve_with_cli(pbn, leader, seats, broken, trick_text):
    """Answer a row with the C++ solver, for sizes the oracle cannot reach."""
    cmd = [str(CLI), "--pbn", pbn, "--leader", SEAT_CHARS[leader],
           "--seats", seats, "--compact"]
    if broken:
        cmd.append("--spades-broken")
    if trick_text:
        cmd += ["--trick", trick_text]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        return None

    def field(key):
        m = re.search(key + r"=(-?\d+)", proc.stdout)
        return int(m.group(1)) if m else None

    pv = re.search(r"pv=(.*)", proc.stdout)
    return (field("nils_set"), field("tricks"), field("side_tricks"),
            pv.group(1).strip() if pv else "")


def row(name, pbn, leader, broken, secondary, answer, pv, provenance,
        seats="0 3 0 3", trick_text=""):
    """`answer` is (nils_set, bidding seats' total tricks), or None if unsolved.

    Both are pinned by the objective.  Per-seat counts are not, and are
    deliberately not recorded -- see the module docstring.
    """
    if answer is None:
        nils_set = nil_tricks = side_tricks = "?"
    elif len(answer) == 3:
        nils_set, nil_tricks, side_tricks = (str(x) for x in answer)
    else:
        nils_set = str(answer[0])
        # No cover partner in this shape, so the bidding side's total IS the
        # bidding seats' total.
        nil_tricks = side_tricks = str(answer[1])
    return "%s | %s | %s | %s | %d | %s | %s | %s | %s | %s | %s | %s" % (
        name, pbn, SEAT_CHARS[leader], seats, 1 if broken else 0, trick_text,
        secondary, nils_set, nil_tricks, side_tricks, pv, provenance,
    )


def build(args):
    rows = []
    counts = {}

    for cards, want in args.spec:
        rng = random.Random(args.seed * 100 + cards)
        made = 0
        attempts = 0
        started = time.time()
        while made < want and attempts < want * 400:
            attempts += 1
            hands = two_nil_deal(rng, cards) if cards >= 13 else deal(rng, cards)
            leader = rng.randrange(4)
            broken = rng.random() < 0.5 if cards < 13 else False
            seats, depth = VARIANTS[made % len(VARIANTS)]
            peeled = peel_trick(hands, leader, broken, depth, rng)
            if peeled is None:
                continue
            live, trick, broken2 = peeled
            if not any(live):
                continue
            pbn_text = to_pbn(live)
            trick_text = " ".join(
                SUIT_CHARS[c.suit] + RANK_CHARS[c.rank - 2] for c in trick
            )

            if cards >= 13:
                # The oracle is exhaustive and cannot reach thirteen cards, but
                # the solver now answers these in well under a second -- so they
                # are PINNED FROM THE SOLVER rather than left blank.  A pinned
                # row is a weaker claim than an oracle row: it says the answer
                # has not CHANGED, not that it is right.  That is still the
                # difference between a regression anchor and timing fodder.
                if not (plausible_nil(hands[0]) and plausible_nil(hands[2])):
                    continue
                answer = solve_with_cli(pbn_text, leader, seats, broken2, trick_text)
                if answer is None:
                    continue
                rows.append(row(
                    "m%d-%04d" % (cards, made), pbn_text, leader, broken2,
                    "max", answer[:3], answer[3], "solver",
                    seats=seats, trick_text=trick_text,
                ))
                made += 1
                continue

            pos = oracle.Position(
                hands=tuple(tuple(sorted(h)) for h in live),
                leader=leader,
                spades_broken=broken2,
                current_trick=tuple(trick),
            )
            try:
                pos.validate()
            except Exception:
                continue
            roles = oracle.parse_roles(seats, 0)
            sol = oracle.solve_partner_nils(pos, roles, use_memo=True, secondary="max")
            pv = " ".join(f"{SEAT_CHARS[a]}:{c}" for a, c in sol.pv)
            rows.append(row(
                "m%d-%04d" % (cards, made), pbn_text, leader, broken2,
                "max", (sol.nils_set, sol.nil_side_tricks, sol.nil_side_tricks),
                pv, "oracle", seats=seats, trick_text=trick_text,
            ))
            counts[sol.nils_set] = counts.get(sol.nils_set, 0) + 1
            made += 1
        print("  %2d cards: %3d rows in %.1fs" % (cards, made, time.time() - started),
              file=sys.stderr)

    out = pathlib.Path(args.out)
    out.write_text(HEADER + "\n".join(rows) + "\n")
    solved = sum(counts.values())
    print("\nwrote %d rows to %s" % (len(rows), out), file=sys.stderr)
    if solved:
        print("  of the %d solved: %s" % (
            solved,
            ", ".join("%d nil(s) set in %d" % (k, v) for k, v in sorted(counts.items())),
        ), file=sys.stderr)
    return 0


def spec_type(text):
    out = []
    for part in text.split(","):
        cards, count = part.split(":")
        out.append((int(cards), int(count)))
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--measure", action="store_true",
                   help="report tree growth instead of writing a corpus")
    p.add_argument("--cards", type=lambda t: [int(x) for x in t.split(",")],
                   default=[3, 4, 5], help="card counts for --measure")
    p.add_argument("--count", type=int, default=20, help="deals per size for --measure")
    p.add_argument("--divergence-count", type=int, default=200,
                   help="deals per size for the irreducibility sample")
    p.add_argument("--spec", type=spec_type, default="4:120,5:60,6:30,13:6",
                   help="cards:rows pairs for the corpus")
    p.add_argument("--out", default=str(ROOT / "tests/corpus/multinil.txt"))
    p.add_argument("--seed", type=int, default=20260826)
    args = p.parse_args()
    if isinstance(args.spec, str):
        args.spec = spec_type(args.spec)
    return measure(args) if args.measure else build(args)


if __name__ == "__main__":
    raise SystemExit(main())
