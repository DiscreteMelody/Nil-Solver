#!/usr/bin/env bash
# Build Nil-Solver and run the test suite.  Works from any directory, and finds
# cmake in the usual Windows install locations when run under Git Bash.
#
#   scripts/build-and-test.sh            release build, full test suite
#   scripts/build-and-test.sh Debug      debug build
set -uo pipefail

cd "$(dirname "$0")/.."
CONFIG="${1:-Release}"

find_cmake() {
    if command -v cmake >/dev/null 2>&1; then
        command -v cmake
        return 0
    fi
    # Git Bash on Windows: cmake is often installed but not on PATH.
    local candidate
    for candidate in \
        "/c/Program Files/CMake/bin/cmake.exe" \
        "/c/Program Files (x86)/CMake/bin/cmake.exe" \
        "/c/Program Files/Microsoft Visual Studio/2022/"*"/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
        "/c/Program Files (x86)/Microsoft Visual Studio/2019/"*"/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
    do
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

CMAKE="$(find_cmake)" || {
    cat >&2 <<'EOF'
Could not find cmake.  Any one of these fixes it:

  * Install CMake from https://cmake.org/download/ and add it to PATH.
  * In the Visual Studio Installer, add the "C++ CMake tools for Windows"
    component.
  * On Linux: apt install cmake  (or your package manager's equivalent).
EOF
    exit 1
}
CTEST="$(dirname "$CMAKE")/ctest"
[ -x "$CTEST" ] || CTEST="${CTEST}.exe"

echo "Using    $CMAKE"
echo "Config   $CONFIG"
echo "Root     $(pwd)"
echo

echo "=== Configure ==="
"$CMAKE" -S . -B build -DCMAKE_BUILD_TYPE="$CONFIG" || exit 1

echo
echo "=== Build ==="
"$CMAKE" --build build --config "$CONFIG" --parallel || exit 1

echo
echo "=== Test ==="
# --build-config matters on multi-config generators (Visual Studio, Xcode);
# it is harmlessly ignored by single-config ones like Makefiles and Ninja.
"$CTEST" --test-dir build --build-config "$CONFIG" --output-on-failure || exit 1

echo
echo "=== Built ==="
ls build/bin
echo
echo "All tests passed."
