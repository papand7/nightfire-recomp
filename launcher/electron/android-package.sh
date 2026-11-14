#!/usr/bin/env bash
set -euo pipefail
repo=$1
assets=$2
output=$3
pack=$4
abi=$5
bundles=${6:-}  # no source in the install (a prebuilt Windows package): the launcher's source bundles of the game
short=${7:-game}
if command -v wslpath >/dev/null 2>&1; then
    [ -z "$repo" ] || repo=$(wslpath -u "$repo")
    [ -z "$bundles" ] || bundles=$(wslpath -u "$bundles")
    assets=$(wslpath -u "$assets")
    output=$(wslpath -u "$output")
    [ -z "$pack" ] || pack=$(wslpath -u "$pack")
fi
for tool in clang clang++ cmake ninja git python3 zip; do
    command -v "$tool" >/dev/null || { echo "Missing $tool. Install the Linux build requirements (in WSL on Windows)." >&2; exit 1; }
done
# WSL's noninteractive shell may not inherit Android Studio's environment.
export ANDROID_HOME=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}
# the game's source from the bundles, checked out in Linux (kept for the next package, updated from newer bundles)
if [ -z "$repo" ] && [ -n "$bundles" ]; then
    repo=$HOME/.cache/xbe-android/source/$short
    echo "Checking out the game's source in $repo"
    if [ -d "$repo/.git" ]; then
        git -C "$repo" fetch -q --no-recurse-submodules "$bundles/nightfire-recomp.bundle" HEAD && git -C "$repo" -c advice.detachedHead=false checkout -q -f FETCH_HEAD
    else
        rm -rf "$repo" && mkdir -p "$(dirname "$repo")"
        git -c advice.detachedHead=false clone -q "$bundles/nightfire-recomp.bundle" "$repo"
    fi
    sdk_rev=$(git -C "$repo" ls-tree HEAD sdk | awk '{print $3}')
    if [ ! -d "$repo/sdk/.git" ]; then
        rm -rf "$repo/sdk"
        git clone -q -n "$bundles/XboxXBERecomp.bundle" "$repo/sdk"
    else
        git -C "$repo/sdk" fetch -q "$bundles/XboxXBERecomp.bundle" HEAD
    fi
    git -C "$repo/sdk" -c advice.detachedHead=false checkout -q -f "${sdk_rev:-FETCH_HEAD}"
    ln -sfn "$assets" "$repo/assets"
fi
[ -f "$repo/sdk/scripts/android/package.sh" ] || { echo "Rebuild the desktop game to restore its source." >&2; exit 1; }
# the Android NDK: where Android Studio, a manual install or a distro puts it (the newest one found)
find_ndk() {
    local d
    for d in "${ANDROID_NDK_HOME:-}" $(ls -d "$ANDROID_HOME"/ndk/* "$HOME"/Android/Sdk/ndk/* "$HOME"/Android/ndk/* /opt/android/ndk/* /usr/lib/android-sdk/ndk/* 2>/dev/null | sort -V -r); do
        if [ -n "$d" ] && [ -f "$d/build/cmake/android.toolchain.cmake" ]; then echo "$d"; return 0; fi
    done
    return 1
}
ndk=$(find_ndk || true)
if [ -z "$ndk" ]; then
    cat >&2 <<'MSG'
No Android NDK found. Install it once (about 600 MB), in WSL on Windows or in a terminal on Linux:

  mkdir -p ~/Android/Sdk/ndk && cd ~/Android/Sdk/ndk && curl -LO https://dl.google.com/android/repository/android-ndk-r27c-linux.zip && unzip -q android-ndk-r27c-linux.zip && mv android-ndk-r27c 27.2.12479018 && rm android-ndk-r27c-linux.zip

Then press Make the package again. (Downloading the NDK accepts its terms: https://developer.android.com/ndk/downloads)
MSG
    exit 1
fi
echo "Using the Android NDK in $ndk"
args=(-r "$repo" -a "$abi" -o "$output" -s "$assets" -n "$ndk")
if [ -n "$pack" ]; then
    [ -d "$pack" ] || { echo "Make an HD texture pack first, or disable Include HD textures." >&2; exit 1; }
    args+=(-p "$pack")
fi
echo "Building Android package ($abi). The first DXC build can take about 20 minutes."
bash "$repo/sdk/scripts/android/package.sh" "${args[@]}"
