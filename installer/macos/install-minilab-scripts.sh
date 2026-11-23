#!/bin/bash
# The postinstall script of the MiniLab 3 choice in CoolSynth's Mac installer (D211, D249): copies CoolSynth's scripts
# where FL Studio, Ableton Live and Bitwig Studio look for them on a Mac. They travel beside this script (a package
# with no payload), so the installer never lays its own folder rights over Documents or Music.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

owner="$(id -un)"
if [[ "$(id -u)" -eq 0 ]]; then
    owner="$(stat -f %Su /dev/console)"
fi
home="$(dscl . -read "/Users/$owner" NFSHomeDirectory | sed 's/^NFSHomeDirectory: //')"

copy() {
    local from="$here/daw-scripts/$1" to="$home/$2"
    mkdir -p "$to"
    cp -f "$from"/* "$to"/
    if [[ "$(id -u)" -eq 0 ]]; then
        chown -R "$owner" "$to"
    fi
    echo "CoolSynth: MiniLab script in $to"
}

copy "fl-studio/CoolSynth MiniLab" "Documents/Image-Line/FL Studio/Settings/Hardware/CoolSynth MiniLab"
copy "ableton/CoolSynth_MiniLab" "Music/Ableton/User Library/Remote Scripts/CoolSynth_MiniLab"
copy "bitwig/CoolSynth" "Documents/Bitwig Studio/Controller Scripts/CoolSynth"
