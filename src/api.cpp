// Implementation of the C ABI declared in include/nil_solver/nil_solver.h.
//
// This layer does nothing but marshal: parse the caller's strings into a
// Position, run the search, copy scalars back out.  All the interesting code is
// in src/nil/.
#include "nil_solver/nil_solver.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>

#include "nil/position.hpp"
#include "nil/search.hpp"

namespace {

// Settable through nil_set_table_size; see the header.  Deliberately not part of
// the flags word, because it is a resource knob rather than a question about
// the position, and callers set it once rather than per solve.
thread_local std::size_t g_table_megabytes = nil::SearchOptions().tt_megabytes;

void copy_err(char* buf, std::int32_t len, const std::string& msg) {
    if (!buf || len <= 0) return;
    const std::size_t n =
        std::min<std::size_t>(msg.size(), static_cast<std::size_t>(len) - 1);
    std::memcpy(buf, msg.data(), n);
    buf[n] = '\0';
}

// Shared body for nil_solve / nil_solve_pv.
std::int32_t solve_impl(const char* pbn, std::int32_t leader, const char* current_trick,
                        std::int32_t nil_seat, std::uint32_t flags, nil_result* out,
                        std::string* pv_out, char* err_buf, std::int32_t err_len) {
    if (!pbn || !out) {
        copy_err(err_buf, err_len, "null argument");
        return NIL_ERR_NULL_ARG;
    }
    if (leader < 0 || leader > 3) {
        copy_err(err_buf, err_len, "leader out of range (expected 0..3 for N/E/S/W)");
        return NIL_ERR_ILLEGAL_POSITION;
    }
    if (nil_seat < 0 || nil_seat > 3) {
        copy_err(err_buf, err_len, "nil seat out of range (expected 0..3 for N/E/S/W)");
        return NIL_ERR_ILLEGAL_POSITION;
    }

    std::string err;
    nil::Position pos;
    if (!nil::parse_pbn(pbn, pos.hands, err)) {
        copy_err(err_buf, err_len, err);
        return NIL_ERR_PARSE;
    }
    pos.leader = leader;
    pos.spades_broken = (flags & NIL_FLAG_SPADES_BROKEN) != 0;

    if (current_trick && *current_trick) {
        int count = 0;
        if (!nil::parse_cards(current_trick, pos.trick, 3, count, err)) {
            copy_err(err_buf, err_len, err);
            return NIL_ERR_PARSE;
        }
        pos.trick_len = count;
    }

    if (!nil::validate(pos, err)) {
        copy_err(err_buf, err_len, err);
        return NIL_ERR_ILLEGAL_POSITION;
    }
    if (pos.cards_per_hand() > NIL_CARD_LIMIT && !(flags & NIL_FLAG_FORCE_LARGE)) {
        copy_err(err_buf, err_len,
                 (flags & NIL_FLAG_FAST_MODE)
                     ? "too many cards per hand: the boolean search does prune, and a full "
                       "thirteen is usually well under a second, but the spread is wide and "
                       "an awkward layout can still take several. Pass NIL_FLAG_FORCE_LARGE "
                       "to insist."
                     : "too many cards per hand: the full search is exhaustive (the "
                       "transposition table collapses repeated positions but prunes nothing) "
                       "and will take a very long time. NIL_FLAG_FAST_MODE prunes hard and "
                       "answers the nil question alone; pass NIL_FLAG_FORCE_LARGE to insist "
                       "on this one.");
        return NIL_ERR_TOO_MANY_CARDS;
    }

    nil::SearchOptions opts;
    opts.break_on_forced_spade_lead = (flags & NIL_FLAG_BREAK_ON_FORCED_SPADE_LEAD) != 0;
    opts.use_memo = (flags & NIL_FLAG_NO_MEMO) == 0;
    opts.collapse_equivalents = (flags & NIL_FLAG_NO_COLLAPSE) == 0;
    opts.tt_megabytes = g_table_megabytes;
    opts.minimise_own_tricks = (flags & NIL_FLAG_MINIMISE_OWN_TRICKS) != 0;
    opts.nil_already_set = (flags & NIL_FLAG_NIL_ALREADY_SET) != 0;
    opts.mode = (flags & NIL_FLAG_FAST_MODE) != 0 ? nil::MODE_FAST : nil::MODE_FULL;

    nil::Solution sol;
    if (!nil::solve(pos, nil_seat, opts, sol, err)) {
        copy_err(err_buf, err_len, err);
        return NIL_ERR_INTERNAL;
    }

    out->nil_fails = sol.nil_fails ? 1 : 0;
    // nil::TRICKS_NOT_COMPUTED and NIL_TRICKS_UNKNOWN are the same -1; the two
    // names exist so neither side of the boundary has to include the other's
    // header to say what it means.
    out->nil_tricks = sol.nil_tricks;
    out->nil_side_tricks = sol.nil_side_tricks;
    out->opponent_tricks = sol.opponent_tricks;
    out->tricks_remaining = pos.tricks_remaining();
    out->nodes = sol.nodes;
    if (pv_out) *pv_out = nil::format_pv_compact(sol);
    return NIL_OK;
}

static_assert(nil::TRICKS_NOT_COMPUTED == NIL_TRICKS_UNKNOWN,
              "the C ABI's unknown-trick sentinel must match the core's");

}  // namespace

