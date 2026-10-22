#!/bin/sh
# Release build of the core and tqrun from the committed PGO profile, then both release zips.
# The same on the PC (Git Bash) and in GitHub Actions (.github/workflows/miyoo-release.yml):
#   miyoo/build/build_release.sh [version_tag]
# The profile in miyoo/pgo/ (*.gcda, named after the objects) comes from the Miyoo
# (miyoo/tools/pgo.sh puts it there): it belongs to the core's source, so after a change
# to the core rerun pgo.sh on the device and commit miyoo/pgo/ with the change.
# The build is reproducible (build_core.sh): the same commit gives the same core anywhere.
# Output in <workspace>/build: core_release/ (core), standalone/tqrun, release/*.zip.
# TC, GIT_VERSION: as in build_core.sh.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
WS=$(cd "$REPO/../.." && pwd)
TAG=${1:-$(cd "$REPO" && git rev-parse --short HEAD)}
OUT=$WS/build/core_release

ls "$REPO"/miyoo/pgo/*.gcda >/dev/null 2>&1 || { echo "no profile in miyoo/pgo" >&2; exit 1; }
rm -rf "$OUT"
# the flags of pgo.sh's final build (the profile is read from miyoo/pgo)
LTO=1 sh "$REPO/miyoo/build/build_core.sh" "$OUT" -O3 -fprofile-use -fprofile-correction -Wno-missing-profile |
	grep -v "^  CC"
sh "$REPO/miyoo/standalone/build_tqrun.sh" "$WS/build/standalone"
sh "$REPO/miyoo/tools/package_release.sh" "$TAG" "$OUT/tyrquake_libretro.so" | tail -1
sh "$REPO/miyoo/standalone/package.sh" "$TAG" "$OUT/tyrquake_libretro.so" | tail -1
