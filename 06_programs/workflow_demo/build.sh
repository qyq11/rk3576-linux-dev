#!/usr/bin/env bash
set -euo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: bash build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
mkdir -p -- "$(dirname -- "$1")"
aarch64-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
    "$source_dir/main.c" -o "$1"
