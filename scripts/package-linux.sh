#!/usr/bin/env bash
set -euo pipefail
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=${1:-0.3.0}
build_dir=${2:-"$source_dir/build-linux"}
output_dir=${3:-"$source_dir/dist"}
if (( $# > 3 )); then
    echo "Usage: scripts/package-linux.sh [version] [build-directory] [output-directory]" >&2
    exit 64
fi
python3 "$source_dir/scripts/package-linux.py" "$version" "$build_dir" "$output_dir"
