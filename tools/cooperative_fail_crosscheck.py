#!/usr/bin/env python3
"""The cooperative-FAIL probe (`solve_cooperative_fail`) against an
independent reference.

The oracle has no fail-mode equivalent -- `solve_cooperative` only answers the
survive question -- so this does NOT crosscheck the probe against a cousin of
itself.  It enumerates the reachable outcome set directly, by exhaustive
depth-first play with a memo, and asks whether any complete line leaves every
named seat having taken at least one trick.  Slow, obvious, and independent:
the point is that it shares no code with the thing under test.

Also checks the two ablations the survive-probe's own crosscheck checks, for
the same reason: collapsing rank-equivalent moves and the memo must both be
answer-neutral, changing node counts only.
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


def reference_fail_reachable(hands, leader, fail_seats):
    """True if some complete line leaves every seat in `fail_seats` having won
    at least one trick.  Plain exhaustive search, memoised on the literal
    state plus which requirements are already met."""
    want = frozenset(fail_seats)
    memo = {}

    def rec(hands, leader, trick, broken, satisfied):
        if not any(hands):
            return satisfied == want
        key = (hands, leader, trick, broken, satisfied)
        if key in memo:
            return memo[key]
        seat = (leader + len(trick)) % 4
        result = False
        for card in oracle.legal_moves(hands[seat], trick, broken):
            nh = tuple(tuple(c for c in h if c != card) if s == seat else h
                       for s, h in enumerate(hands))
            nb = oracle.spades_broken_after(broken, trick, card)
            played = trick + (card,)
            if len(played) == 4:
                winner = oracle.trick_winner(leader, played)
                ns = satisfied | ({winner} & want)
                if rec(nh, winner, (), nb, frozenset(ns)):
                    result = True
                    break
            else:
                if rec(nh, leader, played, nb, satisfied):
                    result = True
                    break
        memo[key] = result
        return result

    return rec(hands, leader, (), False, frozenset())


def to_pbn(hands):
    out = []
    for hand in hands:
        by_suit = [[], [], [], []]
        for c in hand:
            by_suit[c.suit].append(c.rank)
        out.append(".".join("".join(sorted((RANKS[r - 2] for r in x),
                                            key=RANKS.index, reverse=True))
                             for x in by_suit))
    return "N:" + " ".join(out)


def cli_fail(pbn, seats_text, letters, extra=()):
    proc = subprocess.run([CLI, "--pbn", pbn, "--leader", "N", "--seats", seats_text,
                            "--cooperative-fail", letters, "--compact", *extra],
                           capture_output=True, text=True)
    if proc.returncode != 0:
        return None, proc.stderr.strip()[:100]
    m = re.search(r"cooperative_fail=(\d)", proc.stdout)
    if m is None:
        return None, "no cooperative_fail= line: " + proc.stdout[:100]
    return m.group(1) == "1", None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cases", type=int, default=40)
    ap.add_argument("--cards", type=int, default=4)
    ap.add_argument("--seed", type=int, default=23)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    roles_text = "0 0 2 2"
    checked = yes = no = 0
    problems = []

    for _ in range(args.cases):
        deck = [(s, r) for s in range(4) for r in range(2, 15)]
        pick = rng.sample(deck, 4 * args.cards)
        hands = tuple(tuple(oracle.Card(s, r)
                             for s, r in sorted(pick[j * args.cards:(j + 1) * args.cards]))
                       for j in range(4))
        pbn = to_pbn(hands)

        for fail_seats, letters in (([0, 1], "NE"), ([0], "N"), ([1], "E")):
            want = reference_fail_reachable(hands, 0, fail_seats)
            got, err = cli_fail(pbn, roles_text, letters)
            checked += 1
            if got is None:
                problems.append(f"{pbn} fail={letters}: {err}")
                continue
            if got:
                yes += 1
            else:
                no += 1
            if got != want:
                problems.append(f"{pbn} fail={letters}: c++={got} reference={want}")

            for flag in ("--no-collapse", "--no-memo"):
                alt, alt_err = cli_fail(pbn, roles_text, letters, extra=(flag,))
                if alt is None:
                    problems.append(f"{pbn} fail={letters} {flag}: {alt_err}")
                elif alt != got:
                    problems.append(f"{pbn} fail={letters}: {flag} changes the answer "
                                     f"({got} -> {alt})")

    print(f"{checked} fail-queries at {args.cards} cards, {args.cases} deals")
    print(f"  reachable {yes}, unreachable {no}")
    if yes == 0 or no == 0:
        problems.append(f"all {checked} queries answered the same way; not exercising both")
    print()
    for p in problems[:10]:
        print(f"FAIL: {p}")
    if problems:
        return 1
    print("C++ agrees with the independent reference on every query, both answers seen, "
          "collapsing and the memo both answer-neutral")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
