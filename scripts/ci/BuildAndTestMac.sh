#!/usr/bin/env bash
# The macOS build (D234), as CI runs it and as it runs on a Mac: configure, build, test, then package. Apple Silicon,
# macOS 13 or newer, signed ad hoc (not notarised).
#   scripts/ci/BuildAndTestMac.sh <Debug|Release> [--no-tests] [--package <name>]
# Output in build/ci: logs/, Testing/ctest-results.xml, and with --package, packages/ holding the installer
# CoolSynth-Setup-<name>.pkg, CoolSynth-macos-arm64-{standalone,vst3,clap}-<name>.zip and their SHA-256 sums
# (installer/macos/package.py).
set -euo pipefail

configuration="${1:-}"
if [[ "$configuration" != "Debug" && "$configuration" != "Release" ]]; then
    echo "usage: $0 <Debug|Release> [--no-tests] [--package <name>]" >&2
    exit 2
fi
shift

runTests=1
packageName=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-tests) runTests=0; shift ;;
        --package) packageName="${2:?--package needs a name}"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$root/build/ci"
logs="$build/logs"
mkdir -p "$logs"
cd "$root"

echo "Configuration: $configuration"
echo "Runner image: ${ImageOS:-local} ${ImageVersion:-}"
xcrun --show-sdk-version | sed 's/^/macOS SDK: /'

cmake --preset macos-ci 2>&1 | tee "$logs/configure.log"
cmake --build "$build" --config "$configuration" 2>&1 | tee "$logs/build.log"

if [[ $runTests -eq 1 ]]; then
    mkdir -p "$build/Testing"
    ctest --test-dir "$build" -C "$configuration" --output-on-failure --timeout 3600 \
        --output-junit "$build/Testing/ctest-results.xml" 2>&1 | tee "$logs/ctest.log"
fi

if [[ -n "$packageName" ]]; then
    packages="$build/packages"
    rm -rf "$packages"
    mkdir -p "$packages"
    python3 "$root/installer/macos/package.py" "$build/CoolSynth_artefacts/$configuration" "$packages" "$packageName" \
        2>&1 | tee "$logs/package.log"
    cat "$packages/CoolSynth-macos-arm64-sha256-$packageName.txt"
fi
