#!/usr/bin/env bash
set -euo pipefail
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:-"$source_dir/build-linux"}
if (( $# > 1 )); then
    echo "Usage: scripts/build-linux.sh [build-directory]" >&2
    exit 64
fi
cmake -S "$source_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=ON
cmake --build "$build_dir" --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir "$build_dir" --output-on-failure
