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
  src/nil/position.cpp
  src/nil/search.cpp
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

  find_package(Python3 COMPONENTS Interpreter QUIET)
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
