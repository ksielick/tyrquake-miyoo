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

rm -rf $T
mkdir -p $T/game/tqbench $T/saves/tqbench $T/states
ln -s "$G/id1" $T/game/id1
cp "$CORE" $T/core.so
: > $T/game/tqbench/bench.pak
# PERF=1: r_perf logs core time per frame each second (wall minus core = RetroArch's share)
# CFG: extra console commands, ';'-separated (e.g. CFG="r_threads 0")
printf 'r_perf %s\n%s\ntimedemo_quit 1\ntimedemo %s\n' "${PERF:-0}" "$(echo "$CFG" | tr ';' '\n')" "$DEMO" \
	> $T/game/tqbench/autoexec.cfg
echo "autoexec: $(tr '\n' ';' < $T/game/tqbench/autoexec.cfg)"
# the player's Quake settings (viewsize etc.), so the scene matches real play
cp /mnt/SDCARD/Saves/CurrentProfile/saves/TyrQuake/id1/config.cfg $T/saves/tqbench/ 2>/dev/null
# core options: the player's, with the resolution under test
grep -v tyrquake_resolution /mnt/SDCARD/Saves/CurrentProfile/config/TyrQuake/TyrQuake.opt > $T/core.opt
echo "tyrquake_resolution = \"$RES\"" >> $T/core.opt
cp $RA/.retroarch/retroarch.cfg $T/retroarch.cfg
cat > $T/override.cfg <<EOF
savefile_directory = "$T/saves"
savestate_directory = "$T/states"
savestate_auto_load = "false"
savestate_auto_save = "false"
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
audio_sync = "false"
video_vsync = "false"
fps_show = "false"
EOF

# what runtime.sh runs in place of MainUI
cat > $T/run.sh <<EOF
#!/bin/sh
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
cd $RA
T0=\$(cut -d' ' -f1 /proc/uptime)
HOME=$RA LD_PRELOAD=/mnt/SDCARD/miyoo/lib/libpadsp.so ./retroarch -v -c $T/retroarch.cfg \\
	--appendconfig $T/override.cfg ${MAXF:+--max-frames=$MAXF} -L $T/core.so $T/game/tqbench/bench.pak \\
	> $T/log.txt 2>&1 &
P=\$!
( sleep $WATCHDOG; kill -9 \$P ) 2>/dev/null &
W=\$!
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

echo "sh $T/run.sh" > /tmp/cmd_to_run.sh
killall MainUI
i=0
while [ ! -f $T/done ] && [ $i -lt $((WATCHDOG + 30)) ]; do sleep 1; i=$((i + 1)); done
# give runtime.sh a moment to bring MainUI back
sleep 3

cat $T/result.txt 2>/dev/null || echo "no result (runtime.sh didn't run the bench?)"
grep -E "PERF|SW:|music|timedemo|frames .* seconds|Sys_Error|ERROR|Segmentation|hunk|exec config" $T/log.txt
echo "MainUI back: $(pidof MainUI >/dev/null && echo yes || echo no)"
echo "battery $(/customer/app/axp_test)"
