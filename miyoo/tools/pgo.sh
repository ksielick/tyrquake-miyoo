#!/bin/sh
# From the PC (Git Bash): a profile-guided (PGO) + LTO build of the core, ~7 % faster than -O2.
#   miyoo/tools/pgo.sh [demos]        (default demo1 demo2 demo3; LTO=0: PGO only)
# 1. builds an instrumented core (-O3 -fprofile-generate) into <workspace>/build/core_pgo,
# 2. runs the timedemos with it on the Miyoo (bench.sh PGO=1: the profile goes to /tmp/pgo),
# 3. fetches the .gcda files into miyoo/pgo/ (commit them) and rebuilds with -fprofile-use.
# Result: <workspace>/build/core_pgo/tyrquake_libretro.so. Runs on the device: ask the owner first.
# The profile belongs to this source: rerun the whole script after code changes.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
WS=$(cd "$REPO/../.." && pwd)
MSSH=${MSSH:-/g/Emulacja/redriver2-miyoo/repos/REDRIVER2/miyoo/tools/mssh}
SHARE=//192.168.0.246/Roms
OUT=$WS/build/core_pgo
DEMOS=${*:-demo1 demo2 demo3}

[ -d "$WS/build" ] || { echo "no $WS/build" >&2; exit 1; }
rm -rf "$OUT"
# =/tmp/pgo: without a Linux directory the .gcda paths are the Windows ones (G:/...), which
# the device can't create, and this toolchain's gcov runtime then aborts (free(): invalid
# pointer). LTO=1 applies to the final build only: the profile is read per unit before LTO.
LTO= LDEXTRA="-fprofile-generate=/tmp/pgo" sh "$REPO/miyoo/build/build_core.sh" "$OUT" -O3 \
	-fprofile-generate=/tmp/pgo -fprofile-update=atomic | grep -v "^  CC" || true
[ -f "$OUT/tyrquake_libretro.so" ] || { echo "instrumented build failed" >&2; exit 1; }

"$MSSH" "rm -rf /tmp/pgo" || true
for d in $DEMOS; do
	PGO=1 sh "$HERE/bench.sh" "$OUT/tyrquake_libretro.so" 640x480 $d | grep -E "timedemo wall|^exit" || true
done
# (mssh's exit code is its output filter's)
"$MSSH" "cd /tmp && tar cf /mnt/SDCARD/Roms/.pgo.tar pgo && rm -rf /tmp/pgo" || true
[ -f "$SHARE/.pgo.tar" ] || { echo "no profile on the device" >&2; exit 1; }
rm -rf "$WS/build/pgo_data"
mkdir -p "$WS/build/pgo_data"
tar xf "$SHARE/.pgo.tar" -C "$WS/build/pgo_data"
rm -f "$SHARE/.pgo.tar"
G=$(find "$WS/build/pgo_data" -name common_d_scan.gcda)
[ -n "$G" ] || { echo "no .gcda files in the profile" >&2; exit 1; }
# -fprofile-use reads miyoo/pgo/<object>.gcda (build_core.sh's -dumpdir); the release build
# (miyoo/build/build_release.sh, GitHub Actions) uses the same files: commit them
rm -f "$REPO"/miyoo/pgo/*.gcda
cp "$(dirname "$G")"/*.gcda "$REPO/miyoo/pgo/"
echo "profile: $(ls "$REPO"/miyoo/pgo/*.gcda | wc -l) files in miyoo/pgo (commit them)"

# the changed flags rebuild everything, now using the profile (LTO unless LTO=0)
[ "$LTO" = 0 ] && LTO= || LTO=1
LTO=$LTO sh "$REPO/miyoo/build/build_core.sh" "$OUT" -O3 -fprofile-use -fprofile-correction -Wno-missing-profile |
	grep -v "^  CC" || true
