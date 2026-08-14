/* nil_solver.h -- C ABI for the Spades nil double-dummy solver.
 *
 * Everything crossing this boundary is a plain C type so the library can be
 * consumed from C#, C, or anything else that can call a DLL.  Strings are
 * NUL-terminated ASCII/UTF-8.  Nothing is allocated on behalf of the caller:
 * the caller supplies every output buffer.
 *
 * THE QUESTION THIS ANSWERS
 * -------------------------
 * Given a layout, whose turn it is, and which seat bid nil, the solver plays
 * the hand out under a lexicographic objective:
 *
 *   PRIMARY    the nil bidder's trick count.  The nil bidder and its covering
 *              partner minimise it; both opponents maximise it.  So
 *              `nil_fails == 1` means the nil is dead however it is played,
 *              and `nil_fails == 0` means the nil side has a line that
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
 * Pass NIL_FLAG_NIL_ALREADY_SET once the nil has actually been broken in the
 * real game.  The primary objective is dropped, both sides stop protecting and
 * attacking it, and only the secondary objective is optimised.
 *
 * With that flag set and the pair still taking tricks, the tertiary level is
 * what decides the split, and it sits BELOW the pair's total on purpose.  The
 * two sides are not strictly opposed there: both would rather the nil bidder
 * took nothing, so the split is slack only one side cares about rather than a
 * tug of war.  Keeping it below the total leaves the opponents' objective
 * exactly "take as many as we can" and resolves the split against the pair, so
 * nil_side_tricks - nil_tricks is the partner count the pair can GUARANTEE, not
 * the one it might get if the opponents were being unhelpful to themselves.
 *
 * Combine that flag with NIL_FLAG_MINIMISE_OWN_TRICKS and the tertiary level is
 * off: bags accrue to the pair whoever won, the two partners are
 * interchangeable, and nil_tricks is then whatever the tie-break produced
 * rather than an answer to a question anyone asked.
 *
 * TWO MODES
 * ---------
 * All of the above describes the FULL mode, which is the default and which
 * answers everything.  NIL_FLAG_FAST_MODE asks the boolean question alone:
 * `nil_fails` comes back, the three trick counts come back as
 * NIL_TRICKS_UNKNOWN, and there is no principal variation.
 *
 * The two modes always agree on nil_fails.  They have to: full mode packs the
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
 *   "can this nil still be broken?"        fast mode (or nil_fails(), which
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

/* Flags for the `flags` argument. */
#define NIL_FLAG_NONE 0x0u
/* Spades are already broken in the given position. */
#define NIL_FLAG_SPADES_BROKEN 0x1u
/* Treat a forced spade lead (all-spade hand, spades unbroken) as breaking
 * spades.  Off by default, which is the literal reading of the rule. */
#define NIL_FLAG_BREAK_ON_FORCED_SPADE_LEAD 0x2u
/* The tie-break direction: each pair takes as FEW tricks as it can, rather
 * than as many.  Applies to both pairs at once, which is coherent because
 * their trick counts sum to a constant. */
#define NIL_FLAG_MINIMISE_OWN_TRICKS 0x10u
/* The nil has already been broken in the real game.  Drops the primary
 * objective; only the secondary one is optimised.  `nil_fails` is then
 * reported as 1 because you said so, not because it was computed. */
#define NIL_FLAG_NIL_ALREADY_SET 0x20u
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
/* Allow more than NIL_CARD_LIMIT cards per hand.  Without this the solver
 * refuses, because the search is still exhaustive and will take a very long
 * time. */
#define NIL_FLAG_FORCE_LARGE 0x8u
/* Answer the nil question and nothing else.  nil_fails is filled in; the three
 * trick counts come back as NIL_TRICKS_UNKNOWN and there is no principal
 * variation, so nil_solve_pv rejects this flag.
 *
 * Combined with NIL_FLAG_NIL_ALREADY_SET the answer is 1 with no search at all,
 * since that flag asserts the only thing this mode computes.
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
 * Inert under NIL_FLAG_FAST_MODE, which is the presolve, and inert when the nil
 * is already set, where the value range has no gap in it to exploit. */
