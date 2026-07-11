#!/usr/bin/env bash
# Build script for mangly-cpp: configure with CMake, build, and run tests.
# Usage: ./build.sh [--skip-tests] [--clean] [--config <cfg>]
#   --skip-tests   configure and build only
#   --clean        remove the build directory first
#   --config <cfg> build configuration (default: Release)
set -euo pipefail

cd "$(dirname "$0")"

BUILD_DIR=build
CONFIG=Release
RUN_TESTS=1

while [ "$#" -gt 0 ]; do
  case "$1" in
    --skip-tests) RUN_TESTS=0 ;;
    --clean)      rm -rf "$BUILD_DIR" ;;
    --config)     shift; CONFIG="${1:?--config needs a value}" ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

echo ">> configuring ($CONFIG)"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG"

echo ">> building"
cmake --build "$BUILD_DIR" --config "$CONFIG"

if [ "$RUN_TESTS" -eq 1 ]; then
  echo ">> running tests"
  ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure
fi

echo ">> done"
