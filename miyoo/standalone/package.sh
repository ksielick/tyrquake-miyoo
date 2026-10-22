#!/bin/sh
# Release zip of the standalone port (no RetroArch) for OnionOS: unzip onto the SD card's root,
# or let Port Manager install it (layout "sdcard-root").
#   miyoo/standalone/package.sh [version_tag] [core.so]
# Default core: <workspace>/build/core/tyrquake_libretro.so; tqrun from
# <workspace>/build/standalone (miyoo/standalone/build_tqrun.sh).
# Output: <workspace>/build/release/TyrQuake-Miyoo-Standalone-<tag>.zip, holding
#   Roms/PORTS/Games/TyrQuake-Miyoo/  tqrun, tyrquake_libretro.so, launch.sh,
#     tyrquake.opt.default, README.txt, LICENSE.txt, id1/put_your_quake_files_here.txt
#   Roms/PORTS/Shortcuts/FPS/Quake (TyrQuake-Miyoo).port
# No game data: the player brings id1/pak0.pak (and pak1.pak).
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
WS=$(cd "$REPO/../.." && pwd)
TAG=${1:-$(cd "$REPO" && git rev-parse --short HEAD)}
CORE=${2:-$WS/build/core/tyrquake_libretro.so}
TQRUN=$WS/build/standalone/tqrun
STAGE=$WS/build/release/stage-standalone
ZIP=$WS/build/release/TyrQuake-Miyoo-Standalone-$TAG.zip
G="Roms/PORTS/Games/TyrQuake-Miyoo"
SC="Roms/PORTS/Shortcuts/FPS"

[ -f "$CORE" ] || { echo "no core at $CORE: build it with miyoo/build/build_core.sh" >&2; exit 1; }
[ -f "$TQRUN" ] || { echo "no tqrun at $TQRUN: build it with miyoo/standalone/build_tqrun.sh" >&2; exit 1; }

rm -rf "$STAGE"
mkdir -p "$STAGE/$G/id1" "$STAGE/$SC"
cp "$TQRUN" "$STAGE/$G/tqrun"
cp "$CORE" "$STAGE/$G/tyrquake_libretro.so"
C=$REPO/miyoo/standalone/card
cp "$C/launch.sh" "$C/tyrquake.opt.default" "$C/README.txt" "$STAGE/$G/"
cp "$REPO/LICENSE.txt" "$STAGE/$G/LICENSE.txt"
echo "Put Quake's id1 files here: pak0.pak, pak1.pak (full game), music/ (optional)." \
	> "$STAGE/$G/id1/put_your_quake_files_here.txt"
cp "$C/Quake (TyrQuake-Miyoo).port" "$STAGE/$SC/"
# shell scripts with LF line ends whatever git's autocrlf did (busybox sh fails on CRLF)
sed -i 's/\r$//' "$STAGE/$G/launch.sh" "$STAGE/$SC/Quake (TyrQuake-Miyoo).port" "$STAGE/$G/tyrquake.opt.default"

# Python's zipfile writes '/' separators (PowerShell 5's Compress-Archive writes '\')
PY=
for p in "py -3" python3 python; do
	if $p -c "" >/dev/null 2>&1; then PY=$p; break; fi
done
[ -n "$PY" ] || { echo "Python 3 is needed for the zip" >&2; exit 1; }
rm -f "$ZIP"
(cd "$STAGE" && $PY -I -m zipfile -c "$ZIP" Roms)

echo "core $(md5sum "$CORE" | cut -c1-8), tqrun $(md5sum "$TQRUN" | cut -c1-8)"
$PY -I -m zipfile -l "$ZIP"
ls -l "$ZIP"
