#!/bin/sh
# From the PC (Git Bash): timedemo screenshots on the Miyoo, for comparing builds pixel for pixel.
#   miyoo/tools/shots.sh <core.so> <set name> [every] [demo ...]
# Runs each demo (default demo1 demo2 demo3) at 640x480 with "timedemo_shots <every>" (default 50)
# through bench.sh, then fetches /tmp/tqshots into <workspace>/build/shots/td/<set name>/<demo>/.
# CFG adds console commands as for bench.sh (e.g. CFG="r_threads 0" for the reference set).
# Compare two sets: py -3 miyoo/tools/shots_compare.py <ref dir> <new dir>   (not py -I: numpy
# is in the user's site-packages)
# The shots cost time: these runs are not timings. Ask the owner before running.
# FPS (default 40, as when the reference set was made): the core framerate; the game time step
# per frame decides what the frames show (e.g. when console notify lines disappear).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
WS=$(cd "$REPO/../.." && pwd)
. "$REPO/miyoo/tools/env.sh"
SHARE=//${MIYOO_HOST:?set MIYOO_HOST, see miyoo/tools/env.sh}/Roms
CORE=${1:?usage: shots.sh core.so set_name [every] [demo ...]}
NAME=${2:?usage: shots.sh core.so set_name [every] [demo ...]}
EVERY=${3:-50}
[ $# -gt 3 ] && shift 3 || set --
DEMOS=${*:-demo1 demo2 demo3}
DEST=$WS/build/shots/td/$NAME

rm -rf "$DEST"
for d in $DEMOS; do
	FPS=${FPS:-40} CFG="${CFG:+$CFG;}timedemo_shots $EVERY" sh "$HERE/bench.sh" "$CORE" 640x480 $d |
		grep -E "^core|timedemo wall|^exit|no result" || true
	# (mssh's exit code is its output filter's: 1 when the command prints nothing)
	"$MSSH" "cd /tmp && tar cf /mnt/SDCARD/Roms/.tqshots.tar tqshots && rm -rf /tmp/tqshots" || true
	[ -f "$SHARE/.tqshots.tar" ] || { echo "$d: no shots on the device" >&2; exit 1; }
	mkdir -p "$DEST/$d"
	tar xf "$SHARE/.tqshots.tar" -C "$DEST/$d" --strip-components=1
	rm -f "$SHARE/.tqshots.tar"
	echo "$d: $(ls "$DEST/$d" | wc -l) shots in $DEST/$d"
done
