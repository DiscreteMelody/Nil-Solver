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
 * One consequence worth knowing before you rely on the numbers: with that flag
 * set, only nil_side_tricks and opponent_tricks are optimised.  How the side's
 * tricks divide between the nil bidder and its partner is not part of the
 * objective any more, so nil_tricks is whatever the tie-break happened to
 * produce.  It is deterministic, but it is not an answer to a question anyone
 * asked, and it may move when move ordering is added later.
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
/* Disable the full-state memo.  The memo caches a pure function, so it changes
 * neither the answer nor the principal variation; this flag exists only to make
 * the search maximally dumb when cross-checking. */
#define NIL_FLAG_NO_MEMO 0x4u
/* Allow more than 7 cards per hand.  Without this the solver refuses, because
 * there is no pruning and it will not finish. */
#define NIL_FLAG_FORCE_LARGE 0x8u

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
    /* Tricks the nil bidder takes from this position onward.  Meaningful only
     * when NIL_FLAG_NIL_ALREADY_SET was NOT passed; see the note above. */
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

/* Static version string, e.g. "0.1.0". */
NIL_SOLVER_API const char* NIL_SOLVER_CALL nil_solver_version(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* NIL_SOLVER_H */
