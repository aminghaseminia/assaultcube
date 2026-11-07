#!/bin/sh
# Build Dune Strike for Linux (production build) and install the binaries
# into bin_unix/. Run this after `git pull` — the app-drawer entry points
# at dunestrike.sh in this directory, so the drawer always launches the
# freshly built version.
set -e
cd "$(dirname "$(readlink -f "$0")")"

if [ ! -f source/enet/Makefile ]; then
  echo "==> building bundled enet"
  (cd source/enet && ./configure --enable-shared=no --enable-static=yes && make -j"$(nproc)")
fi

echo "==> building client + server (PRODUCTION)"
cd source/src
make -j"$(nproc)" client server install \
  CXX=clang++ \
  "CXXFLAGS=-O3 -fomit-frame-pointer -DPRODUCTION"

echo "==> done: bin_unix/dunestrike_client, bin_unix/dunestrike_server"
