"""Check a dump of the sound the core pushed (bench AUDIODUMP=1 -> raw int16 stereo).

    py -3 miyoo/tools/audio_check.py audio.raw [rate]      (rate default 48000)

Prints: samples at full scale (clipped), the largest jumps between neighbouring samples, and
every silent stretch (>= 5 ms of zeros, e.g. a load) with the level just before and after it:
a hard cut shows a high level right at the edge, a fade a low one.
"""
import sys

import numpy as np


def main():
    path = sys.argv[1]
    rate = int(sys.argv[2]) if len(sys.argv) > 2 else 48000
    a = np.fromfile(path, dtype="<i2")
    a = a[: len(a) // 2 * 2].reshape(-1, 2).astype(np.int32)
    n = len(a)
    print("%s: %d frames, %.1f s" % (path, n, n / rate))
    if not n:
        return

    full = (np.abs(a) >= 32767).any(axis=1)
    print("frames at full scale: %d (%.3f %%)" % (full.sum(), 100.0 * full.sum() / n))
    if full.any():
        secs = np.unique((np.nonzero(full)[0] / rate).astype(int))
        print("  in seconds: " + " ".join(str(s) for s in secs[:40]))

    jump = np.abs(np.diff(a, axis=0)).max(axis=1)
    top = np.argsort(jump)[-8:][::-1]
    print("largest jumps between neighbours: " + ", ".join(
        "%d at %.3f s" % (jump[i], i / rate) for i in top))

    silent = (a == 0).all(axis=1)
    edges = np.diff(np.concatenate(([0], silent.astype(np.int8), [0])))
    starts, ends = np.nonzero(edges == 1)[0], np.nonzero(edges == -1)[0]
    ms1 = rate // 1000
    print("silent stretches >= 5 ms (level = max |sample| in the 1 ms next to them):")
    for s, e in zip(starts, ends):
        if e - s < 5 * ms1:
            continue
        before = np.abs(a[max(0, s - ms1):s]).max() if s else 0
        after = np.abs(a[e:e + ms1]).max() if e < n else 0
        print("  %.3f-%.3f s (%d ms): before %d, after %d"
              % (s / rate, e / rate, (e - s) * 1000 // rate, before, after))


if __name__ == "__main__":
    main()
