/* nil_solver.h -- C ABI for the Spades nil double-dummy solver.
 *
 * Everything crossing this boundary is a plain C type so the library can be
 * consumed from C#, C, or anything else that can call a DLL.  Strings are
 * NUL-terminated ASCII/UTF-8.  Nothing is allocated on behalf of the caller:
 * the caller supplies every output buffer.
 *
 * THE QUESTION THIS ANSWERS
 * -------------------------
 * Given a layout, whose turn it is, and WHAT EACH SEAT IS DOING, the solver
 * plays the hand out under a lexicographic objective:
 *
 *   PRIMARY    the nil bidder's trick count.  The nil bidder and its covering
 *              partner minimise it; both opponents maximise it.  So
 *              `nils_set == 1` means the nil is dead however it is played,
 *              and `nils_set == 0` means the nil side has a line that
 *              survives.
 *
 *   SECONDARY  each pair's own trick count, used only to choose among lines
 *              that are already equally good for the primary.  By default each
 *              pair takes as many tricks as it can; pass
 *              NIL_FLAG_MINIMISE_OWN_TRICKS to make each pair take as few as
 *              it can instead.
 *
 *   TERTIARY   which of the nil side's two partners holds those tricks.  Only
 *              the covering partner's tricks count towards the partner's bid,
 *              so among lines where the pair takes the same total, the pair
 *              prefers the nil bidder to take fewer.  This level is inert while
 *              the primary is on -- that has already pinned the nil bidder's
 *              count -- and off entirely under NIL_FLAG_MINIMISE_OWN_TRICKS,
 *              where bags accrue to the pair whoever won the trick and the two
 *              partners are genuinely interchangeable.
 *
 * Both sides see all four hands (double dummy).  The primary is not ordinary
 * trick maximisation: a side will happily throw tricks away if that forces one
 * onto the nil bidder.  The secondary only breaks ties, so it can never change
 * the answer to the nil question -- it only decides what the two pairs do with
 * the tricks that the nil question leaves undetermined.
 *
 * Give the nil bidder NIL_ROLE_NIL_SET once the nil has actually been broken in
 * the real game.  The primary objective is dropped, both sides stop protecting
 * and attacking it, and only the secondary objective is optimised.
 *
 * With that role set and the pair still taking tricks, the tertiary level is
 * what decides the split, and it sits BELOW the pair's total on purpose.  The
 * two sides are not strictly opposed there: both would rather the nil bidder
 * took nothing, so the split is slack only one side cares about rather than a
 * tug of war.  Keeping it below the total leaves the opponents' objective
 * exactly "take as many as we can" and resolves the split against the pair, so
 * nil_side_tricks - nil_tricks is the partner count the pair can GUARANTEE, not
 * the one it might get if the opponents were being unhelpful to themselves.
 *
 * Combine that role with NIL_FLAG_MINIMISE_OWN_TRICKS and the tertiary level is
 * off: bags accrue to the pair whoever won, the two partners are
 * interchangeable, and nil_tricks is then whatever the tie-break produced
 * rather than an answer to a question anyone asked.
 *
 * TWO MODES
 * ---------
 * All of the above describes the FULL mode, which is the default and which
 * answers everything.  NIL_FLAG_FAST_MODE asks the boolean question alone:
 * `nils_set` comes back, the three trick counts come back as
 * NIL_TRICKS_UNKNOWN, and there is no principal variation.
 *
 * The two modes always agree on nils_set.  They have to: full mode packs the
 * nil bidder's trick count into a scalar with two tie-break levels below it,
 * fast mode drops those levels and searches the count on its own, and the
 * levels below could never have moved the level above.  What fast mode buys is
 * that a boolean objective can be pruned hard and a thousand-wide lexicographic
 * one cannot -- and the pruning has landed, so that is no longer a promise.
 * Fast mode searches an alpha-beta window of [0, 1], which makes it an AND-OR
 * search: the opponents need one line that forces a trick onto the nil bidder,
 * the nil side needs every opponent line to fail, and the first answer either
 * way ends the node.  It visits somewhere between a tenth and a five-hundredth
 * of the nodes full mode does, and the gap widens with the size of the hand.
 * Full mode is deliberately left exhaustive: it is the answer that carries its
 * own evidence, and it is what the boolean answer is checked against.
 *
 * Which to call, from a game client:
 *
 *   "can this nil still be broken?"        fast mode (or nil_count_set(), which
 *                                          selects it for you)
 *   "what is the score if we play on?"     full mode
 *   "show me the line"                     full mode; nil_solve_pv refuses the
 *                                          fast flag rather than hand back an
 *                                          empty string
 */
