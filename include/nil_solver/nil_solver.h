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
/* Allow more than NIL_CARD_LIMIT cards per hand.  Without this the solver
 * refuses, because the search is still exhaustive and will take a very long
 * time. */
#define NIL_FLAG_FORCE_LARGE 0x8u

/* Cards per hand the solver will attempt without NIL_FLAG_FORCE_LARGE. */
#define NIL_CARD_LIMIT 9

/* Status codes.  0 is success, everything else is a failure. */
#define NIL_OK 0
#define NIL_ERR_NULL_ARG (-1)
#define NIL_ERR_PARSE (-2)
#define NIL_ERR_ILLEGAL_POSITION (-3)
#define NIL_ERR_TOO_MANY_CARDS (-4)
#define NIL_ERR_BUFFER_TOO_SMALL (-5)
#define NIL_ERR_INTERNAL (-6)

typedef struct nil_result {
    /* 1 if the opponents can force the nil bidder to take at least one trick
     * against best defence of the nil, 0 if the nil can be held.  Always 1 when
     * NIL_FLAG_NIL_ALREADY_SET was passed, since that is a fact you supplied
     * rather than one the search discovered. */
    int32_t nil_fails;
    /* Tricks the nil bidder takes from this position onward.  Not meaningful
     * when NIL_FLAG_NIL_ALREADY_SET and NIL_FLAG_MINIMISE_OWN_TRICKS are both
     * set; see the note above. */
    int32_t nil_tricks;
    /* Tricks the nil bidder and its covering partner take between them.
     * nil_tricks is included in this. */
    int32_t nil_side_tricks;
    /* Tricks the opposing pair takes.  nil_side_tricks + opponent_tricks
     * always equals tricks_remaining. */
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
 * NIL_ERR_BUFFER_TOO_SMALL if it does not fit; 4 bytes per remaining card plus
 * one is always enough. */
NIL_SOLVER_API int32_t NIL_SOLVER_CALL nil_solve_pv(const char* pbn, int32_t leader,
                                                    const char* current_trick, int32_t nil_seat,
                                                    uint32_t flags, nil_result* out, char* pv_buf,
                                                    int32_t pv_len, char* err_buf,
                                                    int32_t err_len);

/* Convenience wrapper for the yes/no question.  Returns 1 (nil can be forced to
 * take a trick), 0 (it cannot), or a negative NIL_ERR_* code. */
NIL_SOLVER_API int32_t NIL_SOLVER_CALL nil_fails(const char* pbn, int32_t leader,
                                                 const char* current_trick, int32_t nil_seat,
                                                 uint32_t flags);

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
