#!/usr/bin/env python3
"""Is the broken-bid MASK pinned by the objective, or is it a tie-break artifact?

Patch 57 withdrew a proposed per-seat answer column on the grounds that an
answer column may only hold what the OBJECTIVE pins, and demonstrated that
per-seat trick counts are not pinned: re-searching with a different preference
among equally-good cards moved them on 43 of 140 rows.  A per-row broken-bid
mask is an answer column and has to face the same question before it is
reported anywhere.

The question is not "do two solvers agree" -- it is whether the mask is a
FUNCTION of the game value at all.  So this walks the oracle's own backward
induction and carries, beside the value, the SET of final masks reachable when
every seat plays optimally at every node:

    M(node) = union of M(child) over the children achieving value(node)

If |M(root)| > 1 at a position, two optimal lines disagree about which bid dies
while agreeing on the value, and no search can be asked to prefer one -- the
mask would then be reporting move ordering rather than the answer.

Run per shape, because the three shapes pin different things:

  single nil     one bidder, so nils_set already names it.  Determined for
                 free; measured anyway, since a claim this file can check is
                 not a claim this file should assume.
  opposed nil    the primary is far_side_rank(mask), and under strict
                 opposition rank <-> mask is a bijection, so the value pins the
                 mask exactly.  The derivation is checked here rather than
                 trusted.
  twin nil       the primary is a COUNT of bids down.  With one of two down the
                 value cannot say which, so this is where ambiguity is
                 expected, and its RATE is what decides whether the mask can be
                 reported for this shape.
"""
import argparse
import importlib.util
import pathlib
import random
import sys
from typing import Dict, FrozenSet, Optional, Tuple

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("nil_oracle", ROOT / "nil_oracle.py")
oracle = importlib.util.module_from_spec(spec)
sys.modules["nil_oracle"] = oracle
spec.loader.exec_module(oracle)

ROLE_NIL = oracle.ROLE_NIL
ROLE_NIL_SET = oracle.ROLE_NIL_SET
ROLE_COVER = oracle.ROLE_COVER
ROLE_OPPONENT = oracle.ROLE_OPPONENT


def mask_name(mask: int) -> str:
    return "".join("NESW"[s] for s in range(4) if mask & (1 << s)) or "-"


# ---------------------------------------------------------------------------
# Twin nil: two bidders on one side.  Primary is a count, so this is the shape
# where the mask may not be a function of the value.
# ---------------------------------------------------------------------------
def masks_multi(hands, leader, trick, spades_broken, broken_nils, ctx, memo):
    """(best value, set of final masks over optimal lines) for the twin shape."""
    if not any(hands):
        return 0, frozenset({broken_nils})

    key = (hands, leader, trick, spades_broken, broken_nils)
    cached = memo.get(key)
    if cached is not None:
        return cached

    seat = (leader + len(trick)) % 4
    maximizing = (seat % 2) != ctx.minimizing_parity

    best_value: Optional[int] = None
    best_masks: FrozenSet[int] = frozenset()

    for card in oracle.legal_moves(hands[seat], trick, spades_broken):
        next_hands = tuple(
            tuple(c for c in hand if c != card) if s == seat else hand
            for s, hand in enumerate(hands)
        )
        next_broken = oracle.spades_broken_after(spades_broken, trick, card)
        played = trick + (card,)

        if len(played) == 4:
            winner = oracle.trick_winner(leader, played)
            gained = 0
            next_nils = broken_nils
            if winner in ctx.nil_seats and not (broken_nils & (1 << winner)):
                gained += ctx.primary_weight
                next_nils = broken_nils | (1 << winner)
            if winner % 2 == ctx.minimizing_parity:
                gained += ctx.secondary_weight
            sub_value, sub_masks = masks_multi(
                next_hands, winner, (), next_broken, next_nils, ctx, memo
            )
            value = gained + sub_value
        else:
            value, sub_masks = masks_multi(
                next_hands, leader, played, next_broken, broken_nils, ctx, memo
            )

        if best_value is None or value == best_value:
            best_value = value
            best_masks = best_masks | sub_masks
        elif (value > best_value) if maximizing else (value < best_value):
            best_value = value
            best_masks = sub_masks

    result = (best_value, best_masks)
    memo[key] = result
    return result