#ifndef NIL_SOLVER_H
#define NIL_SOLVER_H

#include <stdint.h>

#if defined(_WIN32)
#if defined(NIL_SOLVER_BUILD_DLL)
#define NIL_SOLVER_API __declspec(dllexport)
#elif defined(NIL_SOLVER_STATIC)
#define NIL_SOLVER_API
#else
#define NIL_SOLVER_API __declspec(dllimport)
#endif
#define NIL_SOLVER_CALL __cdecl
#else
#define NIL_SOLVER_API __attribute__((visibility("default")))
#define NIL_SOLVER_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Seats, clockwise. These are also the indices used by PBN. */
#define NIL_SEAT_NORTH 0
#define NIL_SEAT_EAST 1
#define NIL_SEAT_SOUTH 2
#define NIL_SEAT_WEST 3

/* WHO IS WHO: the `seats` argument.
 *
 * Every entry point takes a four-element array of these, and it describes the
 * whole objective.  It replaced two arguments in patch 54 -- a nil seat and an
 * already-set flag -- because two scalars can only describe ONE nil, and real
 * spades puts two on the table often enough that the optimal line changes.
 *
 * ORDER.  The four values run CLOCKWISE FROM THE SEAT THE `pbn` STRING NAMES,
 * which is the same order the hands in that string are in.  So with
 * "W:..." the array reads West, North, East, South, and
 *
 *     seats = { NIL_ROLE_NIL, NIL_ROLE_OPPONENT, NIL_ROLE_COVER,
 *               NIL_ROLE_OPPONENT }
 *
 * says West bid the nil, East is covering it, and North and South are trying to
 * break it.  Note that `leader` is NOT relative in this way: it is an absolute
 * NIL_SEAT_*, as it always was.
 *
 * WHAT IS ACCEPTED TODAY.  Exactly one seat holds a nil, exactly one covers it,
 * the cover sits across from the nil bidder, and the other two oppose.  Any
 * other arrangement is refused: a value outside 0..3 or a malformed layout as
 * NIL_ERR_ILLEGAL_POSITION, and a well-formed layout with two nils in it as
 * NIL_ERR_UNSUPPORTED, so a caller can tell a typo from a feature that has not
 * landed yet. */
/* A nil bidder that has not yet taken a trick. */
#define NIL_ROLE_NIL 0
/* A nil bidder whose nil is already broken.  This is what the retired
 * NIL_FLAG_NIL_ALREADY_SET used to say. */
#define NIL_ROLE_NIL_SET 1
/* The nil bidder's partner, covering it. */
#define NIL_ROLE_COVER 2
/* A seat on a side with no nil bid. */
#define NIL_ROLE_OPPONENT 3

/* Flags for the `flags` argument. */
#define NIL_FLAG_NONE 0x0u
/* Spades are already broken in the given position. */
#define NIL_FLAG_SPADES_BROKEN 0x1u
/* The tie-break direction: each pair takes as FEW tricks as it can, rather
 * than as many.  Applies to both pairs at once, which is coherent because
 * their trick counts sum to a constant. */
#define NIL_FLAG_MINIMISE_OWN_TRICKS 0x10u
/* Bit 0x20u is RETIRED AND BURNED, not free.  It was NIL_FLAG_NIL_ALREADY_SET,
 * which said the nil had already been broken; that is now NIL_ROLE_NIL_SET in
 * the `seats` array.  The bit is left unassigned rather than recycled so an old
 * caller gets an ignored flag rather than a silently different objective, the
 * same treatment 0x2u got in patch 45. */
/* Disable the transposition table.  The table caches a pure function, so it
 * changes neither the answer nor the principal variation; this flag exists only
 * to make the search maximally dumb when cross-checking.  It is also very much
 * slower -- orders of magnitude beyond about five cards a hand. */
#define NIL_FLAG_NO_MEMO 0x4u
/* Generate every legal card instead of one per class of rank-equivalent ones.
 * With the jack gone, playing the king and playing the queen are the same move
 * under two names, and the solver normally searches only one of them; this flag
 * turns that off.  Same answer and same principal variation either way -- it is
 * a diagnostic, and it is several times slower. */
