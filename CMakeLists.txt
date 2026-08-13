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
    # nothing here has to be given up for it.  Should ordering ever reach full
    # mode, item 7's `--canonical` is what this test would move behind.
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
    add_test(NAME corpus_ordering
             COMMAND nil_bench --corpus "${NIL_CORPUS}" --mode fast --no-ordering --quiet
                     --slowest 0)
    set_tests_properties(corpus_ordering PROPERTIES TIMEOUT 900)
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
