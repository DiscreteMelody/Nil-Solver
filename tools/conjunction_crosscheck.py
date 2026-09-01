#!/usr/bin/env python3
"""Does the C++ conjunction probe agree with the oracle?

Item 78's probe asks whether one side can force the OTHER side's bid down while
keeping its own.  Two things check it here, and they are not the same check:

  * the oracle's own boolean AND-OR search, `solve_conjunction`, which is written
    independently of the C++ one;
  * the oracle's exhaustive utility search, whose outcome RANK is 3 for the
    attacking side exactly when the conjunction holds -- an identity that holds
    under both partner leans, since the leans only swap the middle two rungs.

Requiring all three to agree means a shared misreading of the question has to
survive two different algorithms and one derivation.  Every strictly opposed
arrangement is exercised, in all four rotations, with both bidders attacking.
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
# opposite ways, in all four rotations.  The C++ accepts exactly these.
SHAPES = []
for first in range(4):
    for lean_a, lean_b in ((2, 3), (3, 2)):
        roles = [None] * 4
        roles[first] = 0
        roles[(first + 1) % 4] = 0
        roles[(first + 2) % 4] = lean_a
        roles[(first + 3) % 4] = lean_b
        try:
            if oracle.role_shape(roles) == oracle.SHAPE_OPPOSING_NILS:
                SHAPES.append(tuple(roles))
        except ValueError:
            pass
SHAPES = sorted(set(SHAPES))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", type=int, default=24)
    ap.add_argument("--cards", type=int, default=4)
    ap.add_argument("--seed", type=int, default=17)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    deck = [(s, r) for s in range(4) for r in RANKS]
    problems = []
    checked = 0
    forced = 0

    for case in range(args.cases):
        pick = rng.sample(deck, 4 * args.cards)
        hands = [sorted(pick[j * args.cards:(j + 1) * args.cards]) for j in range(4)]
        pbn_hands = []
        for hand in hands:
            suits = [[], [], [], []]
            for suit, rank in hand:
                suits[suit].append(rank)
            pbn_hands.append(".".join(
                "".join(sorted(x, key=RANKS.index, reverse=True)) for x in suits))
        pbn = "N:" + " ".join(pbn_hands)
        leader = rng.randrange(4)
        position = oracle.Position.build(
            [tuple(oracle.Card(s, RANKS.index(r) + 2) for s, r in h) for h in hands],
            leader=leader, spades_broken=False)

        for roles in SHAPES:
            opp = oracle.solve_opposing_nils(position, list(roles), use_memo=True)
            for attacker_seat in (x for x in range(4) if roles[x] == 0):
                want_bool = oracle.solve_conjunction(
                    position, list(roles), attacker_seat % 2, use_memo=True).can_force
                want_rank = opp.ranks[attacker_seat % 2] == 3
                cmd = [CLI, "--pbn", pbn, "--leader", SEATS[leader],
                       "--seats", " ".join(str(r) for r in roles),
                       "--conjunction", SEATS[attacker_seat], "--compact"]
                proc = subprocess.run(cmd, capture_output=True, text=True)
                checked += 1
                if proc.returncode != 0:
                    problems.append((roles, pbn, "refused: " + proc.stderr.strip()[:70]))
                    continue
                got = re.search(r"conjunction=(\d)", proc.stdout)
                if got is None:
                    problems.append((roles, pbn, "no conjunction line"))
                    continue
                got_bool = got.group(1) == "1"
                forced += got_bool
                if not (got_bool == want_bool == want_rank):
                    problems.append((
                        roles, pbn,
                        f"{SEATS[attacker_seat]} attacking: c++={got_bool} "
                        f"oracle-boolean={want_bool} oracle-rank={want_rank}"))

    print(f"{checked} probes at {args.cards} cards across {len(SHAPES)} "
          f"strictly opposed role sets")
    print(f"  C++ agrees with BOTH oracle routes: {checked - len(problems)} of {checked}")
    print(f"  forceable: {forced} of {checked}"
          f"{'   <-- all one answer, the check proves little' if forced in (0, checked) else ''}")
    for roles, pbn, why in problems[:8]:
        print(f"  MISMATCH {roles} {pbn}\n    {why}")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