#define NIL_FLAG_NO_COLLAPSE 0x40u
/* Answer the nil question and nothing else.  nils_set is filled in; the three
 * trick counts come back as NIL_TRICKS_UNKNOWN and there is no principal
 * variation, so nil_solve_pv rejects this flag.
 *
 * Combined with NIL_ROLE_NIL_SET the answer is 1 with no search at all, since
 * that role asserts the only thing this mode computes.
 * NIL_FLAG_MINIMISE_OWN_TRICKS is inert here: it points at a tie-break level
 * this mode does not have. */
#define NIL_FLAG_FAST_MODE 0x80u
/* Do not settle a position by proof when it can be settled by proof: search for
 * the answer instead.  Under NIL_FLAG_FAST_MODE the solver returns immediately
 * when the nil bidder provably cannot be forced to win a trick, or provably
 * must win one; this flag turns both proofs off.  Same answer either way -- it
 * is a diagnostic, and it is slower.  Inert without NIL_FLAG_FAST_MODE, which
 * is the only mode the proofs apply to. */
#define NIL_FLAG_NO_STATIC_BOUNDS 0x100u
/* Try moves in the canonical enumeration order rather than a promising-first
 * one.  Under NIL_FLAG_FAST_MODE the solver orders moves by what the nil
 * question rewards -- shedding the nil bidder's dangerous cards, attacking the
 * suits it is short in -- and this flag turns that off.  Same answer either
 * way: ordering changes which cutoff the search finds first and nothing else.
 * It is a diagnostic and a control arm, and it is slower.  Inert without
 * NIL_FLAG_FAST_MODE, which is the only mode that orders: full mode's cutoffs
 * are reachable as of NIL_FLAG_NO_NARROW's arrival, but ordering there would
 * still cost the principal variation its canonical tie-break. */
#define NIL_FLAG_NO_ORDERING 0x200u

/* Search a node's moves against the window the node was given rather than
 * against a window that narrows as its own moves come back.  Narrowing is the
 * half of alpha-beta that makes a cutoff reachable at all, so this flag turns
 * full mode back into the exhaustive minimax it was before, and is the control
 * arm the saving is measured against.  Same answer either way, and the same
 * principal variation: every entry point that needs an exact value asks with a
 * window no value can reach, and a probe under such a window can only be
 * answered by an exact entry, which is by definition one that did not cut.
 * Inert under NIL_FLAG_FAST_MODE, whose window is null -- there is no integer
 * strictly between alpha and alpha + 1 for a narrowed bound to land on, so a
 * fast search is unchanged node for node with or without it. */
#define NIL_FLAG_NO_NARROW 0x400u

/* Do not spend a MODE_FAST presolve to bound MODE_FULL's root window.  The two
 * modes answer different questions about one position and the cheap answer
 * bounds the dear one: if the nil bidder cannot be forced to take a trick then
 * the packed value cannot reach the range where it does, so a beta just below
 * that range contains the answer and refutes every line that tries.  The
 * presolve costs on the order of a thousandth of the search it bounds, and its
 * nodes are counted in the result rather than hidden.
 *
 * Same value and same principal variation either way -- the bound is derived
 * from the objective's own weights, not estimated -- so this flag is a control
 * arm and a way to pay nothing on positions where the presolve will not help.
 * Inert under NIL_FLAG_FAST_MODE, which is the presolve, and inert under
 * NIL_ROLE_NIL_SET, where the value range has no gap in it to exploit.
 *
 * WITH A BID ON EACH SIDE the same flag governs a second, larger presolve, and
 * it is the same idea rather than a new one.  TWO fast searches run, one per
 * bidder, each asking whether that bid can be broken with both opponents
 * unconstrained -- an ordinary single-nil question.  Each answer is a GUARANTEE
 * that survives being asked inside a deal where the attacker has a bid of its
 * own, so each confines the outcome to two of the four and bounds the value at
 * one end.  When both bids come back safe, or both breakable, the two bounds
 * meet and the root window is a single band: the outcome is settled before the
 * full search starts, and every line heading for a different one is refuted by
 * arithmetic.  When they disagree the band is open at one end, which is the
 * single-nil shape above.
 *
 * Worth 1.31x in nodes and 1.23x on the clock over the eight contested 13-card
 * deals in tests/corpus/opposed13.txt, and roughly a wash on deals whose bids
 * were never in doubt.  It is NOT taken by nil_solve_moves on this shape; see
 * the note at that call site. */
#define NIL_FLAG_NO_PRESOLVE 0x800u

