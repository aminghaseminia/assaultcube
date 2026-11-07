#!/bin/sh

# Directory containing Dune Strike.
GAME_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# Extra command line options for the client.
GAME_OPTIONS="--home=${HOME}/.dunestrike/v1.3 --init"

BINARY="${GAME_DIR}/bin_unix/dunestrike_client"

if [ "$1" = "--outputbinarypath" ]; then
  echo "${BINARY}"
  exit 0
fi

if [ -x "${BINARY}" ]; then
  cd "${GAME_DIR}" || exit 1
  exec "${BINARY}" ${GAME_OPTIONS} "$@"
fi

echo "Dune Strike could not find a compiled client at ${BINARY}."
echo "Build one with:"
echo "  1) Install the development libraries: OpenGL, SDL2, SDL2_image,"
echo "     zlib, libogg, libvorbis and OpenAL Soft."
echo "  2) cd source/src"
echo "  3) make client_install"
echo "  4) Run this script again from the game directory."
exit 1
