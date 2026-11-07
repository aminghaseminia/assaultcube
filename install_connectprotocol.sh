#!/bin/bash
# Adds the handler for the connect protocol for Dune Strike to your system.

GAME_DIR=$(dirname "$(readlink -f "${0}")")
GAME_EXEC=dunestrike.sh
LAUNCHERPATH="${HOME}/.local/share/applications/"

LAUNCHERFILE=dunestrike-connectprotocol.desktop
LAUNCHERTITLE="Dune Strike Connect Protocol"

# Remove existing desktop entries, if they exist:
# avoiding clashing with menuitem scripts & filenames which look for "dunestrike*"
EXISTINGEXEC=`find "${LAUNCHERPATH}" -name "connectprotocol-dunestrike*" | xargs`
if [ "$EXISTINGEXEC" != "" ]; then
  echo "The following entries currently exist:"
  echo "$EXISTINGEXEC"
  read -p "Would you like them all to be deleted? (y/N): " -r REPLY
  if [ "$REPLY" = "y" ] || [ "$REPLY" = "yes" ] || [ "$REPLY" = "Y" ] || [ "$REPLY" = "YES" ]; then
    find "${LAUNCHERPATH}" -name "connectprotocol-dunestrike*" -delete
    echo "Deleted entries as requested." && echo ""
    exit 0
  else
    echo "The existing entries will remain." && echo ""
  fi
fi

mkdir -p "${LAUNCHERPATH}"
cat > "${LAUNCHERPATH}"${LAUNCHERFILE} << EOF
[Desktop Entry]
Type=Application
Name=$LAUNCHERTITLE
StartupNotify=false
Exec=$GAME_DIR/$GAME_EXEC %u
MimeType=x-scheme-handler/dunestrike;
EOF

havexdgmime=$(which xdg-mime)
if [ "$havexdgmime" != "" ]; then
    # ignoring any previously registered handler. this should be the current default.
    xdg-mime default dunestrike-connectprotocol.desktop x-scheme-handler/dunestrike
    worked=$(xdg-mime query default x-scheme-handler/dunestrike 2>/dev/null)
    if [ "$worked" != "${LAUNCHERFILE}" ]; then
        # non-empty output should be the desktop filename
        # due to the EXISTINGEXEC routine we can leave the desktop entry for analysis
        echo "The Dune Strike desktop entry could not be hooked to the x-scheme-handler/dunestrike mimetype."
        exit 1
    fi
else
    echo "your system does not appear to have xdg-mime to register an x-scheme-handler for the Dune Strike connect protocol."
    # you are welcome to provide a patch via PR on our github https://github.com/aminghaseminia/dunestrike
fi

if [ -f "${LAUNCHERPATH}"${LAUNCHERFILE} ]; then
  echo "The Dune Strike connect protocol desktop entry has been successfully created at"
  echo "${LAUNCHERPATH}"${LAUNCHERFILE}
  exit 0
else
  echo "For some reason, we're unable to install the Dune Strike connect protocol desktop entry."
  exit 1
fi