/* Search the final trick rather than evaluating it.  With four cards left every
 * hand holds exactly one, so the trick is forced and its value is arithmetic;
 * the solver computes it in a single node instead of recursing five.  This flag
 * takes the shortcut away and is the control arm the saving is measured
 * against.
 *
 * Same answer, same principal variation, same value either way -- a forced line
 * has one value and no window disagrees about it.  Only the node count and the
 * running time move. */
#define NIL_FLAG_NO_LAST_TRICK 0x2000u

/* Do not spend the static proofs in MODE_FULL.
 *
 * The two proofs in the solver settle a MODE_FAST node outright, because that
 * mode's value is the nil bidder's trick count and the proofs are about exactly
 * that.  MODE_FULL's value also carries the pair's tricks, so the same proof
 * bounds the node instead of settling it, and the bound is returned only when
 * it already clears the window.  This flag turns that off and is the control
 * arm the saving is measured against.
 *
 * Same value and same principal variation either way.  What it does change is
 * MODE_FULL's node count, which was a fixed point from patch 8 to patch 29:
 * counts taken before that patch are not comparable with counts taken after
 * unless this flag is set.  Implied by NIL_FLAG_NO_STATIC_BOUNDS. */
#define NIL_FLAG_NO_FULL_STATIC 0x4000u

/* Consult the transposition table at every ply rather than only at a trick
 * boundary.
 *
 * By default the solver probes and stores only when no card has been played to
 * the current trick, which is what DDS does and what the measurements say is
 * right: building the key is O(live cards) and three nodes in four are
 * mid-trick, so the table's own cost dominates the per-node cost of the search,
 * while the hit rate one ply into a trick is 5-11% against 62-70% at a
 * boundary.  Turning it off is worth 2.3x to 3.6x of wall time at 11 to 13
 * cards and is better at every size measured.
 *
 * This flag restores the old every-ply behaviour and is the control arm the
 * saving is measured against.  The table is a memo, so the value, the trick
 * counts and the principal variation are identical either way; the node count
 * moves in both directions, which is what the roadmap entry is about. */
#define NIL_FLAG_TT_ALL_PLIES 0x8000u

/* Do not answer a MODE_FULL node from arithmetic when the reachable range of
 * the remaining tricks already falls outside the window.
 *
 * A trick is worth a fixed amount to the nil bidder, another fixed amount to
 * the cover partner and nothing to either opponent, so a subtree with t tricks
 * left has a value range computable from t alone -- no cards are read.  When
 * that whole range sits on one side of the window the node is answered with a
 * fail-soft bound.  This is the direction of DDS's TargetReached that the
 * solver did not have.
 *
 * Inert under NIL_FLAG_FAST_MODE, whose reachable range is [0, t] against a
 * window of [0, 1].  Same value and same principal variation; diagnostic and a
 * control arm. */
#define NIL_FLAG_NO_TARGET_BOUNDS 0x10000u

/* Do not put one card from each present suit at the head of the move list.
 *
 * By default the solver searches one card from every suit it may legally choose
 * among before returning to the canonical order, which is DDS section 5's
 * "good mixture of moves (i.e. not all cards from the same suit first)".  It
 * spends nothing to decide the order -- the same moves are searched -- and is
 * worth 5.4% to 9.0% of nodes at 11 to 13 cards.
 *
 * Applies in both modes, and inert on a seat following suit.  Same value and,
 * in full mode, the same principal variation -- the canonical re-derivation
 * pins it.  In fast mode it may return a different one of several equally good
 * moves, exactly as the other move-ordering heuristics may.  Diagnostic and a
 * control arm. */
#define NIL_FLAG_NO_SUIT_MIX 0x20000u

/* Do not tighten the reachable-range bound with the opponents' forced tricks.
 *
 * DDS section 4.  Where one opponent hand holds the top outstanding spades as a
 * run from the top, each of those cards wins the trick it is played on however
 * all four players play -- spades is trump, and the only spades above it sit in
 * the same hand, which plays one card per trick.  Those tricks cannot fall to
 * the nil side, so the range the reach bound ranges over shrinks and a node
 * that the untightened bound could not answer may now be answered.
 *
 * Inert under NIL_FLAG_FAST_MODE, whose value is the nil bidder's own trick
 * count: a count of the OPPONENTS' tricks does not bound it.  Same value and
 * same principal variation; diagnostic and a control arm. */
#define NIL_FLAG_NO_LATER_TRICKS 0x40000u

