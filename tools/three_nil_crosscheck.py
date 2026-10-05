#!/usr/bin/env python3
"""Three nils: does the C++ solver agree with the oracle, and with itself mid-hand?

TWO CHECKS, because the shape makes two different claims.

THE GAME VALUE (default).  Random deals across all eight three-bid role arrays
-- the lone bid in each seat, its partner leaning 2 or 3 -- in both tie-break
directions.  As tools/opposing_crosscheck.py does for one bid per side, it
compares the UTILITY PAIR rather than the cards: the solver's own line is
replayed and scored under the oracle's rules, and must reach the oracle's game
value.  With --moves it also scores every root card: each row's reported
outcome and trick counts must land on the value the oracle gives that card, and
`is_best` must be set exactly on the cards that reach the best of them.  A row's
mask may be a witness where one twin of two is down -- the value cannot say
which -- so it is the row's RANK that is compared, never its mask alone.

THE CONVERSION RULE (--subgame).  T specified the shape by what happens when a
bid breaks: a broken twin takes the lean opposite the lone side's, a broken lone
bid turns its side into two opponents.  So play the solver's line to the trick
on which the first bid breaks, hand what is left back to the solver with the
roles converted the way a caller would (nil_oracle.three_nil_after_set), and
require the same ending: every bid still live ends the same way -- the count of
twins down where the lone bid broke, since that shape does not pin which -- and
each side takes the same tricks over the rest of the hand.  This is solver
against solver, so it runs at sizes the oracle cannot reach.
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

SEATS = "NESW"
RANKS = "23456789TJQKA"

# Every three-bid arrangement: the lone bid in each seat, its partner leaning
# either way.  Written from North, which is the anchor every deal here uses.
SHAPES = []
for lone in range(4):
    for lean in (3, 2):
        roles = [0] * 4
        roles[(lone + 2) % 4] = lean
        SHAPES.append(roles)


def to_pbn(hands):
    return "N:" + " ".join(
        ".".join("".join(RANKS[c.rank - 2] for c in sorted(
            (c for c in h if c.suit == s), key=lambda c: -c.rank)) for s in range(4))
        for h in hands)


def run_cli(cli, pbn, leader, roles, broken, secondary, extra=()):
    # A small table: every call is a fresh process, and the default 512 MiB is
    # paid in page faults per call for positions that need a fraction of it.
    # The table size moves node counts, never answers.
    cmd = [cli, "--pbn", pbn, "--leader", SEATS[leader],
           "--seats", " ".join(str(r) for r in roles), "--compact",
           "--secondary", secondary, "--tt-mb", "16"] + list(extra)
    if broken:
        cmd.append("--spades-broken")
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        return None, proc.stderr.strip()
    return proc.stdout, ""


def parse_pv(out):
    m = re.search(r"^pv=(.*)$", out, re.M)
    toks = m.group(1).split() if m else []
    return [(SEATS.index(t.split(":")[0]), oracle.card_from_str(t.split(":")[1])) for t in toks]


def utility_of(roles, mask, side_tricks, rank_w, trick_w):
    """Both sides' utilities under the oracle's rules, from a mask and side tricks."""
    lone, lean_seat, twins = oracle.three_nil_seats(roles)
    ctx = oracle._ThreeCtx(lone=lone, twins=twins, lean=roles[lean_seat],
                           rank_weight=rank_w, trick_weight=trick_w)
    ranks = oracle._three_ranks(mask, ctx)
    return tuple(rank_w * ranks[i] + trick_w * side_tricks[i] for i in (0, 1))


def oracle_row(pos, roles, card, rank_w, trick_w):
    """The oracle's utility pair for playing `card` at the root, then best play."""
    lone, lean_seat, twins = oracle.three_nil_seats(roles)
    ctx = oracle._ThreeCtx(lone=lone, twins=twins, lean=roles[lean_seat],
                           rank_weight=rank_w, trick_weight=trick_w, memo={})
    seat = (pos.leader + len(pos.current_trick)) % 4
    hands = tuple(tuple(c for c in h if c != card) if s == seat else h
                  for s, h in enumerate(pos.hands))
    broken = oracle.spades_broken_after(pos.spades_broken, pos.current_trick, card)
    played = pos.current_trick + (card,)
    if len(played) == 4:
        winner = oracle.trick_winner(pos.leader, played)
        mask = 1 << winner if (winner == lone or winner in twins) else 0
        sub, _ = oracle._search_three_nils(hands, winner, (), broken, mask, ctx)
        gained = [0, 0]
        gained[winner % 2] = trick_w
        return (sub[0] + gained[0], sub[1] + gained[1])
    sub, _ = oracle._search_three_nils(hands, pos.leader, played, broken, 0, ctx)
    return sub


