#!/bin/bash
# Publishes the Windows engine (no game code) to the repository's dev-builds branch, which Update.bat downloads.
#
#   p3fes/scripts/publish_dev_build.sh <windows build dir> "<change notes>"
#
# The branch is an orphan with a single commit, force-pushed each time, holding only: ps2runtime.dll, pcsx2gs.dll,
# pcsx2gs_resources/, run.bat, Update.bat, Update.ps1, abi.txt, source.txt and CHANGES.txt. The game executable
# (recompiled game code) and anything derived from game data are never published: the script refuses to push if the
# staged tree holds anything outside that list.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
build=${1:?windows build dir}
notes=${2:?change notes}
remote_url=$(git -C "$root" remote get-url --push private)

cmake --build "$build" --target ps2_runtime pcsx2gs

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
cp "$build/ps2xRuntime/ps2runtime.dll" "$build/ps2xRuntime/pcsx2gs.dll" "$stage/"
cp -r "$build/ps2xRuntime/pcsx2gs_resources" "$stage/"
for f in run.bat; do sed 's/\r*$/\r/' "$root/p3fes/$f" > "$stage/$f"; done
for f in Update.bat Update.ps1; do sed 's/\r*$/\r/' "$root/p3fes/launcher/$f" > "$stage/$f"; done
sed -n 's/.*PS2X_RUNTIME_ABI_HASH "\(.*\)"/\1/p' "$build/ps2xRuntime/generated/ps2x_abi.h" > "$stage/abi.txt"
git -C "$root" rev-parse HEAD > "$stage/source.txt"
printf '%s\r\n' "$notes" > "$stage/CHANGES.txt"

# Whitelist check: nothing else may be published.
unexpected=$(cd "$stage" && find . -type f ! -path './pcsx2gs_resources/*' \
    ! -name ps2runtime.dll ! -name pcsx2gs.dll ! -name run.bat ! -name Update.bat ! -name Update.ps1 \
    ! -name abi.txt ! -name source.txt ! -name CHANGES.txt)
if [ -n "$unexpected" ]; then echo "refusing to publish: $unexpected" >&2; exit 1; fi
[ -s "$stage/abi.txt" ] || { echo "no ABI hash" >&2; exit 1; }

name=$(git -C "$root" config user.name || true); email=$(git -C "$root" config user.email || true)
git -C "$stage" init -q
git -C "$stage" checkout -q --orphan dev-builds
git -C "$stage" add -A
git -C "$stage" -c user.name="${name:-poopoocacacacaca}" -c user.email="${email:-251941704+poopoocacacacaca@users.noreply.github.com}" \
    commit -q -m "Engine build from $(cut -c1-12 "$stage/source.txt")" -m "$notes"
git -C "$stage" push -q --force "$remote_url" dev-builds:dev-builds
echo "published dev-builds: ABI $(cat "$stage/abi.txt"), source $(cut -c1-12 "$stage/source.txt")"
