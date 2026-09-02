#!/usr/bin/env python3
"""Check the cooperative reachability probe against three independent routes.

The probe answers a question no other probe in this repo answers: is an outcome
REACHABLE AT ALL, with every seat helping rather than any of them opposing?
Everything else here is an adversarial guarantee, so there is nothing to compare
it against directly and the checking has to come from elsewhere.

Three routes, each catching a different way of being wrong:

  BRUTE FORCE     the definition, played out.  No memo, no early abandonment.
                  Catches the two optimisations in `_search_cooperative` -- a
                  memo key that omits the broken-bid mask, and dropping a line
                  the moment a protected seat wins a trick.  Both are sound
                  only because a bid never un-breaks, and a wrong reading of
                  that would be invisible in the answers alone.

  IMPLICATION     a minimax outcome is a REACHABLE outcome, so whenever the
                  exhaustive utility search reports both bids surviving, the
                  probe must say reachable.  One-directional -- the converse is
                  false and is precisely the gap the probe exists to fill -- but
                  it runs through completely different machinery, so a shared
                  misreading would have to survive two algorithms.

  MONOTONICITY    protecting fewer bids can only ever be easier.  If both bids
                  can survive together then each can survive alone.  A property
                  rather than a fixture, so it holds on every deal and does not
                  need a known answer to check against.

POPULATION IS ASSERTED TOO.  A suite where the probe always answers the same
way would pass while proving nothing, so a run that never sees both answers --
or never sees the implication's premise -- fails rather than passes.
"""
import argparse
import importlib.util
import itertools
import pathlib
import random
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("nil_oracle", ROOT / "nil_oracle.py")
oracle = importlib.util.module_from_spec(spec)
sys.modules["nil_oracle"] = oracle
spec.loader.exec_module(oracle)

ROLE_NIL = oracle.ROLE_NIL
ROLE_COVER = oracle.ROLE_COVER
ROLE_OPPONENT = oracle.ROLE_OPPONENT


def opposing_arrangements():
    """All sixteen deals with one bid per side and either partner lean."""
    out = []
    for first in range(4):
        for a, b in itertools.product((ROLE_OPPONENT, ROLE_COVER), repeat=2):
            roles = [None] * 4
            roles[first] = ROLE_NIL
            roles[(first + 1) % 4] = ROLE_NIL
            roles[(first + 2) % 4] = a
            roles[(first + 3) % 4] = b
            out.append(roles)
    return out


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cards", type=int, default=3)
    ap.add_argument("--cases", type=int, default=48)
    ap.add_argument("--seed", type=int, default=17)
    ap.add_argument("--no-brute-force", action="store_true",
                    help="skip the enumeration route (it is the expensive one)")
    args = ap.parse_args()

    shapes = opposing_arrangements()
    rng = random.Random(args.seed)

    checked = 0
    reachable = 0
    brute_disagree = 0
    implication_seen = 0
    implication_broken = 0
    mono_checked = 0
    mono_broken = 0
    nodes_true = nodes_false = 0
    count_true = count_false = 0
    failures = []

    for i in range(args.cases):
        roles = shapes[i % len(shapes)]
        deck = [oracle.Card(s, r) for s in range(4) for r in range(2, 15)]
        pick = rng.sample(deck, 4 * args.cards)
        hands = tuple(
            tuple(sorted(pick[j * args.cards:(j + 1) * args.cards])) for j in range(4)
        )
        leader = rng.randrange(4)
        pos = oracle.Position(hands=hands, leader=leader)
        bids = [s for s in range(4) if roles[s] == ROLE_NIL]
        mask = sum(1 << s for s in bids)

        got = oracle.solve_cooperative(pos, roles)
        checked += 1
        if got.reachable:
            reachable += 1
            nodes_true += got.nodes
            count_true += 1
        else:
            nodes_false += got.nodes
            count_false += 1

        # Route 1: the definition, enumerated.
        if not args.no_brute_force:
            want = oracle._brute_force_cooperative(hands, leader, (), False, mask)
            if want != got.reachable:
                brute_disagree += 1
                failures.append(
                    f"brute force disagrees on {pos.to_pbn()} leader "
                    f"{oracle.SEAT_CHARS[leader]} roles {roles}: "
                    f"probe {got.reachable}, enumeration {want}")

        # Route 2: a minimax outcome is a reachable outcome.
        sol = oracle.solve_opposing_nils(pos, roles)
        if all(sol.nil_makes):
            implication_seen += 1
            if not got.reachable:
                implication_broken += 1
                failures.append(
                    f"the utility search reports both bids surviving on "
                    f"{pos.to_pbn()} roles {roles}, but the probe calls "
                    f"that outcome unreachable")

        # Route 3: protecting fewer bids is easier.
        for one in bids:
            mono_checked += 1
            if oracle.solve_cooperative(pos, roles, [one]).reachable:
                continue
            if got.reachable:
                mono_broken += 1
                failures.append(
                    f"both bids reachable together but seat "
                    f"{oracle.SEAT_CHARS[one]} alone is not, on "
                    f"{pos.to_pbn()} roles {roles}")

    print(f"{checked} deals at {args.cards} cards across "
          f"{len(shapes)} opposing arrangements")
    if not args.no_brute_force:
        print(f"  agrees with brute-force enumeration: "
              f"{checked - brute_disagree} of {checked}")
    print(f"  minimax-implies-reachable held: "
          f"{implication_seen - implication_broken} of {implication_seen}")
    print(f"  monotonic in the protected set: "
          f"{mono_checked - mono_broken} of {mono_checked}")
    print(f"  reachable on {reachable} of {checked}")
    if count_true and count_false:
        print(f"  cost: reachable mean {nodes_true / count_true:,.0f} nodes, "
              f"unreachable mean {nodes_false / count_false:,.0f} "
              f"({(nodes_false / count_false) / (nodes_true / count_true):.1f}x)")

    # A run that only ever sees one answer proves nothing about the other.
    if reachable == 0 or reachable == checked:
        failures.append(
            f"the probe answered the same way on all {checked} deals "
            f"({'reachable' if reachable else 'unreachable'}); this run is not "
            f"exercising both branches, so it is not checking them")
    if implication_seen == 0:
        failures.append(
            "no deal in this run had the utility search report both bids "
            "surviving, so the implication route checked nothing")

    print()
    for f in failures[:8]:
        print(f"FAIL: {f}")
    if len(failures) > 8:
        print(f"... and {len(failures) - 8} more")
    if failures:
        return 1
    print("all three routes agree, on a population that is not all one answer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