def check_value(args, cli):
    rng = random.Random(args.seed)
    checked = agree = rows = rows_agree = skipped = 0
    outcomes = set()
    problems = []
    for i in range(args.cases):
        roles = SHAPES[i % len(SHAPES)]
        secondary = "min" if (i // len(SHAPES)) % 3 == 2 else "max"
        deck = [oracle.Card(s, r) for s in range(4) for r in range(2, 15)]
        pick = rng.sample(deck, 4 * args.cards)
        hands = [sorted(pick[j * args.cards:(j + 1) * args.cards]) for j in range(4)]
        leader = rng.randrange(4)
        broken = rng.random() < 0.5
        # Mid-trick roots as well as clean leads: one in three starts with a
        # card or two already on the table.
        on_table = rng.choice((0, 0, 1, 2))
        trick = []
        for k in range(on_table):
            seat = (leader + k) % 4
            legal = oracle.legal_moves(tuple(hands[seat]), tuple(trick), broken)
            card = rng.choice(legal)
            hands[seat].remove(card)
            broken = oracle.spades_broken_after(broken, tuple(trick), card)
            trick.append(card)
        pos = oracle.Position(hands=tuple(tuple(h) for h in hands), leader=leader,
                              spades_broken=broken, current_trick=tuple(trick))
        try:
            pos.validate()
        except Exception:
            skipped += 1
            continue
        ref = oracle.solve_three_nils(pos, roles, use_memo=True, secondary=secondary)
        outcomes.add(ref.nils_set_mask)
        rank_w, trick_w = oracle.opposing_weights(pos.tricks_remaining, secondary)

        extra = ["--trick", " ".join(str(c) for c in trick)] if trick else []
        if args.moves:
            extra.append("--moves")
        out, err = run_cli(cli, to_pbn(hands), leader, roles, broken, secondary, extra)
        checked += 1
        label = "%s %s %s" % (" ".join(map(str, roles)), secondary, to_pbn(hands))
        if out is None:
            problems.append((label, "refused: " + err[:80]))
            continue
        plays = parse_pv(out)
        seat_tricks = oracle.replay_pv_by_seat(pos, plays)
        mask = sum(1 << s for s in range(4) if roles[s] == 0 and seat_tricks[s] > 0)
        side = [seat_tricks[0] + seat_tricks[2], seat_tricks[1] + seat_tricks[3]]
        got = utility_of(roles, mask, side, rank_w, trick_w)
        if got == tuple(ref.utility):
            agree += 1
        else:
            problems.append((label, "solver line scores %s, oracle value %s"
                             % (got, tuple(ref.utility))))
        if not args.moves:
            continue

        # Every root card.  `side_tricks` on a row is the side holding the
        # first bidder in N, E, S, W order -- roles.nil_seat() -- and
        # `opponent_tricks` the other.
        near = next(s for s in range(4) if roles[s] == 0) % 2
        to_move = (leader + len(trick)) % 4 % 2
        want = {}
        have = {}
        best_flag = {}
        for m in re.finditer(r"^move=([^:]+):(-?\d+):(-?\d+):(-?\d+):(-?\d+):(-?\d+):(\d)",
                             out, re.M):
            card = oracle.card_from_str(m.group(1))
            row_mask = int(m.group(3))
            row_side = [0, 0]
            row_side[near] = int(m.group(5))
            row_side[1 - near] = int(m.group(6))
            have[card] = utility_of(roles, row_mask, row_side, rank_w, trick_w)
            want[card] = oracle_row(pos, roles, card, rank_w, trick_w)
            best_flag[card] = m.group(7) == "1"
        top = max(u[to_move] for u in want.values())
        for card in want:
            rows += 1
            if have[card] == want[card] and best_flag[card] == (want[card][to_move] == top):
                rows_agree += 1
            else:
                problems.append((label, "row %s: solver %s best=%s, oracle %s best=%s"
                                 % (card, have[card], best_flag[card], want[card],
                                    want[card][to_move] == top)))

    print("%d deals at %d cards across %d three-bid role sets (%d skipped)"
          % (checked, args.cards, len(SHAPES), skipped))
    print("  solver line achieves the oracle's game value: %d of %d" % (agree, checked))
    if args.moves:
        print("  root cards scored as the oracle scores them: %d of %d" % (rows_agree, rows))
    print("  distinct outcome masks seen: %d" % len(outcomes))
    for label, why in problems[:6]:
        print("   MISMATCH %s\n     %s" % (label, why))
    ok = agree == checked and rows_agree == rows and checked > 0
    # A run that only ever sees one ending proves little about a ladder of six.
    if len(outcomes) < 4:
        print("   too few distinct outcomes to exercise the ladder")
        ok = False
    return 0 if ok else 1


def check_subgame(args, cli):
    rng = random.Random(args.seed)
    resolved = agree = skipped = 0
    by_kind = {"lone": 0, "twin": 0}
    problems = []
    for i in range(args.cases):
        roles = SHAPES[i % len(SHAPES)]
        secondary = "min" if (i // len(SHAPES)) % 3 == 2 else "max"
        deck = [oracle.Card(s, r) for s in range(4) for r in range(2, 15)]
        pick = rng.sample(deck, 4 * args.cards)
        hands = [sorted(pick[j * args.cards:(j + 1) * args.cards]) for j in range(4)]
        leader = rng.randrange(4)
        broken = rng.random() < 0.5
        pos = oracle.Position(hands=tuple(tuple(h) for h in hands), leader=leader,
                              spades_broken=broken)
        try:
            pos.validate()
        except Exception:
            skipped += 1
            continue
        out, err = run_cli(cli, to_pbn(hands), leader, roles, broken, secondary)
        if out is None:
            problems.append((roles, to_pbn(hands), "refused: " + err[:80]))
            resolved += 1
            continue
        plays = parse_pv(out)
        whole = oracle.replay_pv_by_seat(pos, plays)
        whole_mask = sum(1 << s for s in range(4) if roles[s] == 0 and whole[s] > 0)

        live = [list(h) for h in hands]
        cur, spades, trick = leader, broken, []
        split = None
        tail = [0, 0]
        for seat, card in plays:
            live[seat].remove(card)
            spades = oracle.spades_broken_after(spades, tuple(trick), card)
            trick.append(card)
            if len(trick) < 4:
                continue
            winner = oracle.trick_winner(cur, tuple(trick))
            cur, trick = winner, []
            if split is not None:
                tail[winner % 2] += 1
            elif roles[winner] == 0 and any(live):
                split = (winner, [list(h) for h in live], cur, spades)
        if split is None:
            skipped += 1
            continue
        broke, rest_hands, rest_leader, rest_spades = split
        conv = oracle.three_nil_after_set(roles, broke)
        out2, err2 = run_cli(cli, to_pbn(rest_hands), rest_leader, conv, rest_spades, secondary)
        resolved += 1
        lone, _, twins = oracle.three_nil_seats(roles)
        kind = "lone" if broke == lone else "twin"
        by_kind[kind] += 1
        label = "%s %s after %s" % (" ".join(map(str, roles)), secondary, SEATS[broke])
        if out2 is None:
            problems.append((roles, to_pbn(hands), label + ": re-solve refused: " + err2[:80]))
            continue
        rest = oracle.Position(hands=tuple(tuple(h) for h in rest_hands), leader=rest_leader,
                               spades_broken=rest_spades)
        again = oracle.replay_pv_by_seat(rest, parse_pv(out2))
        again_side = [again[0] + again[2], again[1] + again[3]]
        if kind == "lone":
            same = (sum(1 for t in twins if again[t] > 0) ==
                    sum(1 for t in twins if whole_mask & (1 << t)))
        else:
            same = all((again[s] > 0) == bool(whole_mask & (1 << s))
                       for s in (lone,) + tuple(twins) if s != broke)
        if same and again_side == tail:
            agree += 1
        else:
            problems.append((roles, to_pbn(hands),
                             "%s: tail tricks %s vs re-solve %s, outcome %s"
                             % (label, tail, again_side, "same" if same else "DIFFERENT")))

    print("%d deals at %d cards: %d re-solved mid-hand (%d after the lone bid broke, "
          "%d after a twin), %d never broke a bid" % (resolved + skipped, args.cards, resolved,
                                                     by_kind["lone"], by_kind["twin"], skipped))
    print("  re-solve from the converted roles agrees with the whole-hand line: %d of %d"
          % (agree, resolved))
    for roles, pbn, why in problems[:6]:
        print("   MISMATCH %s  %s\n     %s" % (" ".join(map(str, roles)), pbn, why))
    ok = agree == resolved and resolved > 0
    # Both conversions must actually be exercised, or one of them is unchecked.
    if by_kind["lone"] == 0 or by_kind["twin"] == 0:
        print("   only one of the two conversions was reached")
        ok = False
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", default=str(ROOT / "build/bin/nil_cli"))
    ap.add_argument("--cases", type=int, default=48)
    ap.add_argument("--cards", type=int, default=4)
    ap.add_argument("--seed", type=int, default=17)
    ap.add_argument("--moves", action="store_true",
                    help="also score every root card against the oracle")
    ap.add_argument("--subgame", action="store_true",
                    help="check the conversion rule mid-hand, solver against solver")
    args = ap.parse_args()
    if args.subgame:
        return check_subgame(args, args.exe)
    return check_value(args, args.exe)


if __name__ == "__main__":
    raise SystemExit(main())
