#!/bin/bash
# The postinstall script of one recorded-instrument choice in CoolSynth's Mac installer (D234): downloads that
# family's zip from the Hugging Face dataset, checks its SHA-256 and unpacks it into ~/Music/CoolSynth, where
# CoolSynth reads the library (~/Music/CoolSynth/Library, SampleLibrary::defaultRoot). The zip holds
# Library/library.index and Library/<family>/..., as on Windows. The User folder in it, the player's own sounds, is
# never touched. installer/macos/build_pkg.py puts instrument.conf (NAME, URL, SHA256) beside it.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "$here/instrument.conf"

# An install for one user runs its scripts as that user. Should it ever run as root, the files still go to the
# person at the screen, and are theirs.
owner="$(id -un)"
if [[ "$(id -u)" -eq 0 ]]; then
    owner="$(stat -f %Su /dev/console)"
fi
home="$(dscl . -read "/Users/$owner" NFSHomeDirectory | sed 's/^NFSHomeDirectory: //')"
destination="$home/Music/CoolSynth"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "CoolSynth: downloading $NAME"
curl --fail --location --silent --show-error --retry 3 --retry-delay 5 --output "$work/$NAME" "$URL"
echo "$SHA256  $work/$NAME" | shasum -a 256 --check --status || { echo "CoolSynth: $NAME is damaged (SHA-256)" >&2; exit 1; }

mkdir -p "$destination"
ditto -x -k "$work/$NAME" "$destination"
if [[ "$(id -u)" -eq 0 ]]; then
    chown -R "$owner" "$destination"
fi
echo "CoolSynth: $NAME unpacked into $destination"
