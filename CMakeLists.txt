cmake_minimum_required(VERSION 3.16)
project(NilSolver VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(NIL_SOLVER_BUILD_CLI "Build the nil_cli command line tool" ON)
option(NIL_SOLVER_BUILD_TESTS "Build the self-tests" ON)
option(NIL_SOLVER_BUILD_BENCH "Build the nil_bench benchmark tool" ON)
# The large corpus is minutes to hours per position; it is not part of a normal
# test run.  Turn this on deliberately, when you want it.
option(NIL_SOLVER_LARGE_TESTS "Also test the 7+ card corpus (slow)" OFF)

# Hide everything that is not explicitly exported, so the DLL/so surface is
# exactly the C ABI in include/nil_solver/nil_solver.h.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Put all artefacts side by side; this is the directory you copy the DLL from.
foreach(_cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${_cfg} "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY${_cfg} "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY${_cfg} "${CMAKE_BINARY_DIR}/lib")
endforeach()

# Link-time optimization (optimization Q6, Sept 2026).  The hot path crosses
# translation units on every trick boundary -- search.cpp calls
# encode_state_key and mix_key (statekey.cpp) and the table's probe and store
# (tt.cpp) -- and without LTO those are real calls.  CMake's IPO support is
# -flto on GCC/Clang and /GL + /LTCG on MSVC.  Release-type configurations
# only; Debug stays quick to build.
#
# MEASURED, GCC 13 on Linux, per-card call over 155 thirteen-card deals, two
# processes interleaved: -0.02% wall, i.e. nothing.  GCC already inlines what
# matters inside search.cpp, and the cross-unit calls left are a few
# nanoseconds against a table miss.  So it is OFF by default.  MSVC /GL is
# unmeasured; note the Visual Studio project already turns on whole-program
# optimization for Release.  Profile-guided optimization is a different
# matter: GCC -fprofile-use measured -5.5% on the same workload (ROADMAP.md),
# and is worth doing for a shipped build, outside this file.
option(NIL_SOLVER_LTO "Link-time optimization for optimized configurations" OFF)
if(NIL_SOLVER_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT NIL_SOLVER_IPO_OK OUTPUT NIL_SOLVER_IPO_MSG LANGUAGES CXX)
  if(NIL_SOLVER_IPO_OK)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
  else()
    message(STATUS "NIL_SOLVER_LTO requested but not supported: ${NIL_SOLVER_IPO_MSG}")
  endif()
endif()

# ---------------------------------------------------------------------------
# Core: the rules, the position type and the search.  Built once, linked into
# the shared library, the CLI and the tests.
# ---------------------------------------------------------------------------
add_library(nil_solver_core STATIC
  src/nil/bigalloc.cpp
  src/nil/cards.cpp
  src/nil/cooperative.cpp
  src/nil/ddtricks.cpp
  src/nil/corpus.cpp
  src/nil/outcome_constraint.cpp
  src/nil/position.cpp
  src/nil/same_lean.cpp
  src/nil/search.cpp
  src/nil/seats.cpp
  src/nil/statekey.cpp
  src/nil/tt.cpp
)
target_include_directories(nil_solver_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
set_property(TARGET nil_solver_core PROPERTY POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  target_compile_options(nil_solver_core PUBLIC /W4 /permissive-)
else()
  target_compile_options(nil_solver_core PUBLIC -Wall -Wextra -Wpedantic)
endif()

# POPCNT.  Population counts are all over the search -- the key encoder, the
# move generator, most of the bounds -- and GCC and Clang only emit the
# instruction when told the CPU has it; otherwise every count is a call into
# libgcc.  MSVC needs no flag: nil/cards.hpp uses __popcnt64 on x64, which the
# double-dummy engine already relied on.  Every x86-64 CPU since 2008 has
# POPCNT (Windows 11 requires it), and the measured gain on the hard 13-card
# deals is about 5%.  Turn this off to build for an older x86-64 CPU; the code
# falls back to a portable count and gives the same answers.
option(NIL_SOLVER_POPCNT "Compile for x86-64 CPUs with the POPCNT instruction (GCC/Clang)" ON)
if(NIL_SOLVER_POPCNT AND NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-mpopcnt NIL_SOLVER_HAVE_MPOPCNT)
  if(NIL_SOLVER_HAVE_MPOPCNT)
    target_compile_options(nil_solver_core PUBLIC -mpopcnt)
  endif()
endif()

# ---------------------------------------------------------------------------
# The shared library: nil_solver.dll on Windows, libnil_solver.so elsewhere.
# ---------------------------------------------------------------------------
add_library(nil_solver SHARED src/api.cpp)
target_link_libraries(nil_solver PRIVATE nil_solver_core)
target_compile_definitions(nil_solver PRIVATE NIL_SOLVER_BUILD_DLL)
set_target_properties(nil_solver PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
if(NIL_SOLVER_BUILD_CLI)
  add_executable(nil_cli tools/nil_cli.cpp)
  target_link_libraries(nil_cli PRIVATE nil_solver_core)
endif()

# ---------------------------------------------------------------------------
# Benchmark / corpus runner.  Verifies as it times, so a fast wrong answer still
# fails the build.
# ---------------------------------------------------------------------------
if(NIL_SOLVER_BUILD_BENCH)
  add_executable(nil_bench tools/nil_bench.cpp)
  target_link_libraries(nil_bench PRIVATE nil_solver_core)
  # $<CONFIG> resolves at build time, so this is right for both single-config
  # generators (Makefiles, Ninja) and multi-config ones (Visual Studio).
  target_compile_definitions(nil_bench PRIVATE NIL_BUILD_CONFIG="$<CONFIG>")
endif()

# ---------------------------------------------------------------------------
# Property tests.  These check a PRIMITIVE against an independent computation
# over its whole input space, rather than checking the solver against recorded
# answers.  The distinction earned itself in patch 31b: restricting coarse table
# entries passed all 560 corpus rows and 900 random deals and was still unsound.
# A corpus pass is evidence about the corpus.
# ---------------------------------------------------------------------------
if(NIL_SOLVER_BUILD_TESTS)
  add_executable(duck_depth_property tools/duck_depth_property.cpp)
  target_link_libraries(duck_depth_property PRIVATE nil_solver_core)
endif()

# ---------------------------------------------------------------------------
# Tests.  Linked against the core statically (with NIL_SOLVER_STATIC so the C
# ABI declarations do not become dllimport) plus api.cpp, so `ctest` needs no
# DLL on the path.
# ---------------------------------------------------------------------------
if(NIL_SOLVER_BUILD_TESTS)
  enable_testing()
  add_executable(nil_tests tests/test_nil_solver.cpp src/api.cpp)
  target_link_libraries(nil_tests PRIVATE nil_solver_core)
  target_compile_definitions(nil_tests PRIVATE NIL_SOLVER_STATIC)
  add_test(NAME nil_tests COMMAND nil_tests --quiet)

  # MOVE_ORDERING.md item C0.  duck_depth has no consumer yet -- the cover
  # partner rules that will read it are not written -- so there is no corpus row
  # and no node count that would notice if it were wrong.  This is the whole of
  # its verification, and it is exhaustive rather than sampled: every one of the
  # 3^13 ways to deal one suit between the nil bidder and its partner, in each
  # of the four suits, checked against an unconstrained maximum-matching search
  # and against Hall's deficiency formula, plus the bound, monotonicity in each
  # hand, and invariance under an order-preserving relabelling of the ranks.
  # About seven seconds.
  add_test(NAME duck_depth_property COMMAND duck_depth_property)
  set_tests_properties(duck_depth_property PROPERTIES TIMEOUT 300)

  # The double-dummy engine (nil/ddtricks) against an independent brute-force
  # minimax: exact counts, every zero-window target, and the reported lead, on
  # random positions with spades broken and unbroken.  The engine's table is
  # never cleared between positions, so this also checks the claim that its
  # entries are valid across deals.
  #
  # The cost is all in the brute force, and it grows steeply with hand size:
  # many small positions for breadth, a hundred six-trick ones for depth.  A
  # few seconds each.  For a deeper run by hand: dd_property <cases> <max
  # tricks> <seed>, e.g. dd_property 300 7 99 (minutes).
  add_executable(dd_property tools/dd_property.cpp)
  target_link_libraries(dd_property PRIVATE nil_solver_core)
  add_test(NAME dd_property COMMAND dd_property 2000 5 7)
  set_tests_properties(dd_property PROPERTIES TIMEOUT 300)
  add_test(NAME dd_property_deep COMMAND dd_property 100 6 11)
  set_tests_properties(dd_property_deep PROPERTIES TIMEOUT 300)
  # The same check on positions the table GENERALIZES to: each position is
  # followed by eight variants that keep the suit lengths and the owners of the
  # top cards of each suit and deal the low cards out again (see the header of
  # tools/dd_property.cpp).  Independent random positions almost never land on
  # a stored fact's generalization, which is how a refuted class of equal cards
  # straddling the pinned ranks went unnoticed; the engine before that fix fails
  # five of these 9000 positions.  About eight seconds.
  add_test(NAME dd_property_variants COMMAND dd_property 1000 5 1 8)
  set_tests_properties(dd_property_variants PROPERTIES TIMEOUT 300)

  # The corpus is the accuracy regression test: answers came from
  # nil_oracle.py once, and replaying them needs neither Python nor the oracle.
  set(NIL_CORPUS "${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus/positions.txt")
  if(NIL_SOLVER_BUILD_BENCH AND EXISTS "${NIL_CORPUS}")
    # --check-pv is on here, and stays on.  The PV is a much stronger regression
    # signal than the value alone, and this test runs MODE_FULL, which searches
    # between sentinels and therefore never cuts and never reorders.  An earlier
    # version of this comment said to drop the flag when move ordering lands;
    # that was written before the mode split and it is wrong.  Ordering is
    # confined to MODE_FAST (roadmap item 6), which produces no PV to check, so
    # nothing here has to be given up for it.
    #
    # Patch 22 moved the goalposts and this comment with them.  Full mode now
    # narrows its window and cuts, but its PV is still canonical and still
    # checked here card for card -- see roadmap item 22 for why narrowing alone
    # cannot disturb it.  What WOULD disturb it is ordering reaching full mode,
    # which is roadmap item 22b, measured at up to 2.99x and deliberately not
    # shipped for exactly this reason.  That is the contingency this test now
    # stands in front of, and the canonical-PV re-extraction pass described in
    # 22b is what would let it through without giving anything up here.
    add_test(NAME corpus
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-pv --quiet --slowest 0)
    # Fast subset for the inner loop: ctest -R corpus_quick
    add_test(NAME corpus_quick
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --cards-only 4 --quiet --slowest 0)
    set_tests_properties(corpus PROPERTIES TIMEOUT 900)

    # The boolean search has no principal variation to replay against itself,
    # so this is what stands in for full mode's internal self-check: every
    # position solved both ways, and the two required to agree on nil_fails.
    # It is not optional cover.  Now that fast mode prunes and full mode does
    # not, the two walk different trees, and this is the only thing between a
    # pruning bug and a confidently wrong boolean.
    add_test(NAME corpus_modes
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --mode both --quiet --slowest 0)
    set_tests_properties(corpus_modes PROPERTIES TIMEOUT 900)

    # The control arm for the static bounds.  corpus_modes exercises them ON --
    # fast mode takes them by default -- and this is the same corpus with them
    # OFF, so the pair is a differential: both must land on the oracle's
    # answers, and a proof that has started lying can only agree with the search
    # by being unreachable.  Same reasoning as --no-collapse, and cheap enough
    # to keep in the default run.
    add_test(NAME corpus_static
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --mode fast --no-static --quiet
                     --slowest 0)
    set_tests_properties(corpus_static PROPERTIES TIMEOUT 900)

    # The control arm for move ordering, on exactly the corpus_static model and
    # for exactly the same reason.  corpus_modes exercises ordering ON -- fast
    # mode takes it by default -- and this is the same corpus with it OFF, so
    # the pair is a differential: both must land on the oracle's answers, and a
    # heuristic that has started changing answers can only agree with both by
    # being unreachable.
    #
    # It is inert as it stands, because patch 15 shipped the flag before the
    # first heuristic (roadmap item 6a).  That is the point: corpus_modes was
    # added the same way, one patch ahead of the pruning it was there to check,
    # and it is cheaper to have the test already running than to remember to
    # write it in the patch that needs it.

    # The control arm for window narrowing (roadmap item 22), on the same model
    # as corpus_static and corpus_ordering.  --no-narrow restores the search
    # full mode ran before patch 22 -- no reachable cutoff, exhaustive minimax
    # over a memo -- so the pair is the strongest differential on this list:
    # both columns must land on the oracle's answers AND on the oracle's
    # principal variations, which is why this one carries --check-pv where the
    # other control arms do not.
    #
    # Narrowing is answer-neutral by argument rather than by measurement (see
    # item 22), and an argument is exactly the kind of thing that stops being
    # true quietly.  This is what says so out loud.  It is roughly six times the
    # nodes of the default corpus run and still well inside the timeout.
    add_test(NAME corpus_narrow
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-pv --no-narrow --quiet
                     --slowest 0)
    set_tests_properties(corpus_narrow PROPERTIES TIMEOUT 900)

    # The control arm for the presolve-seeded root window (roadmap item 23), on
    # the corpus_narrow model and carrying --check-pv for the same reason: the
    # bound is derived rather than estimated, so it owes the same value AND the
    # same line, and a derivation that has stopped holding would show up here
    # first.  Cheap -- it is the default run minus one fast search per position.
    add_test(NAME corpus_presolve
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-pv --no-presolve --quiet
                     --slowest 0)
    set_tests_properties(corpus_presolve PROPERTIES TIMEOUT 900)

    # Move ordering reaching MODE_FULL (roadmap item 22b), which the ABI exposes
    # as NIL_FLAG_FAST_LINE.  No --check-pv here, and that omission is the whole
    # point of the arm: ordering is answer-neutral on every VALUE and is not
    # tie-break-neutral, so this checks the thing that must not move and leaves
    # alone the three things that may.  If a value ever differs between this arm
    # and the default one, ordering has stopped being a pure reorder.
    add_test(NAME corpus_ordered
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --no-canonical-pv --quiet --slowest 0)
    set_tests_properties(corpus_ordered PROPERTIES TIMEOUT 900)

    # The move list against the search that produced it.  Scoring every legal
    # card is a second reading of the same position, so what it owes is
    # consistency with the first reading: same answer, same principal variation,
    # and a set of classes that partitions the legal cards rather than a subset
    # of them.  Both modes, because they build the list by different routes --
    # full mode recovers each card's counts by replaying its line, fast mode
    # reads a boolean off a bound.
    add_test(NAME corpus_moves
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --check-pv --quiet
                     --slowest 0)
    set_tests_properties(corpus_moves PROPERTIES TIMEOUT 900)

    add_test(NAME corpus_moves_fast
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --mode fast --check-moves --quiet
                     --slowest 0)
    set_tests_properties(corpus_moves_fast PROPERTIES TIMEOUT 900)

    add_test(NAME corpus_ordering
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --mode fast --no-ordering --quiet
                     --slowest 0)
    set_tests_properties(corpus_ordering PROPERTIES TIMEOUT 900)

    # The last-trick shortcut returns an exact value rather than a bound, so
    # taking it away must not move a single answer OR a single principal
    # variation -- unlike --no-ordering, which is only claimed to preserve the
    # value.  --check-pv is therefore carried here, in both modes: the shortcut
    # fires in each of them and the forced trick it evaluates is the tail of
    # every PV the corpus records.
    add_test(NAME corpus_last_trick
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --no-last-trick --check-pv --quiet
                     --slowest 0)
    set_tests_properties(corpus_last_trick PROPERTIES TIMEOUT 900)

    add_test(NAME corpus_last_trick_fast
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --mode fast --no-last-trick --quiet
                     --slowest 0)
    set_tests_properties(corpus_last_trick_fast PROPERTIES TIMEOUT 900)

    # The control arm for confining the table to trick boundaries.  The table is
    # a memo, so this is the strongest claim on the list: not merely the same
    # value but the same principal variation, in both modes, because declining
    # to write or read part of a memo can change how long a search takes and
    # nothing else.  --check-pv is carried for that reason.
    add_test(NAME corpus_tt_plies
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --tt-all-plies --check-pv --quiet
                     --slowest 0)
    set_tests_properties(corpus_tt_plies PROPERTIES TIMEOUT 900)

    # The control arm for the reach bound.  It returns a fail-soft bound rather
    # than an exact value, so what it must not move is the value and the
    # principal variation -- which the sentinel window at every PV step already
    # guarantees, and which this asserts rather than assumes.  Full mode only:
    # the bound is inert in fast mode, where the reachable range is [0, t]
    # against a window of [0, 1].
    # The control arm for suit mixing, in both modes, because ordering runs in
    # both.  Full mode carries --check-pv: the canonical re-derivation pins the
    # line there whatever order the moves were tried in, and that is worth
    # asserting rather than assuming.  Fast mode checks values only -- a
    # reordering can return a different one of several equally good moves, which
    # is the same claim corpus_ordering makes.
    add_test(NAME corpus_suit_mix
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --no-suit-mix --check-pv --quiet
                     --slowest 0)
    set_tests_properties(corpus_suit_mix PROPERTIES TIMEOUT 900)

    add_test(NAME corpus_suit_mix_fast
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --mode fast --no-suit-mix --quiet
                     --slowest 0)
    set_tests_properties(corpus_suit_mix_fast PROPERTIES TIMEOUT 900)

    add_test(NAME corpus_target
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --no-target-bounds --check-pv --quiet
                     --slowest 0)
    set_tests_properties(corpus_target PROPERTIES TIMEOUT 900)

    # The control arm for the cutoff bound taken off a partial table match
    # (roadmap item 41), carrying --check-pv on the corpus_narrow model.  It
    # earns the PV check rather than merely inheriting it: the item cuts a node
    # short on the strength of a stored bound, and the argument that this is
    # allowed is that the value it stops at is squeezed exact.  A search that
    # returned a bound where the PV walk expects a value would still produce the
    # right number at the root and the wrong line under it.
    add_test(NAME corpus_tt_narrow
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --no-tt-narrow --check-pv --quiet
                     --slowest 0)
    set_tests_properties(corpus_tt_narrow PROPERTIES TIMEOUT 900)

    add_test(NAME corpus_tt_plies_fast
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --mode fast --tt-all-plies --quiet
                     --slowest 0)
    set_tests_properties(corpus_tt_plies_fast PROPERTIES TIMEOUT 900)

    # THE SEPT 2026 PERFORMANCE PASS'S CONTROL ARMS.  Every change in it is
    # claimed exact -- same values, same principal variations, same per-card
    # rows -- so each arm turns one off and must still land on the oracle's
    # values AND lines, with the per-card list checked against the position
    # wherever the change touches it.  The default runs above are the other
    # half of each pair.  Under a second each.
    #
    #   --no-row-mtd       the per-card rows as they were scored before: the
    #                      root search first, one windowed search per card;
    #   --no-tight-pv      each line step re-derived under the caller's window;
    #   --no-tt-two-bounds one bound per table entry, overwritten on store;
    #   --no-doom-charge   a doomed bid charged on the trick it breaks;
    #   --no-live-order    the ordering keyed to the nil seat, and no cheapest-
    #                      winning-card-first rule (ordering only, but full mode
    #                      re-derives its lines canonically, so --check-pv holds).
    add_test(NAME corpus_moves_no_row_mtd
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --check-pv --no-row-mtd
                     --quiet --slowest 0)
    set_tests_properties(corpus_moves_no_row_mtd PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_no_tight_pv
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --check-pv --no-tight-pv
                     --quiet --slowest 0)
    set_tests_properties(corpus_no_tight_pv PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_no_tt_two_bounds
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --check-pv
                     --no-tt-two-bounds --quiet --slowest 0)
    set_tests_properties(corpus_no_tt_two_bounds PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_no_doom_charge
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --check-pv --no-doom-charge
                     --quiet --slowest 0)
    set_tests_properties(corpus_no_doom_charge PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_no_live_order
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --check-pv --no-live-order
                     --quiet --slowest 0)
    set_tests_properties(corpus_no_live_order PROPERTIES TIMEOUT 900)

    # The control arm for the trick-oriented order (the Sept 2026 ordering
    # study; see trick_order_moves in search.cpp).  Every default run of this
    # corpus orders every seat without a live bid by the new score; these two
    # put the old seat rules back, in both modes, because the order runs in
    # both.  Ordering only, so both must land on the oracle's values -- and in
    # full mode on its lines, which the canonical re-derivation pins.
    add_test(NAME corpus_no_trick_order
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --check-pv --no-trick-order
                     --quiet --slowest 0)
    set_tests_properties(corpus_no_trick_order PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_no_trick_order_fast
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --mode fast --check-moves
                     --no-trick-order --quiet --slowest 0)
    set_tests_properties(corpus_no_trick_order_fast PROPERTIES TIMEOUT 900)
    # ...and the killer move that rides on it, full mode only.
    add_test(NAME corpus_no_killer_order
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --check-pv
                     --no-killer-order --quiet --slowest 0)
    set_tests_properties(corpus_no_killer_order PROPERTIES TIMEOUT 900)
    # The control arms for the shed order (the Oct 2026 minimise-direction
    # study; see shed_order_moves in search.cpp).  It reaches only the corpus
    # rows that ask for the minimise direction -- 271 of them here -- and
    # only in full mode, so these are full-mode arms: one puts the trick
    # order back for those rows, the other puts 6b's attacking lead back in
    # front of the shed order against a single nil.  Ordering only, so both
    # must land on the oracle's values and lines.
    add_test(NAME corpus_no_shed_order
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --check-pv --no-shed-order
                     --quiet --slowest 0)
    set_tests_properties(corpus_no_shed_order PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_shed_single_attack
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --check-pv
                     --shed-single-attack --quiet --slowest 0)
    set_tests_properties(corpus_shed_single_attack PROPERTIES TIMEOUT 900)

    # The control arm for the ADVERSARIAL proofs (duck or cover, forcing lead;
    # see bounds.hpp), on the corpus_static model: every other run of this
    # corpus takes them, this one does not, and both must land on the oracle's
    # answers.  Full mode carries --check-moves and --check-pv because there
    # the proofs are spent as fail-soft bounds, which owe the same value AND
    # the same line; the fast arm is where they settle nodes outright.
    add_test(NAME corpus_no_adversarial
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --check-pv
                     --no-adversarial-proofs --quiet --slowest 0)
    set_tests_properties(corpus_no_adversarial PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_no_adversarial_fast
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --mode fast --check-moves
                     --no-adversarial-proofs --quiet --slowest 0)
    set_tests_properties(corpus_no_adversarial_fast PROPERTIES TIMEOUT 900)

    # The second Sept 2026 optimization pass.  Each switch below is a control
    # arm in the usual sense -- default runs above take the optimization, these
    # do not, and every one must land on the oracle's values and lines.
    #   --no-huge-pages        tables on ordinary pages (memory layout only)
    #   --no-dd-prefetch       engine profiles read one miss at a time
    #   --no-dd-age            the engine's old entries replaced on equal terms
    #   --no-boundary-facts    nil proofs re-asked at every call site
    #   --no-row-order         per-card rows scored in canonical order
    # --values-only is the ON arm of an opt-in: rows decoded from their values,
    # held field by field against the walked lines by nil_bench itself.
    foreach(_arm no-huge-pages no-dd-prefetch no-dd-age no-boundary-facts no-row-order)
      string(REPLACE "-" "_" _name "${_arm}")
      add_test(NAME corpus_${_name}
               COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --check-pv --${_arm}
                       --quiet --slowest 0)
      set_tests_properties(corpus_${_name} PROPERTIES TIMEOUT 900)
    endforeach()
    add_test(NAME corpus_values_only
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --values-only
                     --quiet --slowest 0)
    set_tests_properties(corpus_values_only PROPERTIES TIMEOUT 900)
    # The nil-demoted double-dummy bound (A4) runs from 8 tricks up by default,
    # which no row of this corpus reaches -- so the default run above is its
    # OFF arm here, and this one pushes it down to 2 tricks, where it fires on
    # most safe-band boundaries, against the oracle's values and lines.
    add_test(NAME corpus_demoted_dd_low
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --check-moves --check-pv
                     --demoted-dd 2 --quiet --slowest 0)
    set_tests_properties(corpus_demoted_dd_low PROPERTIES TIMEOUT 900)
  endif()

  # TWO NILS ON ONE SIDE.  A separate corpus because it is a separate question:
  # the primary level counts bids down rather than weighting one seat's tricks,
  # so these rows exercise a fork of the objective, a fork of the trick
  # accounting, the broken-nil mask in the state key and its own value tag --
  # none of which any row in positions.txt touches.
  set(NIL_MULTINIL_CORPUS "${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus/multinil.txt")
  if(NIL_SOLVER_BUILD_BENCH AND EXISTS "${NIL_MULTINIL_CORPUS}")
    # All 150 oracle-verified rows plus the six 13-card deals, which carry no
    # recorded answer and are here as a crash-and-timeout regression at a size
    # the oracle cannot reach.
    add_test(NAME corpus_multinil
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-pv --quiet --slowest 0)
    set_tests_properties(corpus_multinil PROPERTIES TIMEOUT 900)

    # Fast subset for the inner loop: ctest -R corpus_multinil_quick
    add_test(NAME corpus_multinil_quick
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --cards-only 4
                     --quiet --slowest 0)

    # THE CONTROL ARM FOR THE KEY.  Patch 59 put the broken-nil mask into the
    # state key, and the failure mode if it is wrong is not a crash: it is a
    # table hit from a position that only LOOKS the same, returning a value from
    # a line where different bids were already down.  Running the same rows with
    # the table off is what stands between that and a confidently wrong answer.
    # THE CONTROL ARM FOR THE DOUBLE-DUMMY HANDOFF.  Every row here reaches
    # positions with both bids down, which is where the handoff fires; with it
    # off the general search answers them instead, and both must match the
    # oracle's values AND principal variations.
    add_test(NAME corpus_multinil_no_dd
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-pv --no-dd-engine
                     --quiet --slowest 0)
    set_tests_properties(corpus_multinil_no_dd PROPERTIES TIMEOUT 900)

    # THE CONTROL ARM FOR THE ONE-LIVE-BID BOUND.  Rows reach positions with
    # one bid down and the other live, which is where it fires; with it on the
    # search is bounded there, and both must match the oracle's values and
    # principal variations.  The arm is now the ON side: the bound has been off
    # by default since the Sept 2026 performance pass (see
    # SearchOptions::dd_live_bounds), so the default run above is the
    # unbounded half of the pair.
    add_test(NAME corpus_multinil_live_bounds
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-pv --dd-live-bounds
                     --quiet --slowest 0)
    set_tests_properties(corpus_multinil_live_bounds PROPERTIES TIMEOUT 900)

    # The Sept 2026 pass's control arms on two nils, where doom charging and
    # the live-bid ordering do most of their work -- see the positions.txt
    # block for what each arm turns off.  corpus_multinil_moves is the default
    # half for the per-card rows, which corpus_multinil does not score.
    add_test(NAME corpus_multinil_moves
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-moves --check-pv
                     --quiet --slowest 0)
    set_tests_properties(corpus_multinil_moves PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_multinil_moves_no_row_mtd
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-moves --check-pv
                     --no-row-mtd --quiet --slowest 0)
    set_tests_properties(corpus_multinil_moves_no_row_mtd PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_multinil_no_doom_charge
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-moves --check-pv
                     --no-doom-charge --quiet --slowest 0)
    set_tests_properties(corpus_multinil_no_doom_charge PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_multinil_no_live_order
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-moves --check-pv
                     --no-live-order --quiet --slowest 0)
    set_tests_properties(corpus_multinil_no_live_order PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_multinil_no_trick_order
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-moves --check-pv
                     --no-trick-order --quiet --slowest 0)
    set_tests_properties(corpus_multinil_no_trick_order PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_multinil_no_shed_order
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-moves --check-pv
                     --no-shed-order --quiet --slowest 0)
    set_tests_properties(corpus_multinil_no_shed_order PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_multinil_no_tt_two_bounds
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-moves --check-pv
                     --no-tt-two-bounds --quiet --slowest 0)
    set_tests_properties(corpus_multinil_no_tt_two_bounds PROPERTIES TIMEOUT 900)

    # Second Sept 2026 pass, on two nils: the per-card driver changes (row
    # order; values-only rows, whose mask the value cannot always pin here, so
    # those rows still walk their line) and the memory-layout arms.
    foreach(_arm no-row-order no-huge-pages no-dd-prefetch no-multi-live-proofs)
      string(REPLACE "-" "_" _name "${_arm}")
      add_test(NAME corpus_multinil_${_name}
               COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-moves --check-pv
                       --${_arm} --quiet --slowest 0)
      set_tests_properties(corpus_multinil_${_name} PROPERTIES TIMEOUT 900)
    endforeach()
    add_test(NAME corpus_multinil_values_only
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-moves --values-only
                     --quiet --slowest 0)
    set_tests_properties(corpus_multinil_values_only PROPERTIES TIMEOUT 900)

    # The Oct 2026 twin pass.  corpus_multinil_no_pair_proofs is the control
    # arm for the one-of-two proofs while both bids are live.  The one-live
    # double-dummy bound this shape now takes runs from 9 tricks up, which no
    # oracle row here reaches -- so the default runs above are its OFF arm,
    # and the two *_twin_dd_live_low runs push it down to 2 tricks, where it
    # fires, against the oracle's values and lines and the values-only decode.
    add_test(NAME corpus_multinil_no_pair_proofs
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-moves --check-pv
                     --no-pair-proofs --quiet --slowest 0)
    set_tests_properties(corpus_multinil_no_pair_proofs PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_multinil_twin_dd_live_low
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-moves --check-pv
                     --twin-dd-live 2 --quiet --slowest 0)
    set_tests_properties(corpus_multinil_twin_dd_live_low PROPERTIES TIMEOUT 900)
    add_test(NAME corpus_multinil_twin_dd_live_low_values_only
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --check-moves --values-only
                     --twin-dd-live 2 --quiet --slowest 0)
    set_tests_properties(corpus_multinil_twin_dd_live_low_values_only PROPERTIES TIMEOUT 900)

    add_test(NAME corpus_multinil_no_tt
             COMMAND nil_bench --corpus "${NIL_MULTINIL_CORPUS}" --cards-only 4
                     --no-memo --quiet --slowest 0)
  endif()

  # THREE NILS (Oct 2026).  A separate corpus for the same reason the twin one
  # is: a separate question.  One pair both bid and the other has one bid and a
  # lean, the primary is a six-rung outcome ladder over an eight-state mask,
  # and the shape rides on the opposed machinery through a rank table -- so
  # these rows exercise that table, the mask-indexed reach bound, its own value
  # tag, and every opposed-shape gate with a third bidder behind it, none of
  # which any other corpus reaches.  Every row is the oracle's, PV included.
  set(NIL_THREENIL_CORPUS "${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus/threenil.txt")
  if(NIL_SOLVER_BUILD_BENCH AND EXISTS "${NIL_THREENIL_CORPUS}")
    add_test(NAME corpus_threenil
             COMMAND nil_bench --corpus "${NIL_THREENIL_CORPUS}" --check-pv --quiet --slowest 0)
    set_tests_properties(corpus_threenil PROPERTIES TIMEOUT 600)
    # The per-card rows, which is the call the bot makes, and the values-only
    # decode, which on this shape must fall through to a line walk exactly
    # when one twin of two is down.
    add_test(NAME corpus_threenil_moves
             COMMAND nil_bench --corpus "${NIL_THREENIL_CORPUS}" --check-moves --check-pv
                     --quiet --slowest 0)
    set_tests_properties(corpus_threenil_moves PROPERTIES TIMEOUT 600)
    add_test(NAME corpus_threenil_values_only
             COMMAND nil_bench --corpus "${NIL_THREENIL_CORPUS}" --check-moves --values-only
                     --quiet --slowest 0)
    set_tests_properties(corpus_threenil_values_only PROPERTIES TIMEOUT 600)

    # THE CONTROL ARMS for every opposed-shape optimisation three bids now
    # reach.  Each one reads the outcome rank through the table and had only
    # ever seen two bidders behind it; with it off the general search answers
    # instead, and both must match the oracle's values AND lines.
    # --dd-live-bounds is the ON side of a bound that is off by default.
    foreach(_arm no-dd-engine no-doom-charge no-opposed-reach no-multi-live-proofs
                 no-settled-tricks no-live-order no-trick-order no-row-mtd
                 no-tt-two-bounds dd-live-bounds)
      string(REPLACE "-" "_" _name "${_arm}")
      add_test(NAME corpus_threenil_${_name}
               COMMAND nil_bench --corpus "${NIL_THREENIL_CORPUS}" --check-moves --check-pv
                       --${_arm} --quiet --slowest 0)
      set_tests_properties(corpus_threenil_${_name} PROPERTIES TIMEOUT 600)
    endforeach()

    # The key carries the broken-bid mask and the shape has its own tag; with
    # the table off nothing can be confused for anything.
    add_test(NAME corpus_threenil_no_tt
             COMMAND nil_bench --corpus "${NIL_THREENIL_CORPUS}" --cards-only 4
                     --no-memo --quiet --slowest 0)
  endif()

  set(NIL_LARGE_CORPUS "${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus/large.txt")
  if(NIL_SOLVER_LARGE_TESTS AND NIL_SOLVER_BUILD_BENCH AND EXISTS "${NIL_LARGE_CORPUS}")
    # 7 cards only: at 8 and above the rows are benchmark fodder, and several
    # have no recorded answer to check against anyway.
    add_test(NAME corpus_large
             COMMAND nil_bench --corpus "${NIL_LARGE_CORPUS}" --cards-only 7 --slowest 0)
    set_tests_properties(corpus_large PROPERTIES TIMEOUT 3600)
  endif()

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_FOUND AND NIL_SOLVER_BUILD_CLI)
    # Invariance checks need no reference answer, so they are the only
    # verification that works at hand sizes the oracle cannot reach.  The fast
    # subset runs every time; point the script at the large corpus by hand.
    add_test(NAME invariants
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/invariants.py"
              --exe $<TARGET_FILE:nil_cli>
              --corpus "${NIL_CORPUS}" --cards 4 --limit 40 --quiet
    )
    set_tests_properties(invariants PROPERTIES TIMEOUT 600)

    # The same transforms against the pruned search.  Below nine cards this is
    # weaker than corpus_modes, which compares the boolean against an unpruned
    # oracle-checked answer -- but it is the check that scales, and it is the
    # only verification the boolean search has at the hand sizes where full
    # mode no longer finishes.  Running it small here keeps it honest; run it
    # by hand at 11 or 12 cards when the search changes.
    add_test(NAME invariants_fast
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/invariants.py"
              --exe $<TARGET_FILE:nil_cli>
              --corpus "${NIL_CORPUS}" --cards 6 --limit 40 --mode fast --quiet
    )
    set_tests_properties(invariants_fast PROPERTIES TIMEOUT 600)
  endif()

  if(Python3_FOUND AND NIL_SOLVER_BUILD_CLI)
    # Differential test against nil_oracle.py.  Skipped (not failed) by the
    # script itself if the oracle is not present.
    # MID-HAND RE-SOLVE.  Play a two-bid deal along its own principal variation
    # until one bid breaks, hand the REMAINING position back with that seat
    # marked already-down, and require the same verdict on the surviving bid and
    # the same trick total over what is left.
    #
    # This is the property a table actually depends on: the answer you get at
    # trick four must not contradict the answer you got at trick one.  It holds
    # because the primary weight is charged on a bidder's FIRST trick and never
    # again, so from the moment a bid is down the rest of the subtree is scored
    # on the surviving bid and the pair's total -- which is exactly what
    # ROLE_NIL_SET asks for.  Nothing enforces that but this test.
    # ONE BID PER SIDE.  Compares the UTILITY PAIR rather than the cards: two
    # optimal lines can name different equally-good cards, but the game value is
    # the game value, so the solver's own line replayed under the oracle's rules
    # must reach it.  Covers all eight strictly opposed role sets.
    add_test(NAME opposing_crosscheck
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/opposing_crosscheck.py"
              --cases 48 --cards 4 --seed 17)
    set_tests_properties(opposing_crosscheck PROPERTIES TIMEOUT 600)

    # ITEM 78'S PROBE, checked against TWO oracle routes at once: its own boolean
    # AND-OR search, and the outcome rank of the exhaustive utility search, which
    # is 3 for the attacking side exactly when the conjunction holds.  A shared
    # misreading of the question would have to survive two algorithms and one
    # derivation.  The tool reports the forceable count so a run that is all one
    # answer, and therefore proves little, says so.
    add_test(NAME conjunction_crosscheck
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/conjunction_crosscheck.py"
              --cases 14 --cards 3 --seed 17)
    set_tests_properties(conjunction_crosscheck PROPERTIES TIMEOUT 600)

    # ITEM 2B'S PORT, checked against the oracle's solve_cooperative (patch 90)
    # rather than against itself -- cooperative_crosscheck.py below checks the
    # oracle against three routes of its OWN; this is the oracle against the
    # new C++.  Every strictly-opposed-per-pair role set is exercised in both
    # partner leans, INCLUDING the same-lean (2/2, 3/3) cells solve() itself
    # still refuses -- this probe reads no role but the one it protects, so it
    # answers those cells too, and they are exactly the ones item 60 needs it
    # for.  Also checks that collapsing rank-equivalent moves and the memo are
    # both answer-neutral, and fails a run that never sees both a reachable and
    # an unreachable answer.
    add_test(NAME cooperative_cpp_crosscheck
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/cooperative_cpp_crosscheck.py"
              --cases 12 --cards 4 --seed 17)
    set_tests_properties(cooperative_cpp_crosscheck PROPERTIES TIMEOUT 600)

    # THE FAIL PROBE AGAINST AN INDEPENDENT REFERENCE, not against a cousin of
    # itself: the oracle has no fail-mode equivalent, so this enumerates the
    # reachable outcome set directly by exhaustive play.  The probe is used by
    # nothing in the shipped decision procedure as of step 2 (the rung it was
    # built for is disproved), but it is correct code and checked as such.
    add_test(NAME cooperative_fail_crosscheck
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/cooperative_fail_crosscheck.py"
              --cases 25 --cards 4 --seed 23)
    set_tests_properties(cooperative_fail_crosscheck PROPERTIES TIMEOUT 600)

    # ITEM 60 STEP 4: the trick split under T's priority, against an
    # INDEPENDENT backward induction written from that priority directly, with
    # the constraint applied the slow way (both halves at the terminal state).
    # Checks BOTH leans, since the priority is written per side and a bug that
    # swapped "my cover hand" for "theirs" would pass a one-lean run.
    add_test(NAME constrained_tricks_crosscheck
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/constrained_tricks_crosscheck.py"
              --cases 10 --cards 4 --seed 41)
    set_tests_properties(constrained_tricks_crosscheck PROPERTIES TIMEOUT 900)

    # ITEM 60 STEP 3: the pinned-outcome filter against an INDEPENDENT
    # reference that applies both halves of the constraint at the end of the
    # line, the slow obvious way.  What that checks is precisely the
    # optimisation: abandoning a must-make violation the instant it happens,
    # without recursing, must not change the answer.  The run fails if the
    # early-abandon path never fires, so the check cannot silently stop
    # exercising the thing it exists for.
    add_test(NAME outcome_constraint_crosscheck
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/outcome_constraint_crosscheck.py"
              --cases 20 --cards 4 --seed 31)
    set_tests_properties(outcome_constraint_crosscheck PROPERTIES TIMEOUT 600)

    # ITEM 60 STEP 1: solve()'s new same-lean branch against the oracle's
    # exhaustive backward induction, on the two cells it implements (neither
    # bid individually reachable; exactly one is) -- and confirms solve()
    # REFUSES rather than guesses on the third cell (both reachable), which
    # is step 2's job.  A wrong-looking success on that third cell would be a
    # correctness hazard, not just a missing feature, so it is checked here
    # too, not just the two implemented branches.
    add_test(NAME same_lean_partial_crosscheck
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/same_lean_partial_crosscheck.py"
              --cases 200 --cards 4 --seed 5)
    set_tests_properties(same_lean_partial_crosscheck PROPERTIES TIMEOUT 600)

    # THE CLAIM `nils_set_mask_determined` IS DERIVED FROM.  The field says the
    # broken-bid mask is pinned by the objective on the single-nil and opposed
    # shapes and not on the twin one; this asserts both halves.  The second half
    # matters as much as the first: if the twin shape ever measured NO ambiguity,
    # the honest reading is that this suite stopped reaching the case, not that
    # the shape became determined -- so a run that sees none is a failure rather
    # than a pass.
    # ITEM 2'S PROBE: is a mutual outcome REACHABLE AT ALL?  Every other probe
    # here is an adversarial guarantee, so there is nothing to compare this one
    # against directly and the checking comes from three routes instead --
    # enumeration of the definition, the one-way implication that a minimax
    # outcome is a reachable one, and monotonicity in the protected set.  The
    # tool fails a run in which the probe never sees both answers, since such a
    # run would pass while checking only one branch.
    add_test(NAME cooperative_crosscheck
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/cooperative_crosscheck.py"
              --cards 3 --cases 48 --seed 17)
    set_tests_properties(cooperative_crosscheck PROPERTIES TIMEOUT 600)

    # THREE NILS AGAINST THE ORACLE, and against T's conversion rule.  The
    # first run replays the solver's line and every root card's row under the
    # oracle's utility pair, across all eight role arrays and both directions.
    # The second plays each line to its first broken bid, re-solves what is
    # left from the roles a caller converts to, and requires the same endings
    # and trick totals -- solver against solver, so it runs past oracle sizes.
    # Both fail a run that does not reach what they exist to check: too few
    # distinct outcomes, or only one of the two conversions.
    add_test(NAME three_nil_crosscheck
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/three_nil_crosscheck.py"
              --exe $<TARGET_FILE:nil_cli> --cases 48 --cards 4 --seed 17 --moves)
    set_tests_properties(three_nil_crosscheck PROPERTIES TIMEOUT 600)
    add_test(NAME three_nil_subgame
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/three_nil_crosscheck.py"
              --exe $<TARGET_FILE:nil_cli> --subgame --cases 40 --cards 7 --seed 5)
    set_tests_properties(three_nil_subgame PROPERTIES TIMEOUT 600)

    add_test(NAME mask_determinacy
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/mask_determinacy.py"
              --assert-claim --cards 4 --cases 24 --seed 17)
    set_tests_properties(mask_determinacy PROPERTIES TIMEOUT 600)

    add_test(NAME subgame_consistency
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/subgame_consistency.py"
              --cards 5 --count 40 --seed 3)
    set_tests_properties(subgame_consistency PROPERTIES TIMEOUT 600)

    # THE ORACLE'S OWN SELFTEST, which until now ran only by hand.  It is the
    # ground truth every crosscheck above is measured against, and its checks
    # are the ones most likely to encode an assumption rather than test one --
    # patch 106's separate oracle patch found exactly that, twice.  A check
    # that quietly stops checking is this repo's recurring bug; a check that
    # was never wired up has simply never started.  ~7 s, no arguments.
    add_test(NAME oracle_selftest
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/nil_oracle.py" --selftest)
    set_tests_properties(oracle_selftest PROPERTIES TIMEOUT 600)

    add_test(NAME crosscheck
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/crosscheck.py"
              --exe $<TARGET_FILE:nil_cli>
              --cases 60 --cards 4 --quiet
    )
  endif()
endif()

# ---------------------------------------------------------------------------
# Install: `cmake --install build --prefix <dir>` gives you the DLL, the import
# library and the header to drop next to your C# project.
# ---------------------------------------------------------------------------
install(TARGETS nil_solver
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)
