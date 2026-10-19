"""Check that a kernel cut by tools/trim_spk.py gives the source's positions.

    uv run --python 3.12 tests/check_trim_spk.py data/kernels/de440s.bsp web/data/kernels/de440s.bsp 1970-01-01 2100-01-01

Evaluates every segment of both files at 2,000 times spread over the window
(and at its two ends) and requires identical results, bit for bit. It also
checks that the cut file's coverage holds the whole window.
"""

import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import trim_spk  # noqa: E402


def load(path):
    data = Path(path).read_bytes()
    words, segments = trim_spk.read_segments(data)
    out = {}
    for start, end, ints, _name in segments:
        target, center, _frame, kind, _begin, last = ints
        if kind != 2:
            continue
        init, length, rsize, n = (words[last - 4], words[last - 3], int(words[last - 2]), int(words[last - 1]))
        out[(target, center)] = (start, end, init, length, rsize, n, ints[4], words)
    return out


def position(seg, et):
    start, end, init, length, rsize, n, begin, words = seg
    assert start <= et <= end, (start, et, end)
    i = min(max(int((et - init) // length), 0), n - 1)
    rec = begin - 1 + i * rsize
    mid, radius = words[rec], words[rec + 1]
    k = (rsize - 2) // 3
    x = (et - mid) / radius
    t = [1.0, x]
    while len(t) < k:
        t.append(2 * x * t[-1] - t[-2])
    return tuple(sum(words[rec + 2 + c * k + j] * t[j] for j in range(k)) for c in range(3))


def main(source, cut, start, stop):
    a, b = load(source), load(cut)
    if set(a) != set(b):
        raise SystemExit("segment sets differ: %s vs %s" % (sorted(a), sorted(b)))
    et0, et1 = trim_spk.et_of(start), trim_spk.et_of(stop)
    rng = random.Random(20261018)
    times = [et0, et1] + [rng.uniform(et0, et1) for _ in range(2000)]
    for key in sorted(a):
        if b[key][0] > et0 or b[key][1] < et1:
            raise SystemExit("segment %s does not cover the window" % (key,))
        for et in times:
            if position(a[key], et) != position(b[key], et):
                raise SystemExit("segment %s differs at ET %.1f" % (key, et))
    print("%s: %d segments match %s at %d times, %s .. %s" % (cut, len(b), source, len(times), start, stop))


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