# ---------------------------------------------------------------------------
# One bid per side.  Utility PAIR; each side maximises its own component.  On a
# strictly opposed shape the two components sum to a constant, so a tie for the
# mover is a tie for both and "optimal" is unambiguous.
# ---------------------------------------------------------------------------
def masks_opposing(hands, leader, trick, spades_broken, broken_nils, ctx, memo):
    if not any(hands):
        return oracle._terminal_utility(broken_nils, ctx), frozenset({broken_nils})

    key = (hands, leader, trick, spades_broken, broken_nils)
    cached = memo.get(key)
    if cached is not None:
        return cached

    seat = (leader + len(trick)) % 4
    side = seat % 2

    best: Optional[Tuple[int, int]] = None
    best_masks: FrozenSet[int] = frozenset()

    for card in oracle.legal_moves(hands[seat], trick, spades_broken):
        next_hands = tuple(
            tuple(c for c in hand if c != card) if s == seat else hand
            for s, hand in enumerate(hands)
        )
        next_broken = oracle.spades_broken_after(spades_broken, trick, card)
        played = trick + (card,)

        if len(played) == 4:
            winner = oracle.trick_winner(leader, played)
            next_nils = broken_nils
            for s in ctx.nil_of_side:
                if winner == s:
                    next_nils |= 1 << s
            sub_value, sub_masks = masks_opposing(
                next_hands, winner, (), next_broken, next_nils, ctx, memo
            )
            gained = [0, 0]
            gained[winner % 2] = ctx.trick_weight
            value = (sub_value[0] + gained[0], sub_value[1] + gained[1])
        else:
            value, sub_masks = masks_opposing(
                next_hands, leader, played, next_broken, broken_nils, ctx, memo
            )

        if best is None or value[side] == best[side]:
            best = value if best is None else best
            best_masks = best_masks | sub_masks
        elif value[side] > best[side]:
            best = value
            best_masks = sub_masks

    result = (best, best_masks)
    memo[key] = result
    return result


# ---------------------------------------------------------------------------
# Single nil.  One bidder, so the mask has two possible values and nils_set
# already distinguishes them.  Walked anyway.
# ---------------------------------------------------------------------------
def masks_single(hands, leader, trick, spades_broken, broken_nils, ctx, memo):
    if not any(hands):
        return 0, frozenset({broken_nils})

    key = (hands, leader, trick, spades_broken, broken_nils)
    cached = memo.get(key)
    if cached is not None:
        return cached

    seat = (leader + len(trick)) % 4
    maximizing = (seat % 2) == 1

    best_value: Optional[int] = None
    best_masks: FrozenSet[int] = frozenset()

    for card in oracle.legal_moves(hands[seat], trick, spades_broken):
        next_hands = tuple(
            tuple(c for c in hand if c != card) if s == seat else hand
            for s, hand in enumerate(hands)
        )
        next_broken = oracle.spades_broken_after(spades_broken, trick, card)
        played = trick + (card,)

        if len(played) == 4:
            winner = oracle.trick_winner(leader, played)
            gained = 0
            next_nils = broken_nils
            if winner == ctx.designated:
                gained += ctx.primary_weight + ctx.tertiary_weight
                next_nils = broken_nils | (1 << winner)
            if winner % 2 == ctx.designated % 2:
                gained += ctx.secondary_weight
            sub_value, sub_masks = masks_single(
                next_hands, winner, (), next_broken, next_nils, ctx, memo
            )
            value = gained + sub_value
        else:
            value, sub_masks = masks_single(
                next_hands, leader, played, next_broken, broken_nils, ctx, memo
            )

        if best_value is None or value == best_value:
            best_value = value
            best_masks = best_masks | sub_masks
        elif (value > best_value) if maximizing else (value < best_value):
            best_value = value
            best_masks = sub_masks

    result = (best_value, best_masks)
    memo[key] = result
    return result


def run_shape(shape, roles, cards, cases, seed, secondary, verbose):
    rng = random.Random(seed)
    ambiguous = 0
    count_moved = 0
    checked = 0
    by_size: Dict[int, int] = {}
    examples = []

    for _ in range(cases):
        deck = [oracle.Card(s, r) for s in range(4) for r in range(2, 15)]
        pick = rng.sample(deck, 4 * cards)
        hands = tuple(tuple(sorted(pick[j * cards:(j + 1) * cards])) for j in range(4))
        leader = rng.randrange(4)
        pos = oracle.Position(hands=hands, leader=leader)

        if shape == "twin":
            nil_seats = tuple(s for s, r in enumerate(roles) if r == ROLE_NIL)
            pair = tuple(s for s, r in enumerate(roles) if r in (ROLE_NIL, ROLE_NIL_SET))
            pw, sw = oracle.multi_objective_weights(cards, secondary)
            ctx = oracle._MultiCtx(
                nil_seats=nil_seats,
                minimizing_parity=pair[0] % 2,
                primary_weight=pw,
                secondary_weight=sw,
            )
            _, masks = masks_multi(hands, leader, (), False, 0, ctx, {})
        elif shape == "opposed":
            nil_of_side = tuple(
                next(s for s in range(4) if s % 2 == side and roles[s] == ROLE_NIL)
                for side in range(2)
            )
            rw, tw = oracle.opposing_weights(cards, secondary)
            ctx = oracle._OppCtx(
                nil_of_side=nil_of_side,
                partner_role=tuple(roles[(nil_of_side[i] + 2) % 4] for i in range(2)),
                rank_weight=rw,
                trick_weight=tw,
            )
            _, masks = masks_opposing(hands, leader, (), False, 0, ctx, {})
        else:
            designated = next(s for s, r in enumerate(roles) if r == ROLE_NIL)
            pw, sw, tw = oracle.objective_weights(cards, secondary, False)
            ctx = oracle._Ctx(
                designated=designated,
                primary_weight=pw,
                secondary_weight=sw,
                tertiary_weight=tw,
            )
            _, masks = masks_single(hands, leader, (), False, 0, ctx, {})

        checked += 1
        by_size[len(masks)] = by_size.get(len(masks), 0) + 1
        if len(masks) > 1:
            ambiguous += 1
            # Does the COUNT move too, or only the identity?  `nils_set` ships
            # today and is a count; if the count is stable across optimal lines
            # then the widening to a mask is exactly the step that crosses from
            # determined to undetermined, and the existing field is untouched.
            if len({bin(m).count("1") for m in masks}) > 1:
                count_moved += 1
            if len(examples) < 3:
                examples.append((pos, sorted(mask_name(m) for m in masks)))

    pct = 100.0 * ambiguous / checked if checked else 0.0
    print(f"  {shape:<8} roles={' '.join(str(r) for r in roles)}  "
          f"{cards} cards, {checked} positions")
    sizes = ", ".join(f"{k} mask{'s' if k > 1 else ''}: {v}"
                      for k, v in sorted(by_size.items()))
    print(f"           {sizes}")
    print(f"           AMBIGUOUS: {ambiguous}/{checked} = {pct:.2f}%"
          f"   (nils_set count also moved on {count_moved})")
    if verbose and examples:
        for pos, names in examples:
            print(f"             e.g. leader={'NESW'[pos.leader]} -> {{{', '.join(names)}}}")
    return ambiguous, checked


