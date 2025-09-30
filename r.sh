#!/usr/bin/env bash

# Export the in-tree mimalloc install early so all make invocations see it.
export MIMALLOC_DIR="$(pwd)/third_party/mimalloc/install"

# Use the in-tree mimalloc install so the build will succeed by default.
# On macOS use sysctl to get CPU count instead of nproc.
make clean
make -j$(sysctl -n hw.ncpu)
./RobusText Inter_18pt-Regular.ttf testdata/combining_10k.txt
