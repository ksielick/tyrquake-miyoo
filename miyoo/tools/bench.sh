#!/bin/sh
# From the PC (Git Bash): copy a core and the bench script to the Miyoo and run one timedemo.
#   miyoo/tools/bench.sh <core.so | stock> [resolution] [demo] [max_frames]
# "stock" = the core Onion ships (/mnt/SDCARD/RetroArch/.retroarch/cores). Files go over SMB to
# Roms/PORTS/Games/TyrQuake-bench (not shown in Onion's menu). Ask the owner before running.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
MSSH=${MSSH:-/g/Emulacja/redriver2-miyoo/repos/REDRIVER2/miyoo/tools/mssh}
SHARE='\\192.168.0.246\Roms\PORTS\Games\TyrQuake-bench'
DEV=/mnt/SDCARD/Roms/PORTS/Games/TyrQuake-bench
CORE=$1
shift

powershell.exe -NoProfile -Command "New-Item -ItemType Directory -Force '$SHARE' | Out-Null;
	Copy-Item '$(cygpath -w "$REPO/miyoo/bench")\*.sh' '$SHARE';
	Copy-Item '$(cygpath -w "$REPO/miyoo/card")\tyrquake_miyoo_libretro.info' '$SHARE'"
if [ "$CORE" = stock ]; then
	DCORE=/mnt/SDCARD/RetroArch/.retroarch/cores/tyrquake_libretro.so
else
	powershell.exe -NoProfile -Command "Copy-Item '$(cygpath -w "$CORE")' '$SHARE\tyrquake_libretro.so'"
	DCORE=$DEV/tyrquake_libretro.so
	echo "local core $(md5sum "$CORE" | cut -c1-8)"
fi
"$MSSH" "WATCHDOG=${WATCHDOG:-240} PERF=${PERF:-0} SYNC=${SYNC:-0} FPS='${FPS:-}' VSYNC=${VSYNC:-0} REFRESH='${REFRESH:-}' LAT='${LAT:-}' RC='${RC:-}' RESAMPLER='${RESAMPLER:-}' CFG='$CFG' LOAD='$LOAD' STATE='$STATE' PORT='$PORT' SHOTS='$SHOTS' SDELAY='${SDELAY:-15}' sh $DEV/bench_device.sh $DCORE $*"
