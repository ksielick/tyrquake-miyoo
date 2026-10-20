#!/bin/sh
# Runs on the Miyoo (over SSH): one Quake timedemo in RetroArch, isolated from the player's setup.
#   bench_device.sh <core.so> <resolution WxH> <demo> [max_frames]
# Everything lives in /tmp/tqbench: RetroArch config copy + overrides, core options, saves,
# and a game dir "tqbench" (autoexec.cfg runs the timedemo) next to a symlink to the player's
# id1/ (with music/ inside since 2026-10-19). Our core logs "timedemo wall: ..." and quits by itself (timedemo_quit 1);
# with max_frames (e.g. for the stock core, which can't quit) RetroArch stops after that many
# frames and the wall time of the whole run is printed instead.
#
# RetroArch is started the way Onion starts games: /tmp/cmd_to_run.sh + MainUI exits, and
# runtime.sh runs the command, then brings MainUI back. Started next to a running (or
# SIGSTOPped) MainUI, Onion's RetroArch hangs in VT_WAITACTIVE on tty3. The command doesn't
# mention /mnt/SDCARD/Roms, so runtime.sh treats it as an app and leaves SSH/SMB running.
CORE=$1
RES=${2:-320x240}
DEMO=${3:-demo1}
MAXF=$4
G="/mnt/SDCARD/Roms/PORTS/Games/Quake Collection (TyrQuake)"
RA=/mnt/SDCARD/RetroArch
T=/tmp/tqbench
WATCHDOG=${WATCHDOG:-240}

[ -f "$CORE" ] || { echo "no core $CORE"; exit 1; }
if pidof retroarch >/dev/null; then echo "retroarch already running"; exit 1; fi
pidof MainUI >/dev/null || { echo "MainUI not running (a game or app is open?)"; exit 1; }

# STATE=save: after SDELAY s, what keymon sends on Menu (SAVE_STATE_SLOT -1, QUIT);
# STATE=load: resume from that state (savestate_auto_load, as Onion does), dump the screen
# after SDELAY s to $T/fb.raw, quit. The states live in $TS, kept between the two runs.
TS=/tmp/tqbench_states
SDELAY=${SDELAY:-15}
SHOTDIR=/mnt/SDCARD/Roms/.tqshots
if [ -n "$SHOTS" ]; then rm -rf $SHOTDIR; mkdir -p $SHOTDIR; fi
[ "$STATE" = save ] && rm -rf $TS
mkdir -p $TS

rm -rf $T /tmp/tqshots      # tqshots: timedemo_shots (miyoo/tools/shots.sh)
mkdir -p $T/game/tqbench $T/saves/tqbench $T/info
ln -s "$G/id1" $T/game/id1
# named as installed, with its .info beside it: RetroArch takes "savestate" from the .info
cp "$CORE" $T/tyrquake_miyoo_libretro.so
cp "$(dirname "$CORE")/tyrquake_miyoo_libretro.info" $T/info/ 2>/dev/null
: > $T/game/tqbench/bench.pak
CONTENT=$T/game/tqbench/bench.pak
# PORT=1: the content is an Onion-style shortcut, "$T/Shortcuts/Test/Bench Quake.port", whose
# QuakeDir the core finds under $T/Games (= $T/game)
if [ "$PORT" = 1 ]; then
	ln -s $T/game $T/Games
	mkdir -p $T/Shortcuts/Test
	CONTENT="$T/Shortcuts/Test/Bench Quake.port"
	printf '#!/bin/sh\nQuakeDir="tqbench"\n' > "$CONTENT"
