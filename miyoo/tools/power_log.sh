#!/bin/sh
# On the Miyoo: CPU load, clock, temperature and battery every N seconds into a CSV on the SD card,
# with the standalone port's mode, so play can be measured with Wi-Fi off (no SSH needed while it
# runs). Start it detached over SSH, then turn Wi-Fi off and play:
#   setsid sh /mnt/SDCARD/Roms/PORTS/Games/TyrQuake-bench/power_log.sh [seconds] </dev/null >/dev/null 2>&1 &
# Stop: touch /mnt/SDCARD/power_log.stop (or kill it). Output: Roms/PORTS/Games/TyrQuake-bench/power_log.csv (SMB share Roms)
# Columns: uptime_s, cpu jiffies (busy, total: /proc/stat's first line), clock kHz, temp C,
# battery %, voltage mV, charging, tqrun running (0/1), resolution, framerate, then from axp_probe
# (next to this script; the power chip read over I2C, read only) once per row (fewer reads keep
# the I2C bus free for Onion's batmon; min = max = the one reading):
# battery mV, discharge mA (average, min, max), charge mA. CPU % = delta busy / delta total
# between rows (both cores together).
EVERY=${1:-5}
OUT=/mnt/SDCARD/Roms/PORTS/Games/TyrQuake-bench/power_log.csv
STOP=/mnt/SDCARD/power_log.stop
OPT=/mnt/SDCARD/Roms/PORTS/Games/TyrQuake-Miyoo/tyrquake.opt
PROBE=$(dirname "$0")/axp_probe
rm -f $STOP
[ -f $OUT ] || echo "uptime,busy,total,khz,temp,battery,mv,charging,tqrun,resolution,framerate,mv_avg,ma,ma_min,ma_max,chg_ma" > $OUT
while [ ! -f $STOP ]; do
	up=$(cut -d' ' -f1 /proc/uptime)
	set -- $(head -1 /proc/stat)
	# cpu user nice system idle iowait irq softirq ...
	busy=$(($2 + $3 + $4 + $7 + $8))
	total=$(($2 + $3 + $4 + $5 + $6 + $7 + $8))
	khz=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null)
	temp=$(sed 's/[^0-9]//g' /sys/devices/system/cpu/cpufreq/temp_out 2>/dev/null)
	bat=$(/customer/app/axp_test 2>/dev/null)
	pct=$(echo "$bat" | sed 's/.*"battery":\([0-9]*\).*/\1/')
	mv=$(echo "$bat" | sed 's/.*"voltage":\([0-9]*\).*/\1/')
	chg=$(echo "$bat" | sed 's/.*"charging":\([0-9]*\).*/\1/')
	tq=0
	pidof tqrun >/dev/null 2>&1 && tq=1
	res=$(sed -n 's/^tyrquake_resolution = "\(.*\)"/\1/p' $OPT 2>/dev/null)
	fps=$(sed -n 's/^tyrquake_framerate = "\(.*\)"/\1/p' $OPT 2>/dev/null)
	# one reading per row, then wait out the interval
	pm=$("$PROBE" avg 1 2>/dev/null) || pm=""
	sleep $EVERY
	echo "$up,$busy,$total,$khz,$temp,$pct,$mv,$chg,$tq,$res,$fps,$(echo $pm | tr ' ' ',')" >> $OUT
done
rm -f $STOP
