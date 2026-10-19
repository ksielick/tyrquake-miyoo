#!/bin/sh
# Build tyrquake_libretro.so for the Miyoo Mini Plus (Cortex-A7, RetroArch on OnionOS, glibc 2.28)
# with the Windows-hosted Arm GNU toolchain (no make needed). Run from Git Bash:
#   miyoo/build/build_core.sh [out_dir] [extra CFLAGS...]
# Default out_dir: <workspace>/build/core. Objects go to <out_dir>/obj.
# Only .c timestamps are checked: after a header change run with FORCE=1.
# LDEXTRA: extra link flags (-fprofile-generate=... for a PGO profiling build, see NOTES).
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
WS=$(cd "$REPO/../.." && pwd)
OUT=${1:-$WS/build/core}
[ $# -gt 0 ] && shift
EXTRA="$*"
TC=${TC:-/g/Emulacja/redriver2-miyoo/deps/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-linux-gnueabihf/bin}
CC="$TC/arm-none-linux-gnueabihf-gcc"
JOBS=${JOBS:-8}

CORE="cl_input cd_common alias_model chase cl_demo cl_main cl_parse cl_tent common cmd crc console cvar
d_edge d_init d_part d_modech d_polyse d_scan d_sky d_sprite d_surf d_vars draw host host_cmd keys mathlib
menu model net_common net_loop net_main pr_cmds pr_exec pr_edict r_aclip r_alias r_bsp r_draw r_edge
r_efrag r_light r_main r_misc r_model r_part r_sky r_sprite r_subdiv r_vars r_surf rb_tree rhi backend_sw
backend_vulkan sbar screen shell surf_atlas perf_timing bgmusic snd_codec snd_flac snd_mikmod snd_modplug
snd_mp3 snd_mpg123 snd_opus snd_umx snd_vorbis snd_wave snd_dma snd_mem snd_mix sprite_model sv_main
sv_move sv_phys sv_user libretro view wad zone world net_none snd_vorbis_float d_mt"
LRC="file/retro_dirent encodings/encoding_utf string/stdstring streams/file_stream
streams/file_stream_transforms vfs/vfs_implementation file/file_path file/file_path_io features/features_cpu
compat/fopen_utf8 compat/compat_strl compat/compat_posix_string compat/compat_strcasestr
compat/compat_snprintf time/rtime"
FLAC="bitreader crc fixed float format lpc md5 memory ogg_decoder_aspect ogg_mapping stream_decoder"
TREMOR="block codebook floor0 floor1 info mapping0 mdct registry res012 sharedbook synthesis vorbisfile window"
VORBIS="bitrate block codebook decode_stubs floor0 floor1 info lookup lpc lsp mapping0 mdct registry res0
sharedbook synthesis vorbisfile window"

SRCS=""
for f in $CORE; do SRCS="$SRCS common/$f.c"; done
for f in $LRC; do SRCS="$SRCS libretro-common/$f.c"; done
for f in $FLAC; do SRCS="$SRCS deps/libFLAC/$f.c"; done
for f in $TREMOR; do SRCS="$SRCS deps/tremor/$f.c"; done
SRCS="$SRCS deps/libogg/src/bitwise.c deps/libogg/src/framing.c"
for f in $VORBIS; do SRCS="$SRCS deps/libvorbis/lib/$f.c"; done
SRCS="$SRCS miyoo/build/glibc_compat.c"

CFLAGS="-O2 -DNDEBUG -marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -fPIC -fomit-frame-pointer -Wall
-Wno-unused -Wno-maybe-uninitialized
-DHAVE_STRINGS_H -DHAVE_STDINT_H -DHAVE_INTTYPES_H -D__LIBRETRO__ -DNQ_HACK -DQBASEDIR=. -DTYR_VERSION=0.62
-DINLINE=inline -DUSE_CODEC_WAVE -DUSE_CODEC_FLAC -DUSE_CODEC_VORBIS -DVORBIS_USE_TREMOR
-DGIT_VERSION=\\\"$(cd "$REPO" && git rev-parse --short HEAD)\\\"
-include miyoo/build/glibc_compat.h -I. -Ilibretro-common/include -Iinclude -Ideps/libFLAC/include -Ideps -Ideps/libogg/include $EXTRA"

mkdir -p "$OUT/obj"
# a changed flag set rebuilds everything
echo $CFLAGS > "$OUT/cflags.new"
cmp -s "$OUT/cflags.new" "$OUT/cflags.rsp" || { mv "$OUT/cflags.new" "$OUT/cflags.rsp"; FORCE=1; }
export CC RSP="$OUT/cflags.rsp"
cd "$REPO"
# one compile per line for xargs -P; per-file extras as in the upstream Makefile
for s in $SRCS; do
	o="$OUT/obj/$(echo "$s" | tr '/' '_' | sed 's/\.c$/.o/')"
	x=""
	case $s in
	common/snd_mix.c|common/r_alias.c) x="-O3" ;;
	deps/libvorbis/lib/*) x="-include deps/libvorbis/lib/fvorbis_rename.h -Ideps/libvorbis/lib -Ideps/libvorbis/include" ;;
	common/snd_vorbis_float.c) x="-Ideps/libvorbis/include" ;;
	esac
	if [ ! -f "$o" ] || [ "$s" -nt "$o" ] || [ -n "$FORCE" ]; then
		echo "$s $o${x:+ $x}"
	fi
done | xargs -r -P "$JOBS" -L 1 sh -c 'echo "  CC $0"; "$CC" @"$RSP" $2 $3 $4 $5 $6 -c "$0" -o "$1"'

"$CC" -shared -Wl,--version-script=common/libretro-link.T -Wl,--no-undefined -Wl,--wrap=stat -static-libgcc $LDEXTRA \
	-o "$OUT/tyrquake_libretro.so" "$OUT"/obj/*.o -lm
cp "$OUT/tyrquake_libretro.so" "$OUT/tyrquake_libretro.unstripped.so"
"$TC/arm-none-linux-gnueabihf-strip" "$OUT/tyrquake_libretro.so"
# the Miyoo has glibc 2.28: no symbol may need a newer version
NEW=$("$TC/arm-none-linux-gnueabihf-objdump" -T "$OUT/tyrquake_libretro.so" | grep -o 'GLIBC_2\.[0-9]*' | sort -u -t. -k2,2n |
	awk -F. '$2 > 28')
if [ -n "$NEW" ]; then
	echo "ERROR: needs newer glibc than the Miyoo's 2.28:"
	"$TC/arm-none-linux-gnueabihf-objdump" -T "$OUT/tyrquake_libretro.so" | grep -E "$(echo $NEW | tr ' ' '|')"
	exit 1
fi
ls -la "$OUT/tyrquake_libretro.so"
md5sum "$OUT/tyrquake_libretro.so"