/* Do not let a transposition-table entry that matches the position without
 * settling it shorten the node.
 *
 * By default such a match -- the table calls it a PARTIAL -- supplies the
 * threshold the node's own cutoff test reads, when it is tighter than the one
 * the window carries: beta at a maximising node, alpha at a minimising one.
 * The window itself is untouched, so children are searched under exactly what
 * the caller asked and nothing about their stored entries changes.  Tightening
 * the window instead, which is what the textbook form of this does, was
 * measured and is a loss; see ROADMAP.md item 41.
 *
 * Inert under NIL_FLAG_FAST_MODE, and by arithmetic rather than by a gate:
 * every fast node is asked about [0, 1] and every value it stores is a bound at
 * 0 or at 1, so every match settles its window and there are no partials.  Same
 * value and same principal variation; diagnostic and a control arm. */
#define NIL_FLAG_NO_TT_NARROW 0x80000u

/* Take the forced-trump floor from ALL FOUR hands rather than from the
 * top-spade hand alone.  OFF by default: it is a node saving that costs more
 * throughput than it buys.
 *
 * NIL_FLAG_NO_LATER_TRICKS above describes one hand holding the top outstanding
 * spades as a run.  The same argument carried further gives a floor for EVERY
 * hand: with o_i the number of spades outside a hand ranked above that hand's
 * i-th spade from the top, the hand wins at least max_i (i - o_i) tricks down
 * every line, because a spade above it is played once and so spoils at most one
 * of the i tricks.  DDS section 4's rules 2 and 3 are the i <= 2 cases.
 *
 * With this set, all four floors are used at once: the nil bidder's and the
 * cover partner's bound the range from below, and the two opponents' ADD -- two
 * hands cannot win the same trick -- to bound it from above.  Never weaker than
 * the default single-hand form, and measurably slower anyway; see ROADMAP.md
 * item 44 for the numbers and for what would change them.
 *
 * Inert under NIL_FLAG_NO_LATER_TRICKS, which switches off the bound it rides
 * on, and under NIL_FLAG_FAST_MODE for the same reason that one is.  Same value
 * and same principal variation; diagnostic and a control arm. */
#define NIL_FLAG_SPADE_MATRIX 0x100000u

/* Do not spend the opponents' quick tricks.
 *
 * DDS section 3 counts what the side about to lead can cash immediately.  That
 * is a claim about one STRATEGY rather than a floor that holds down every line,
 * so it bounds a node only from the side that owns it: the opponents maximise,
 * so their count says the nil side splits at most t - c and the value cannot
 * fall below the worst corner of that.  Spent against beta, and only where the
 * opponents are on lead.
 *
 * The mirror image -- the cover partner's count as an upper bound -- is not
 * taken.  That claim is about a named hand rather than a side, and a nil bidder
 * holding nothing but spades is forced to ruff its own partner's winner.
 *
 * Nearly disjoint from NIL_FLAG_NO_SPADE_MATRIX's arm in practice, which is why
 * both exist and both are on.  Inert under NIL_FLAG_NO_LATER_TRICKS and under
 * NIL_FLAG_FAST_MODE.  Same value and same principal variation; diagnostic and
 * a control arm. */
#define NIL_FLAG_NO_QUICK_TRICKS 0x200000u

/* With a bid on each side, do not take the static end-of-trick cutoff in
 * positions where every bid is already down.  Roadmap item 76.  Same answer,
 * more nodes; a control arm.  Inert for every other seat shape. */
#define NIL_FLAG_NO_SETTLED_GAINS 0x800000u

/* Let the cover partner, on lead, play the cheapest card the nil bidder can
 * duck beneath in the nil bidder's shortest suit.  Roadmap item C5.
 *
 * OPT-IN, unlike the other ordering flags, because the arm is PARKED rather
 * than shipped: measured, it wins some workloads on every rep and loses others
 * on every rep, which fails this project's bar.  Ordering only, so the answer
 * is the same either way; the node count is not.  See MOVE_ORDERING.md. */
#define NIL_FLAG_COVER_DUCK_SHORT 0x400000u

