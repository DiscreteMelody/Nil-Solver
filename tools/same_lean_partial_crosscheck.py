#!/usr/bin/env python3
"""Item 60: does solve()'s same-lean path agree with the oracle's exhaustive
backward induction (solve_opposing_nils) on all three branches of the
decision procedure -- both bids doomed on cards alone, exactly one doomed,
and the delicate cell where both are individually reachable?

Population is asserted, not just checked: a run that never lands in one of
the three cells is not exercising the thing this step is for.
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


def random_position(rng, cards):
    deck = [(s, r) for s in range(4) for r in range(2, 15)]
    pick = rng.sample(deck, 4 * cards)
    hands = [tuple(oracle.Card(s, r) for s, r in sorted(pick[j * cards:(j + 1) * cards]))
             for j in range(4)]
    return oracle.Position.build(hands, leader=0, spades_broken=False)


def to_pbn(hands):
    suits = []
    for hand in hands:
        by_suit = [[], [], [], []]
        for c in hand:
            by_suit[c.suit].append(c.rank)
        suits.append(".".join("".join(sorted((RANKS[r - 2] for r in x),
                                              key=RANKS.index, reverse=True))
                               for x in by_suit))
    return "N:" + " ".join(suits)


def cli_solve(pbn, seats_text):
    proc = subprocess.run([CLI, "--pbn", pbn, "--leader", "N", "--seats", seats_text,
                            "--compact"], capture_output=True, text=True)
    if proc.returncode != 0:
        return None, proc.stderr.strip()[:100]
    m = re.search(r"nils_set_mask=(\d+)", proc.stdout)
    if m is None:
        return None, "no nils_set_mask= line: " + proc.stdout[:100]
    return int(m.group(1)), None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cases", type=int, default=200)
    ap.add_argument("--cards", type=int, default=4)
    ap.add_argument("--seed", type=int, default=5)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    role_sets = [(0, 0, 2, 2), (0, 0, 3, 3)]

    branch_a = branch_b = branch_c_forced = branch_c_open = 0
    mismatches = []

    for case in range(args.cases):
        hands = random_position(rng, args.cards).hands
        pos = oracle.Position.build(hands, leader=0, spades_broken=False)
        pbn = to_pbn(hands)
        roles = role_sets[case % len(role_sets)]
        seats_text = " ".join(str(r) for r in roles)

        coop_near = oracle.solve_cooperative(pos, list(roles), [0], use_memo=True).reachable
        coop_far = oracle.solve_cooperative(pos, list(roles), [1], use_memo=True).reachable
        got_mask, cli_err = cli_solve(pbn, seats_text)

        if not coop_near and not coop_far:
            branch_a += 1
            want_mask = 0b0011  # both near(seat0) and far(seat1) fail
            if got_mask is None:
                mismatches.append(f"branch A but refused: {pbn} seats={seats_text}: {cli_err}")
            elif got_mask != want_mask:
                mismatches.append(
                    f"branch A mismatch: {pbn} seats={seats_text} got={got_mask:04b} "
                    f"want={want_mask:04b}")
        elif coop_near != coop_far:
            branch_b += 1
            truth = oracle.solve_opposing_nils(pos, list(roles), use_memo=True)
            want_mask = (0 if truth.nil_makes[0] else 1) | (0 if truth.nil_makes[1] else 2)
            if got_mask is None:
                mismatches.append(f"branch B but refused: {pbn} seats={seats_text}: {cli_err}")
            elif got_mask != want_mask:
                mismatches.append(
                    f"branch B mismatch: {pbn} seats={seats_text} got={got_mask:04b} "
                    f"want={want_mask:04b} (oracle nil_makes={truth.nil_makes})")
        else:
            # The delicate cell, split by the conjunction probe.  Step 2
            # resolves the FORCING rung and leaves the rest open, so which
            # behaviour is correct here depends on that probe, not on the
            # cooperative ones.
            c0 = oracle.solve_conjunction(pos, list(roles), 0, use_memo=True).can_force
            c1 = oracle.solve_conjunction(pos, list(roles), 1, use_memo=True).can_force
            truth = oracle.solve_opposing_nils(pos, list(roles), use_memo=True)
            want_mask = (0 if truth.nil_makes[0] else 1) | (0 if truth.nil_makes[1] else 2)
            if c0 or c1:
                branch_c_forced += 1
                if got_mask is None:
                    mismatches.append(f"branch C forcing rung refused: {pbn} "
                                       f"seats={seats_text}: {cli_err}")
                elif got_mask != want_mask:
                    mismatches.append(
                        f"branch C forcing rung mismatch: {pbn} seats={seats_text} "
                        f"got={got_mask:04b} want={want_mask:04b} "
                        f"(oracle nil_makes={truth.nil_makes})")
            else:
                branch_c_open += 1
                # The open cell: solve() must REFUSE, never guess.  The
                # tiebreak once intended here is disproved, so a confident
                # answer would be a correctness hazard, not a feature.
                if got_mask is not None:
                    mismatches.append(
                        f"open cell answered instead of refusing: {pbn} "
                        f"seats={seats_text} got={got_mask:04b}")

    print(f"{args.cases} deals at {args.cards} cards")
    print(f"  branch A (neither reachable):      {branch_a}")
    print(f"  branch B (exactly one reachable):  {branch_b}")
    print(f"  branch C forcing rung (resolved):    {branch_c_forced}")
    print(f"  branch C open cell (must refuse):    {branch_c_open}")
    print()
    for m in mismatches[:10]:
        print(f"FAIL: {m}")
    if len(mismatches) > 10:
        print(f"... and {len(mismatches) - 10} more")

    if branch_a == 0 or branch_b == 0 or branch_c_forced == 0 or branch_c_open == 0:
        print("FAIL: this run never exercised one of the four cells")
        return 1
    if mismatches:
        return 1
    print("C++ agrees with the oracle's exhaustive backward induction on every cell "
          "the procedure resolves, and refuses (never guesses) the one still open")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
