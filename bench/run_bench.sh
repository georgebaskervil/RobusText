#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

CC=gcc
CFLAGS="-O2 -g"
INCLUDES="-I.."
LDLIBS=""

$CC $CFLAGS $INCLUDES cluster_cache_bench.c -o cluster_cache_bench

echo "Running benchmark (1M combining marks)..."
./cluster_cache_bench 1000000