/* Pass to nil_set_table_size to go back to letting the library choose the
 * table size, which is what a process that never calls it already gets.
 *
 * It resolves to a FIXED 256 MiB, for every hand size and both modes, as of
 * patch 33.  It used to be a schedule that sized the table to the position;
 * that is a worse idea than it looks, because the table is re-zeroed whenever
 * the requested size changes and a hand played out asks for a smaller table
 * every trick or two.  A worker following a live game paid 51 ms per deal
 * walking the schedule down and back up.  One size makes every resize after the
 * first free.
 *
 * The table is thread_local and never shrinks, so 256 MiB per worker thread is
 * both the steady state and the high-water mark -- which is the point: the
 * footprint is now predictable rather than a function of what was last asked.
 * The first solve on a thread spends about 133 ms allocating and faulting it
 * in; every later one spends nothing.  A caller that would rather trade nodes
 * for footprint, or that solves one small position and exits, should set the
 * size explicitly.  0 still means no table at all. */
#define NIL_TABLE_AUTO 0xFFFFFFFFu

/* Skip the canonical re-derivation of the reported move.
 *
 * MODE_FULL orders its moves as of patch 25 and then asks the position again,
 * in canonical order, which of its moves achieves the value just returned --
 * because ordering is a pure reorder, so the only thing it could ever have
 * disturbed is which of several equally-optimal moves gets reported.  That
 * re-derivation costs about 16% at thirteen cards.  This flag declines it.
 *
 * What moves without it: the principal variation walks a different line, and
 * the move list names a different single card as achieving the position's
 * value.  What does NOT move, with or without it: every value, every trick
 * count, nils_set, and the set of cards flagged best.  Where the value alone
 * cannot pin nil_tricks -- with the nil already set and a minimising tie-break,
 * which makes a nil trick worth exactly zero -- the re-derivation runs anyway
 * and this flag does not reach it, because there it is the only thing making
 * that field deterministic.
 *
 * Ignored by nil_solve_pv: a caller asking for a line is asking for that one. */
#define NIL_FLAG_FAST_LINE 0x1000u

/* What the trick counts read under NIL_FLAG_FAST_MODE.  Deliberately not zero:
 * zero is a real answer to "how many tricks did the nil bidder take", and a
 * caller that mistook one for the other would read a failing nil as a made
 * one.  Test nils_set, or ask again without the flag. */
#define NIL_TRICKS_UNKNOWN (-1)

/* Status codes.  0 is success, everything else is a failure.
 *
 * There is no size-related code here.  The solver attempts every position it
 * can represent, and the only reason it declines one is that the position is
 * ILLEGAL -- see nil_validate.  A hand too large for a legal deal comes back as
 * NIL_ERR_ILLEGAL_POSITION like any other malformed layout.
 *
 * These values are renumbered from earlier headers: -4 was NIL_ERR_TOO_MANY_
 * CARDS, which reported a refusal that no longer exists, and the codes below it
 * have moved up by one. */
#define NIL_OK 0
#define NIL_ERR_NULL_ARG (-1)
#define NIL_ERR_PARSE (-2)
#define NIL_ERR_ILLEGAL_POSITION (-3)
#define NIL_ERR_BUFFER_TOO_SMALL (-4)
#define NIL_ERR_INTERNAL (-5)
/* The call asked for something this build cannot produce: a principal variation
 * in fast mode, or a `seats` array with more than one nil in it. */
#define NIL_ERR_UNSUPPORTED (-6)

typedef struct nil_result {
    /* HOW MANY BIDS ARE BROKEN under best play by both sides -- not whether one
     * is.  With a single nil that is 0 or 1, which is what the old `nil_fails`
     * held, so a caller testing it for truth reads the same answer it always
     * did; the field is a count because a pair that both bid nil has three
     * possible answers rather than two.  A bid declared broken by the caller
     * with NIL_ROLE_NIL_SET counts toward it, since the question is how many are
     * down and not how many the search knocked down. */
    int32_t nils_set;
    /* Tricks the nil bidder takes from this position onward.  Not meaningful
     * when NIL_ROLE_NIL_SET and NIL_FLAG_MINIMISE_OWN_TRICKS are both in play;
     * see the note above.  NIL_TRICKS_UNKNOWN under
     * NIL_FLAG_FAST_MODE, which never computes it. */
    int32_t nil_tricks;
    /* Tricks the nil bidder and its covering partner take between them.
     * nil_tricks is included in this.  NIL_TRICKS_UNKNOWN under
     * NIL_FLAG_FAST_MODE. */
    int32_t nil_side_tricks;
    /* Tricks the opposing pair takes.  nil_side_tricks + opponent_tricks
     * always equals tricks_remaining.  NIL_TRICKS_UNKNOWN under
     * NIL_FLAG_FAST_MODE. */
    int32_t opponent_tricks;
    /* Tricks left to play in the position. */
    int32_t tricks_remaining;
    /* Nodes visited by the search, for benchmarking. */
    uint64_t nodes;
} nil_result;