fi
# PERF=1: r_perf logs core time per frame each second (wall minus core = RetroArch's share)
# SYNC=1: audio_sync on as in play (paced by the sound, ~60 fps): compare "timedemo cpu"
# VSYNC=1: video_vsync on; REFRESH=59.67: RetroArch's video_refresh_rate (Onion: 60); LAT=128: audio_latency (Onion: 64); RC=0: no audio rate control; RESAMPLER=sinc/cc/nearest
# PGO=1: a -fprofile-generate core writes its profile under /tmp/pgo (see NOTES, PGO)
# CFG: extra console commands, ';'-separated (e.g. CFG="r_threads 0"); '\;' stays a ';' (inside an alias)
# LOAD=s0: load that savegame (copies of the player's saves) instead of a timedemo; use max_frames
if [ -n "$LOAD" ]; then
	cp /mnt/SDCARD/Saves/CurrentProfile/saves/TyrQuake/id1/*.sav $T/saves/tqbench/ 2>/dev/null
	ACT="load $LOAD"
elif [ "$STATE" = load ]; then
	ACT=""
else
	ACT="timedemo_quit 1
timedemo $DEMO"
fi
printf 'r_perf %s\n%s\n%s\n' "${PERF:-0}" "$(echo "$CFG" | sed 's/\\;/^/g' | tr ';' '\n' | tr '^' ';')" "$ACT" \
	> $T/game/tqbench/autoexec.cfg
echo "autoexec: $(tr '\n' ';' < $T/game/tqbench/autoexec.cfg)"
# the player's Quake settings (viewsize etc.), so the scene matches real play
cp /mnt/SDCARD/Saves/CurrentProfile/saves/TyrQuake/id1/config.cfg $T/saves/tqbench/ 2>/dev/null
# core options: the player's, with the resolution under test
grep -v tyrquake_resolution /mnt/SDCARD/Saves/CurrentProfile/config/TyrQuake/TyrQuake.opt > $T/core.opt
echo "tyrquake_resolution = \"$RES\"" >> $T/core.opt
# FPS=60/auto/...: the core framerate instead of the player's (theirs was 40 on 2026-10-19)
if [ -n "$FPS" ]; then
	grep -v tyrquake_framerate $T/core.opt > $T/core.opt.tmp && mv $T/core.opt.tmp $T/core.opt
	echo "tyrquake_framerate = \"$FPS\"" >> $T/core.opt
fi
# AUDIODUMP=1: the core appends the sound it pushes (raw int16 stereo) to $T/audio.raw
# PERFLOG=1: the play log (core option Performance Log) to $T/saves/tqbench/perflog.csv
# DSPTAP=1: what RetroArch writes to /dev/dsp (after its resampler) to $T/dsp.raw (dsp_tap.so
# from the bench folder, preloaded before libpadsp.so)
[ "$DSPTAP" = 1 ] && cp "$(dirname "$0")/dsp_tap.so" $T/
[ -n "$PERFLOG" ] && echo "tyrquake_perf_log = \"enabled\"" >> $T/core.opt
echo "core options: $(grep -E "framerate|resolution" $T/core.opt | tr '\n' ' ')"
cp $RA/.retroarch/retroarch.cfg $T/retroarch.cfg
cat > $T/override.cfg <<EOF
savefile_directory = "$T/saves"
savestate_directory = "$TS"
savestate_auto_load = "$([ "$STATE" = load ] && echo true || echo false)"
savestate_auto_save = "false"
sort_savestates_enable = "false"
sort_savestates_by_content_enable = "false"
libretro_info_path = "$T/info"
global_core_options = "true"
game_specific_options = "false"
core_options_path = "$T/core.opt"
config_save_on_exit = "false"
load_dummy_on_core_shutdown = "false"
sort_savefiles_enable = "false"
sort_savefiles_by_content_enable = "false"
libretro_log_level = "0"
log_verbosity = "true"
log_to_file = "false"
audio_sync = "$([ "${SYNC:-0}" = 1 ] && echo true || echo false)"
video_vsync = "$([ "${VSYNC:-0}" = 1 ] && echo true || echo false)"
${REFRESH:+video_refresh_rate = "$REFRESH"}
${LAT:+audio_latency = "$LAT"}
${RC:+audio_rate_control = "$([ "$RC" = 1 ] && echo true || echo false)"}
${RESAMPLER:+audio_resampler = "$RESAMPLER"}
fps_show = "false"
# the player's core/game overrides (config/TyrQuake/TyrQuake.cfg) must not apply here
# (OVERRIDES=1: they do, to test them)
auto_overrides_enable = "$([ "$OVERRIDES" = 1 ] && echo true || echo false)"
game_specific_options = "false"
EOF

# what runtime.sh runs in place of MainUI
cat > $T/run.sh <<EOF
#!/bin/sh
touch $T/started
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
cd $RA
T0=\$(cut -d' ' -f1 /proc/uptime)
${PGO:+GCOV_PREFIX=/tmp/pgo GCOV_PREFIX_STRIP=0 }${AUDIODUMP:+TQ_AUDIO_DUMP=$T/audio.raw }${DSPTAP:+DSP_TAP=$T/dsp.raw }HOME=$RA LD_PRELOAD="${DSPTAP:+$T/dsp_tap.so }/mnt/SDCARD/miyoo/lib/libpadsp.so" ./retroarch -v -c $T/retroarch.cfg \\
	--appendconfig $T/override.cfg ${MAXF:+--max-frames=$MAXF} -L $T/tyrquake_miyoo_libretro.so \\
	"$CONTENT" > $T/log.txt 2>&1 &
P=\$!
( sleep $WATCHDOG; kill -9 \$P ) 2>/dev/null &
W=\$!
U=/mnt/SDCARD/.tmp_update/bin/sendUDP
# SHOTS=n: the shown screen page every ~0.2 s from the start, n times, to $SHOTDIR/NN.raw
# (640x480 BGRA, upside down; on the card, as /tmp is RAM)
[ -n "$SHOTS" ] && ( i=0; while [ \$i -lt $SHOTS ]; do
	y=\$(cut -d, -f2 /sys/class/graphics/fb0/pan)
	dd if=/dev/fb0 bs=2560 skip=\$y count=480 of=$SHOTDIR/\$(printf %02d \$i).raw 2>/dev/null
	i=\$((i + 1)); usleep 150000; done ) &
case "$STATE" in
save) ( sleep $SDELAY; \$U -r 512 GET_STATUS; echo; \$U -r 128 "SAVE_STATE_SLOT -1"; echo
	sleep 4; \$U QUIT ) > $T/udp.txt 2>&1 & ;;
load) ( sleep $SDELAY; cut -d, -f2 /sys/class/graphics/fb0/pan > $T/fb.pan
	head -c 3686400 /dev/fb0 > $T/fb.raw; \$U QUIT ) > $T/udp.txt 2>&1 & ;;
esac
wait \$P
RC=\$?
T1=\$(cut -d' ' -f1 /proc/uptime)
kill \$W 2>/dev/null
echo "exit \$RC, run \$(echo "\$T1 \$T0" | awk '{printf "%.2f", \$1 - \$2}') s" > $T/result.txt
touch $T/done
exit 0
EOF
chmod +x $T/run.sh

echo "core $(md5sum "$CORE" | cut -c1-8) res $RES demo $DEMO maxframes ${MAXF:--}"
echo "battery $(/customer/app/axp_test)"
echo "load $(cat /proc/loadavg)"

# Once (2026-10-19) a run started right after the previous one never ran: let a freshly
# restarted MainUI settle before ending it.
i=0
while [ $i -lt 10 ]; do
	MUI=$(pidof MainUI)
	if [ -n "$MUI" ]; then
		age=$(awk -v s="$(cut -d' ' -f22 /proc/$MUI/stat)" '{print int($1 - s / 100)}' /proc/uptime)
		[ "$age" -ge 5 ] && break
	fi
	sleep 1
	i=$((i + 1))
done
echo "sh $T/run.sh" > /tmp/cmd_to_run.sh
killall MainUI
i=0
while [ ! -f $T/started ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
[ -f $T/started ] || echo "runtime.sh didn't start the bench within 20 s"
while [ ! -f $T/done ] && [ $i -lt $((WATCHDOG + 30)) ]; do sleep 1; i=$((i + 1)); done
# give runtime.sh a moment to bring MainUI back
sleep 3

cat $T/result.txt 2>/dev/null || {
	echo "no result: started=$([ -f $T/started ] && echo yes || echo no)" \
		"retroarch=$(pidof retroarch) cmd_to_run=$(ls /tmp/cmd_to_run.sh /mnt/SDCARD/.tmp_update/cmd_to_run.sh 2>/dev/null)"
	tail -5 $T/log.txt 2>/dev/null
}
grep -E "PERF|SW:|music|timedemo|Loading|load|couldn|Couldn|ERROR|frames .* seconds|Sys_Error|ERROR|Segmentation|hunk|exec config|[Ss]tate|[Ss]erializ|Audio:|Music|cpu|core options" $T/log.txt
[ -n "$STATE" ] && { echo "states: $(ls -l $TS | tail -n +2)"; cat $T/udp.txt; }
echo "MainUI back: $(pidof MainUI >/dev/null && echo yes || echo no)"
echo "battery $(/customer/app/axp_test)"
