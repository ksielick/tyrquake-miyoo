#!/bin/sh
# Build tqrun (miyoo/standalone/tqrun.c), the frontend that runs the core without RetroArch,
# with the Windows-hosted Arm GNU toolchain. Run from Git Bash:
#   miyoo/standalone/build_tqrun.sh [out_dir]
# Default out_dir: <workspace>/build/standalone.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
WS=$(cd "$REPO/../.." && pwd)
OUT=${1:-$WS/build/standalone}
. "$REPO/miyoo/tools/env.sh"
[ -n "$TC" ] || { echo "no Arm toolchain: set TC, see miyoo/tools/env.sh" >&2; exit 1; }
CC="$TC/arm-none-linux-gnueabihf-gcc"

mkdir -p "$OUT"
cd "$REPO"
# -nostartfiles + start.S: the toolchain's crt1.o needs __libc_start_main@GLIBC_2.34; dlopen and
# pthread_* live in libdl.so.2 / libpthread.so.0 on the device, so they are named explicitly
"$CC" -O2 -Wall -marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -no-pie -nostartfiles \
	-Ilibretro-common/include -o "$OUT/tqrun" miyoo/standalone/start.S miyoo/standalone/tqrun.c \
	-Wl,--no-as-needed -l:libdl.so.2 -l:libpthread.so.0
"$TC/arm-none-linux-gnueabihf-strip" "$OUT/tqrun"
# the Miyoo has glibc 2.28: no symbol may need a newer version
NEW=$("$TC/arm-none-linux-gnueabihf-objdump" -T "$OUT/tqrun" | grep -o 'GLIBC_2\.[0-9]*' | sort -u -t. -k2,2n |
	awk -F. '$2 > 28')
if [ -n "$NEW" ]; then
	echo "ERROR: needs newer glibc than the Miyoo's 2.28:"
	"$TC/arm-none-linux-gnueabihf-objdump" -T "$OUT/tqrun" | grep -E "$(echo $NEW | tr ' ' '|')"
	exit 1
fi
ls -la "$OUT/tqrun"
md5sum "$OUT/tqrun"
