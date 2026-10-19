#!/bin/sh
# Runs on the Miyoo while retroarch runs: per-thread state, kernel wait channel and CPU ticks.
P=$(pidof retroarch)
[ -n "$P" ] || { echo "retroarch not running"; exit 1; }
for i in 1 2; do
	echo "--- $(cut -d' ' -f1 /proc/uptime)"
	for t in /proc/$P/task/*; do
		echo "$(basename $t) $(cut -d' ' -f2,3,14,15 $t/stat) syscall $(cat $t/syscall 2>/dev/null)"
	done
	sleep 2
done
ls -l /proc/$P/fd | sed 's/.* \([0-9]* -> \)/\1/'
cat /proc/$P/stack 2>/dev/null
tail -5 /tmp/tqbench/log.txt
