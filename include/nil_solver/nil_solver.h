/* nil_solver.h -- C ABI for the Spades nil double-dummy solver.
 *
 * Everything crossing this boundary is a plain C type so the library can be
 * consumed from C#, C, or anything else that can call a DLL.  Strings are
 * NUL-terminated ASCII/UTF-8.  Nothing is allocated on behalf of the caller:
 * the caller supplies every output buffer.
 *
 * THE QUESTION THIS ANSWERS
 * -------------------------
 * Given a layout, whose turn it is, and which seat bid nil, can the two
 * opponents force the nil bidder to win at least one trick, assuming the nil
 * bidder and its partner play perfectly to prevent it?  `nil_fails == 1` means
 * yes -- the nil is dead however it is played.  `nil_fails == 0` means the nil
 * bidder and partner have a line that survives.
 *
 * Both sides see all four hands (double dummy).  The defenders will happily
 * throw tricks away if that forces one onto the nil bidder; only the nil
 * bidder's own trick count is being optimised.
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
     * against best defence of the nil, 0 if the nil can be held. */
    int32_t nil_fails;
    /* Exact number of tricks the nil bidder takes under optimal play by both
     * coalitions.  nil_fails == (tricks > 0). */
    int32_t tricks;
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
