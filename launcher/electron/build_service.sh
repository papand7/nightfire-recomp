#!/usr/bin/env bash
# Builds NightFireLauncherService (and AgentUnderFireLauncherService when AUF_REPO is set, below) (sdk/launcher's builder driven over stdin/stdout, launcher/game.json's game) for
# Linux into out/launcher-svc, where `npm start` and `npm run dist:linux` take it. Needs clang (lld on Linux), CMake and Ninja.
set -euo pipefail
# (AUF_REPO relative to where this was started)
if [[ -n "${AUF_REPO:-}" ]]; then AUF_REPO="$(cd "$AUF_REPO" && pwd)"; fi
cd "$(dirname "$0")/../.."
# macOS: Apple's clang links with its own linker (no lld); the guarded expansion is for macOS's bash 3.2 under set -u
LINK=(-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld)
[[ "$(uname -s)" == Darwin ]] && LINK=()
cmake -S sdk/launcher -B out/launcher-svc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ ${LINK[@]+"${LINK[@]}"} -DLAUNCHER_GAME="$PWD/launcher"
cmake --build out/launcher-svc --target launcher_service
echo "BUILD OK: out/launcher-svc/NightFireLauncherService"
# Agent Under Fire's service (its own launcher core, the AUF repository's sdk/launcher and launcher/game.json) into
# out/launcher-svc-auf: AUF_REPO names that repository's folder
if [[ -z "${AUF_REPO:-}" ]]; then
  echo "AUF_REPO is not set: the launcher is made without Agent Under Fire's service."
  exit 0
fi
cmake -S "$AUF_REPO/sdk/launcher" -B out/launcher-svc-auf -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ ${LINK[@]+"${LINK[@]}"} -DLAUNCHER_GAME="$AUF_REPO/launcher"
cmake --build out/launcher-svc-auf --target launcher_service
echo "BUILD OK: out/launcher-svc-auf/AgentUnderFireLauncherService"
