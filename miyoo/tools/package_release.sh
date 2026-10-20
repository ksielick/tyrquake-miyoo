#!/bin/sh
# Release zip for OnionOS: unzip it onto the root of the SD card.
#   miyoo/tools/package_release.sh [version_tag] [core.so]
# Default core: <workspace>/build/core/tyrquake_libretro.so (miyoo/build/build_core.sh).
# Output: <workspace>/build/release/TyrQuake-Miyoo-<tag>.zip, holding
#   RetroArch/.retroarch/cores/tyrquake_miyoo_libretro.so + .info
#   RetroArch/.retroarch/core_info/tyrquake_miyoo_libretro.info
#   Roms/PORTS/Shortcuts/FPS - Quake Collection (TyrQuake)/1996 Quake.port
#   (same folder) the mission pack / dopa shortcuts as .notfound (Onion's import enables them)
#   Saves/CurrentProfile/config/TyrQuake-Miyoo/TyrQuake-Miyoo.cfg (RetroArch core override:
#   audio sync, sinc, 59.67 Hz)
# No game data: the player brings id1/pak0.pak (and pak1.pak).
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
WS=$(cd "$REPO/../.." && pwd)
TAG=${1:-$(cd "$REPO" && git rev-parse --short HEAD)}
CORE=${2:-$WS/build/core/tyrquake_libretro.so}
STAGE=$WS/build/release/stage
ZIP=$WS/build/release/TyrQuake-Miyoo-$TAG.zip
SC="Roms/PORTS/Shortcuts/FPS - Quake Collection (TyrQuake)"

[ -f "$CORE" ] || { echo "no core at $CORE: build it with miyoo/build/build_core.sh" >&2; exit 1; }

rm -rf "$STAGE"
mkdir -p "$STAGE/RetroArch/.retroarch/cores" "$STAGE/RetroArch/.retroarch/core_info" "$STAGE/$SC"
cp "$CORE" "$STAGE/RetroArch/.retroarch/cores/tyrquake_miyoo_libretro.so"
cp "$REPO/miyoo/card/tyrquake_miyoo_libretro.info" "$STAGE/RetroArch/.retroarch/cores/"
cp "$REPO/miyoo/card/tyrquake_miyoo_libretro.info" "$STAGE/RetroArch/.retroarch/core_info/"
cp "$REPO/miyoo/card/1996 Quake.port" "$REPO/miyoo/card/"*.notfound "$STAGE/$SC/"
# RetroArch settings for this core (core override, config/<library_name>/<library_name>.cfg)
mkdir -p "$STAGE/Saves/CurrentProfile/config/TyrQuake-Miyoo"
cp "$REPO/miyoo/card/TyrQuake-Miyoo.cfg" "$STAGE/Saves/CurrentProfile/config/TyrQuake-Miyoo/"

# Python's zipfile writes '/' separators (PowerShell 5's Compress-Archive writes '\')
# (Windows' python3 may be the Store's stub: try each until one runs)
PY=
for p in "py -3" python3 python; do
	if $p -c "" >/dev/null 2>&1; then PY=$p; break; fi
done
[ -n "$PY" ] || { echo "Python 3 is needed for the zip" >&2; exit 1; }
rm -f "$ZIP"
(cd "$STAGE" && $PY -I -m zipfile -c "$ZIP" RetroArch Roms Saves)

echo "core $(md5sum "$CORE" | cut -c1-8)"
$PY -I -m zipfile -l "$ZIP"
ls -l "$ZIP"
