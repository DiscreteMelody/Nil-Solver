#!/usr/bin/env python3
"""Step 4's constrained trick split against an independent reference.

The reference is a plain backward induction written from the priority
directly -- each node's mover picks the child best by ITS OWN order (own
cover hand up, other cover hand down, other nil up) -- with the constraint
applied the slow obvious way: both halves checked at the terminal state, no
early abandonment anywhere.  It shares no code with the C++ and does not
reproduce its optimisations, which is the point.

Also checks the memo ablation.  `--no-collapse` is NOT checked, because this
search deliberately does not collapse rank-equivalent moves: equivalence for
"who wins THIS trick" does not imply equivalence for "who wins the later
ones", and this search reports exactly that.  See outcome_constraint.cpp.
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
RANKS = "23456789TJQKA"
NIL = {0: 0, 1: 1}      # side -> nil seat, for roles "0 0 ? ?"
COVER = {0: 2, 1: 3}    # side -> cover seat


def reference(hands, leader, live_seats, set_seats):
    """Returns the optimal per-seat trick tuple, or None if no valid line."""
    live = frozenset(live_seats)
    want = frozenset(set_seats)
    memo = {}

    def key_for(seat, t):
        side = seat & 1
        return (t[COVER[side]], -t[COVER[side ^ 1]], t[NIL[side ^ 1]])

    def rec(hands, leader, trick, broken, winners):
        if not any(hands):
            if (winners & live) or not (want <= winners):
                return None
            return (0, 0, 0, 0)
        k = (hands, leader, trick, broken, winners)
        if k in memo:
            return memo[k]
        seat = (leader + len(trick)) % 4
        best = None
        for card in oracle.legal_moves(hands[seat], trick, broken):
            nh = tuple(tuple(c for c in h if c != card) if s == seat else h
                       for s, h in enumerate(hands))
            nb = oracle.spades_broken_after(broken, trick, card)
            played = trick + (card,)
            if len(played) == 4:
                w = oracle.trick_winner(leader, played)
                sub = rec(nh, w, (), nb, winners | {w})
                if sub is None:
                    continue
                cand = tuple(sub[i] + (1 if i == w else 0) for i in range(4))
            else:
                cand = rec(nh, leader, played, nb, winners)
                if cand is None:
                    continue
            if best is None or key_for(seat, cand) > key_for(seat, best):
                best = cand
        memo[k] = best
        return best

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
    cmd = [CLI, "--pbn", pbn, "--leader", "N", "--seats", seats_text,
           "--pinned-tricks", "--compact", *extra]
    if live:
        cmd += ["--require-live", live]
    if sett:
        cmd += ["--require-set", sett]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        return None, p.stderr.strip()[:100]
    ok = re.search(r"constrained=(\d)", p.stdout)
    if ok is None:
        return None, "no constrained= line"
    if ok.group(1) != "1":
        return "UNSAT", None
    t = []
    for ch in "NESW":
        m = re.search(rf"tricks_{ch}=(\d+)", p.stdout)
        if m is None:
            return None, f"no tricks_{ch}"
        t.append(int(m.group(1)))
    return tuple(t), None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cases", type=int, default=25)
    ap.add_argument("--cards", type=int, default=4)
    ap.add_argument("--seed", type=int, default=41)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    combos = [(["E"], ["N"], "E", "N"), (["N"], ["E"], "N", "E"),
              ([], ["N", "E"], "", "NE")]
    seat_ix = {"N": 0, "E": 1, "S": 2, "W": 3}

    checked = sat = unsat = 0
    problems = []
    for _ in range(args.cases):
        deck = [(s, r) for s in range(4) for r in range(2, 15)]
        pick = rng.sample(deck, 4 * args.cards)
        hands = tuple(tuple(oracle.Card(s, r)
                             for s, r in sorted(pick[j * args.cards:(j + 1) * args.cards]))
                       for j in range(4))
        pbn = to_pbn(hands)
        for roles_text in ("0 0 2 2", "0 0 3 3"):
            for live_l, set_l, live_txt, set_txt in combos:
                want = reference(hands, 0, [seat_ix[x] for x in live_l],
                                  [seat_ix[x] for x in set_l])
                got, err = cli(pbn, roles_text, live_txt, set_txt)
                checked += 1
                if got is None:
                    problems.append(f"{pbn} live={live_txt} set={set_txt}: {err}")
                    continue
                if got == "UNSAT":
                    unsat += 1
                    if want is not None:
                        problems.append(f"{pbn} live={live_txt} set={set_txt}: "
                                         f"c++ UNSAT, reference {want}")
                    continue
                sat += 1
                if want is None:
                    problems.append(f"{pbn} live={live_txt} set={set_txt}: "
                                     f"c++ {got}, reference UNSAT")
                elif tuple(got) != tuple(want):
                    problems.append(f"{pbn} live={live_txt} set={set_txt}: "
                                     f"c++={got} reference={want}")
                alt, aerr = cli(pbn, roles_text, live_txt, set_txt, extra=("--no-memo",))
                if alt is None:
                    problems.append(f"{pbn} --no-memo: {aerr}")
                elif alt != got:
                    problems.append(f"{pbn} live={live_txt} set={set_txt}: "
                                     f"--no-memo changes the split ({got} -> {alt})")

    print(f"{checked} constrained-trick queries at {args.cards} cards, {args.cases} deals")
    print(f"  satisfiable {sat}, unsatisfiable {unsat}")
    if sat == 0 or unsat == 0:
        problems.append(f"all {checked} queries answered the same way; not exercising both")
    print()
    for p in problems[:10]:
        print(f"FAIL: {p}")
    if problems:
        return 1
    print("C++ agrees with the independent reference on every trick split, both leans, "
          "both answers seen, the memo answer-neutral")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
