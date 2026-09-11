#!/usr/bin/env python3
"""Step 3's pinned-outcome filter against an independent reference.

The reference enumerates lines directly and asks, of each COMPLETE line,
whether every --require-set seat took a trick and no --require-live seat did.
It shares no code with the thing under test, and in particular it does NOT
reproduce the asymmetric pruning: it applies both halves of the constraint at
the end of the line, the slow obvious way.  That is the point -- the
optimisation being checked is precisely that abandoning a must-make violation
early cannot change the answer.

Also checks the two ablations, for the usual reason: collapsing
rank-equivalent moves and the memo must change node counts only.
"""
import argparse
import importlib.util
import pathlib
import random
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("nil_oracle", ROOT / "nil_oracle.py")
oracle = importlib.util.module_from_spec(spec)
sys.modules["nil_oracle"] = oracle
spec.loader.exec_module(oracle)

CLI = str(ROOT / "build/bin/nil_cli")
SEATS = "NESW"
RANKS = "23456789TJQKA"


def reference(hands, leader, live_seats, set_seats):
    """True if some complete line satisfies both halves.  Both checked at the
    terminal state; no early abandonment anywhere."""
    live = frozenset(live_seats)
    want = frozenset(set_seats)
    memo = {}

    def rec(hands, leader, trick, broken, winners):
        if not any(hands):
            return not (winners & live) and want <= winners
        key = (hands, leader, trick, broken, winners)
        if key in memo:
            return memo[key]
        seat = (leader + len(trick)) % 4
        out = False
        for card in oracle.legal_moves(hands[seat], trick, broken):
            nh = tuple(tuple(c for c in h if c != card) if s == seat else h
                       for s, h in enumerate(hands))
            nb = oracle.spades_broken_after(broken, trick, card)
            played = trick + (card,)
            if len(played) == 4:
                w = oracle.trick_winner(leader, played)
                if rec(nh, w, (), nb, winners | {w}):
                    out = True
                    break
            else:
                if rec(nh, leader, played, nb, winners):
                    out = True
                    break
        memo[key] = out
        return out

    return rec(hands, leader, (), False, frozenset())


def to_pbn(hands):
    out = []
    for hand in hands:
        bs = [[], [], [], []]
        for c in hand:
            bs[c.suit].append(c.rank)
        out.append(".".join("".join(sorted((RANKS[r - 2] for r in x),
                                            key=RANKS.index, reverse=True)) for x in bs))
    return "N:" + " ".join(out)


def cli(pbn, seats_text, live, sett, extra=()):
    cmd = [CLI, "--pbn", pbn, "--leader", "N", "--seats", seats_text, "--compact", *extra]
    if live:
        cmd += ["--require-live", live]
    if sett:
        cmd += ["--require-set", sett]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        return None, None, p.stderr.strip()[:100]
    m = re.search(r"constrained=(\d)", p.stdout)
    n = re.search(r"live_prunes=(\d+)", p.stdout)
    if m is None:
        return None, None, "no constrained= line: " + p.stdout[:100]
    return m.group(1) == "1", int(n.group(1)) if n else 0, None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cases", type=int, default=30)
    ap.add_argument("--cards", type=int, default=4)
    ap.add_argument("--seed", type=int, default=31)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    roles_text = "0 0 2 2"
    checked = yes = no = 0
    pruning_seen = 0
    problems = []

    # Both orientations of the pinned outcome, plus the two one-sided halves,
    # so neither half is only ever exercised alongside the other.
    combos = [([1], [0], "E", "N"), ([0], [1], "N", "E"),
              ([], [0, 1], "", "NE"), ([0, 1], [], "NE", "")]

    for _ in range(args.cases):
        deck = [(s, r) for s in range(4) for r in range(2, 15)]
        pick = rng.sample(deck, 4 * args.cards)
        hands = tuple(tuple(oracle.Card(s, r)
                             for s, r in sorted(pick[j * args.cards:(j + 1) * args.cards]))
                       for j in range(4))
        pbn = to_pbn(hands)

        for live_seats, set_seats, live_txt, set_txt in combos:
            want = reference(hands, 0, live_seats, set_seats)
            got, prunes, err = cli(pbn, roles_text, live_txt, set_txt)
            checked += 1
            if got is None:
                problems.append(f"{pbn} live={live_txt} set={set_txt}: {err}")
                continue
            if got:
                yes += 1
            else:
                no += 1
            if prunes:
                pruning_seen += 1
            if got != want:
                problems.append(f"{pbn} live={live_txt} set={set_txt}: "
                                 f"c++={got} reference={want}")
            for flag in ("--no-collapse", "--no-memo"):
                alt, _, aerr = cli(pbn, roles_text, live_txt, set_txt, extra=(flag,))
                if alt is None:
                    problems.append(f"{pbn} {flag}: {aerr}")
                elif alt != got:
                    problems.append(f"{pbn} live={live_txt} set={set_txt}: {flag} "
                                     f"changes the answer ({got} -> {alt})")

    print(f"{checked} constraint queries at {args.cards} cards, {args.cases} deals")
    print(f"  satisfiable {yes}, unsatisfiable {no}")
    print(f"  queries where the must-make half pruned at least once: {pruning_seen}")
    if yes == 0 or no == 0:
        problems.append(f"all {checked} queries answered the same way; not exercising both")
    if pruning_seen == 0:
        problems.append("the must-make half never fired; its early-abandon path is untested")
    print()
    for p in problems[:10]:
        print(f"FAIL: {p}")
    if problems:
        return 1
    print("C++ agrees with the independent reference on every query, both answers seen, "
          "early must-make abandonment exercised, collapsing and the memo answer-neutral")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
