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

# ---------------------------------------------------------------------------
# Core: the rules, the position type and the search.  Built once, linked into
# the shared library, the CLI and the tests.
# ---------------------------------------------------------------------------
add_library(nil_solver_core STATIC
  src/nil/cards.cpp
  src/nil/corpus.cpp
  src/nil/position.cpp
  src/nil/search.cpp
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

    add_test(NAME corpus_tt_plies_fast
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --mode fast --tt-all-plies --quiet
                     --slowest 0)
    set_tests_properties(corpus_tt_plies_fast PROPERTIES TIMEOUT 900)
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
