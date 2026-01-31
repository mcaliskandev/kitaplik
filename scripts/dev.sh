#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-"$root_dir/build"}"

usage() {
  cat <<'EOF'
Usage: scripts/dev.sh <build|clean|run> [args...]

Environment:
  BUILD_DIR   Build directory (default: ./build)

Examples:
  bash scripts/dev.sh build
  bash scripts/dev.sh run
  BUILD_DIR=build-debug bash scripts/dev.sh build
EOF
}

cmd="${1:-}"
shift || true

case "$cmd" in
  build)
    cmake -S "$root_dir" -B "$build_dir" -DKITAPLIK_BUILD_APP=ON
    cmake --build "$build_dir" --parallel
    ;;
  clean)
    rm -rf -- "$build_dir"
    ;;
  run)
    exe="$build_dir/kitaplik"
    if [[ ! -x "$exe" ]]; then
      echo "error: executable not found at: $exe (run: bash scripts/dev.sh build)" >&2
      exit 1
    fi
    exec "$exe" "$@"
    ;;
  *)
    usage
    exit 2
    ;;
esac