/* Solve a position.
 *
 *   pbn           PBN deal string, e.g.
 *                 "N:8.K5.KT2.KQT9762 AQT643.T.QJ864.8 J9.A943.73.AJ543 K752.QJ8762.A95."
 *                 Suits are spades.hearts.diamonds.clubs; hands run clockwise
 *                 from the named seat.  Shortened (mid-play) hands are fine, as
 *                 long as every seat is consistent with `current_trick`.  Cards
 *                 already played to the current trick must NOT appear here.
 *   leader        Seat that led the current trick (NIL_SEAT_*).
 *   current_trick Cards already played to the trick in progress, in play order
 *                 starting from `leader`, e.g. "H4 HK".  May be NULL or "".
 *                 At most 3 cards.
 *   seats         Four NIL_ROLE_* values, clockwise from the seat `pbn` names.
 *                 See the NIL_ROLE_* block above; NULL is an error.
 *   flags         Bitwise OR of NIL_FLAG_*.
 *   out           Filled in on success.  Required.
 *   err_buf       Optional buffer for a human-readable error message.
 *   err_len       Size of err_buf in bytes; ignored if err_buf is NULL.
 *
 * Returns NIL_OK or a negative NIL_ERR_* code.
 */
NIL_SOLVER_API int32_t NIL_SOLVER_CALL nil_solve(const char* pbn, int32_t leader,
                                                 const char* current_trick,
                                                 const int32_t* seats, uint32_t flags,
                                                 nil_result* out, char* err_buf,
                                                 int32_t err_len);

/* As nil_solve, but also writes the principal variation as a space-separated
 * play list ("N:D2 E:DA S:D5 W:D7") into pv_buf.  Returns
 * NIL_ERR_BUFFER_TOO_SMALL if it does not fit.
 *
 * Sizing: each card is four characters and a separator, so 5 bytes per
 * remaining card is exactly enough -- the last card's separator is the
 * terminator's slot -- and 5 * 4 * tricks_remaining covers any position.  An
 * earlier version of this comment said 4 bytes per card plus one, which is the
 * arithmetic with the separators left out and is short by a fifth: a 4-trick
 * ending writes 79 bytes plus a NUL and that formula asks for 65.
 *
 * Returns NIL_ERR_UNSUPPORTED if NIL_FLAG_FAST_MODE is set: that mode has no
 * principal variation, and quietly running the slower mode instead -- or
 * quietly returning an empty string -- are both worse than saying so. */
NIL_SOLVER_API int32_t NIL_SOLVER_CALL nil_solve_pv(const char* pbn, int32_t leader,
                                                    const char* current_trick,
                                                    const int32_t* seats, uint32_t flags,
                                                    nil_result* out, char* pv_buf,
                                                    int32_t pv_len, char* err_buf,
                                                    int32_t err_len);

/* Convenience wrapper for the yes/no question.  Returns 1 (nil can be forced to
 * take a trick), 0 (it cannot), or a negative NIL_ERR_* code.
 *
 * Always runs in fast mode: the boolean is this function's entire output, and
 * fast mode is the mode that computes exactly that.  NIL_FLAG_FAST_MODE is
 * therefore implied and passing it changes nothing.  The answer is identical to
 * what nil_solve gives without the flag; only the work done to reach it
 * differs. */
NIL_SOLVER_API int32_t NIL_SOLVER_CALL nil_count_set(const char* pbn, int32_t leader,
                                                 const char* current_trick,
                                                 const int32_t* seats, uint32_t flags);

/* Legal cards a position can offer.  Thirteen is the most any hand holds, and
 * the equivalent-card reduction only ever returns fewer, so a buffer of this
 * size is always enough. */
#define NIL_MAX_MOVES 13

/* One legal card at the root, and what playing it leads to.
 *
 * Deliberately shaped like DDS's futureTricks rows, because the question is the
 * same one -- "here is every card you may play and what each costs" -- and a
 * caller that already unpacks those can unpack these. */
