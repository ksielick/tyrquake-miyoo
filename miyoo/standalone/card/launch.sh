#!/bin/sh
# Quake (TyrQuake-Miyoo) without RetroArch, on the Miyoo Mini Plus with OnionOS.
# Started by the Ports shortcut through Onion's launch_standalone.sh. tqrun runs the
# TyrQuake-Miyoo core itself: picture through the screen's 2D engine, sound through Onion's
# audioserver, its own menu on the Menu button (save/load states, exit).
DIR=$(cd "$(dirname "$0")" && pwd)
cd "$DIR" || exit 1

# The game looks for lower case names (id1/pak0.pak, id1/music/track02.ogg); files copied from
# a PC may come in upper or mixed case: renamed through a temporary name (FAT may not tell the
# cases apart). (As cacuracaptors' Hexen II port does.)
lower_names () {
	for f in "$1"/*; do
		[ -e "$f" ] || continue
		b=$(basename "$f")
		l=$(echo "$b" | tr 'A-Z' 'a-z')
		[ "$b" = "$l" ] && continue
		mv "$f" "$1/.rename_tmp" && mv "$1/.rename_tmp" "$1/$l"
	done
}
[ -d id1 ] && lower_names id1
[ -d id1/music ] && lower_names id1/music

# Quake's data: id1/ in this folder, else the Quake of Onion's Ports Collection (already set up
# for the RetroArch version; its files may be upper case, PAK0.PAK, and the card's file system
# tells the cases apart here: the name is looked up as it is)
PAK=""
for d in "$DIR" "/mnt/SDCARD/Roms/PORTS/Games/Quake Collection (TyrQuake)"; do
	p=$(ls "$d/id1" 2>/dev/null | grep -i '^pak0\.pak$' | head -n 1)
	if [ -n "$p" ]; then
		PAK="$d/id1/$p"
		break
	fi
done
if [ -z "$PAK" ]; then
	printf 'Quake data missing: put id1/pak0.pak\n(and pak1.pak) in\nRoms/PORTS/Games/TyrQuake-Miyoo/' > /tmp/MissingPortFile.tmp
	exit 1
fi

# settings: the core options (the player's copy is kept on updates)
[ -f tyrquake.opt ] || cp tyrquake.opt.default tyrquake.opt
mkdir -p saves
# the first start: the savegames and Quake settings of the RetroArch version, if any
RA=/mnt/SDCARD/Saves/CurrentProfile/saves/TyrQuake-Miyoo/id1
if [ ! -d saves/id1 ] && [ -d "$RA" ]; then
	mkdir -p saves/id1
	cp "$RA"/*.sav "$RA"/config.cfg saves/id1/ 2>/dev/null
fi

# sound: through audioserver (libpadsp.so turns /dev/dsp into its FIFO), as RetroArch does
unset LD_PRELOAD
if pgrep audioserver > /dev/null 2>&1; then
	for PADSP in /mnt/SDCARD/miyoo/lib/libpadsp.so /customer/lib/libpadsp.so; do
		if [ -f "$PADSP" ]; then
			export LD_PRELOAD="$PADSP"
			break
		fi
	done
fi
{ echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor; } 2>/dev/null
CPU1=/sys/devices/system/cpu/cpu1/online
[ -f "$CPU1" ] && [ "$(cat "$CPU1" 2>/dev/null)" = 0 ] && { echo 1 > "$CPU1"; } 2>/dev/null

chmod +x ./tqrun 2>/dev/null
# exec: Onion's SIGTERM (power-off) reaches tqrun itself, which then saves where you are
exec ./tqrun -L ./tyrquake_libretro.so -o tyrquake.opt -s "$DIR/saves" -S "$DIR/saves/quake.state" \
	"$PAK" > log.txt 2>&1