SHAPES = {
    "single":  [ROLE_NIL, ROLE_OPPONENT, ROLE_COVER, ROLE_OPPONENT],
    "twin":    [ROLE_NIL, ROLE_OPPONENT, ROLE_NIL, ROLE_OPPONENT],
    "opposed": [ROLE_NIL, ROLE_NIL, ROLE_OPPONENT, ROLE_COVER],
}


def run_assertions(args):
    """Pin the claim the C++ field `nils_set_mask_determined` is derived from.

    BOTH directions are asserted, because only one of them is a regression test.
    That the pinned shapes never disagree is the safety claim.  That the twin
    shape DOES disagree is what stops this from being a check that passes by
    seeing nothing -- if the ambiguity ever measured zero, the honest conclusion
    would be that this file had stopped exercising the case, not that the shape
    had become determined.
    """
    failures = []
    print(f"asserting the determinacy claim, {args.cards} cards, "
          f"{args.cases} positions per shape")

    for shape in ("single", "opposed"):
        for secondary in ("max", "min"):
            amb, checked = run_shape(shape, SHAPES[shape], args.cards, args.cases,
                                     args.seed, secondary, False)
            if amb != 0:
                failures.append(
                    f"{shape}/{secondary}: the objective is claimed to PIN the mask, "
                    f"but {amb}/{checked} positions have two optimal lines "
                    f"breaking different bids")

    twin_total = 0
    twin_checked = 0
    for secondary in ("max", "min"):
        amb, checked = run_shape("twin", SHAPES["twin"], args.cards, args.cases,
                                 args.seed, secondary, False)
        twin_total += amb
        twin_checked += checked
    if twin_total == 0:
        failures.append(
            f"twin: 0/{twin_checked} ambiguous.  The twin shape is REPORTED as "
            f"undetermined on the strength of this disagreement existing; "
            f"measuring none of it means this suite has stopped reaching the "
            f"case, not that the shape has become determined")

    print()
    if failures:
        for f in failures:
            print(f"FAIL: {f}")
        return 1
    print(f"the pinned shapes never disagree; the twin shape disagrees on "
          f"{twin_total}/{twin_checked}, so the case is being reached")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cards", type=int, default=4)
    ap.add_argument("--cases", type=int, default=40)
    ap.add_argument("--seed", type=int, default=17)
    ap.add_argument("--secondary", default="max", choices=("max", "min"))
    ap.add_argument("--shape", default="all",
                    choices=("all", "single", "twin", "opposed"))
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--assert-claim", dest="assert_claim", action="store_true",
                    help="check the claim nils_set_mask_determined is built on, "
                         "and fail if it does not hold")
    args = ap.parse_args()

    if args.assert_claim:
        return run_assertions(args)

    shapes = list(SHAPES) if args.shape == "all" else [args.shape]
    print(f"broken-bid mask determinacy, {args.cards} cards, "
          f"secondary={args.secondary}, seed={args.seed}")
    total_amb = 0
    for shape in shapes:
        amb, _ = run_shape(shape, SHAPES[shape], args.cards, args.cases,
                           args.seed, args.secondary, args.verbose)
        total_amb += amb
    print()
    print(f"total ambiguous: {total_amb}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
