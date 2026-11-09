#!/usr/bin/env bash
set -euo pipefail
repo=$1
assets=$2
output=$3
pack=$4
abi=$5
if command -v wslpath >/dev/null 2>&1; then
    repo=$(wslpath -u "$repo")
    assets=$(wslpath -u "$assets")
    output=$(wslpath -u "$output")
    [ -z "$pack" ] || pack=$(wslpath -u "$pack")
fi
for tool in clang clang++ cmake ninja git python3 zip; do
    command -v "$tool" >/dev/null || { echo "Missing $tool. Install the Linux build requirements (in WSL on Windows)." >&2; exit 1; }
done
# WSL's noninteractive shell may not inherit Android Studio's environment.
export ANDROID_HOME=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}
[ -f "$repo/sdk/scripts/android/package.sh" ] || { echo "Rebuild the desktop game to restore its source." >&2; exit 1; }
args=(-r "$repo" -a "$abi" -o "$output" -s "$assets")
if [ -n "$pack" ]; then
    [ -d "$pack" ] || { echo "Make an HD texture pack first, or disable Include HD textures." >&2; exit 1; }
    args+=(-p "$pack")
fi
echo "Building Android package ($abi). The first DXC build can take about 20 minutes."
bash "$repo/sdk/scripts/android/package.sh" "${args[@]}"
