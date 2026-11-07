#!/bin/bash
# Adds a custom menuitem for Dune Strike to your Desktop's launcher.

GAME_DIR=$(dirname "$(readlink -f "${0}")")
GAME_EXEC=dunestrike.sh
LAUNCHERPATH="${HOME}/.local/share/applications/"

LAUNCHERFILE=dunestrike_dev.desktop
LAUNCHERTITLE="Dune Strike"

# Remove existing menuitem, if it exists:
EXISTINGEXEC=`find "${LAUNCHERPATH}" -name "dunestrike*" | xargs`
if [ "$EXISTINGEXEC" != "" ]; then
  echo "The following menuitem(s) currently exist:"
  echo "$EXISTINGEXEC"
  read -p "Would you like them all to be deleted? (y/N): " -r REPLY
  if [ "$REPLY" = "y" ] || [ "$REPLY" = "yes" ] || [ "$REPLY" = "Y" ] || [ "$REPLY" = "YES" ]; then
    find "${LAUNCHERPATH}" -name "dunestrike*" -delete
    echo "Deleted menuitems as requested." && echo ""
    exit 0
  else
    echo "The existing menuitems will remain." && echo ""
  fi
fi

mkdir -p "${LAUNCHERPATH}"
cat > "${LAUNCHERPATH}"${LAUNCHERFILE} << EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=$LAUNCHERTITLE
Keywords=dunestrike;game;fps;
GenericName=First Person Shooter Game
Categories=Game;ActionGame;Shooter;
Terminal=false
StartupNotify=false
Exec=$GAME_DIR/$GAME_EXEC
Icon=$GAME_DIR/packages/misc/icon.png
Comment=A multiplayer, first-person shooter game, built on the Cube engine. Fast, arcade gameplay.
EOF

chmod +x "${LAUNCHERPATH}"${LAUNCHERFILE}
if [ -x "${LAUNCHERPATH}"${LAUNCHERFILE} ]; then
  echo "A Dune Strike menuitem has been successfully created at"
  echo "${LAUNCHERPATH}"${LAUNCHERFILE}
  exit 0
else
  echo "For some reason, we're unable to install a Dune Strike menuitem."
  exit 1
fi

