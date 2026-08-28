#!/usr/bin/env python3
"""Does the C++ solver agree with the oracle on one bid per side?

Compares the UTILITY PAIR rather than the cards.  Two optimal lines can name
different equally-good cards, but the game value is the game value, so both
solvers must land on the same pair -- and computing it from the C++ solver's own
PV, replayed independently, checks the line as well as the number.

Only strictly opposed role sets are exercised, because those are the only ones
the C++ accepts: the partners must lean OPPOSITE ways.
"""
import argparse, importlib.util, pathlib, random, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("nil_oracle", ROOT / "nil_oracle.py")
oracle = importlib.util.module_from_spec(spec)
sys.modules["nil_oracle"] = oracle
spec.loader.exec_module(oracle)

CLI = str(ROOT / "build/bin/nil_cli")
SEATS = "NESW"
RANKS = "23456789TJQKA"

# Every strictly opposed arrangement: a bid on each side, partners leaning
# opposite ways, in all four rotations.
SHAPES = []
for first in range(4):
    for a, b in ((2, 3), (3, 2)):
        roles = [None] * 4
        roles[first] = 0
        roles[(first + 1) % 4] = 0
        roles[(first + 2) % 4] = a        # partner of the first bidder
        roles[(first + 3) % 4] = b        # partner of the second
        SHAPES.append(" ".join(str(r) for r in roles))


def to_pbn(hands):
    return "N:" + " ".join(
        ".".join("".join(RANKS[c.rank - 2] for c in sorted(
            (c for c in h if c.suit == s), key=lambda c: -c.rank)) for s in range(4))
        for h in hands)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", type=int, default=48)
    ap.add_argument("--cards", type=int, default=4)
    ap.add_argument("--seed", type=int, default=17)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    checked = agree = skipped = 0
    problems = []

    for i in range(args.cases):
        seats = SHAPES[i % len(SHAPES)]
        deck = [oracle.Card(s, r) for s in range(4) for r in range(2, 15)]
        pick = rng.sample(deck, 4 * args.cards)
        hands = [sorted(pick[j * args.cards:(j + 1) * args.cards]) for j in range(4)]
        leader = rng.randrange(4)
        broken = rng.random() < 0.5
        pbn = to_pbn(hands)
        pos = oracle.Position(hands=tuple(tuple(h) for h in hands), leader=leader,
                              spades_broken=broken)
        try:
            pos.validate()
        except Exception:
            skipped += 1
            continue

        roles = oracle.parse_roles(seats, 0)
        ref = oracle.solve_opposing_nils(pos, roles, use_memo=True)

        cmd = [CLI, "--pbn", pbn, "--leader", SEATS[leader], "--seats", seats, "--compact"]
        if broken:
            cmd.append("--spades-broken")
        proc = subprocess.run(cmd, capture_output=True, text=True)
        checked += 1
        if proc.returncode != 0:
            problems.append((seats, pbn, "refused: " + proc.stderr.strip()[:70]))
            continue

        pv_text = re.search(r"pv=(.*)", proc.stdout).group(1).split()
        plays = [(SEATS.index(t.split(":")[0]), oracle.card_from_str(t.split(":")[1]))
                 for t in pv_text]
        # Replay the SOLVER's line and score it under the ORACLE's rules.
        seat_tricks = oracle.replay_pv_by_seat(pos, plays)
        rank_w, trick_w = oracle.opposing_weights(pos.tricks_remaining, "max")
        makes = [seat_tricks[s] == 0 for s in ref.nil_of_side]
        side = [seat_tricks[0] + seat_tricks[2], seat_tricks[1] + seat_tricks[3]]
        got = tuple(
            rank_w * oracle.side_rank(makes[k], makes[1 - k],
                                      ref.roles[(ref.nil_of_side[k] + 2) % 4])
            + trick_w * side[k]
            for k in (0, 1))
        if got == tuple(ref.utility):
            agree += 1
        else:
            problems.append((seats, pbn,
                             "solver line scores %s, oracle value %s" % (got, tuple(ref.utility))))

    print("%d deals at %d cards across %d strictly opposed role sets (%d skipped)"
          % (checked, args.cards, len(SHAPES), skipped))
    print("  solver line achieves the oracle's game value: %d of %d" % (agree, checked))
    for seats, pbn, why in problems[:5]:
        print("   MISMATCH %s  %s\n     %s" % (seats, pbn, why))
    return 0 if agree == checked else 1


if __name__ == "__main__":
    raise SystemExit(main())
