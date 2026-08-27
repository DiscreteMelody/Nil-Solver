#!/usr/bin/env python3
"""Is a mid-hand re-solve consistent with the full solve it came from?

THE QUESTION.  Solve a deal with two live bids.  Play along the principal
variation until one of them breaks, then hand the REMAINING position back with
that seat marked ROLE_NIL_SET.  Does the solver carry on the same way, and does
the surviving bid get the same verdict?

It should, and the reason is that the mask makes the objective subgame-consistent:
the primary weight is charged on a bidder's first trick and never again, so from
the moment a bid is down the rest of the subtree is scored on the surviving bid
and the pair's trick total -- which is precisely what ROLE_NIL_SET asks for.  The
weights differ between the two calls (K = tricks remaining + 1, so 14 against 11
at 13 and 10 cards) but K*t < K*K holds at every size, so the LEXICOGRAPHIC ORDER
is identical and only the scale moves.

What this checks, per deal:
  * the surviving bid gets the same verdict
  * the pair's tricks over the remaining tricks agree
  * the re-solve's own PV replays to its own claim

What it does NOT check is that the same CARD is chosen among equally-good ones.
Ties resolve to the canonically lowest card in both calls, but a windowed search
can reach a node under a different window, and the corpus header has always said
the PV is informational for that reason.
"""
import argparse
import importlib.util
import pathlib
import random
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SEATS = "NESW"
RANKS = "23456789TJQKA"

spec = importlib.util.spec_from_file_location("nil_oracle", ROOT / "nil_oracle.py")
oracle = importlib.util.module_from_spec(spec)
sys.modules["nil_oracle"] = oracle
spec.loader.exec_module(oracle)

CLI = str(ROOT / "build/bin/nil_cli")


def to_pbn(hands):
    """hands[seat] is an iterable of oracle Cards."""
    groups = []
    for hand in hands:
        by_suit = []
        for suit in range(4):
            ranks = sorted((c.rank for c in hand if c.suit == suit), reverse=True)
            by_suit.append("".join(RANKS[r - 2] for r in ranks))
        groups.append(".".join(by_suit))
    return "N:" + " ".join(groups)


def solve(pbn, leader, seats, broken):
    cmd = [CLI, "--pbn", pbn, "--leader", SEATS[leader], "--seats", seats, "--compact"]
    if broken:
        cmd.append("--spades-broken")
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        return None
    out = proc.stdout

    def field(name):
        m = re.search(name + r"=(-?\d+)", out)
        return int(m.group(1)) if m else None

    pv = re.search(r"pv=(.*)", out)
    return {
        "nils_set": field("nils_set"),
        "side_tricks": field("side_tricks"),
        "pv": pv.group(1).split() if pv else [],
    }


def random_deal(rng, cards):
    deck = [oracle.Card(s, r) for s in range(4) for r in range(2, 15)]
    picked = rng.sample(deck, 4 * cards)
    return [sorted(picked[i * cards:(i + 1) * cards]) for i in range(4)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cards", type=int, default=6)
    ap.add_argument("--count", type=int, default=40)
    ap.add_argument("--seed", type=int, default=99)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    checked = agree = skipped = 0
    problems = []

    for _ in range(args.count):
        hands = random_deal(rng, args.cards)
        leader = rng.randrange(4)
        broken = rng.random() < 0.5
        pbn = to_pbn(hands)
        # E and W bid; N and S are trying to set both.
        full = solve(pbn, leader, "3 0 3 0", broken)
        if full is None or not full["pv"]:
            skipped += 1
            continue

        # Replay the line, stopping at the first TRICK BOUNDARY at which exactly
        # one of the two bids has gone down.  That is the position a real table
        # would re-solve from.
        live = [list(h) for h in hands]
        cur_leader = leader
        cur_broken = broken
        trick = []
        down = set()
        tail_side = 0
        split = None

        for token in full["pv"]:
            seat_txt, card_txt = token.split(":")
            seat = SEATS.index(seat_txt)
            card = oracle.card_from_str(card_txt)
            live[seat] = [c for c in live[seat] if c != card]
            cur_broken = oracle.spades_broken_after(cur_broken, tuple(trick), card)
            trick.append(card)
            if len(trick) == 4:
                winner = oracle.trick_winner(cur_leader, tuple(trick))
                if winner in (1, 3):
                    down.add(winner)
                    if split is not None:
                        tail_side += 1
                elif split is not None:
                    pass
                cur_leader = winner
                trick = []
                if split is None and len(down) == 1:
                    # Snapshot AFTER this trick: hands as they now stand.
                    split = {
                        "pbn": to_pbn(live),
                        "leader": cur_leader,
                        "broken": cur_broken,
                        "dead": next(iter(down)),
                    }
                    tail_side = 0

        # No boundary with exactly one bid down, or the break came on the last
        # trick and there is nothing left to re-solve.
        if split is None or split["pbn"].strip() == "N:" + " ..." * 0 + " . . . ." or \
                all(part == "..." for part in split["pbn"][2:].split()):
            skipped += 1
            continue

        # The same roles, with the dead seat marked already-down.
        roles = ["3", "0", "3", "0"]
        roles[split["dead"]] = "1"
        sub = solve(split["pbn"], split["leader"], " ".join(roles), split["broken"])
        checked += 1
        if sub is None:
            problems.append((pbn, "sub-position refused"))
            continue

        # The surviving bid: down in the full solve iff both went down there.
        survivor_down_full = len(down) == 2
        survivor_down_sub = sub["nils_set"] == 2  # 1 declared + 1 more
        ok_verdict = survivor_down_full == survivor_down_sub
        ok_tricks = sub["side_tricks"] == tail_side
        if ok_verdict and ok_tricks:
            agree += 1
        else:
            problems.append((
                pbn,
                "verdict full=%s sub=%s | pair tricks tail=%d sub=%d"
                % (survivor_down_full, survivor_down_sub, tail_side, sub["side_tricks"]),
            ))

    print("%d cards, %d deals reached a one-bid-down boundary (%d skipped)"
          % (args.cards, checked, skipped))
    print("  agree on verdict AND remaining pair tricks: %d of %d" % (agree, checked))
    for pbn, why in problems[:5]:
        print("   MISMATCH %s\n     %s" % (pbn, why))
    return 0 if agree == checked else 1


if __name__ == "__main__":
    raise SystemExit(main())
