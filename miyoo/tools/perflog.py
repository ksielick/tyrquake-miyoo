"""Summarise the core's play log (core option "Performance Log" -> perflog.csv).

    py -3 miyoo/tools/perflog.py [perflog.csv] [--all] [--keep]

Without a file it fetches //<MIYOO_HOST>/Saves/TyrQuake-Miyoo/id1/perflog.csv over SMB into
<workspace>/build/perflog/perflog-<date>.csv and empties the card's copy (--keep: leave it).
Prints the last session (--all: every session): totals, per-map table, the worst seconds, and
writes a chart next to the CSV (<name>-<session>.png).

Columns (one line per second): frames, fps; late20/late25 = frames that came more than 20/25 ms
after the previous one; busy = the frame's own work (wall time minus the wait in
audio_batch_cb), its avg/p90/max; over = frames whose work exceeded the budget (1/framerate);
realsteps = frames that got a longer game step (core option Slow Frames); audio_wait = ms per
frame blocked in audio_batch_cb (spare time when paced by the sound); samples_s = sound handed
to RetroArch per second; bgm_dry = frames the music decoder had nothing ready; bgm_min_ms = the
least music decoded ahead; mix_short = mixer chunks where the music ran out (a gap = a click);
clip = sample frames where the mix went over full scale (before the limiter: what it turned
down; before 2026-10-20 11:30 only music + effects, hard-clipped); lim_min = the limiter's
lowest gain in the second (1 = untouched); buf_min/underruns = RetroArch's audio buffer, the
lowest occupancy (percent) and frames it expected an underrun;
cpu_main/cpu_proc = CPU ms per frame (main thread / whole RetroArch); the r_perf sections in ms
per frame; mhz, temp (C).
"""
import csv
import datetime
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))


def miyoo_host():
    """MIYOO_HOST from the environment or miyoo/env.local.sh (see miyoo/tools/env.sh)."""
    host = os.environ.get("MIYOO_HOST")
    try:
        with open(os.path.join(HERE, "..", "env.local.sh")) as f:
            for line in f:
                if not host and line.startswith("MIYOO_HOST="):
                    host = line.split("=", 1)[1].strip().strip("\"'")
    except OSError:
        pass
    if not host:
        sys.exit("set MIYOO_HOST (the Miyoo's address), see miyoo/tools/env.sh")
    return host


def fetch(keep):
    CARD = "//%s/Saves/TyrQuake-Miyoo/id1/perflog.csv" % miyoo_host()
    if not os.path.exists(CARD):
        sys.exit("no %s (is Performance Log on and the Miyoo awake?)" % CARD)
    out_dir = os.path.join(WS, "build", "perflog")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, datetime.datetime.now().strftime("perflog-%Y%m%d-%H%M%S.csv"))
    shutil.copyfile(CARD, out)
    if not keep:
        open(CARD, "w").close()
    print("fetched", out)
    return out


def sessions(path):
    """[(header, [row dict, ...]), ...] in file order."""
    result = []
    head, rows, cols = None, None, None
    with open(path, newline="") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line:
                continue
            if line.startswith("# session"):
                head, rows, cols = line[2:], [], None
                result.append((head, rows))
            elif line.startswith("t,"):
                cols = line.split(",")
            elif cols and rows is not None:
                vals = next(csv.reader([line]))
                if len(vals) != len(cols):
                    continue
                r = {}
                for k, v in zip(cols, vals):
                    if k == "map":  # maps/e1m1.bsp -> e1m1
                        r[k] = os.path.splitext(os.path.basename(v))[0]
                        continue
                    try:
                        r[k] = float(v)
                    except ValueError:
                        r[k] = v
                rows.append(r)
    return [s for s in result if s[1]]


def budget_of(head):
    try:
        return float(head.split("budget ")[1].split(" ms")[0])
    except (IndexError, ValueError):
        return 1000.0 / 60