extern "C" {

NIL_SOLVER_API std::int32_t NIL_SOLVER_CALL nil_solve(const char* pbn, std::int32_t leader,
                                                      const char* current_trick,
                                                      std::int32_t nil_seat, std::uint32_t flags,
                                                      nil_result* out, char* err_buf,
                                                      std::int32_t err_len) {
    return solve_impl(pbn, leader, current_trick, nil_seat, flags, out, nullptr, err_buf, err_len);
}

NIL_SOLVER_API std::int32_t NIL_SOLVER_CALL nil_solve_pv(const char* pbn, std::int32_t leader,
                                                         const char* current_trick,
                                                         std::int32_t nil_seat,
                                                         std::uint32_t flags, nil_result* out,
                                                         char* pv_buf, std::int32_t pv_len,
                                                         char* err_buf, std::int32_t err_len) {
    if (!pv_buf || pv_len <= 0) {
        copy_err(err_buf, err_len, "null or empty PV buffer");
        return NIL_ERR_NULL_ARG;
    }
    if (flags & NIL_FLAG_FAST_MODE) {
        pv_buf[0] = '\0';
        copy_err(err_buf, err_len,
                 "fast mode has no principal variation: it optimises the nil bidder's trick "
                 "count alone and never chooses among lines that tie on it. Drop "
                 "NIL_FLAG_FAST_MODE, or call nil_solve if the boolean is all you need.");
        return NIL_ERR_UNSUPPORTED;
    }
    std::string pv;
    const std::int32_t rc =
        solve_impl(pbn, leader, current_trick, nil_seat, flags, out, &pv, err_buf, err_len);
    if (rc != NIL_OK) {
        pv_buf[0] = '\0';
        return rc;
    }
    if (static_cast<std::size_t>(pv_len) < pv.size() + 1) {
        pv_buf[0] = '\0';
        copy_err(err_buf, err_len, "PV buffer too small");
        return NIL_ERR_BUFFER_TOO_SMALL;
    }
    std::memcpy(pv_buf, pv.c_str(), pv.size() + 1);
    return NIL_OK;
}

NIL_SOLVER_API std::int32_t NIL_SOLVER_CALL nil_fails(const char* pbn, std::int32_t leader,
                                                      const char* current_trick,
                                                      std::int32_t nil_seat, std::uint32_t flags) {
    nil_result result;
    // The boolean is the whole output, and fast mode is the mode that computes
    // exactly the boolean.  Selecting it here rather than making every caller
    // remember the flag is the point of this entry point existing.
    const std::int32_t rc = solve_impl(pbn, leader, current_trick, nil_seat,
                                       flags | NIL_FLAG_FAST_MODE, &result, nullptr, nullptr, 0);
    return rc == NIL_OK ? result.nil_fails : rc;
}

NIL_SOLVER_API void NIL_SOLVER_CALL nil_set_table_size(std::uint32_t megabytes) {
    g_table_megabytes = static_cast<std::size_t>(megabytes);
}

NIL_SOLVER_API void NIL_SOLVER_CALL nil_release_table(void) {
    nil::release_transposition_table();
}

NIL_SOLVER_API const char* NIL_SOLVER_CALL nil_solver_version(void) { return "0.1.0"; }

}  // extern "C"