#define NIL_FLAG_NO_PRESOLVE 0x800u

/* Pass to nil_set_table_size to go back to choosing the table size from the
 * position, which is what a process that never calls it already gets.  The
 * size follows tricks_remaining and mode: a four-card endgame takes 32 MiB and
 * a thirteen-card full solve takes 512, which is the difference between 111
 * million nodes and 688 million on a hard one.  An explicit size still wins,
 * and 0 still means no table at all. */
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
 * count, nil_fails, and the set of cards flagged best.  Where the value alone
 * cannot pin nil_tricks -- with the nil already set and a minimising tie-break,
 * which makes a nil trick worth exactly zero -- the re-derivation runs anyway
 * and this flag does not reach it, because there it is the only thing making
 * that field deterministic.
 *
 * Ignored by nil_solve_pv: a caller asking for a line is asking for that one. */
#define NIL_FLAG_FAST_LINE 0x1000u

/* Cards per hand the solver will attempt without NIL_FLAG_FORCE_LARGE. */
#define NIL_CARD_LIMIT 9

/* What the trick counts read under NIL_FLAG_FAST_MODE.  Deliberately not zero:
 * zero is a real answer to "how many tricks did the nil bidder take", and a
 * caller that mistook one for the other would read a failing nil as a made
 * one.  Test nil_fails, or ask again without the flag. */
#define NIL_TRICKS_UNKNOWN (-1)

/* Status codes.  0 is success, everything else is a failure. */
#define NIL_OK 0
#define NIL_ERR_NULL_ARG (-1)
#define NIL_ERR_PARSE (-2)
#define NIL_ERR_ILLEGAL_POSITION (-3)
#define NIL_ERR_TOO_MANY_CARDS (-4)
#define NIL_ERR_BUFFER_TOO_SMALL (-5)
#define NIL_ERR_INTERNAL (-6)
/* The flags asked for something this entry point cannot produce -- today, a
 * principal variation in fast mode. */
#define NIL_ERR_UNSUPPORTED (-7)

typedef struct nil_result {
    /* 1 if the opponents can force the nil bidder to take at least one trick
     * against best defence of the nil, 0 if the nil can be held.  Always 1 when
     * NIL_FLAG_NIL_ALREADY_SET was passed, since that is a fact you supplied
     * rather than one the search discovered. */
    int32_t nil_fails;
    /* Tricks the nil bidder takes from this position onward.  Not meaningful
     * when NIL_FLAG_NIL_ALREADY_SET and NIL_FLAG_MINIMISE_OWN_TRICKS are both
     * set; see the note above.  NIL_TRICKS_UNKNOWN under
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
 *   nil_seat      Seat that bid nil (NIL_SEAT_*).
 *   flags         Bitwise OR of NIL_FLAG_*.
 *   out           Filled in on success.  Required.
 *   err_buf       Optional buffer for a human-readable error message.
 *   err_len       Size of err_buf in bytes; ignored if err_buf is NULL.
 *
 * Returns NIL_OK or a negative NIL_ERR_* code.
 */
NIL_SOLVER_API int32_t NIL_SOLVER_CALL nil_solve(const char* pbn, int32_t leader,
                                                 const char* current_trick, int32_t nil_seat,
                                                 uint32_t flags, nil_result* out, char* err_buf,
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
                                                    const char* current_trick, int32_t nil_seat,
                                                    uint32_t flags, nil_result* out, char* pv_buf,
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
NIL_SOLVER_API int32_t NIL_SOLVER_CALL nil_fails(const char* pbn, int32_t leader,
                                                 const char* current_trick, int32_t nil_seat,
                                                 uint32_t flags);

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
    int32_t nil_fails;
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
                                                       int32_t nil_seat, uint32_t flags,
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