typedef struct nil_move {
    /* 0 = spades, 1 = hearts, 2 = diamonds, 3 = clubs. */
    int32_t suit;
    /* 2..14, ace high. */
    int32_t rank;
    /* Other legal cards that are this same move under a different name, as a
     * bitmask with bit r set for rank r (so the king is bit 13, decimal 8192).
     * Same encoding as DDS's `equals`, including the part that trips people up:
     * this card's OWN rank is NOT set.  Zero means the card stands alone.
     *
     * With the jack already played, holding the king and the queen is one move
     * under two names -- every card still in existence is above both or below
     * both, so the two plays reach positions that differ only by swapping two
     * labels.  The solver searches one of them and names the other here, rather
     * than searching the same tree twice.  A caller that wants a row per legal
     * card expands these; a caller choosing a move can ignore them and play the
     * card named above. */
    int32_t equal_ranks;
    /* Does the nil fail AFTER this card is played, against best play by
     * everyone from there on?
     *
     * Read it from whichever side you are on: for the nil bidder or its
     * covering partner, 0 means this card holds the nil together; for an
     * opponent, 1 means this card breaks it.  One fact rather than two, because
     * a double dummy answer does not depend on who asked. */
    int32_t nils_set;
    /* As nil_result's, but for the line this card leads to, and INCLUDING the
     * trick this card completes if it completes one.  All three are
     * NIL_TRICKS_UNKNOWN under NIL_FLAG_FAST_MODE. */
    int32_t nil_tricks;
    int32_t nil_side_tricks;
    int32_t opponent_tricks;
    /* 1 when this card achieves the position's own value -- one of the moves
     * the solver would have been content to pick.  There is usually more than
     * one, and among cards that tie, any of them is as good as any other. */
    int32_t is_best;
} nil_move;

/* Solve a position and report EVERY legal card rather than just the answer.
 *
 * `out` is filled in exactly as nil_solve would fill it.  `moves` receives one
 * row per equivalence class (or one per legal card under NIL_FLAG_NO_COLLAPSE),
 * in canonical order: spades first, then hearts, diamonds, clubs, ascending by
 * rank within each.  `*moves_len` is set to the number of rows written.
 *
 * `moves_cap` is the capacity of `moves` in elements.  NIL_MAX_MOVES is always
 * enough.  A buffer too small returns NIL_ERR_BUFFER_TOO_SMALL and still writes
 * the count that was needed to `*moves_len`, so a caller can size and retry.
 *
 * A position with no cards left is not an error: `out` is filled in and
 * `*moves_len` is zero.
 *
 * WHAT IT COSTS.  Less than it looks, and the reason is worth knowing before
 * budgeting for it.  The position is solved first and every card is then scored
 * against the same transposition table, so the per-card searches spend most of
 * their time reading back work the first search already did.  Measured over
 * twelve random thirteen-card deals in fast mode it came to 1.0x the nodes and
 * +0.4% of the wall time of the plain call.  The cases that cost anything are
 * the ones the plain call answered by proof without looking at a card: there
 * the position is free and the move list is not, though at 1 node against 79
 * that is not a number anybody has to plan around.
 *
 * Returns NIL_OK or a negative NIL_ERR_* code. */
NIL_SOLVER_API int32_t NIL_SOLVER_CALL nil_solve_moves(const char* pbn, int32_t leader,
                                                       const char* current_trick,
                                                       const int32_t* seats, uint32_t flags,
                                                       nil_result* out, nil_move* moves,
                                                       int32_t moves_cap, int32_t* moves_len,
                                                       char* err_buf, int32_t err_len);

/* Set the transposition table size, in mebibytes, for subsequent calls on the
 * calling thread.  The table is per-thread, and so is this setting.  Rounded DOWN to a power-of-two bucket count, so the table actually
 * allocated holds between half and all of what was asked for.  Zero has the
 * same effect as NIL_FLAG_NO_MEMO.  The default is 32.
 *
 * Bigger is faster on deep positions and makes no difference on shallow ones.
 * The table is bounded, so the node count a deep search reports depends on this
 * setting; the ANSWER never does. */
NIL_SOLVER_API void NIL_SOLVER_CALL nil_set_table_size(uint32_t megabytes);

/* Release the transposition table held by the calling thread.  Purely a
 * memory-reclaim call: the next solve on that thread allocates again.  Call it
 * from a thread that is done solving, or before unloading the library. */
NIL_SOLVER_API void NIL_SOLVER_CALL nil_release_table(void);

/* Static version string, e.g. "0.1.0". */
NIL_SOLVER_API const char* NIL_SOLVER_CALL nil_solver_version(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* NIL_SOLVER_H */
