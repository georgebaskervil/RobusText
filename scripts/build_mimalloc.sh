#!/usr/bin/env bash
set -euo pipefail

# Build and install mimalloc into an in-tree prefix. Designed to be called
# from the top-level Makefile. Uses an out/release build dir and installs to
# third_party/mimalloc/install under the repo root.

REPO_ROOT="$(pwd)"
MIROOT="$REPO_ROOT/third_party/mimalloc"
INSTALLDIR="$MIROOT/install"

if [ ! -d "$MIROOT" ]; then
  # Prefer submodule workflow when running inside a git repo: try to add
  # mimalloc as a submodule if possible.
  if command -v git >/dev/null 2>&1 && [ -d .git ] && ! git submodule status | grep -q "third_party/mimalloc"; then
    echo "Adding mimalloc as a git submodule..."
    git submodule add https://github.com/microsoft/mimalloc.git "$MIROOT" || true
  fi
  if [ ! -d "$MIROOT" ]; then
    echo "Cloning mimalloc into $MIROOT..."
    git clone https://github.com/microsoft/mimalloc.git "$MIROOT"
  fi
fi

cd "$MIROOT"
mkdir -p out/release
cd out/release
echo "Configuring mimalloc..."
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$INSTALLDIR" -DCMAKE_C_FLAGS="-O3 -march=native" ../..
echo "Building mimalloc..."
make -j$(sysctl -n hw.ncpu)
echo "Installing mimalloc into $INSTALLDIR..."
make install

# Create compatibility symlinks so top-level Makefile finds headers and libs
mkdir -p "$INSTALLDIR/include"
if [ -d "$INSTALLDIR/include/mimalloc-2.2" ] && [ ! -f "$INSTALLDIR/include/mimalloc.h" ]; then
  ln -sf "$INSTALLDIR/include/mimalloc-2.2/mimalloc.h" "$INSTALLDIR/include/mimalloc.h"
fi
if [ -d "$INSTALLDIR/lib/mimalloc-2.2" ] && [ ! -f "$INSTALLDIR/lib/libmimalloc.a" ]; then
  ln -sf "$INSTALLDIR/lib/mimalloc-2.2/libmimalloc.a" "$INSTALLDIR/lib/libmimalloc.a"
fi

echo "mimalloc built and installed into $INSTALLDIR"
