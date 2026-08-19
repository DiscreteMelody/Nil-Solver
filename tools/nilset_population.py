#!/usr/bin/env python3
"""Roadmap item 32: how much is an adversarial nil-set proof actually worth?

`nil_must_take_a_trick` proves the nil bidder wins a trick whatever ANY of the
four players do. DDS section 4 asks the cheaper question -- can the side that
did not lead force a trick later, against best defence -- and the ROADMAP notes
a holding the current proof misses: ♠K2 against one opponent's ♠AQJ is forced,
because the opponent leads the jack rather than the ace and the king is stranded.

The question is not whether such holdings exist. It is how many there are, and
how many of them a proof could actually reach. So: compute both predicates in
Python, take the solver as ground truth, and split the outcomes.

Ground truth is exact -- MODE_FAST's value is the nil bidder's trick count and
the opponents maximise it, so `nil_fails` IS "the opponents can force a trick",
which is precisely what the proof claims.

Usage:  nilset_population.py [--cards N] [--count N] [--seed N]
"""
import argparse
import random
import subprocess
import sys

RANKS = "23456789TJQKA"
CLI = "./build/bin/nil_cli"


def deal(cards, rng):
    deck = [(s, r) for s in range(4) for r in range(13)]
    rng.shuffle(deck)
    hands = [sorted(deck[i * cards:(i + 1) * cards]) for i in range(4)]
    out = []
    for hand in hands:
        by_suit = [[] for _ in range(4)]
        for s, r in hand:
            by_suit[s].append(RANKS[r])
        out.append(".".join("".join(reversed(x)) for x in by_suit))
    return "N:" + " ".join(out), hands


def spades(hand):
    """Rank indices (0=deuce) of a hand's spades, ascending. Suit 0 is spades."""
    return sorted(r for s, r in hand if s == 0)


def proof_today(mine, others):
    """cover_deficit_depth: the j-th of the nil bidder's spades from the top
    needs j outstanding spades above it, pooled over every other hand."""
    if not mine:
        return False
    held = above = 0
    for r in range(12, -1, -1):
        if r in mine:
            held += 1
            if held > above:
                return True
        elif r in others:
            above += 1
    return False


def adversarial(mine, opp):
    """The opponents lead their smallest spades in increasing order; the nil
    bidder must follow and survives the j-th lead only if it still holds a
    spade below it."""
    if not mine:
        return False
    m = len(mine)
    j = 0
    for r in sorted(opp):
        j += 1
        if j > m:
            break
        if sum(1 for c in mine if c < r) < j:
            return True
    return False


def partner_shielded(mine, partner):
    """Greedily let each partner spade overtake the highest nil spade below it.
    Those cards can never win a trick the partner is willing to take, so strip
    them before asking whether the rest can be forced."""
    left = sorted(mine)
    for pc_ in sorted(partner, reverse=True):
        below = [c for c in left if c < pc_]
        if below:
            left.remove(max(below))
    return left


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cards", type=int, default=7)
    ap.add_argument("--count", type=int, default=400)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    rng = random.Random(args.seed)

    # Buckets, all conditioned on the current proof staying SILENT -- what fires
    # today is already banked and is not what this item would add.
    reachable = 0      # adversarial test fires and the nil really is forced
    false_alarm = 0    # adversarial test fires and it is not
    missed = 0         # forced, and the adversarial test does not see it either
    safe = 0           # not forced
    fires_today = 0
    total = 0
    tally = {k: [0, 0, 0] for k in
             ('A ignore partner', 'B partner void  ', 'C partner shield')}

    for _ in range(args.count):
        pbn, hands = deal(args.cards, rng)
        nil_seat = rng.randrange(4)
        leader = "NESW"[rng.randrange(4)]
        broken = rng.random() < 0.5
        mine = spades(hands[nil_seat])
        if not mine:
            continue
        others = set()
        for q in range(4):
            if q != nil_seat:
                others |= set(spades(hands[q]))
        opp = set(spades(hands[(nil_seat + 1) % 4])) | set(spades(hands[(nil_seat + 3) % 4]))
        partner = set(spades(hands[(nil_seat + 2) % 4]))

        cmd = [CLI, "--pbn", pbn, "--leader", leader, "--nil", "NESW"[nil_seat],
               "--mode", "fast", "--tt-mb", "8", "--compact"]
        if broken:
            cmd.append("--spades-broken")
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            continue
        forced = "nil_fails=1" in r.stdout
        total += 1

        if proof_today(mine, others):
            fires_today += 1
            continue
        variants = {
            "A ignore partner": adversarial(mine, opp),
            "B partner void  ": adversarial(mine, opp) and not partner,
            "C partner shield": adversarial(partner_shielded(mine, partner), opp),
        }
        for k, fires in variants.items():
            if fires:
                tally[k][0 if forced else 1] += 1
                if not forced and k.startswith("B"):
                    print(f"  FALSE ALARM (B) pbn={pbn} nil={'NESW'[nil_seat]} "
                          f"leader={leader} broken={int(broken)}")
            elif forced:
                tally[k][2] += 1
        if variants["A ignore partner"]:
            if forced:
                reachable += 1
            else:
                false_alarm += 1
        elif forced:
            missed += 1
        else:
            safe += 1

    silent = total - fires_today
    def pc(x, d):
        return f"{100.0 * x / d:.1f}%" if d else "n/a"

    print(f"{total} positions at {args.cards} cards with a spade in the nil hand")
    print(f"  proof fires today                {fires_today:6d}  {pc(fires_today, total)}")
    print(f"  --- of the {silent} where it stays silent ---")
    print(f"  adversarial fires, really forced {reachable:6d}  {pc(reachable, silent)}"
          f"   <- what item 32 could win")
    print(f"  adversarial fires, NOT forced    {false_alarm:6d}  {pc(false_alarm, silent)}"
          f"   <- the test is wrong here")
    print(f"  forced, adversarial misses it    {missed:6d}  {pc(missed, silent)}")
    print(f"  genuinely safe                   {safe:6d}  {pc(safe, silent)}")
    print(f"  --- variants, all conditioned on the proof staying silent ---")
    print(f"  {'variant':18s} {'hits':>6s} {'false':>6s} {'precision':>10s} {'population':>11s}")
    for k, (hit, false_, miss) in tally.items():
        print(f"  {k:18s} {hit:6d} {false_:6d} {pc(hit, hit + false_):>10s} "
              f"{pc(hit, silent):>11s}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
