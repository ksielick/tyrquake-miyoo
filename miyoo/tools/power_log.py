"""Summary of a power_log.sh run (CPU load, clock, temperature, battery per mode).

    py -3 miyoo/tools/power_log.py [csv]

Default csv: the card's Roms/PORTS/Games/TyrQuake-bench/power_log.csv over SMB. Rows are grouped
into stretches of one mode (tqrun running + resolution + framerate); for each: duration, CPU %
(both cores together, from /proc/stat deltas), clock, temperature at the start / end / max, and
battery percent and voltage at the start and end (with the drain per hour where long enough), and
from the power chip (axp_probe columns, when present) the average discharge current and power.
"""
import csv
import sys

DEFAULT = "//192.168.0.246/Roms/PORTS/Games/TyrQuake-bench/power_log.csv"


def mode_of(r):
    if r["tqrun"] != "1":
        return "no game"
    return "%s %s fps" % (r["resolution"], r["framerate"])


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    if len(rows) < 2:
        print("too few rows")
        return
    stretches = []
    for prev, r in zip(rows, rows[1:]):
        m = mode_of(r)
        if not stretches or stretches[-1]["mode"] != m:
            stretches.append({"mode": m, "rows": [prev]})
        stretches[-1]["rows"].append(r)
    for s in stretches:
        rs = s["rows"]
        t0, t1 = float(rs[0]["uptime"]), float(rs[-1]["uptime"])
        dt = t1 - t0
        if dt < 20:
            continue
        busy = int(rs[-1]["busy"]) - int(rs[0]["busy"])
        total = int(rs[-1]["total"]) - int(rs[0]["total"])
        cpu = 100.0 * busy / total if total else 0.0
        khz = [int(r["khz"]) for r in rs if r["khz"]]
        temps = [int(r["temp"]) for r in rs if r["temp"]]
        b0, b1 = int(rs[0]["battery"]), int(rs[-1]["battery"])
        v0, v1 = int(rs[0]["mv"]), int(rs[-1]["mv"])
        chg = any(r["charging"] not in ("", "0") for r in rs)
        line = "%-22s %6.1f min  CPU %5.1f %%  clock %4d-%4d MHz  temp %d -> %d (max %d) C  " \
               "battery %d -> %d %%, %d -> %d mV" % (
                   s["mode"], dt / 60, cpu, min(khz) // 1000, max(khz) // 1000,
                   temps[0], temps[-1], max(temps), b0, b1, v0, v1)
        mas = [(float(r["ma"]), float(r["mv_avg"])) for r in rs[1:] if r.get("ma")]
        if mas:
            ma = sum(m for m, _ in mas) / len(mas)
            w = sum(m * v for m, v in mas) / len(mas) / 1e6
            line += "  %4.0f mA %.2f W" % (ma, w)
        if dt >= 600 and not chg:
            line += "  (%.1f %%/h)" % ((b0 - b1) * 3600 / dt)
        if chg:
            line += "  CHARGING"
        print(line)


if __name__ == "__main__":
    main()
