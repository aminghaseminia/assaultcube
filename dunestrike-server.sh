#!/bin/sh

# Directory containing Dune Strike.
GAME_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# Voice chat is enabled by default so this script alone is enough.
# Disable it with -R0; options passed on the command line win.
GAME_OPTIONS="-R"
GAME_OPTIONFILE="-Cconfig/servercmdline.txt"

BINARY="${GAME_DIR}/bin_unix/dunestrike_server"

if [ "$1" = "--outputbinarypath" ]; then
  echo "${BINARY}"
  exit 0
fi

if [ -x "${BINARY}" ]; then
  cd "${GAME_DIR}" || exit 1
  exec "${BINARY}" ${GAME_OPTIONS} ${GAME_OPTIONFILE} "$@"
fi

echo "Dune Strike could not find a compiled server at ${BINARY}."
echo "Build one with:"
echo "  1) Install zlib and OpenAL development files if needed."
echo "  2) cd source/src"
echo "  3) make server_install"
echo "  4) Run this script again from the game directory."
exit 1