def summary(head, rows):
    budget = budget_of(head)
    frames = sum(r["frames"] for r in rows)
    secs = len(rows)
    print("\n== %s" % head)
    print("%d s, %d frames, %.2f fps avg" % (secs, frames, frames / max(secs, 1)))

    def pct(key):
        return 100.0 * sum(r[key] for r in rows) / max(frames, 1)

    slow = [r for r in rows if r["fps"] < 59.0]
    print("frames: work over the %.2f ms budget %.1f %%, more than 20 ms apart %.1f %%, "
          "more than 25 ms apart %.1f %%, longer game steps %.1f %%"
          % (budget, pct("over"), pct("late20"), pct("late25"), pct("realsteps")))
    print("seconds under 59 fps: %d of %d" % (len(slow), secs))
    busy = sorted(r["busy_p90"] for r in rows)
    print("work per frame: avg %.2f ms, p90 per second: median %.2f worst %.2f, max %.1f ms"
          % (sum(r["busy_avg"] * r["frames"] for r in rows) / max(frames, 1),
             busy[len(busy) // 2], busy[-1], max(r["busy_max"] for r in rows)))
    print("CPU per frame: main thread %.2f ms, whole process %.2f ms"
          % (sum(r["cpu_main"] for r in rows) / secs, sum(r["cpu_proc"] for r in rows) / secs))
    sam = [r["samples_s"] for r in rows[1:]] or [0]
    fills = [r["bgm_min_ms"] for r in rows if r["bgm_min_ms"] >= 0]
    print("sound: %.0f samples/s avg (min %.0f); music: decoder dry in %d frames, gaps in the "
          "mix %d, least decoded ahead %s"
          % (sum(sam) / len(sam), min(sam), sum(r["bgm_dry"] for r in rows),
             sum(r["mix_short"] for r in rows),
             ("%d ms" % min(fills)) if fills else "- (no music)"))
    clip = [r for r in rows if r.get("clip", 0)]
    print("mix over full scale (before the limiter): %d sample frames in %d of %d seconds"
          % (sum(r["clip"] for r in clip), len(clip), secs))
    if "lim_min" in rows[0]:
        import math
        lim = sorted(r["lim_min"] for r in rows)
        low = [r for r in rows if r["lim_min"] < 0.99]
        print("limiter: active in %d seconds, lowest gain %.2f (%.1f dB), median of those %.2f"
              % (len(low), lim[0], 20 * math.log10(max(lim[0], 1e-3)),
                 sorted(r["lim_min"] for r in low)[len(low) // 2] if low else 1.0))
    if "buf_min" in rows[0]:
        occ = [r["buf_min"] for r in rows if r["buf_min"] >= 0]
        if occ:
            low = sum(1 for v in occ if v < 25)
            print("RetroArch audio buffer: lowest %d %%, seconds under 25 %%: %d, underrun "
                  "warnings: %d frames in %d seconds"
                  % (min(occ), low, sum(r["underruns"] for r in rows),
                     sum(1 for r in rows if r["underruns"])))
        else:
            print("RetroArch audio buffer: not reported by the frontend")
    mhz = [r["mhz"] for r in rows if r["mhz"] > 0]
    temp = [r["temp"] for r in rows if r["temp"] > 0]
    if mhz:
        print("CPU clock %d-%d MHz" % (min(mhz), max(mhz)), end="")
    if temp:
        print(", temperature %.0f-%.0f C" % (min(temp), max(temp)), end="")
    print()

    maps = {}
    for r in rows:
        m = maps.setdefault(r["map"] or "-", [0, 0, 0, 0, 0, []])
        m[0] += 1
        m[1] += r["frames"]
        m[2] += r["over"]
        m[3] += r["bgm_dry"] + r["mix_short"]
        m[4] += r["late20"]
        m[5].append(r["busy_p90"])
    print("\n%-12s %6s %7s %7s %8s %9s %11s" % ("map", "secs", "fps", "over%", "late20%",
                                               "p90 med", "music gaps"))
    for name, m in maps.items():
        p = sorted(m[5])
        print("%-12s %6d %7.2f %7.1f %8.1f %9.2f %11d" % (name, m[0], m[1] / m[0],
              100.0 * m[2] / max(m[1], 1), 100.0 * m[4] / max(m[1], 1), p[len(p) // 2], m[3]))

    print("\nworst seconds (by work p90):")
    print("%8s %-10s %6s %6s %6s %6s %5s %5s %5s %6s %6s %6s %6s" % (
        "t", "map", "fps", "p90", "max", "over", "dry", "gaps", "fill", "clip", "rend", "ents",
        "audio"))
    for r in sorted(rows, key=lambda r: -r["busy_p90"])[:10]:
        print("%8.1f %-10s %6.1f %6.2f %6.1f %6d %5d %5d %5d %6d %6.2f %6.2f %6.2f" % (
            r["t"], r["map"][:10], r["fps"], r["busy_p90"], r["busy_max"], r["over"],
            r["bgm_dry"], r["mix_short"], r["bgm_min_ms"], r.get("clip", 0), r["render"],
            r["ents"], r["audio"]))
    if clip:
        print("\nloudest seconds (clipped sample frames): " + ", ".join(
            "%.0f (%s) %d" % (r["t"], r["map"], r["clip"])
            for r in sorted(clip, key=lambda r: -r["clip"])[:10]))
    gaps = [r for r in rows if r["bgm_dry"] or r["mix_short"]]
    if gaps:
        print("\nseconds with music gaps: " + ", ".join(
            "%.0f (%s, dry %d, gaps %d, fill %d ms)" % (r["t"], r["map"], r["bgm_dry"],
                                                         r["mix_short"], r["bgm_min_ms"])
            for r in gaps[:20]) + (" ..." if len(gaps) > 20 else ""))


def chart(head, rows, png):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("(no matplotlib: no chart)")
        return
    budget = budget_of(head)
    t = [r["t"] for r in rows]
    fig, ax = plt.subplots(4, 1, figsize=(14, 10), sharex=True)
    ax[0].plot(t, [r["fps"] for r in rows], label="fps")
    ax[0].set_ylabel("fps")
    ax[0].axhline(1000.0 / budget, color="gray", lw=0.8, ls="--")
    ax[1].plot(t, [r["busy_avg"] for r in rows], label="work avg")
    ax[1].plot(t, [r["busy_p90"] for r in rows], label="work p90")
    ax[1].plot(t, [r["busy_max"] for r in rows], label="work max", alpha=0.4)
    ax[1].axhline(budget, color="red", lw=0.8, ls="--", label="budget")
    ax[1].set_ylabel("ms per frame")
    ax[1].set_ylim(0, max(30, budget * 1.6))
    ax[1].legend(loc="upper right", fontsize=8)
    ax[2].stackplot(t, [r["render"] for r in rows], [r["audio"] for r in rows],
                    labels=["render", "audio (incl. the wait for the sound)"], alpha=0.7)
    ax[2].plot(t, [r["cpu_main"] for r in rows], color="black", lw=0.8, label="main thread CPU")
    ax[2].set_ylabel("ms per frame")
    ax[2].legend(loc="upper right", fontsize=8)
    ax[3].plot(t, [r["bgm_min_ms"] for r in rows], label="music decoded ahead (min, ms)")
    gx = [r["t"] for r in rows if r["bgm_dry"] or r["mix_short"]]
    if gx:
        ax[3].scatter(gx, [0] * len(gx), color="red", marker="x", label="music gap", zorder=3)
    cx = [r for r in rows if r.get("clip", 0)]
    if cx:
        ax[3].bar([r["t"] for r in cx], [r["clip"] for r in cx], width=0.8, color="purple",
                  alpha=0.6, label="clipped sample frames")
    ax[3].set_ylabel("ms")
    ax[3].set_xlabel("seconds")
    ax[3].legend(loc="upper right", fontsize=8)
    temp = [r["temp"] for r in rows]
    if any(v > 0 for v in temp):
        tw = ax[3].twinx()
        tw.plot(t, temp, color="orange", lw=0.8)
        tw.set_ylabel("temp C", color="orange")
    fig.suptitle(head)
    fig.tight_layout()
    fig.savefig(png, dpi=90)
    plt.close(fig)
    print("chart:", png)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    path = args[0] if args else fetch("--keep" in sys.argv)
    ss = sessions(path)
    if not ss:
        sys.exit("no data in " + path)
    chosen = ss if "--all" in sys.argv else ss[-1:]
    for i, (head, rows) in enumerate(chosen):
        summary(head, rows)
        chart(head, rows, os.path.splitext(path)[0] + "-%d.png" % (len(ss) - len(chosen) + i + 1))


if __name__ == "__main__":
    main()
